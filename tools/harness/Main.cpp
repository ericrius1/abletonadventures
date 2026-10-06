// AdventureHarness: a tiny VST3 host for testing the pack without a DAW.
//
//   aa_harness info   <plugin.vst3>
//   aa_harness render <plugin.vst3> <out.wav> [--seconds 8] [--input drums|pluck|sine|noise|silence]
//                     [--midi chords|drums|arp|none] [--param "Name=text"]... [--sr 48000] [--block 512]
//   aa_harness stress <plugin.vst3> [--seconds 3]
//   aa_harness gui    <plugin.vst3> <out.png> [--seconds 4] [--input ..] [--midi ..] [--param ..] [--width px]
//   aa_harness record <plugin.vst3> <prefix>  [--seconds 6] [--lead-in 1.5] [...same options as gui]
//                     -> <prefix>.mkv (screen capture incl. a sync strip), <prefix>.wav (exact audio), <prefix>.json
//
// Extra signal options for render/gui/record:
//   --midi-file song.mid          play a standard MIDI file (instruments)
//   --input-file loop.wav         feed an audio file as input, looped (effects)
//   --automate "Name=a:b@t0:t1"   ramp a parameter from a to b (text values, or normalised with an 'n' suffix)
//                                 between t0 and t1 seconds; t0 == t1 makes it a step

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>

using namespace juce;

namespace
{
//==============================================================================
struct Options
{
    String mode, pluginPath, outPath, input = "auto", midi = "auto";
    double seconds = 6.0, sampleRate = 48000.0, bpm = 120.0;
    int block = 512, width = 0;
    StringArray params;
    String stateFile;
    bool dumpState = false;
    String midiFile, inputFile;
    StringArray automations;
    double leadIn = 1.5;
};

Options parse (const StringArray& args)
{
    Options o;
    o.mode = args[0];
    o.pluginPath = args[1];
    int i = 2;
    if (o.mode == "render" || o.mode == "gui" || o.mode == "record")
        o.outPath = args[i++];

    for (; i < args.size(); ++i)
    {
        const auto a = args[i];
        auto next = [&] { return args[++i]; };
        if (a == "--seconds") o.seconds = next().getDoubleValue();
        else if (a == "--input") o.input = next();
        else if (a == "--midi") o.midi = next();
        else if (a == "--param") o.params.add (next());
        else if (a == "--sr") o.sampleRate = next().getDoubleValue();
        else if (a == "--block") o.block = next().getIntValue();
        else if (a == "--width") o.width = next().getIntValue();
        else if (a == "--bpm") o.bpm = next().getDoubleValue();
        else if (a == "--state") o.stateFile = next();
        else if (a == "--dump-state") o.dumpState = true;
        else if (a == "--midi-file") o.midiFile = next();
        else if (a == "--input-file") o.inputFile = next();
        else if (a == "--automate") o.automations.add (next());
        else if (a == "--lead-in") o.leadIn = next().getDoubleValue();
    }
    return o;
}

//==============================================================================
/** Free-running host transport at a fixed tempo. */
struct TestPlayHead : public AudioPlayHead
{
    double bpm = 120.0, sampleRate = 48000.0;
    int64 samplePos = 0;
    bool playing = true;

    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setTimeSignature (TimeSignature { 4, 4 });
        info.setIsPlaying (playing);
        info.setTimeInSamples (samplePos);
        info.setTimeInSeconds ((double) samplePos / sampleRate);
        const double ppq = (double) samplePos / sampleRate * bpm / 60.0;
        info.setPpqPosition (ppq);
        info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
        return info;
    }
};

//==============================================================================
/** Procedural test signals (audio input for effects, MIDI for instruments). */
struct SignalSource
{
    String input, midi;
    double sr = 48000.0, bpm = 120.0;
    int64 pos = 0;
    Random rng { 1234 };
    float kickPhase = 0, kickEnv = 0, kickFreq = 0, snareEnv = 0, hatEnv = 0, pluckEnv = 0, pluckPhase = 0, pluckFreq = 220;
    float sinePhase = 0, lp = 0;
    int lastArpNote = -1;
    std::vector<std::pair<int64, MidiMessage>> fileEvents;
    size_t nextFileEvent = 0;
    AudioBuffer<float> inputAudio;

    bool loadMidiFile (const File& f)
    {
        FileInputStream in (f);
        MidiFile mf;
        if (! in.openedOk() || ! mf.readFrom (in))
            return false;
        mf.convertTimestampTicksToSeconds();
        for (int t = 0; t < mf.getNumTracks(); ++t)
            for (auto* e : *mf.getTrack (t))
                if (e->message.isNoteOnOrOff() || e->message.isController() || e->message.isPitchWheel())
                    fileEvents.push_back ({ (int64) (e->message.getTimeStamp() * sr), e->message });
        std::stable_sort (fileEvents.begin(), fileEvents.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
        midi = "file";
        return true;
    }

    bool loadInputFile (const File& f)
    {
        AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<AudioFormatReader> reader (fm.createReaderFor (f));
        if (reader == nullptr || reader->lengthInSamples <= 0)
            return false;
        AudioBuffer<float> raw (2, (int) reader->lengthInSamples);
        reader->read (&raw, 0, raw.getNumSamples(), 0, true, true);
        if (reader->numChannels == 1)
            raw.copyFrom (1, 0, raw, 0, 0, raw.getNumSamples());
        // naive linear resample to the session rate if needed
        const double ratio = reader->sampleRate / sr;
        const int n = (int) ((double) raw.getNumSamples() / ratio);
        inputAudio.setSize (2, n);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
            {
                const double pos = i * ratio;
                const int i0 = (int) pos;
                const int i1 = jmin (raw.getNumSamples() - 1, i0 + 1);
                const float fr = (float) (pos - i0);
                inputAudio.setSample (ch, i, raw.getSample (ch, i0) * (1.0f - fr) + raw.getSample (ch, i1) * fr);
            }
        input = "file";
        return true;
    }

    void fill (AudioBuffer<float>& buffer, MidiBuffer& midiOut, int numSamples)
    {
        const int chans = buffer.getNumChannels();

        while (nextFileEvent < fileEvents.size() && fileEvents[nextFileEvent].first < pos + numSamples)
        {
            midiOut.addEvent (fileEvents[nextFileEvent].second, (int) jmax ((int64) 0, fileEvents[nextFileEvent].first - pos));
            ++nextFileEvent;
        }

        if (input == "file" && inputAudio.getNumSamples() > 0)
        {
            const int len = inputAudio.getNumSamples();
            for (int i = 0; i < numSamples; ++i)
                for (int ch = 0; ch < chans; ++ch)
                    buffer.setSample (ch, i, inputAudio.getSample (jmin (ch, 1), (int) ((pos + i) % len)));
            pos += numSamples;
            return;
        }
        const double samplesPerBeat = 60.0 / bpm * sr;
        const double samplesPer16th = samplesPerBeat / 4.0;

        for (int i = 0; i < numSamples; ++i)
        {
            const int64 p = pos + i;
            const bool on16th = (int64) std::floor ((double) p / samplesPer16th) != (int64) std::floor ((double) (p - 1) / samplesPer16th);
            const int step = (int) ((int64) std::floor ((double) p / samplesPer16th) % 16);

            // MIDI
            if (on16th && midi != "none")
                addMidiForStep (midiOut, i, step, (int) ((int64) std::floor ((double) p / (samplesPer16th * 16.0))));

            float s = 0.0f;
            if (input == "drums")
            {
                if (on16th)
                {
                    if (step == 0 || step == 6 || step == 10) { kickEnv = 1.0f; kickFreq = 160.0f; }
                    if (step == 4 || step == 12) snareEnv = 0.8f;
                    if (step % 2 == 0) hatEnv = 0.35f;
                }
                kickPhase += kickFreq / (float) sr;
                kickFreq = 45.0f + (kickFreq - 45.0f) * 0.9993f;
                s += std::sin (MathConstants<float>::twoPi * kickPhase) * kickEnv;
                kickEnv *= 0.99975f;
                const float n = rng.nextFloat() * 2.0f - 1.0f;
                s += n * snareEnv * 0.6f;
                snareEnv *= 0.9992f;
                lp += 0.5f * (n - lp);
                s += (n - lp) * hatEnv;
                hatEnv *= 0.997f;
                s *= 0.6f;
            }
            else if (input == "pluck")
            {
                if (on16th && (step % 3 == 0))
                {
                    const int notes[] = { 57, 60, 64, 67, 69, 72 };
                    pluckFreq = 440.0f * std::pow (2.0f, (notes[rng.nextInt (6)] - 69) / 12.0f);
                    pluckEnv = 0.6f;
                }
                pluckPhase += pluckFreq / (float) sr;
                pluckPhase -= std::floor (pluckPhase);
                s = (2.0f * pluckPhase - 1.0f) * pluckEnv;
                lp += 0.15f * (s - lp);
                s = lp;
                pluckEnv *= 0.9996f;
            }
            else if (input == "sine")
            {
                sinePhase += 440.0f / (float) sr;
                sinePhase -= std::floor (sinePhase);
                s = 0.4f * std::sin (MathConstants<float>::twoPi * sinePhase);
            }
            else if (input == "noise")
            {
                s = (rng.nextFloat() * 2.0f - 1.0f) * 0.25f;
            }

            for (int ch = 0; ch < chans; ++ch)
                buffer.setSample (ch, i, s);
        }
        pos += numSamples;
    }

    void addMidiForStep (MidiBuffer& out, int sampleOffset, int step, int bar)
    {
        if (midi == "chords")
        {
            static const int chords[4][4] = { { 48, 55, 64, 71 }, { 45, 52, 60, 67 }, { 41, 48, 57, 64 }, { 43, 50, 59, 65 } };
            const auto& c = chords[bar % 4];
            if (step == 0)
            {
                const auto& prev = chords[(bar + 3) % 4];
                for (int n : prev) out.addEvent (MidiMessage::noteOff (1, n), sampleOffset);
                for (int n : c) out.addEvent (MidiMessage::noteOn (1, n, (uint8) 96), sampleOffset);
            }
            // melody on top
            static const int mel[] = { 76, -1, 79, -1, 81, -1, 79, 76, -1, 74, -1, 72, -1, -1, 74, -1 };
            if (step > 0)
                out.addEvent (MidiMessage::noteOff (1, mel[(step + 15) % 16] > 0 ? mel[(step + 15) % 16] : 0), sampleOffset);
            if (mel[step] > 0)
                out.addEvent (MidiMessage::noteOn (1, mel[step], (uint8) 80), sampleOffset);
        }
        else if (midi == "drums")
        {
            static const char* pattern[8] = { "x.....x...x.....", "....x.......x...", "........x.......",
                                              "x.x.x.x.x.x.x.x.", "......x.......x.", "...........x..x.",
                                              "..x.......x.....", "...............x" };
            for (int v = 0; v < 8; ++v)
                if (pattern[v][step] == 'x')
                {
                    out.addEvent (MidiMessage::noteOn (10, 36 + v, (uint8) (90 + (v * 5) % 30)), sampleOffset);
                    out.addEvent (MidiMessage::noteOff (10, 36 + v), jmax (0, sampleOffset + 10));
                }
        }
        else if (midi == "arp")
        {
            static const int notes[] = { 60, 64, 67, 72, 76, 72, 67, 64 };
            const int n = notes[step % 8] + ((bar % 2) ? -3 : 0);
            if (lastArpNote >= 0)
                out.addEvent (MidiMessage::noteOff (1, lastArpNote), sampleOffset); // each note lasts one 16th
            out.addEvent (MidiMessage::noteOn (1, n, (uint8) 100), sampleOffset);
            lastArpNote = n;
        }
        else if (midi == "hold")
        {
            if (step == 0 && bar == 0)
                for (int n : { 48, 60, 64, 67 })
                    out.addEvent (MidiMessage::noteOn (1, n, (uint8) 100), sampleOffset);
        }
    }
};

//==============================================================================
std::unique_ptr<AudioPluginInstance> loadPlugin (AudioPluginFormatManager& fm, const String& path, double sr, int block,
                                                 String& error)
{
    OwnedArray<PluginDescription> types;
    for (auto* format : fm.getFormats())
        format->findAllTypesForFile (types, path);

    if (types.isEmpty())
    {
        error = "No plugin found in " + path;
        return nullptr;
    }

    return fm.createPluginInstance (*types[0], sr, block, error);
}

void applyState (AudioPluginInstance& plugin, const String& stateFile)
{
    if (stateFile.isEmpty())
        return;
    if (auto xml = parseXML (File (stateFile)))
    {
        // The hosted VST3 expects JUCE's host-side envelope around the plugin's own state blob.
        MemoryBlock pluginData;
        AudioProcessor::copyXmlToBinary (*xml, pluginData);
        XmlElement envelope ("VST3PluginState");
        envelope.createNewChildElement ("IComponent")->addTextElement (pluginData.toBase64Encoding());
        MemoryBlock mb;
        AudioProcessor::copyXmlToBinary (envelope, mb);
        plugin.setStateInformation (mb.getData(), (int) mb.getSize());
        std::cout << "  state loaded from " << stateFile << std::endl;
    }
    else
        std::cout << "  !! could not parse " << stateFile << std::endl;
}

void applyParams (AudioPluginInstance& plugin, const StringArray& params)
{
    for (auto& spec : params)
    {
        const auto name = spec.upToFirstOccurrenceOf ("=", false, false).trim();
        const auto value = spec.fromFirstOccurrenceOf ("=", false, false).trim();
        bool found = false;
        for (auto* p : plugin.getParameters())
            if (p->getName (100).equalsIgnoreCase (name))
            {
                const bool normalised = value.endsWithIgnoreCase ("n") && value.dropLastCharacters (1).trim().containsOnly ("0123456789.-");
                const float v = normalised ? value.dropLastCharacters (1).getFloatValue() : p->getValueForText (value);
                p->setValueNotifyingHost (v);
                std::cout << "  param " << name << " = " << p->getText (p->getValue(), 50) << std::endl;
                found = true;
            }
        if (! found)
            std::cout << "  !! unknown param " << name << std::endl;
    }
}

void configureSignals (SignalSource& src, const AudioPluginInstance& plugin, const Options& o)
{
    const bool instrument = plugin.getTotalNumInputChannels() == 0 || plugin.acceptsMidi();
    src.input = o.input == "auto" ? (instrument ? "silence" : "drums") : o.input;
    src.midi = o.midi == "auto" ? (instrument ? "chords" : "none") : o.midi;
    src.sr = o.sampleRate;
    src.bpm = o.bpm;
    if (o.midiFile.isNotEmpty() && ! src.loadMidiFile (File (o.midiFile)))
        std::cout << "  !! could not read MIDI file " << o.midiFile << std::endl;
    if (o.inputFile.isNotEmpty() && ! src.loadInputFile (File (o.inputFile)))
        std::cout << "  !! could not read input file " << o.inputFile << std::endl;
}

//==============================================================================
/** Timed parameter ramps ("Name=from:to@t0:t1"). */
struct Automation
{
    AudioProcessorParameter* param = nullptr;
    float from = 0.0f, to = 0.0f;
    double t0 = 0.0, t1 = 0.0;
    bool done = false;

    static std::vector<Automation> parse (AudioPluginInstance& plugin, const StringArray& specs)
    {
        std::vector<Automation> result;
        for (auto& spec : specs)
        {
            const auto name = spec.upToFirstOccurrenceOf ("=", false, false).trim();
            const auto rest = spec.fromFirstOccurrenceOf ("=", false, false);
            const auto values = rest.upToFirstOccurrenceOf ("@", false, false);
            const auto times = rest.fromFirstOccurrenceOf ("@", false, false);
            Automation a;
            for (auto* p : plugin.getParameters())
                if (p->getName (100).equalsIgnoreCase (name))
                    a.param = p;
            if (a.param == nullptr)
            {
                std::cout << "  !! unknown automation param " << name << std::endl;
                continue;
            }
            auto toNorm = [&a] (const String& v)
            {
                const auto t = v.trim();
                const bool normalised = t.endsWithIgnoreCase ("n") && t.dropLastCharacters (1).trim().containsOnly ("0123456789.-");
                return normalised ? t.dropLastCharacters (1).getFloatValue() : a.param->getValueForText (t);
            };
            a.from = toNorm (values.upToFirstOccurrenceOf (":", false, false));
            a.to = toNorm (values.fromFirstOccurrenceOf (":", false, false));
            a.t0 = times.upToFirstOccurrenceOf (":", false, false).getDoubleValue();
            a.t1 = jmax (a.t0, times.fromFirstOccurrenceOf (":", false, false).getDoubleValue());
            result.push_back (a);
        }
        return result;
    }

    static void apply (std::vector<Automation>& list, double t)
    {
        for (auto& a : list)
        {
            if (a.done || t < a.t0)
                continue;
            const double u = a.t1 > a.t0 ? jlimit (0.0, 1.0, (t - a.t0) / (a.t1 - a.t0)) : 1.0;
            a.param->setValueNotifyingHost (a.from + (a.to - a.from) * (float) u);
            if (u >= 1.0)
                a.done = true;
        }
    }
};

void setupBuses (AudioPluginInstance& plugin)
{
    // Prefer stereo in/out
    auto layout = plugin.getBusesLayout();
    if (layout.inputBuses.size() > 0)
        layout.inputBuses.getReference (0) = AudioChannelSet::stereo();
    if (layout.outputBuses.size() > 0)
        layout.outputBuses.getReference (0) = AudioChannelSet::stereo();
    plugin.setBusesLayout (layout);
}

struct Stats
{
    double peak = 0, sumSq = 0, sum = 0;
    int64 count = 0, nanCount = 0;

    void add (const AudioBuffer<float>& b, int n)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < n; ++i)
            {
                const float s = b.getSample (ch, i);
                if (! std::isfinite (s)) { ++nanCount; continue; }
                peak = jmax (peak, (double) std::abs (s));
                sumSq += (double) s * s;
                sum += s;
                ++count;
            }
    }
    String describe() const
    {
        const double rms = count > 0 ? std::sqrt (sumSq / (double) count) : 0.0;
        return "peak=" + String (Decibels::gainToDecibels (peak, -120.0), 1) + "dBFS rms="
               + String (Decibels::gainToDecibels (rms, -120.0), 1) + "dBFS dc=" + String (count > 0 ? sum / (double) count : 0.0, 5)
               + " nan/inf=" + String (nanCount);
    }
};

int runRender (const Options& o)
{
    AudioPluginFormatManager fm;
    addDefaultFormatsToManager (fm);
    String error;
    auto plugin = loadPlugin (fm, o.pluginPath, o.sampleRate, o.block, error);
    if (plugin == nullptr) { std::cerr << error << std::endl; return 1; }

    setupBuses (*plugin);
    TestPlayHead playHead;
    playHead.bpm = o.bpm;
    playHead.sampleRate = o.sampleRate;
    plugin->setPlayHead (&playHead);
    plugin->setRateAndBufferSizeDetails (o.sampleRate, o.block);
    plugin->prepareToPlay (o.sampleRate, o.block);
    applyState (*plugin, o.stateFile);
    applyParams (*plugin, o.params);

    SignalSource src;
    configureSignals (src, *plugin, o);
    std::cout << plugin->getName() << ": input=" << src.input << " midi=" << src.midi << std::endl;

    const int chans = jmax (plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels());
    const int64 total = (int64) (o.seconds * o.sampleRate);
    AudioBuffer<float> out (2, (int) total);
    out.clear();
    AudioBuffer<float> buf (chans, o.block);
    Stats stats;
    auto automations = Automation::parse (*plugin, o.automations);

    for (int64 pos = 0; pos < total; pos += o.block)
    {
        Automation::apply (automations, (double) pos / o.sampleRate);
        const int n = (int) jmin ((int64) o.block, total - pos);
        buf.setSize (chans, n, false, false, true);
        buf.clear();
        MidiBuffer midi;
        src.fill (buf, midi, n);
        playHead.samplePos = pos;
        plugin->processBlock (buf, midi);
        stats.add (buf, n);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, (int) pos, buf, jmin (ch, plugin->getTotalNumOutputChannels() - 1), 0, n);
    }

    if (o.dumpState)
    {
        MemoryBlock state;
        plugin->getStateInformation (state);
        if (auto envelope = AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize()))
            if (auto* comp = envelope->getChildByName ("IComponent"))
            {
                MemoryBlock inner;
                inner.fromBase64Encoding (comp->getAllSubText());
                if (auto xml = AudioProcessor::getXmlFromBinary (inner.getData(), (int) inner.getSize()))
                {
                    std::cout << "STATE " << xml->toString().substring (0, 1500) << std::endl;
                    xml->writeTo (File (o.outPath).withFileExtension ("state.xml"));
                }
                else
                    std::cout << "STATE (not JUCE xml, " << (int) inner.getSize() << " bytes)" << std::endl;
            }
    }

    plugin->releaseResources();

    File outFile (o.outPath);
    outFile.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<OutputStream> stream = outFile.createOutputStream();
    if (stream != nullptr)
    {
        auto writer = wav.createWriterFor (stream, AudioFormatWriterOptions().withSampleRate (o.sampleRate)
                                                                              .withNumChannels (2)
                                                                              .withBitsPerSample (24));
        if (writer != nullptr)
            writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
        // writer (and the stream it owns) are closed here, finalising the WAV header
    }

    std::cout << "RESULT " << plugin->getName() << " " << stats.describe() << std::endl;
    return stats.nanCount > 0 ? 2 : 0;
}

int runStress (const Options& o)
{
    AudioPluginFormatManager fm;
    addDefaultFormatsToManager (fm);
    String error;
    int failures = 0;

    for (double sr : { 44100.0, 96000.0 })
        for (int block : { 32, 333, 1024 })
        {
            auto plugin = loadPlugin (fm, o.pluginPath, sr, block, error);
            if (plugin == nullptr) { std::cerr << error << std::endl; return 1; }
            setupBuses (*plugin);
            TestPlayHead playHead;
            playHead.sampleRate = sr;
            plugin->setPlayHead (&playHead);
            plugin->setRateAndBufferSizeDetails (sr, block);
            plugin->prepareToPlay (sr, block);

            Options local = o;
            local.sampleRate = sr;
            SignalSource src;
            configureSignals (src, *plugin, local);
            const int chans = jmax (plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels());
            AudioBuffer<float> buf (chans, block);
            Random rng ((int64) (sr + block));
            Stats stats;
            const int64 total = (int64) (o.seconds * sr);
            int64 lastRandomise = 0;
            auto params = plugin->getParameters();

            for (int64 pos = 0; pos < total; pos += block)
            {
                if (pos - lastRandomise > (int64) (0.08 * sr))
                {
                    lastRandomise = pos;
                    for (auto* p : params)
                        if (rng.nextFloat() < 0.3f)
                            p->setValue (rng.nextFloat());
                    playHead.playing = rng.nextFloat() < 0.9f;
                }
                buf.clear();
                MidiBuffer midi;
                src.fill (buf, midi, block);
                playHead.samplePos = pos;
                plugin->processBlock (buf, midi);
                stats.add (buf, block);
            }

            const bool bad = stats.nanCount > 0 || stats.peak > 16.0;
            failures += bad ? 1 : 0;
            std::cout << (bad ? "FAIL " : "ok   ") << "sr=" << sr << " block=" << block << " " << stats.describe() << std::endl;

            // State round trip
            MemoryBlock state;
            plugin->getStateInformation (state);
            StringArray before;
            for (auto* p : params) before.add (p->getText (p->getValue(), 64));
            for (auto* p : params) p->setValue (rng.nextFloat());
            plugin->setStateInformation (state.getData(), (int) state.getSize());
            int mismatches = 0;
            for (int i = 0; i < params.size(); ++i)
            {
                const auto pname = params[i]->getName (40);
                if (pname == "Bypass" || pname.startsWith ("MIDI CC") || pname == "Program")
                    continue; // host-side VST3 wrapper parameters (bypass, MIDI-CC emulation), not plugin state
                const auto after = params[i]->getText (params[i]->getValue(), 64);
                if (after != before[i])
                {
                    ++mismatches;
                    if (mismatches < 5)
                        std::cout << "     state mismatch: " << params[i]->getName (40) << " " << before[i] << " vs " << after << std::endl;
                }
            }
            if (mismatches > 0) { ++failures; std::cout << "FAIL state round-trip: " << mismatches << " params" << std::endl; }

            plugin->reset();
            plugin->releaseResources();
        }

    std::cout << "STRESS " << (failures == 0 ? "PASSED" : "FAILED") << std::endl;
    return failures == 0 ? 0 : 3;
}

int runInfo (const Options& o)
{
    AudioPluginFormatManager fm;
    addDefaultFormatsToManager (fm);
    String error;
    auto plugin = loadPlugin (fm, o.pluginPath, 48000.0, 512, error);
    if (plugin == nullptr) { std::cerr << error << std::endl; return 1; }
    auto d = plugin->getPluginDescription();
    std::cout << d.name << " by " << d.manufacturerName << " v" << d.version << " category=" << d.category
              << " instrument=" << (int) d.isInstrument << " in=" << plugin->getTotalNumInputChannels()
              << " out=" << plugin->getTotalNumOutputChannels() << " midiIn=" << (int) plugin->acceptsMidi()
              << " midiOut=" << (int) plugin->producesMidi() << std::endl;
    for (auto* p : plugin->getParameters())
        std::cout << "  " << p->getName (40) << " = " << p->getCurrentValueAsText() << std::endl;
    return 0;
}

//==============================================================================
/** Runs the plugin in (simulated) real time on a background thread while its editor is open. */
class RealtimeRunner : public Thread
{
public:
    RealtimeRunner (AudioPluginInstance& p, const Options& o) : Thread ("audio"), plugin (p), opts (o)
    {
        configureSignals (src, plugin, o);
        playHead.bpm = o.bpm;
        playHead.sampleRate = o.sampleRate;
        plugin.setPlayHead (&playHead);
    }
    ~RealtimeRunner() override { stopThread (2000); }

    /** Pre-allocates a buffer that captures the first `seconds` of output. */
    void enableRecording (double seconds) { recorded.setSize (2, (int) (seconds * opts.sampleRate)); recorded.clear(); }
    const AudioBuffer<float>& getRecording() const { return recorded; }
    int64 getRecordedSamples() const { return recordedCount.load(); }
    double getClockSeconds() const { return (double) clock.load() / opts.sampleRate; }

    void run() override
    {
        const int block = opts.block;
        const int chans = jmax (plugin.getTotalNumInputChannels(), plugin.getTotalNumOutputChannels());
        AudioBuffer<float> buf (chans, block);
        const double blockMs = 1000.0 * block / opts.sampleRate;
        double next = Time::getMillisecondCounterHiRes();
        int64 pos = 0;
        while (! threadShouldExit())
        {
            buf.clear();
            MidiBuffer midi;
            src.fill (buf, midi, block);
            playHead.samplePos = pos;
            plugin.processBlock (buf, midi);

            const int64 done = recordedCount.load();
            const int canStore = (int) jmin ((int64) block, (int64) recorded.getNumSamples() - done);
            for (int ch = 0; ch < 2 && canStore > 0; ++ch)
                recorded.copyFrom (ch, (int) done, buf, jmin (ch, plugin.getTotalNumOutputChannels() - 1), 0, canStore);
            if (canStore > 0)
                recordedCount.store (done + canStore);

            pos += block;
            clock.store (pos);
            next += blockMs;
            const double wait = next - Time::getMillisecondCounterHiRes();
            if (wait > 0)
                Thread::sleep ((int) wait);
        }
    }

private:
    AudioPluginInstance& plugin;
    Options opts;
    SignalSource src;
    TestPlayHead playHead;
    AudioBuffer<float> recorded;
    std::atomic<int64> recordedCount { 0 }, clock { 0 };
};

//==============================================================================
/** Hosts the editor plus a thin strip underneath that turns white the moment audio starts,
    so a screen capture can be aligned with the recorded audio frame-accurately. */
class CaptureFrame : public Component, private ComponentListener
{
public:
    static constexpr int stripHeight = 8;

    explicit CaptureFrame (Component& ed) : editor (ed)
    {
        addAndMakeVisible (editor);
        editor.addComponentListener (this);
        fitToEditor();
    }
    ~CaptureFrame() override { editor.removeComponentListener (this); }

    void setStarted (bool s) { started = s; repaint (0, getHeight() - stripHeight, getWidth(), stripHeight); }
    void paint (Graphics& g) override
    {
        g.fillAll (Colours::black);
        g.setColour (started ? Colours::white : Colours::black);
        g.fillRect (0, getHeight() - stripHeight, getWidth(), stripHeight);
    }

private:
    void componentMovedOrResized (Component&, bool, bool wasResized) override { if (wasResized) fitToEditor(); }
    void fitToEditor()
    {
        editor.setTopLeftPosition (0, 0);
        setSize (editor.getWidth(), editor.getHeight() + stripHeight);
    }

    Component& editor;
    bool started = false;
};

class GuiRun : private Timer
{
public:
    explicit GuiRun (const Options& o) : opts (o)
    {
        addDefaultFormatsToManager (fm);
        String error;
        plugin = loadPlugin (fm, o.pluginPath, o.sampleRate, o.block, error);
        if (plugin == nullptr)
        {
            std::cerr << error << std::endl;
            JUCEApplicationBase::quit();
            return;
        }
        setupBuses (*plugin);
        plugin->setRateAndBufferSizeDetails (o.sampleRate, o.block);
        plugin->prepareToPlay (o.sampleRate, o.block);
        applyState (*plugin, o.stateFile);
        applyParams (*plugin, o.params);

        window = std::make_unique<DocumentWindow> ("harness", Colours::black, 0, true);
        window->setUsingNativeTitleBar (false);
        window->setTitleBarHeight (0);
        auto* editor = plugin->createEditorIfNeeded();
        if (editor == nullptr)
        {
            std::cerr << "no editor" << std::endl;
            JUCEApplicationBase::quit();
            return;
        }
        recording = o.mode == "record";
        if (recording)
        {
            frame = std::make_unique<CaptureFrame> (*editor);
            window->setContentNonOwned (frame.get(), true);
        }
        else
            window->setContentNonOwned (editor, true);
        window->setTopLeftPosition (0, 0);
        window->setVisible (true);
        // Park the pointer far away so no control shows its hover state in screenshots/recordings.
        Desktop::setMousePosition ({ 4000, 4000 });

        if (o.width > 0)
        {
            const double ratio = (double) editor->getHeight() / (double) editor->getWidth();
            editor->setSize (o.width, roundToInt (o.width * ratio));
        }

        runner = std::make_unique<RealtimeRunner> (*plugin, o);
        automations = Automation::parse (*plugin, o.automations);
        startTime = Time::getMillisecondCounterHiRes();

        if (recording)
        {
            runner->enableRecording (o.seconds);
            stage = Stage::settling;
            startTimer (15);
        }
        else
        {
            runner->startThread();
            startTimer (15);
        }
    }

    ~GuiRun() override
    {
        stopTimer();
        runner.reset();
        if (window != nullptr)
            window->clearContentComponent();
        if (plugin != nullptr)
            if (auto* ed = plugin->getActiveEditor())
                delete ed;
        window.reset();
        plugin.reset();
    }

private:
    enum class Stage { settling, capturing, running, finishing };

    void startCapture()
    {
        // Let ffmpeg grab the exact window area (editor + sync strip).
        const auto b = window->getContentComponent()->getScreenBounds();
        const File out = File (opts.outPath).withFileExtension ("mkv");
        out.deleteFile();
        const double total = opts.leadIn + opts.seconds + 1.0;
        const String display = SystemStats::getEnvironmentVariable ("DISPLAY", ":0");
        StringArray cmd { "ffmpeg", "-y", "-loglevel", "error", "-f", "x11grab", "-draw_mouse", "0", "-framerate", "30",
                          "-video_size", String (b.getWidth() & ~1) + "x" + String (b.getHeight() & ~1),
                          "-i", display + "+" + String (b.getX()) + "," + String (b.getY()),
                          "-t", String (total, 2), "-c:v", "libx264", "-preset", "ultrafast", "-crf", "10",
                          "-pix_fmt", "yuv444p", out.getFullPathName() };
        captureSize = b;
        ffmpeg.start (cmd, 0);
    }

    void finishRecording()
    {
        runner->stopThread (2000);
        ffmpeg.waitForProcessToFinish (30000);

        const File wavFile = File (opts.outPath).withFileExtension ("wav");
        wavFile.deleteFile();
        WavAudioFormat wav;
        std::unique_ptr<OutputStream> stream = wavFile.createOutputStream();
        if (stream != nullptr)
            if (auto writer = wav.createWriterFor (stream, AudioFormatWriterOptions().withSampleRate (opts.sampleRate)
                                                                                   .withNumChannels (2)
                                                                                   .withBitsPerSample (24)))
                writer->writeFromAudioSampleBuffer (runner->getRecording(), 0, (int) runner->getRecordedSamples());

        DynamicObject::Ptr meta = new DynamicObject();
        meta->setProperty ("width", captureSize.getWidth() & ~1);
        meta->setProperty ("height", captureSize.getHeight() & ~1);
        meta->setProperty ("stripHeight", CaptureFrame::stripHeight);
        meta->setProperty ("editorHeight", (captureSize.getHeight() & ~1) - CaptureFrame::stripHeight);
        meta->setProperty ("seconds", opts.seconds);
        meta->setProperty ("sampleRate", opts.sampleRate);
        File (opts.outPath).withFileExtension ("json").replaceWithText (JSON::toString (var (meta.get())));
        std::cout << "RECORDED " << opts.outPath << " " << captureSize.getWidth() << "x" << captureSize.getHeight() << std::endl;
        JUCEApplicationBase::quit();
    }

    void timerCallback() override
    {
        if (runner != nullptr && runner->isThreadRunning())
            Automation::apply (automations, runner->getClockSeconds());

        const double elapsed = (Time::getMillisecondCounterHiRes() - startTime) * 0.001;

        if (recording)
        {
            switch (stage)
            {
                case Stage::settling:
                    if (elapsed > 0.6) { startCapture(); stage = Stage::capturing; stageTime = elapsed; }
                    break;
                case Stage::capturing:
                    if (elapsed - stageTime > opts.leadIn)
                    {
                        runner->startThread (Thread::Priority::highest);
                        frame->setStarted (true);
                        stage = Stage::running;
                    }
                    break;
                case Stage::running:
                    if (runner->getRecordedSamples() >= runner->getRecording().getNumSamples())
                    {
                        stage = Stage::finishing;
                        stopTimer();
                        finishRecording();
                    }
                    break;
                case Stage::finishing:
                    break;
            }
            return;
        }

        if (elapsed < opts.seconds)
            return;
        stopTimer();

        auto* content = window->getContentComponent();
        const auto b = content->getScreenBounds();
        ChildProcess proc;
        const String cmd = "import -window root -crop " + String (b.getWidth()) + "x" + String (b.getHeight()) + "+"
                           + String (b.getX()) + "+" + String (b.getY()) + " " + opts.outPath;
        if (proc.start (cmd))
            proc.waitForProcessToFinish (20000);
        std::cout << "SCREENSHOT " << opts.outPath << " " << b.getWidth() << "x" << b.getHeight() << std::endl;
        JUCEApplicationBase::quit();
    }

    Options opts;
    AudioPluginFormatManager fm;
    std::unique_ptr<AudioPluginInstance> plugin;
    std::unique_ptr<DocumentWindow> window;
    std::unique_ptr<CaptureFrame> frame;
    std::unique_ptr<RealtimeRunner> runner;
    std::vector<Automation> automations;
    ChildProcess ffmpeg;
    Rectangle<int> captureSize;
    bool recording = false;
    Stage stage = Stage::settling;
    double startTime = 0.0, stageTime = 0.0;
};
} // namespace

//==============================================================================
class HarnessApp : public JUCEApplication
{
public:
    const String getApplicationName() override { return "AdventureHarness"; }
    const String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const String&) override
    {
        auto args = getCommandLineParameterArray();
        if (args.size() < 2)
        {
            std::cerr << "usage: aa_harness info|render|stress|gui <plugin.vst3> ..." << std::endl;
            setApplicationReturnValue (1);
            quit();
            return;
        }

        const auto opts = parse (args);
        if (opts.mode == "gui" || opts.mode == "record")
        {
            gui = std::make_unique<GuiRun> (opts);
            return;
        }

        int result = 1;
        if (opts.mode == "render") result = runRender (opts);
        else if (opts.mode == "stress") result = runStress (opts);
        else if (opts.mode == "info") result = runInfo (opts);
        setApplicationReturnValue (result);
        quit();
    }

    void shutdown() override { gui.reset(); }
    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<GuiRun> gui;
};

START_JUCE_APPLICATION (HarnessApp)
