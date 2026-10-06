#include "CritterProcessor.h"
#include "CritterEditor.h"

using namespace juce;
namespace P = aa::params;

namespace
{
    struct VoiceDefaults { float tune, decayMs, tone, snap, levelDb, pan; };

    // The "Critter Party" kit: every critter's sweet spot sits at Tone/Snap 50%.
    const VoiceDefaults voiceDefaults[critter::numVoices] = {
        { 0.0f, 480.0f, 50.0f, 50.0f,   0.0f,   0.0f },  // Bumble  (kick)
        { 0.0f, 230.0f, 50.0f, 50.0f,  -2.0f,   0.0f },  // Snappy  (snare)
        { 0.0f, 300.0f, 50.0f, 50.0f,  -3.0f, -12.0f },  // Clappo  (clap)
        { 0.0f,  60.0f, 50.0f, 50.0f,  -7.0f,  18.0f },  // Tiki    (closed hat)
        { 0.0f, 420.0f, 50.0f, 50.0f,  -9.0f,  24.0f },  // Tsss    (open hat)
        { 0.0f, 420.0f, 50.0f, 50.0f,  -6.0f, -28.0f },  // Boomer  (tom)
        { 0.0f, 220.0f, 50.0f, 50.0f,  -8.0f,  35.0f },  // Clink   (cowbell)
        { 0.0f, 220.0f, 50.0f, 50.0f, -10.0f, -20.0f },  // Zappy   (zap)
    };

    String panText (float v)
    {
        const int i = roundToInt (v);
        if (i == 0)
            return "C";
        return i < 0 ? String (-i) + "L" : String (i) + "R";
    }

    float panFromText (const String& text)
    {
        const auto t = text.trim().toUpperCase();
        if (t.startsWith ("C"))
            return 0.0f;
        const float v = std::abs (t.retainCharacters ("0123456789.").getFloatValue());
        if (t.containsChar ('L') || t.startsWith ("-"))
            return -v;
        return v;
    }

    std::unique_ptr<AudioParameterFloat> panParam (const String& id, const String& name, float def)
    {
        return std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, NormalisableRange<float> (-100.0f, 100.0f), def,
            AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return panText (v); })
                .withValueFromStringFunction ([] (const String& s) { return panFromText (s); }));
    }

    inline float levelToGain (float db) { return db <= -59.9f ? 0.0f : aa::dsp::dbToGain (db); }
    inline float roomToSend (float room01) { return std::pow (aa::dsp::clamp01 (room01), 1.2f) * 0.8f; }

    inline float safetyClip (float x)
    {
        const float a = std::abs (x);
        if (a <= 0.9f)
            return x;
        return std::copysign (0.9f + 0.099f * std::tanh ((a - 0.9f) / 0.099f), x);
    }

    using Pattern = std::array<const char*, critter::numVoices>;

    std::function<void (aa::PluginBase&)> withPattern (Pattern rows)
    {
        return [rows] (aa::PluginBase& base)
        {
            auto& p = static_cast<CritterProcessor&> (base);
            for (int r = 0; r < critter::numVoices; ++r)
                p.setRowPattern (r, rows[(size_t) r]);
        };
    }
} // namespace

//==============================================================================
AudioProcessorValueTreeState::ParameterLayout CritterProcessor::createLayout()
{
    P::Layout layout;

    // Bank 1 (Push shows parameters in banks of 8): the master controls.
    layout.add (P::floatParam ("volume", "Volume", -60.0f, 6.0f, 0.0f, P::Unit::db, -12.0f));
    layout.add (P::percent ("drive", "Drive", 20.0f));
    layout.add (P::percent ("swing", "Swing", 10.0f));
    layout.add (P::percent ("room", "Room", 15.0f));
    layout.add (P::percent ("accent", "Accent", 50.0f));
    layout.add (P::percent ("humanize", "Humanize", 10.0f));
    layout.add (P::floatParam ("pitch", "Kit Pitch", -12.0f, 12.0f, 0.0f, P::Unit::semitones));
    layout.add (P::toggle ("seq_on", "Sequencer", true));

    // Then each critter's six controls, contiguous.
    for (int v = 0; v < critter::numVoices; ++v)
    {
        const String role (critter::roleName (v));
        const auto& d = voiceDefaults[v];
        layout.add (P::floatParam (voiceParamId (v, "tune"), role + " Tune", -24.0f, 24.0f, d.tune, P::Unit::semitones));
        layout.add (P::floatParam (voiceParamId (v, "decay"), role + " Decay", 10.0f, 4000.0f, d.decayMs, P::Unit::ms, 300.0f));
        layout.add (P::percent (voiceParamId (v, "tone"), role + " Tone", d.tone));
        layout.add (P::percent (voiceParamId (v, "snap"), role + " Snap", d.snap));
        layout.add (P::floatParam (voiceParamId (v, "level"), role + " Level", -60.0f, 6.0f, d.levelDb, P::Unit::db, -12.0f));
        layout.add (panParam (voiceParamId (v, "pan"), role + " Pan", d.pan));
    }
    return layout;
}

CritterProcessor::CritterProcessor()
    : aa::PluginBase (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true), createLayout())
{
    formats.registerBasicFormats();

    volume = raw ("volume");
    drive = raw ("drive");
    swing = raw ("swing");
    room = raw ("room");
    accent = raw ("accent");
    humanize = raw ("humanize");
    pitch = raw ("pitch");
    seqOn = raw ("seq_on");

    for (int v = 0; v < critter::numVoices; ++v)
    {
        auto& vp = voiceParams[(size_t) v];
        vp.tune = raw (voiceParamId (v, "tune"));
        vp.decay = raw (voiceParamId (v, "decay"));
        vp.tone = raw (voiceParamId (v, "tone"));
        vp.snap = raw (voiceParamId (v, "snap"));
        vp.level = raw (voiceParamId (v, "level"));
        vp.pan = raw (voiceParamId (v, "pan"));
        vp.decayParam = param (voiceParamId (v, "decay"));
        sampleSlots[(size_t) v].store (nullptr);
    }

    tailSeconds = 4.0;

    //                     Kick                Snare               Clap                Closed hat
    //                     Open hat            Tom                 Cowbell             Zap
    setFactoryPresets ({
        { "Critter Party", {},
          withPattern ({ "X.....x.X.....x.", "....X.......X...", "....x.......x..x", "x.X.x.X.x.X.x...",
                         "..............x.", "..........x..x..", "..x.......x.....", "...............x" }) },

        { "Boom Bap Bugs",
          { { "swing", 55.0f }, { "drive", 45.0f }, { "room", 18.0f }, { "humanize", 25.0f }, { "pitch", -1.0f },
            { "k1_decay", 650.0f }, { "k1_tone", 62.0f }, { "k1_snap", 40.0f }, { "k1_tune", -2.0f },
            { "k2_tune", -3.0f }, { "k2_tone", 35.0f }, { "k2_snap", 65.0f }, { "k2_decay", 260.0f },
            { "k3_level", -9.0f }, { "k3_tone", 40.0f },
            { "k4_tone", 35.0f }, { "k4_decay", 45.0f }, { "k4_level", -8.0f } },
          withPattern ({ "X......X.X......", "....X.......X...", "............x...", "X.x.X.x.X.x.X.x.",
                         "................", "................", "................", "................" }) },

        { "Four on the Floor Frogs",
          { { "swing", 0.0f }, { "drive", 25.0f }, { "room", 22.0f },
            { "k1_decay", 380.0f }, { "k1_snap", 60.0f },
            { "k3_decay", 350.0f }, { "k3_tone", 55.0f }, { "k3_level", -2.0f },
            { "k4_decay", 40.0f }, { "k4_level", -9.0f }, { "k5_decay", 200.0f }, { "k5_level", -7.0f },
            { "k6_tune", 10.0f }, { "k6_decay", 140.0f }, { "k6_snap", 85.0f }, { "k6_tone", 75.0f },
            { "k6_level", -9.0f }, { "k6_pan", 30.0f } },
          withPattern ({ "X...X...X...X...", "................", "....X.......X...", "...x...x...x...x",
                         "..x...x...x...x.", ".......x......x.", "................", "................" }) },

        { "Breakbeat Beasties",
          { { "swing", 18.0f }, { "drive", 50.0f }, { "room", 28.0f }, { "humanize", 30.0f }, { "accent", 80.0f },
            { "k1_decay", 350.0f }, { "k1_snap", 65.0f }, { "k1_tone", 55.0f },
            { "k2_tone", 60.0f }, { "k2_snap", 70.0f }, { "k2_decay", 200.0f }, { "k2_level", 0.0f },
            { "k4_decay", 50.0f } },
          withPattern ({ "X.x.......xX....", "....X..x.x..X..x", "................", "X.x.X.x.X.x.X...",
                         "..............x.", "................", "................", "................" }) },

        { "Lo-Fi Lizards",
          { { "swing", 60.0f }, { "drive", 55.0f }, { "room", 35.0f }, { "humanize", 30.0f }, { "pitch", -2.0f },
            { "volume", -1.0f },
            { "k1_tone", 30.0f }, { "k1_decay", 600.0f }, { "k2_tone", 25.0f }, { "k2_decay", 300.0f },
            { "k2_snap", 40.0f }, { "k4_tone", 25.0f }, { "k4_decay", 70.0f }, { "k6_tone", 20.0f },
            { "k6_decay", 500.0f }, { "k6_level", -8.0f } },
          withPattern ({ "X......x..X.....", "....X.......X...", "................", "x.x.x.x.x.x.x.x.",
                         "................", "..............x.", "................", "................" }) },

        { "Electro Gecko",
          { { "swing", 0.0f }, { "drive", 30.0f }, { "room", 20.0f },
            { "k1_decay", 1100.0f }, { "k1_snap", 35.0f }, { "k1_tone", 20.0f },
            { "k3_tone", 60.0f }, { "k4_decay", 45.0f },
            { "k7_level", -6.0f }, { "k7_tone", 55.0f }, { "k8_tone", 70.0f }, { "k8_decay", 160.0f }, { "k8_level", -9.0f } },
          withPattern ({ "X.....x...X..x..", "................", "....X.......X...", "x.xxx.xxx.xxx.xx",
                         "................", "................", "..x..x....x..x..", "...........x...x" }) },

        { "Trap Toads",
          { { "swing", 0.0f }, { "drive", 35.0f }, { "room", 18.0f }, { "humanize", 15.0f },
            { "k1_decay", 1600.0f }, { "k1_tune", -3.0f }, { "k1_snap", 30.0f }, { "k1_tone", 40.0f },
            { "k2_tune", 2.0f }, { "k2_tone", 55.0f }, { "k2_decay", 200.0f }, { "k3_decay", 280.0f },
            { "k4_decay", 35.0f }, { "k4_level", -8.0f },
            { "k8_tune", -5.0f }, { "k8_tone", 30.0f }, { "k8_snap", 30.0f }, { "k8_decay", 180.0f }, { "k8_level", -10.0f } },
          withPattern ({ "X......X..X.....", "........X.......", "........X.......", "X.x.X.xxX.x.Xxxx",
                         "................", "................", "................", "..............x." }) },

        { "Reggaeton Raccoons",
          { { "swing", 0.0f }, { "drive", 25.0f }, { "room", 20.0f },
            { "k1_decay", 400.0f }, { "k2_tune", 3.0f }, { "k2_tone", 60.0f }, { "k2_decay", 160.0f }, { "k2_snap", 60.0f } },
          withPattern ({ "X...X...X...X...", "...X..x....X..x.", "................", "..x...x...x...x.",
                         "................", "................", "................", "................" }) },

        { "Minimal Moths",
          { { "swing", 8.0f }, { "drive", 10.0f }, { "room", 40.0f }, { "humanize", 15.0f },
            { "k1_decay", 420.0f }, { "k3_level", -6.0f }, { "k3_decay", 450.0f },
            { "k4_decay", 45.0f }, { "k4_tone", 60.0f },
            { "k7_snap", 95.0f }, { "k7_tune", 5.0f }, { "k7_decay", 80.0f }, { "k7_level", -8.0f },
            { "k8_decay", 30.0f }, { "k8_tune", 12.0f }, { "k8_tone", 20.0f }, { "k8_level", -12.0f } },
          withPattern ({ "X...X...X...X...", "................", "............X...", "..x...x...x...x.",
                         "................", "................", ".x....x..x...x..", "......x........." }) },

        { "Tribal Turtles",
          { { "swing", 12.0f }, { "drive", 30.0f }, { "room", 30.0f }, { "humanize", 25.0f },
            { "k4_level", -12.0f }, { "k4_tone", 60.0f }, { "k4_decay", 30.0f },
            { "k6_decay", 600.0f }, { "k6_tone", 40.0f }, { "k6_snap", 40.0f }, { "k6_tune", -3.0f }, { "k6_level", -2.0f },
            { "k7_snap", 100.0f }, { "k7_decay", 120.0f }, { "k7_level", -7.0f } },
          withPattern ({ "X.....x.X.......", "................", "....x.......x...", "x.x.x.x.x.x.x.x.",
                         "................", "..x..x...x..x.x.", "x..x..x...x.x...", "................" }) },

        { "Space Slugs",
          { { "swing", 0.0f }, { "room", 55.0f }, { "drive", 20.0f },
            { "k1_decay", 900.0f }, { "k1_tune", -2.0f },
            { "k5_decay", 600.0f }, { "k5_level", -10.0f },
            { "k8_decay", 320.0f }, { "k8_tone", 60.0f }, { "k8_snap", 70.0f }, { "k8_level", -8.0f } },
          withPattern ({ "X.........X.....", "........X.......", "................", "x...x...x...x...",
                         "..x.......x.....", "................", "................", "...x..x.....x.x." }) },
    });

    // Start with the default preset's pattern.
    if (auto& extra = getFactoryPresets().front().applyExtra)
        extra (*this);
}

CritterProcessor::~CritterProcessor()
{
    if (loaderPool != nullptr)
        loaderPool->removeAllJobs (true, 5000);

    for (auto& slot : sampleSlots)
        delete slot.exchange (nullptr);

    const ScopedLock sl (graveyardLock);
    graveyard.clear();
}

//==============================================================================
bool CritterProcessor::isStepOn (int row, int step) const noexcept
{
    return ((rows[(size_t) row].load (std::memory_order_relaxed) >> step) & 1u) != 0;
}

bool CritterProcessor::isStepAccent (int row, int step) const noexcept
{
    return ((rows[(size_t) row].load (std::memory_order_relaxed) >> (16 + step)) & 1u) != 0;
}

void CritterProcessor::setStep (int row, int step, bool on, bool acc)
{
    if (! isPositiveAndBelow (row, critter::numVoices) || ! isPositiveAndBelow (step, critter::numSteps))
        return;

    uint32_t bits = rows[(size_t) row].load();
    bits &= ~((1u << step) | (1u << (16 + step)));
    if (on)
        bits |= (1u << step) | (acc ? (1u << (16 + step)) : 0u);
    setRowBits (row, bits);
}

void CritterProcessor::setRowBits (int row, uint32_t bits)
{
    // accents only exist on steps that are on
    bits &= 0xffffu | ((bits & 0xffffu) << 16);
    rows[(size_t) row].store (bits);
    patternVersion.fetch_add (1);
}

void CritterProcessor::setRowPattern (int row, const String& text)
{
    uint32_t bits = 0;
    for (int s = 0; s < jmin (critter::numSteps, text.length()); ++s)
    {
        const auto c = text[s];
        if (c == 'x' || c == 'X' || c == 'o' || c == 'O')
            bits |= 1u << s;
        if (c == 'X' || c == 'O')
            bits |= 1u << (16 + s);
    }
    setRowBits (row, bits);
}

String CritterProcessor::getRowPattern (int row) const
{
    String s;
    const auto bits = getRowBits (row);
    for (int i = 0; i < critter::numSteps; ++i)
        s << (((bits >> i) & 1u) == 0 ? '.' : (((bits >> (16 + i)) & 1u) != 0 ? 'X' : 'x'));
    return s;
}

//==============================================================================
void CritterProcessor::saveExtraState (ValueTree& extra)
{
    ValueTree pattern ("PATTERN");
    for (int r = 0; r < critter::numVoices; ++r)
        pattern.setProperty ("r" + String (r), getRowPattern (r), nullptr);
    extra.appendChild (pattern, nullptr);

    const ScopedLock sl (graveyardLock); // keeps the samples alive while we copy them
    for (int v = 0; v < critter::numVoices; ++v)
        if (auto* s = sampleSlots[(size_t) v].load())
        {
            ValueTree t ("SAMPLE");
            t.setProperty ("voice", v, nullptr);
            t.setProperty ("path", s->filePath, nullptr);
            t.setProperty ("name", s->displayName, nullptr);
            t.setProperty ("format", s->embeddedFormat, nullptr);
            t.setProperty ("data", s->embedded.toBase64Encoding(), nullptr);
            extra.appendChild (t, nullptr);
        }
}

void CritterProcessor::loadExtraState (const ValueTree& extra)
{
    const auto pattern = extra.getChildWithName ("PATTERN");
    for (int r = 0; r < critter::numVoices; ++r)
        setRowPattern (r, pattern.isValid() ? pattern.getProperty ("r" + String (r)).toString() : String());

    std::array<bool, critter::numVoices> found {};
    for (const auto& child : extra)
    {
        if (! child.hasType ("SAMPLE"))
            continue;

        const int v = child.getProperty ("voice", -1);
        if (! isPositiveAndBelow (v, critter::numVoices) || found[(size_t) v])
            continue;

        MemoryBlock block;
        block.fromBase64Encoding (child.getProperty ("data").toString());
        const auto path = child.getProperty ("path").toString();

        {
            // Same sample already loaded? (hosts like to restore the same state repeatedly)
            const ScopedLock sl (graveyardLock);
            if (auto* existing = sampleSlots[(size_t) v].load())
                if (existing->embedded == block && existing->filePath == path)
                {
                    found[(size_t) v] = true;
                    continue;
                }
        }

        auto data = critter::SampleCodec::fromEmbedded (formats, block, child.getProperty ("format").toString(), path,
                                                       child.getProperty ("name").toString());
        if (data == nullptr && path.isNotEmpty() && File::isAbsolutePath (path))
        {
            String error;
            data = critter::SampleCodec::fromFile (formats, File (path), error);
        }

        if (data != nullptr)
        {
            installSample (v, std::move (data));
            found[(size_t) v] = true;
        }
    }

    for (int v = 0; v < critter::numVoices; ++v)
        if (! found[(size_t) v])
            clearSample (v);
}

void CritterProcessor::resetExtraState()
{
    for (int r = 0; r < critter::numVoices; ++r)
        setRowBits (r, 0);
    for (int v = 0; v < critter::numVoices; ++v)
        clearSample (v);
}

//==============================================================================
void CritterProcessor::loadSampleAsync (int voice, const File& file)
{
    if (! isPositiveAndBelow (voice, critter::numVoices))
        return;

    if (loaderPool == nullptr)
        loaderPool = std::make_unique<ThreadPool> (ThreadPoolOptions().withThreadName ("Critter Kit loader").withNumberOfThreads (1));

    loading[(size_t) voice] = true;
    loaderPool->addJob ([this, voice, file]
    {
        String error;
        auto data = critter::SampleCodec::fromFile (formats, file, error);
        if (data != nullptr)
        {
            installSample (voice, std::move (data));
            hostNotifyPending = true;
        }
        else
        {
            loadFailures[(size_t) voice].fetch_add (1);
        }
        loading[(size_t) voice] = false;
    });
}

void CritterProcessor::clearSample (int voice)
{
    if (sampleSlots[(size_t) voice].load() != nullptr)
        installSample (voice, nullptr);
}

void CritterProcessor::installSample (int voice, std::unique_ptr<critter::SampleData> data)
{
    auto* old = sampleSlots[(size_t) voice].exchange (data.release());
    if (old != nullptr)
        retire (old);
    sampleVersion[(size_t) voice].fetch_add (1);
    collectGarbage();
}

void CritterProcessor::retire (critter::SampleData* old)
{
    const ScopedLock sl (graveyardLock);
    // The epoch is read after the pointer swap: if the audio thread is mid-block (odd) it may still
    // be reading `old`, so it can only go once the epoch has moved on.
    graveyard.push_back ({ std::unique_ptr<critter::SampleData> (old), audioEpoch.load() });
    graveyardSize = (int) graveyard.size();
}

void CritterProcessor::collectGarbage()
{
    if (graveyardSize.load() == 0 || ! MessageManager::existsAndIsCurrentThread())
        return;

    const ScopedLock sl (graveyardLock);
    const auto now = audioEpoch.load();
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
                                     [now] (const Retired& r) { return (r.epoch & 1u) == 0 || r.epoch != now; }),
                     graveyard.end());
    graveyardSize = (int) graveyard.size();
}

void CritterProcessor::notifyStateChanged()
{
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

//==============================================================================
void CritterProcessor::prepareToPlay (double sampleRate, int)
{
    sr = (float) sampleRate;
    transport.prepare (sampleRate);
    for (int v = 0; v < critter::numVoices; ++v)
        voices[(size_t) v].prepare (sr, v);

    reverb.prepare (sampleRate);
    lastRoom = -1.0f;
    driveSm.reset (sampleRate, 0.03, drive->load() / 100.0f);
    roomSm.reset (sampleRate, 0.05, roomToSend (room->load() / 100.0f));
    volumeSm.reset (sampleRate, 0.03, levelToGain (volume->load()));
    lastStepIndex = std::numeric_limits<int64_t>::min();
    expectedPpq = 0.0;
    dcL = dcR = dcInL = dcInR = 0.0f;
    numEvents = 0;
}

void CritterProcessor::reset()
{
    for (auto& v : voices)
        v.reset();
    reverb.reset();
    dcL = dcR = dcInL = dcInR = 0.0f;
}

void CritterProcessor::addEvent (int offset, int voice, float velocity, float detune, int type)
{
    if (numEvents < maxEvents)
        events[(size_t) numEvents++] = { offset, voice, velocity, detune, type };
}

void CritterProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    buffer.clear();
    if (numSamples <= 0 || numChannels <= 0)
        return;

    audioEpoch.fetch_add (1); // odd: we may now be reading sample pointers
    std::array<const critter::SampleData*, critter::numVoices> blockSamples {};
    for (int v = 0; v < critter::numVoices; ++v)
        blockSamples[(size_t) v] = sampleSlots[(size_t) v].load();

    transport.update (getPlayHead(), numSamples);

    // ---- voice settings ------------------------------------------------------
    const float kitPitch = pitch->load();
    for (int v = 0; v < critter::numVoices; ++v)
    {
        const auto& vp = voiceParams[(size_t) v];
        critter::VoiceSettings s;
        const float decayMs = vp.decay->load();
        s.tune = vp.tune->load() + kitPitch;
        s.decay = decayMs * 0.001f;
        s.decayNorm = vp.decayParam != nullptr ? vp.decayParam->convertTo0to1 (decayMs) : 0.5f;
        s.tone = vp.tone->load() / 100.0f;
        s.snap = vp.snap->load() / 100.0f;
        s.level = levelToGain (vp.level->load());
        s.pan = jlimit (-1.0f, 1.0f, vp.pan->load() / 100.0f);
        voices[(size_t) v].update (s);
    }

    // ---- events: MIDI, auditions, sequencer ---------------------------------
    numEvents = 0;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const int offset = jlimit (0, numSamples - 1, meta.samplePosition);
        if (msg.isNoteOn())
        {
            const int v = msg.getNoteNumber() - critter::firstNote;
            if (isPositiveAndBelow (v, critter::numVoices))
                addEvent (offset, v, msg.getFloatVelocity(), 0.0f, 0);
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            addEvent (offset, -1, 0.0f, 0.0f, 1);
        }
    }

    if (const auto mask = auditionMask.exchange (0))
        for (int v = 0; v < critter::numVoices; ++v)
            if ((mask >> v) & 1u)
                addEvent (0, v, 0.85f, 0.0f, 0);

    const bool running = seqOn->load() > 0.5f && transport.hostPlaying;
    if (running)
    {
        constexpr double stepLen = 0.25; // a 16th note in quarter notes
        const double swingPpq = jlimit (0.0, 1.0, (double) swing->load() / 100.0) * 0.125;
        const double ppqPerSample = transport.samplesToPpq (1.0);
        const double ppq0 = transport.ppqAtBlockStart;
        const double ppq1 = ppq0 + ppqPerSample * numSamples;

        if (transport.justStarted || ppq0 < expectedPpq - 0.01)
            lastStepIndex = std::numeric_limits<int64_t>::min(); // started, or the host looped back
        expectedPpq = ppq1;

        const float accentAmt = accent->load() / 100.0f;
        const float human = humanize->load() / 100.0f;
        const auto firstStep = (int64_t) std::floor (ppq0 / stepLen) - 1;
        const auto lastStep = (int64_t) std::floor (ppq1 / stepLen);

        for (auto n = firstStep; n <= lastStep; ++n)
        {
            if (n <= lastStepIndex)
                continue;
            const double t = (double) n * stepLen + ((n & 1) != 0 ? swingPpq : 0.0);
            if (t < ppq0 || t >= ppq1)
                continue;

            lastStepIndex = n;
            const int offset = jlimit (0, numSamples - 1, (int) ((t - ppq0) / ppqPerSample));
            const int step = (int) (((n % critter::numSteps) + critter::numSteps) % critter::numSteps);

            for (int r = 0; r < critter::numVoices; ++r)
            {
                const auto bits = rows[(size_t) r].load (std::memory_order_relaxed);
                if (((bits >> step) & 1u) == 0)
                    continue;
                const bool acc = ((bits >> (16 + step)) & 1u) != 0;
                float vel = acc ? 1.0f : 1.0f - 0.5f * accentAmt;
                float detune = 0.0f;
                if (human > 0.0f)
                {
                    vel *= 1.0f - human * 0.35f * humanRng.next01();
                    detune = human * 0.25f * humanRng.nextBipolar();
                }
                addEvent (offset, r, vel, detune, 0);
            }
        }

        const double stepPos = ppq1 / stepLen;
        const auto stepIndex = (int64_t) std::floor (stepPos);
        uiStep.store ((int) (((stepIndex % critter::numSteps) + critter::numSteps) % critter::numSteps));
        uiStepPhase.store (stepPos - std::floor (stepPos));
    }
    else
    {
        lastStepIndex = std::numeric_limits<int64_t>::min();
        expectedPpq = transport.ppqAtBlockStart;
        uiStep.store (-1);
    }
    uiRunning.store (running);

    // stable insertion sort by offset (MIDI and sequencer events interleave)
    for (int i = 1; i < numEvents; ++i)
    {
        const auto e = events[(size_t) i];
        int j = i - 1;
        while (j >= 0 && events[(size_t) j].offset > e.offset)
        {
            events[(size_t) j + 1] = events[(size_t) j];
            --j;
        }
        events[(size_t) j + 1] = e;
    }

    // ---- master settings ----------------------------------------------------
    const float room01 = room->load() / 100.0f;
    if (std::abs (room01 - lastRoom) > 0.005f)
    {
        lastRoom = room01;
        aa::dsp::FdnReverb::Params rp;
        rp.size = 0.22f + 0.25f * room01;
        rp.decaySeconds = 0.45f + 0.9f * room01;
        rp.damping = 0.55f;
        rp.predelayMs = 8.0f;
        rp.modDepth = 0.25f;
        rp.modRate = 0.5f;
        rp.width = 1.0f;
        rp.lowCutHz = 280.0f;
        reverb.setParams (rp);
    }
    roomSm.setTarget (roomToSend (room01));
    driveSm.setTarget (drive->load() / 100.0f);
    volumeSm.setTarget (levelToGain (volume->load()));
    const float dcCoeff = 1.0f - aa::dsp::twoPi * 8.0f / sr;

    // ---- render in chunks, splitting at events ------------------------------
    float* outL = buffer.getWritePointer (0);
    float* outR = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;
    int ev = 0;

    for (int chunkStart = 0; chunkStart < numSamples; chunkStart += critter::maxChunk)
    {
        const int chunkLen = jmin (critter::maxChunk, numSamples - chunkStart);
        std::fill (scratchL, scratchL + chunkLen, 0.0f);
        std::fill (scratchR, scratchR + chunkLen, 0.0f);

        int pos = 0;
        while (pos < chunkLen)
        {
            while (ev < numEvents && events[(size_t) ev].offset <= chunkStart + pos)
            {
                const auto& e = events[(size_t) ev++];
                if (e.type == 1)
                {
                    for (auto& v : voices)
                        v.choke (0.005f);
                    continue;
                }
                if (e.voice == critter::closedHat)
                    voices[critter::openHat].choke (0.008f); // Tiki shushes Tsss
                voices[(size_t) e.voice].trigger (e.velocity, e.detune, blockSamples[(size_t) e.voice]);
                hits.push ({ e.voice, e.velocity });
            }

            int end = chunkLen;
            if (ev < numEvents)
                end = jmin (end, events[(size_t) ev].offset - chunkStart);
            end = jmax (end, pos + 1);

            for (int v = 0; v < critter::numVoices; ++v)
                if (voices[(size_t) v].isActive())
                    voices[(size_t) v].render (scratchL + pos, scratchR + pos, end - pos, blockSamples[(size_t) v]);
            pos = end;
        }

        // master: room -> drive -> volume -> DC blocker -> safety clip
        for (int i = 0; i < chunkLen; ++i)
        {
            float l = scratchL[i], r = scratchR[i];

            const float send = roomSm.next();
            float wl = 0.0f, wr = 0.0f;
            reverb.processSample (l * send, r * send, wl, wr);
            l += wl;
            r += wr;

            const float d = driveSm.next();
            if (d > 0.0005f)
            {
                const float g = 1.0f + 3.0f * d;
                const float norm = 0.6f / aa::dsp::softClip (0.6f * g);
                l = aa::dsp::softClip (l * g) * norm;
                r = aa::dsp::softClip (r * g) * norm;
            }

            const float vol = volumeSm.next();
            l *= vol;
            r *= vol;

            const float hl = l - dcInL + dcCoeff * dcL;
            const float hr = r - dcInR + dcCoeff * dcR;
            dcInL = l;
            dcInR = r;
            dcL = aa::dsp::undenormalise (hl);
            dcR = aa::dsp::undenormalise (hr);

            l = safetyClip (hl);
            r = safetyClip (hr);

            if (outR != nullptr)
            {
                outL[chunkStart + i] = l;
                outR[chunkStart + i] = r;
            }
            else
            {
                outL[chunkStart + i] = 0.5f * (l + r);
            }
        }
    }

    audioEpoch.fetch_add (1); // even: finished with this block's sample pointers
}

AudioProcessorEditor* CritterProcessor::createEditor()
{
    return new CritterEditor (*this);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CritterProcessor();
}
