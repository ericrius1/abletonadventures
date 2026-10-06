#include "DandelionProcessor.h"
#include "DandelionEditor.h"

using namespace juce;
namespace P = aa::params;

namespace
{
    constexpr double maxLoadSeconds = 90.0;
    constexpr double maxEmbedSeconds = 45.0;
    constexpr int chunk = 32;

    std::unique_ptr<AudioParameterFloat> customFloat (const String& id, const String& name, float min, float max, float def,
                                                      float skewCentre, std::function<String (float)> toText)
    {
        NormalisableRange<float> range (min, max);
        if (skewCentre > min && skewCentre < max)
            range.setSkewForCentre (skewCentre);
        return std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, range, def,
            AudioParameterFloatAttributes()
                .withStringFromValueFunction ([toText] (float v, int) { return toText (v); })
                .withValueFromStringFunction ([] (const String& s) { return s.retainCharacters ("0123456789.-").getFloatValue(); }));
    }
} // namespace

AudioProcessorValueTreeState::ParameterLayout DandelionProcessor::createLayout()
{
    P::Layout layout;
    // First eight = Push's first bank.
    layout.add (P::percent ("position", "Position", 30.0f));
    layout.add (P::percent ("spray", "Spray", 18.0f));
    layout.add (P::floatParam ("size", "Grain Size", 10.0f, 600.0f, 140.0f, P::Unit::ms, 120.0f));
    layout.add (customFloat ("density", "Density", 1.0f, 80.0f, 20.0f, 16.0f,
                             [] (float v) { return String (v, v < 10.0f ? 1 : 0) + " /s"; }));
    layout.add (P::floatParam ("drift", "Drift", -100.0f, 100.0f, 8.0f, P::Unit::percent));
    layout.add (P::percent ("seeds", "Seeds", 15.0f));
    layout.add (P::percent ("wind", "Wind", 30.0f));
    layout.add (P::percent ("reverb", "Reverb", 35.0f));

    layout.add (P::percent ("shimmer", "Shimmer", 10.0f));
    layout.add (P::percent ("reverse", "Reverse", 0.0f));
    layout.add (P::percent ("shape", "Grain Shape", 20.0f));
    layout.add (P::percent ("width", "Width", 70.0f));
    layout.add (P::floatParam ("cutoff", "Cutoff", 200.0f, 20000.0f, 14000.0f, P::Unit::hz, 2000.0f));
    layout.add (P::floatParam ("attack", "Attack", 1.0f, 4000.0f, 180.0f, P::Unit::ms, 300.0f));
    layout.add (P::floatParam ("release", "Release", 10.0f, 8000.0f, 1400.0f, P::Unit::ms, 800.0f));
    layout.add (P::floatParam ("volume", "Volume", -36.0f, 6.0f, -4.0f, P::Unit::db));

    auto sources = dandelion::BuiltinBank::names();
    sources.add ("Your Sample");
    layout.add (P::choice ("source", "Source", sources, 0));
    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { "root", 1 }, "Root Key", 24, 96, 60,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return MidiMessage::getMidiNoteName (v, true, true, 3); })));
    return layout;
}

DandelionProcessor::DandelionProcessor()
    : aa::PluginBase (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true), createLayout())
{
    formatManager.registerBasicFormats();

    position = raw ("position");
    spray = raw ("spray");
    grainSize = raw ("size");
    density = raw ("density");
    drift = raw ("drift");
    seeds = raw ("seeds");
    wind = raw ("wind");
    reverbMix = raw ("reverb");
    shimmer = raw ("shimmer");
    reverse = raw ("reverse");
    shape = raw ("shape");
    width = raw ("width");
    cutoff = raw ("cutoff");
    attack = raw ("attack");
    release = raw ("release");
    volume = raw ("volume");
    source = raw ("source");
    rootKey = raw ("root");

    for (auto& v : voicePosition)
        v.store (-1.0f);

    // Grain envelopes: smooth Hann, and a plucky fast-attack/long-tail shape.
    for (size_t i = 0; i < hannTable.size(); ++i)
    {
        const float t = (float) i / (float) (hannTable.size() - 1);
        hannTable[i] = 0.5f - 0.5f * std::cos (aa::dsp::twoPi * t);
        const float a = 0.04f;
        percTable[i] = t < a ? std::sin (0.5f * aa::dsp::pi * t / a)
                             : std::pow (std::cos (0.5f * aa::dsp::pi * (t - a) / (1.0f - a)), 3.0f);
    }

    tailSeconds = 8.0;

    auto src = [] (int i) { return (float) i; };
    setFactoryPresets ({
        { "Dandelion Wishes", {} },
        { "Music Box Dreams", { { "source", src (1) }, { "size", 90.0f }, { "density", 26.0f }, { "seeds", 30.0f },
                                { "shape", 65.0f }, { "spray", 35.0f }, { "drift", 15.0f }, { "reverb", 45.0f } } },
        { "Windy Meadow", { { "source", src (2) }, { "wind", 85.0f }, { "spray", 60.0f }, { "density", 12.0f },
                            { "size", 220.0f }, { "width", 100.0f }, { "reverb", 50.0f } } },
        { "Velvet Cloud", { { "source", src (3) }, { "size", 320.0f }, { "density", 36.0f }, { "spray", 28.0f },
                            { "drift", 5.0f }, { "reverb", 55.0f }, { "attack", 600.0f }, { "release", 2600.0f } } },
        { "Frozen Moment", { { "position", 45.0f }, { "drift", 0.0f }, { "spray", 2.0f }, { "size", 480.0f },
                             { "density", 44.0f }, { "wind", 0.0f }, { "shimmer", 4.0f }, { "reverb", 45.0f } } },
        { "Firefly Swarm", { { "source", src (1) }, { "size", 28.0f }, { "density", 70.0f }, { "spray", 100.0f },
                             { "seeds", 55.0f }, { "width", 100.0f }, { "shape", 80.0f }, { "volume", -6.0f } } },
        { "Reverse Dreams", { { "source", src (3) }, { "reverse", 85.0f }, { "size", 240.0f }, { "density", 22.0f },
                              { "spray", 40.0f }, { "reverb", 50.0f } } },
        { "Seed Storm", { { "source", src (2) }, { "density", 78.0f }, { "wind", 100.0f }, { "seeds", 70.0f },
                          { "size", 60.0f }, { "spray", 80.0f }, { "volume", -7.0f } } },
        { "Slow Bloom", { { "attack", 2600.0f }, { "release", 5000.0f }, { "drift", 22.0f }, { "size", 260.0f },
                          { "density", 30.0f }, { "reverb", 60.0f } } },
        { "Tape Granules", { { "source", src (3) }, { "drift", 100.0f }, { "spray", 4.0f }, { "size", 70.0f },
                             { "density", 40.0f }, { "shimmer", 30.0f }, { "wind", 10.0f } } },
        { "Thistle Pluck", { { "source", src (1) }, { "attack", 2.0f }, { "release", 450.0f }, { "size", 120.0f },
                             { "density", 18.0f }, { "shape", 90.0f }, { "spray", 12.0f }, { "reverb", 30.0f } } },
    });
}

DandelionProcessor::~DandelionProcessor()
{
    userSample.store (nullptr);
}

//==============================================================================
void DandelionProcessor::prepareToPlay (double sampleRate, int)
{
    sr = (float) sampleRate;

    // The built-in sources are generated on a background thread; give it a moment so the very
    // first notes (e.g. an offline bounce right after loading) aren't silent.
    for (int i = 0; i < 400 && ! bank->ready.load(); ++i)
        Thread::sleep (5);

    for (auto& v : voices)
    {
        v = Voice {};
        v.env.setSampleRate (sr);
    }
    for (auto& g : grains)
        g.active = false;

    filterL.reset();
    filterR.reset();
    reverb.prepare (sampleRate);
    cutoffSmooth.reset (sampleRate, 0.03, cutoff->load());
    volumeSmooth.reset (sampleRate, 0.03, aa::dsp::dbToGain (volume->load()));
    wetBuffer.setSize (2, 4096, false, true, true);
    meter.prepare (sr);
    for (auto& p : voicePosition)
        p.store (-1.0f);
}

const dandelion::SampleData* DandelionProcessor::activeSource() const
{
    const int index = (int) source->load();
    if (index == userSourceIndex)
        if (auto* s = userSample.load())
            return s;

    if (! bank->ready.load())
        return nullptr;

    return &bank->sources[(size_t) jlimit (0, dandelion::BuiltinBank::count - 1, index == userSourceIndex ? 0 : index)];
}

const dandelion::SampleData* DandelionProcessor::getDisplaySource() const
{
    return activeSource();
}

String DandelionProcessor::getUserSampleName() const
{
    if (auto* s = userSample.load())
        return s->name;
    return {};
}

//==============================================================================
void DandelionProcessor::noteOn (int note, float velocity)
{
    // Re-trigger a voice already playing this note, else a free one, else steal the oldest.
    int chosen = -1;
    for (int i = 0; i < numVoices; ++i)
        if (voices[(size_t) i].note == note && voices[(size_t) i].env.isActive())
            chosen = i;

    const bool retrigger = chosen >= 0;

    if (chosen < 0)
        for (int i = 0; i < numVoices; ++i)
            if (voices[(size_t) i].isFree()) { chosen = i; break; }

    if (chosen < 0)
    {
        uint32_t oldest = std::numeric_limits<uint32_t>::max();
        for (int i = 0; i < numVoices; ++i)
            if (voices[(size_t) i].startOrder < oldest)
            {
                oldest = voices[(size_t) i].startOrder;
                chosen = i;
            }
    }

    auto& v = voices[(size_t) chosen];
    if (! retrigger)
    {
        // Grains left over from this voice's previous note fade out quickly on their own.
        for (auto& g : grains)
            if (g.active && g.voice == chosen && g.fadeLeft < 0)
                g.fadeLeft = 256;
        v.env.kill();
        v.envValue = v.prevEnvValue = 0.0f;
        v.scan = position->load() / 100.0;
        v.grainCountdown = 0.0;
    }

    v.note = note;
    v.velocity = velocity;
    v.held = true;
    v.sustained = false;
    v.startOrder = ++noteCounter;
    v.env.set (attack->load() * 0.001f, 0.2f, 1.0f, release->load() * 0.001f);
    v.env.noteOn();
}

void DandelionProcessor::noteOff (int note)
{
    for (auto& v : voices)
        if (v.note == note && v.held)
        {
            v.held = false;
            if (sustainPedal)
                v.sustained = true;
            else
                v.env.noteOff();
        }
}

void DandelionProcessor::handleMidi (const MidiMessage& m)
{
    if (m.isNoteOn())
        noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())
        noteOff (m.getNoteNumber());
    else if (m.isPitchWheel())
        pitchBend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
    else if (m.isControllerOfType (1))
        modWheel = (float) m.getControllerValue() / 127.0f;
    else if (m.isSustainPedalOn())
        sustainPedal = true;
    else if (m.isSustainPedalOff())
    {
        sustainPedal = false;
        for (auto& v : voices)
            if (v.sustained)
            {
                v.sustained = false;
                v.env.noteOff();
            }
    }
    else if (m.isAllNotesOff())
    {
        for (auto& v : voices)
        {
            v.held = v.sustained = false;
            v.env.noteOff();
        }
    }
    else if (m.isAllSoundOff())
    {
        for (auto& v : voices)
        {
            v = Voice {};
            v.env.setSampleRate (sr);
        }
        for (auto& g : grains)
            g.active = false;
    }
}

//==============================================================================
void DandelionProcessor::spawnGrain (int voiceIndex, const dandelion::SampleData& src, float windAmt, int rootNote)
{
    Grain* slot = nullptr;
    for (auto& g : grains)
        if (! g.active) { slot = &g; break; }
    if (slot == nullptr)
        return;

    const auto& v = voices[(size_t) voiceIndex];
    const int length = src.buffer.getNumSamples();
    if (length < 16)
        return;

    const float sprayAmt = spray->load() / 100.0f * (1.0f + 0.5f * windAmt);
    double frac = v.scan + (double) (rng.nextBipolar() * 0.5f * sprayAmt);
    frac -= std::floor (frac);

    float semis = (float) (v.note - rootNote)
                  + pitchBend * 2.0f
                  + rng.nextBipolar() * (shimmer->load() / 100.0f) * 0.5f;
    int octave = 0;
    if (rng.chance (seeds->load() / 100.0f * 0.5f))
    {
        octave = rng.chance (0.8f) ? 1 : -1;
        semis += 12.0f * (float) octave;
    }

    double inc = src.sampleRate / (double) sr * std::pow (2.0, (double) semis / 12.0);
    if (rng.chance (reverse->load() / 100.0f))
        inc = -inc;

    const float sizeSeconds = grainSize->load() * 0.001f;
    const float windValue = windNow.load (std::memory_order_relaxed);
    const float densityNow = jmax (0.5f, density->load() * (1.0f + 0.7f * windAmt * windValue));
    const float overlap = jmax (1.0f, densityNow * sizeSeconds);
    const float amp = v.velocity * 1.35f / std::sqrt (overlap);

    float pan = (width->load() / 100.0f) * rng.nextBipolar() + 0.35f * windAmt * windValue;
    pan = jlimit (-1.0f, 1.0f, pan);
    const float angle = (pan + 1.0f) * aa::dsp::pi * 0.25f;

    slot->active = true;
    slot->voice = voiceIndex;
    slot->pos = frac * (double) length;
    slot->inc = inc;
    slot->length = jmax (16, (int) (sizeSeconds * sr));
    slot->age = 0;
    slot->gainL = std::cos (angle) * amp * 1.41f;
    slot->gainR = std::sin (angle) * amp * 1.41f;
    slot->shape = shape->load() / 100.0f;
    slot->fadeLeft = -1;

    grainEvents.push ({ (float) frac, pan, amp, octave });
}

void DandelionProcessor::renderGrains (const dandelion::SampleData& src, float* outL, float* outR, int numSamples)
{
    const int length = src.buffer.getNumSamples();
    const float* srcL = src.buffer.getReadPointer (0);
    const float* srcR = src.buffer.getReadPointer (jmin (1, src.buffer.getNumChannels() - 1));
    const double len = (double) length;

    const float invN = 1.0f / (float) numSamples;

    for (auto& g : grains)
    {
        if (! g.active)
            continue;

        const auto& voice = voices[(size_t) g.voice];
        const float e0 = voice.prevEnvValue, e1 = voice.envValue;
        for (int i = 0; i < numSamples; ++i)
        {
            const float t = (float) g.age / (float) g.length * 1024.0f;
            const int ti = jmin (1023, (int) t);
            const float tf = t - (float) ti;
            const float h = hannTable[(size_t) ti] + (hannTable[(size_t) ti + 1] - hannTable[(size_t) ti]) * tf;
            const float p = percTable[(size_t) ti] + (percTable[(size_t) ti + 1] - percTable[(size_t) ti]) * tf;
            float env = h + (p - h) * g.shape;

            if (g.fadeLeft >= 0)
            {
                env *= (float) g.fadeLeft / 256.0f;
                if (--g.fadeLeft < 0) { g.active = false; break; }
            }
            else
                env *= e0 + (e1 - e0) * (float) (i + 1) * invN;

            // cubic read with wrap-around
            double pos = g.pos;
            pos -= std::floor (pos / len) * len;
            const int i0 = (int) pos;
            const float f = (float) (pos - (double) i0);
            const int im1 = i0 == 0 ? length - 1 : i0 - 1;
            const int i1 = i0 + 1 >= length ? 0 : i0 + 1;
            const int i2 = i1 + 1 >= length ? 0 : i1 + 1;

            auto cubic = [f] (float xm1, float x0, float x1, float x2)
            {
                const float c1 = 0.5f * (x1 - xm1);
                const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
                const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
                return ((c3 * f + c2) * f + c1) * f + x0;
            };

            const float l = cubic (srcL[im1], srcL[i0], srcL[i1], srcL[i2]);
            const float r = cubic (srcR[im1], srcR[i0], srcR[i1], srcR[i2]);
            outL[i] += l * env * g.gainL;
            outR[i] += r * env * g.gainR;

            g.pos += g.inc;
            if (++g.age >= g.length)
            {
                g.active = false;
                break;
            }
        }
    }
}

void DandelionProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    if (numSamples == 0)
        return;

    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    const auto* src = activeSource();
    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    if (wetBuffer.getNumSamples() < numSamples)
        wetBuffer.setSize (2, numSamples, false, false, true); // only if the host lied about block size

    const float windAmt = jlimit (0.0f, 1.0f, wind->load() / 100.0f + modWheel * 0.6f);
    const float driftAmt = drift->load() / 100.0f;
    const float densityBase = density->load();
    const int rootNote = (src != nullptr && src == userSample.load()) ? (int) rootKey->load() : 60;

    // scratch for the right channel if mono
    float monoScratch[chunk];

    auto midiIt = midi.begin();
    for (int start = 0; start < numSamples; start += chunk)
    {
        const int n = jmin (chunk, numSamples - start);

        // MIDI events in this chunk (quantised to 32 samples, < 1 ms)
        while (midiIt != midi.end() && (*midiIt).samplePosition < start + n)
        {
            handleMidi ((*midiIt).getMessage());
            ++midiIt;
        }

        // Wind: slow gusts
        const float w = windLfo.process (0.18f + 0.25f * windAmt, sr / (float) n, aa::dsp::Lfo::smoothRandom);
        windNow.store (w * windAmt, std::memory_order_relaxed);

        for (int vi = 0; vi < numVoices; ++vi)
        {
            auto& v = voices[(size_t) vi];
            v.prevEnvValue = v.envValue;
            if (! v.env.isActive())
            {
                v.envValue = 0.0f;
                voicePosition[(size_t) vi].store (-1.0f, std::memory_order_relaxed);
                continue;
            }

            v.env.set (attack->load() * 0.001f, 0.2f, 1.0f, release->load() * 0.001f);
            float e = 0.0f;
            for (int i = 0; i < n; ++i)
                e = v.env.process();
            v.envValue = e;

            if (src != nullptr)
            {
                const double srcLen = (double) jmax (1, src->buffer.getNumSamples());
                v.scan += (double) driftAmt * (src->sampleRate / (double) sr) * (double) n / srcLen;
                v.scan -= std::floor (v.scan);

                const float densityNow = jmax (0.5f, densityBase * (1.0f + 0.7f * windAmt * w));
                v.grainCountdown -= (double) n;
                int guard = 0;
                while (v.grainCountdown <= 0.0 && guard++ < 8)
                {
                    spawnGrain (vi, *src, windAmt, rootNote);
                    v.grainCountdown += (double) sr / (double) densityNow * (0.7 + 0.6 * (double) rng.next01());
                }
            }
            voicePosition[(size_t) vi].store ((float) v.scan, std::memory_order_relaxed);
        }

        if (src != nullptr)
            renderGrains (*src, outL + start, outR != nullptr ? outR + start : monoScratch, n);
    }

    // Tone, space, level
    const float* dryL = outL;
    const float* dryR = outR != nullptr ? outR : outL;
    float* wetL = wetBuffer.getWritePointer (0);
    float* wetR = wetBuffer.getWritePointer (1);
    const float mix = reverbMix->load() / 100.0f;

    aa::dsp::FdnReverb::Params rp;
    rp.size = 0.85f;
    rp.decaySeconds = 4.5f;
    rp.damping = 0.45f;
    rp.predelayMs = 25.0f;
    rp.modDepth = 0.5f;
    rp.width = 1.0f;
    reverb.setParams (rp);

    cutoffSmooth.setTarget (cutoff->load());
    volumeSmooth.setTarget (aa::dsp::dbToGain (volume->load()));

    for (int i = 0; i < numSamples; ++i)
    {
        if ((i & 15) == 0)
        {
            const float c = cutoffSmooth.current;
            filterL.setCutoffRes (c, 0.12f, sr);
            filterR.setCutoffRes (c, 0.12f, sr);
        }
        cutoffSmooth.next();
        const float l = filterL.lowpass (dryL[i]);
        const float r = filterR.lowpass (dryR[i]);
        float rl, rr;
        reverb.processSample (l, r, rl, rr);
        const float g = volumeSmooth.next();
        wetL[i] = (l * (1.0f - mix * 0.5f) + rl * mix * 1.2f) * g;
        wetR[i] = (r * (1.0f - mix * 0.5f) + rr * mix * 1.2f) * g;
        meter.process (0.5f * (std::abs (wetL[i]) + std::abs (wetR[i])));
    }

    for (int i = 0; i < numSamples; ++i)
    {
        outL[i] = aa::dsp::softClip (wetL[i] * 0.9f) / 0.9f;
        if (outR != nullptr)
            outR[i] = aa::dsp::softClip (wetR[i] * 0.9f) / 0.9f;
        else
            outL[i] = 0.5f * (outL[i] + aa::dsp::softClip (wetR[i] * 0.9f) / 0.9f);
    }

    meter.publish();
    outputLevel.store (meter.level.load());
}

//==============================================================================
void DandelionProcessor::retireOldSamples()
{
    const auto now = Time::getMillisecondCounter();
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [now] (const auto& r) { return now - r.second > 3000; }),
                   retired.end());
}

void DandelionProcessor::setUserSample (std::unique_ptr<dandelion::SampleData> sample, const File& file, MemoryBlock flac)
{
    const ScopedLock sl (sampleLock);
    retireOldSamples();
    auto* raw = sample.release();
    auto* old = userSample.exchange (raw);
    if (old != nullptr)
        retired.emplace_back (std::unique_ptr<dandelion::SampleData> (old), Time::getMillisecondCounter());
    userSampleFile = file;
    userSampleFlac = std::move (flac);
    ++sourceVersion;
}

void DandelionProcessor::clearUserSample()
{
    setUserSample (nullptr, {}, {});
    if (auto* p = param ("source"))
        if ((int) source->load() == userSourceIndex)
            p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
}

namespace
{
    std::unique_ptr<dandelion::SampleData> readToSample (AudioFormatReader& reader, const String& name)
    {
        const auto maxSamples = (juce::int64) (maxLoadSeconds * reader.sampleRate);
        const int n = (int) jmin (reader.lengthInSamples, maxSamples);
        if (n < 64 || reader.sampleRate <= 0.0)
            return nullptr;

        auto s = std::make_unique<dandelion::SampleData>();
        s->name = name;
        s->sampleRate = reader.sampleRate;
        s->buffer.setSize (2, n);
        s->buffer.clear();
        reader.read (&s->buffer, 0, n, 0, true, true);
        if (reader.numChannels == 1)
            s->buffer.copyFrom (1, 0, s->buffer, 0, 0, n);

        const float peak = s->buffer.getMagnitude (0, n);
        if (peak > 0.0001f)
            s->buffer.applyGain (0.9f / peak);
        return s;
    }

    MemoryBlock encodeFlac (const dandelion::SampleData& s)
    {
        MemoryBlock block;
        if (s.buffer.getNumSamples() > (int) (maxEmbedSeconds * s.sampleRate))
            return block;

        FlacAudioFormat flac;
        std::unique_ptr<OutputStream> stream = std::make_unique<MemoryOutputStream> (block, false);
        auto writer = flac.createWriterFor (stream, AudioFormatWriterOptions().withSampleRate (s.sampleRate)
                                                                              .withNumChannels (2)
                                                                              .withBitsPerSample (16));
        if (writer == nullptr)
            return {};
        writer->writeFromAudioSampleBuffer (s.buffer, 0, s.buffer.getNumSamples());
        writer.reset();
        return block;
    }
} // namespace

bool DandelionProcessor::loadSampleFile (const File& file, String& error)
{
    std::unique_ptr<AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Couldn't read " + file.getFileName();
        return false;
    }

    auto sample = readToSample (*reader, file.getFileNameWithoutExtension());
    if (sample == nullptr)
    {
        error = file.getFileName() + " is too short";
        return false;
    }

    auto flac = encodeFlac (*sample);
    setUserSample (std::move (sample), file, std::move (flac));

    if (auto* p = param ("source"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) userSourceIndex));
        p->endChangeGesture();
    }
    return true;
}

void DandelionProcessor::saveExtraState (ValueTree& extra)
{
    const ScopedLock sl (sampleLock);
    if (userSample.load() == nullptr)
        return;

    ValueTree s ("SAMPLE");
    s.setProperty ("path", userSampleFile.getFullPathName(), nullptr);
    s.setProperty ("name", getUserSampleName(), nullptr);
    if (userSampleFlac.getSize() > 0)
        s.setProperty ("flac", userSampleFlac.toBase64Encoding(), nullptr);
    extra.appendChild (s, nullptr);
}

void DandelionProcessor::loadExtraState (const ValueTree& extra)
{
    auto s = extra.getChildWithName ("SAMPLE");
    if (! s.isValid())
    {
        if (userSample.load() != nullptr)
            setUserSample (nullptr, {}, {});
        return;
    }

    const File file (s.getProperty ("path").toString());
    const String name = s.getProperty ("name").toString();

    MemoryBlock flac;
    if (flac.fromBase64Encoding (s.getProperty ("flac").toString()) && flac.getSize() > 0)
    {
        FlacAudioFormat format;
        std::unique_ptr<AudioFormatReader> reader (format.createReaderFor (new MemoryInputStream (flac, false), true));
        if (reader != nullptr)
            if (auto sample = readToSample (*reader, name.isNotEmpty() ? name : file.getFileNameWithoutExtension()))
            {
                setUserSample (std::move (sample), file, std::move (flac));
                return;
            }
    }

    if (file.existsAsFile())
    {
        String error;
        std::unique_ptr<AudioFormatReader> reader (formatManager.createReaderFor (file));
        if (reader != nullptr)
            if (auto sample = readToSample (*reader, file.getFileNameWithoutExtension()))
            {
                auto encoded = encodeFlac (*sample);
                setUserSample (std::move (sample), file, std::move (encoded));
            }
    }
}

AudioProcessorEditor* DandelionProcessor::createEditor()
{
    return new DandelionEditor (*this);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DandelionProcessor();
}
