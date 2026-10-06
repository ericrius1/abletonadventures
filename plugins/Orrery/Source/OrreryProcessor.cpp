#include "OrreryProcessor.h"
#include "OrreryEditor.h"

using namespace juce;
namespace P = aa::params;

namespace
{
    std::unique_ptr<AudioParameterInt> intParam (const String& id, const String& name, int min, int max, int def,
                                                 std::function<String (int)> toText,
                                                 std::function<int (const String&)> fromText)
    {
        return std::make_unique<AudioParameterInt> (
            ParameterID { id, 1 }, name, min, max, def,
            AudioParameterIntAttributes()
                .withStringFromValueFunction ([toText] (int v, int) { return toText (v); })
                .withValueFromStringFunction ([fromText] (const String& s) { return fromText (s); }));
    }

    String materialText (float v)
    {
        const char* name = v < 12.5f ? "Glass" : v < 37.5f ? "Glass/Wood" : v < 62.5f ? "Wood" : v < 87.5f ? "Wood/Metal" : "Metal";
        return String (name) + " " + String (roundToInt (v)) + "%";
    }

    int parseSigned (const String& s)
    {
        auto t = s.trim();
        const bool negative = t.startsWithChar ('-');
        const int v = t.trimCharactersAtStart ("+-").getIntValue();
        return negative ? -v : v;
    }

    /** Gentle safety limiter: transparent below -1 dBFS, soft above. */
    inline float safety (float x)
    {
        const float a = std::abs (x);
        if (a <= 0.89f)
            return x;
        return std::copysign (0.89f + 0.11f * aa::dsp::softClip ((a - 0.89f) / 0.11f), x);
    }

    // ---- presets --------------------------------------------------------------
    struct Orb
    {
        int beats, pulses, note, octave;
        float velocity, chance, gate, offset;
        bool on = true;
    };

    const Orb off { 4, 4, 0, 0, 70.0f, 100.0f, 50.0f, 0.0f, false };

    using Values = std::vector<std::pair<String, float>>;

    Values preset (Values globals, const std::array<Orb, orrery::numOrbits>& orbs)
    {
        for (int i = 0; i < orrery::numOrbits; ++i)
        {
            const auto& o = orbs[(size_t) i];
            globals.push_back ({ orrery::orbitParamId (i, "on"), o.on ? 1.0f : 0.0f });
            globals.push_back ({ orrery::orbitParamId (i, "beats"), (float) o.beats });
            globals.push_back ({ orrery::orbitParamId (i, "pulses"), (float) o.pulses });
            globals.push_back ({ orrery::orbitParamId (i, "note"), (float) o.note });
            globals.push_back ({ orrery::orbitParamId (i, "octave"), (float) o.octave });
            globals.push_back ({ orrery::orbitParamId (i, "vel"), o.velocity });
            globals.push_back ({ orrery::orbitParamId (i, "prob"), o.chance });
            globals.push_back ({ orrery::orbitParamId (i, "gate"), o.gate });
            globals.push_back ({ orrery::orbitParamId (i, "offset"), o.offset });
        }
        return globals;
    }

    // Default orbit settings ("Clockwork Cosmos"): a 4/4 pulse with 3-, 5-, 7- and 9-against-4
    // polyrhythms and a slow 2-in-3 bass, all in C major pentatonic.
    constexpr int defBeats[]   = { 4, 4, 4, 8, 3, 16 };
    constexpr int defPulses[]  = { 4, 3, 5, 7, 2, 9 };
    constexpr int defNote[]    = { 0, 2, 4, 6, 0, 8 };
    constexpr int defOctave[]  = { 0, 0, 0, 0, -1, 0 };
    constexpr float defVel[]   = { 72.0f, 66.0f, 58.0f, 60.0f, 74.0f, 52.0f };
    constexpr float defProb[]  = { 100.0f, 100.0f, 80.0f, 100.0f, 100.0f, 75.0f };
    constexpr float defGate[]  = { 40.0f, 50.0f, 50.0f, 50.0f, 80.0f, 50.0f };
} // namespace

//==============================================================================
AudioProcessorValueTreeState::ParameterLayout OrreryProcessor::createLayout()
{
    P::Layout layout;

    // The first eight (Push's first bank) are the most playable.
    layout.add (P::choice ("scale", "Scale", orrery::scaleNames(), 2));
    layout.add (P::choice ("root", "Root", orrery::rootNames(), 0));
    layout.add (P::choice ("speed", "Speed", orrery::speedNames(), 4));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "material", 1 }, "Material", NormalisableRange<float> (0.0f, 100.0f), 22.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return materialText (v); })
            .withValueFromStringFunction ([] (const String& s) { return s.retainCharacters ("0123456789.").getFloatValue(); })));
    layout.add (P::percent ("bright", "Brightness", 55.0f));
    layout.add (P::floatParam ("decay", "Decay", 0.2f, 10.0f, 2.6f, P::Unit::seconds, 2.0f));
    layout.add (P::percent ("reverb", "Reverb", 32.0f));
    layout.add (P::floatParam ("volume", "Volume", -30.0f, 6.0f, 0.0f, P::Unit::db));

    layout.add (P::toggle ("freerun", "Free Run", false));
    layout.add (P::toggle ("follow", "Follow MIDI", true));
    layout.add (P::toggle ("sound", "Sound", true));
    layout.add (P::percent ("spread", "Spread", 60.0f));
    layout.add (P::percent ("echo", "Echo", 22.0f));
    layout.add (P::choice ("echotime", "Echo Time", aa::dsp::syncDivisionNames(), 6));
    layout.add (P::floatParam ("feedback", "Feedback", 0.0f, 90.0f, 38.0f, P::Unit::percent));

    for (int i = 0; i < orrery::numOrbits; ++i)
    {
        const String name = orrery::planets()[(size_t) i].name;
        auto id = [i] (const char* s) { return orrery::orbitParamId (i, s); };

        layout.add (P::toggle (id ("on"), name + " On", true));
        layout.add (intParam (id ("beats"), name + " Beats", 1, 16, defBeats[i],
                              [] (int v) { return String (v) + (v == 1 ? " beat" : " beats"); },
                              [] (const String& s) { return s.getIntValue(); }));
        layout.add (intParam (id ("pulses"), name + " Pulses", 1, 16, defPulses[i],
                              [] (int v) { return String (v) + (v == 1 ? " pulse" : " pulses"); },
                              [] (const String& s) { return s.getIntValue(); }));
        layout.add (intParam (id ("note"), name + " Note", 0, orrery::maxDegree, defNote[i],
                              [] (int v) { return v == 0 ? String ("Root") : orrery::ordinal (v + 1); },
                              [] (const String& s) { return s.containsIgnoreCase ("root") ? 0 : jmax (0, s.getIntValue() - 1); }));
        layout.add (intParam (id ("octave"), name + " Octave", -2, 2, defOctave[i],
                              [] (int v) { return (v > 0 ? "+" : "") + String (v) + " oct"; },
                              [] (const String& s) { return parseSigned (s); }));
        layout.add (P::floatParam (id ("vel"), name + " Velocity", 1.0f, 100.0f, defVel[i], P::Unit::percent));
        layout.add (P::percent (id ("prob"), name + " Chance", defProb[i]));
        layout.add (P::floatParam (id ("gate"), name + " Gate", 5.0f, 100.0f, defGate[i], P::Unit::percent));
        layout.add (P::percent (id ("offset"), name + " Offset", 0.0f));
    }
    return layout;
}

OrreryProcessor::OrreryProcessor()
    : aa::PluginBase (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true), createLayout())
{
    scale = raw ("scale");
    root = raw ("root");
    speed = raw ("speed");
    freeRun = raw ("freerun");
    follow = raw ("follow");
    sound = raw ("sound");
    material = raw ("material");
    brightness = raw ("bright");
    decay = raw ("decay");
    spread = raw ("spread");
    echo = raw ("echo");
    echoTime = raw ("echotime");
    feedback = raw ("feedback");
    reverb = raw ("reverb");
    volume = raw ("volume");

    for (int i = 0; i < orrery::numOrbits; ++i)
    {
        auto& o = orbitParams[(size_t) i];
        o.on = raw (orrery::orbitParamId (i, "on"));
        o.beats = raw (orrery::orbitParamId (i, "beats"));
        o.pulses = raw (orrery::orbitParamId (i, "pulses"));
        o.offset = raw (orrery::orbitParamId (i, "offset"));
        o.note = raw (orrery::orbitParamId (i, "note"));
        o.octave = raw (orrery::orbitParamId (i, "octave"));
        o.velocity = raw (orrery::orbitParamId (i, "vel"));
        o.chance = raw (orrery::orbitParamId (i, "prob"));
        o.gate = raw (orrery::orbitParamId (i, "gate"));
    }

    tailSeconds = 8.0;

    setFactoryPresets ({
        { "Clockwork Cosmos", {} },

        { "Three Against Four",
          preset ({ { "scale", 1 }, { "root", 9 }, { "material", 14 }, { "bright", 50 }, { "decay", 2.2f },
                    { "echo", 18 }, { "echotime", 5 }, { "reverb", 28 } },
                  { { { 4, 4, 0, -1, 74, 100, 40, 0 }, { 4, 3, 4, 0, 70, 100, 50, 0 }, { 4, 4, 2, 0, 50, 70, 30, 50 },
                      { 4, 6, 7, 0, 46, 55, 30, 0 }, { 4, 1, 0, -2, 72, 100, 90, 0 }, off } }) },

        { "Celestial Waltz",
          preset ({ { "scale", 5 }, { "root", 5 }, { "material", 30 }, { "bright", 48 }, { "decay", 3.2f },
                    { "echo", 20 }, { "echotime", 9 }, { "reverb", 42 }, { "volume", 1.5f } },
                  { { { 3, 3, 4, 0, 52, 100, 40, 0 }, { 6, 4, 2, 0, 58, 100, 50, 0 }, { 12, 5, 6, 0, 50, 90, 50, 0 },
                      { 6, 2, 9, 0, 44, 80, 50, 0 }, { 3, 1, 0, -1, 80, 100, 90, 0 }, { 24, 7, 11, 0, 40, 70, 50, 0 } } }) },

        { "Pentatonic Rain",
          preset ({ { "scale", 2 }, { "root", 7 }, { "material", 8 }, { "bright", 70 }, { "decay", 3.5f },
                    { "echo", 34 }, { "feedback", 50 }, { "reverb", 55 }, { "spread", 85 }, { "volume", 4.0f } },
                  { { { 1, 4, 7, 0, 45, 22, 30, 0 }, { 2, 7, 9, 0, 40, 30, 30, 0 }, { 3, 8, 5, 0, 45, 34, 30, 0 },
                      { 1, 3, 11, 0, 38, 18, 30, 0 }, { 4, 2, 0, -1, 56, 100, 80, 0 }, { 5, 6, 12, 0, 35, 25, 30, 0 } } }) },

        { "Kepler's Dream",
          preset ({ { "scale", 7 }, { "root", 4 }, { "speed", 2 }, { "material", 35 }, { "bright", 45 }, { "decay", 5.0f },
                    { "echo", 30 }, { "echotime", 8 }, { "feedback", 45 }, { "reverb", 60 }, { "spread", 80 } },
                  { { { 2, 3, 6, 0, 52, 90, 50, 0 }, { 3, 4, 4, 0, 56, 100, 50, 0 }, { 4, 5, 2, 0, 54, 90, 50, 0 },
                      { 5, 6, 3, 0, 50, 85, 50, 0 }, { 6, 7, 0, -1, 66, 100, 70, 0 }, { 8, 9, 8, 0, 44, 80, 50, 0 } } }) },

        { "Gamelan Moons",
          preset ({ { "scale", 8 }, { "root", 2 }, { "material", 100 }, { "bright", 60 }, { "decay", 3.0f },
                    { "echo", 14 }, { "echotime", 2 }, { "reverb", 30 } },
                  { { { 2, 4, 5, 0, 60, 100, 30, 0 }, { 2, 4, 6, 0, 54, 100, 30, 50 }, { 4, 2, 2, 0, 60, 100, 50, 0 },
                      { 8, 1, 0, -1, 84, 100, 100, 0 }, { 4, 3, 4, 0, 48, 80, 50, 0 }, { 16, 5, 8, 0, 44, 90, 50, 0 } } }) },

        { "Slow Galaxy",
          preset ({ { "scale", 4 }, { "root", 2 }, { "speed", 0 }, { "material", 40 }, { "bright", 36 }, { "decay", 8.0f },
                    { "echo", 26 }, { "echotime", 10 }, { "feedback", 50 }, { "reverb", 70 }, { "spread", 85 }, { "volume", 3.5f } },
                  { { { 4, 4, 0, 0, 60, 100, 60, 0 }, { 4, 3, 4, 0, 55, 100, 60, 0 }, { 8, 5, 9, 0, 50, 100, 60, 0 },
                      { 6, 4, 6, 0, 45, 80, 60, 0 }, { 16, 1, 0, -1, 70, 100, 100, 0 }, { 12, 7, 11, 0, 40, 70, 60, 0 } } }) },

        { "Solar Wind",
          preset ({ { "scale", 6 }, { "root", 9 }, { "speed", 6 }, { "material", 52 }, { "bright", 64 }, { "decay", 1.2f },
                    { "echo", 34 }, { "echotime", 6 }, { "feedback", 45 }, { "reverb", 25 }, { "volume", 2.0f } },
                  { { { 4, 8, 0, 0, 68, 90, 30, 0 }, { 4, 6, 2, 0, 62, 100, 40, 0 }, { 4, 5, 4, 0, 60, 100, 40, 0 },
                      { 8, 3, 7, 0, 55, 80, 50, 0 }, { 4, 2, 0, -1, 80, 100, 70, 0 }, { 16, 11, 9, 0, 50, 70, 40, 0 } } }) },

        { "Lunar Lullaby",
          preset ({ { "scale", 0 }, { "root", 5 }, { "speed", 2 }, { "material", 18 }, { "bright", 30 }, { "decay", 4.0f },
                    { "echo", 20 }, { "echotime", 8 }, { "reverb", 50 }, { "volume", 3.5f } },
                  { { { 3, 3, 4, 0, 46, 100, 50, 0 }, { 3, 2, 2, 0, 50, 100, 60, 0 }, { 6, 4, 7, 0, 42, 80, 50, 0 },
                      { 12, 1, 0, -1, 60, 100, 100, 0 }, off, { 12, 5, 9, 0, 38, 60, 50, 0 } } }) },

        { "Meteor Shower",
          preset ({ { "scale", 5 }, { "root", 2 }, { "speed", 6 }, { "material", 12 }, { "bright", 85 }, { "decay", 2.0f },
                    { "echo", 42 }, { "echotime", 2 }, { "feedback", 55 }, { "reverb", 45 }, { "spread", 100 }, { "volume", 2.0f } },
                  { { { 1, 4, 7, 1, 50, 30, 20, 0 }, { 1, 3, 9, 1, 45, 25, 20, 0 }, { 2, 5, 11, 0, 50, 35, 20, 0 },
                      { 1, 2, 12, 0, 40, 30, 20, 0 }, { 8, 2, 0, -1, 70, 100, 80, 0 }, { 3, 7, 14, 0, 40, 30, 20, 0 } } }) },

        { "Marimba Machine",
          preset ({ { "scale", 1 }, { "root", 0 }, { "material", 50 }, { "bright", 56 }, { "decay", 1.0f },
                    { "echo", 15 }, { "echotime", 5 }, { "reverb", 18 }, { "volume", 4.5f } },
                  { { { 4, 8, 0, 0, 70, 100, 30, 0 }, { 4, 6, 2, 0, 60, 100, 40, 0 }, { 3, 4, 4, 0, 60, 100, 40, 0 },
                      { 8, 5, 6, 0, 55, 100, 40, 0 }, { 4, 1, 0, -1, 85, 100, 80, 0 }, { 6, 5, 9, 0, 50, 80, 40, 0 } } }) },
    });
}

//==============================================================================
void OrreryProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    transport.prepare (sampleRate);

    for (auto& v : voices)
        v.prepare ((float) sampleRate);

    midiOut.ensureSize (8192);
    monoScratch.setSize (1, jmax (64, samplesPerBlock));

    const int maxDelay = (int) (4.0 * sampleRate) + 16;
    echoL.prepare (maxDelay);
    echoR.prepare (maxDelay);
    echoDelaySamples = (float) (0.75 * sampleRate);
    for (auto* f : { &echoLowL, &echoLowR, &echoHighL, &echoHighR, &dcBlockL, &dcBlockR })
        f->reset();
    dcBlockL.setCutoff (12.0f, (float) sampleRate);
    dcBlockR.setCutoff (12.0f, (float) sampleRate);
    echoLowL.setCutoff (5200.0f, (float) sampleRate);
    echoLowR.setCutoff (5200.0f, (float) sampleRate);
    echoHighL.setCutoff (170.0f, (float) sampleRate);
    echoHighR.setCutoff (170.0f, (float) sampleRate);

    reverbFx.prepare (sampleRate);
    lastReverbAmount = -1.0f;

    echoMix.reset (sampleRate, 0.03, echo->load() / 100.0f);
    reverbMix.reset (sampleRate, 0.03, reverb->load() / 100.0f);
    outGain.reset (sampleRate, 0.03, aa::dsp::dbToGain (volume->load()));
    fbAmount.reset (sampleRate, 0.05, feedback->load() / 100.0f);
    flushFade = 1.0f;
    flushState = 0;
    glueEnv = 0.0f;
    glueGain = 1.0f;
    meter.prepare ((float) sampleRate);

    held.fill (false);
    numHeld = 0;
    chordSize = 0;
    chordGraceSamples = 0;

    wasRunning = false;
    lastFirePpq.fill (-1.0e12);
    for (int i = 0; i < orrery::numOrbits; ++i)
        orbitWasOn[(size_t) i] = orbitParams[(size_t) i].on->load() > 0.5f;

    // Anything that was still sounding gets a note-off at the start of the next block.
    flushRequested.store (true);
    prepared = true;
}

void OrreryProcessor::releaseResources()
{
    flushRequested.store (true);
}

void OrreryProcessor::reset()
{
    flushRequested.store (true);
    resetRequested.store (true);
}

//==============================================================================
void OrreryProcessor::startVoice (int note, float velocity, float pan, int source, int& voiceIndexOut, uint32& idOut)
{
    // Soft polyphony cap: fade out the quietest ringing bell when too many are sounding.
    int sounding = 0, quietest = -1;
    float quietestLevel = 1.0e9f;
    for (int i = 0; i < numVoices; ++i)
    {
        auto& v = voices[(size_t) i];
        if (v.isActive() && ! v.fading)
        {
            ++sounding;
            if (v.getLevel() < quietestLevel)
            {
                quietestLevel = v.getLevel();
                quietest = i;
            }
        }
    }
    if (sounding >= maxSounding && quietest >= 0)
        voices[(size_t) quietest].fadeOut (0.008f);

    int slot = -1;
    for (int i = 0; i < numVoices && slot < 0; ++i)
        if (! voices[(size_t) i].isActive())
            slot = i;

    if (slot < 0)
    {
        float lowest = 1.0e9f;
        for (int i = 0; i < numVoices; ++i)
            if (voices[(size_t) i].getLevel() < lowest)
            {
                lowest = voices[(size_t) i].getLevel();
                slot = i;
            }
        slot = jmax (0, slot);
    }

    if (++nextVoiceId == 0)
        nextVoiceId = 1;

    voices[(size_t) slot].start (note, velocity, pan, bellSettings, nextVoiceId, source, rng);
    voiceIndexOut = slot;
    idOut = nextVoiceId;
}

void OrreryProcessor::releaseVoice (int voiceIndex, uint32 id)
{
    if (isPositiveAndBelow (voiceIndex, numVoices))
    {
        auto& v = voices[(size_t) voiceIndex];
        if (v.isActive() && v.id == id)
            v.release();
    }
}

void OrreryProcessor::releaseKeyboardVoices (int note)
{
    for (auto& v : voices)
        if (v.isActive() && v.source < 0 && (note < 0 || v.note == note))
            v.release();
}

void OrreryProcessor::renderVoices (float* left, float* right, int start, int num)
{
    if (num <= 0)
        return;
    for (auto& v : voices)
        if (v.isActive())
            v.render (left + start, right + start, num);
}

//==============================================================================
void OrreryProcessor::endGeneratedNote (int pitch, MidiBuffer& out, int sample)
{
    auto& a = activeNotes[(size_t) pitch];
    if (! a.on)
        return;
    out.addEvent (MidiMessage::noteOff (1, pitch), sample);
    releaseVoice (a.voice, a.voiceId);
    a.on = false;
    a.voice = -1;
    numActiveNotes = jmax (0, numActiveNotes - 1);
}

void OrreryProcessor::noteOffAll (MidiBuffer& out, int sample)
{
    for (int p = 0; p < 128; ++p)
        if (activeNotes[(size_t) p].on)
            endGeneratedNote (p, out, sample);
    numActiveNotes = 0;
}

void OrreryProcessor::rebuildChord()
{
    numHeld = 0;
    int n = 0;
    for (int p = 0; p < 128; ++p)
    {
        if (held[(size_t) p])
        {
            ++numHeld;
            if (n < orrery::maxChord)
                chord[(size_t) n++] = p;
        }
    }

    if (n > 0)
    {
        chordSize = n;
        chordGraceSamples = 0;
    }
    else if (chordSize > 0 && chordGraceSamples <= 0)
    {
        // Keep the last chord briefly so a gap between chord changes doesn't leak scale notes.
        chordGraceSamples = (int64) (0.35 * sr);
    }
}

void OrreryProcessor::publishChord()
{
    uint32 mask[4] = { 0, 0, 0, 0 };
    if (follow->load() > 0.5f)
        for (int i = 0; i < chordSize; ++i)
            mask[chord[(size_t) i] >> 5] |= 1u << (chord[(size_t) i] & 31);
    for (int i = 0; i < 4; ++i)
        uiChord[i].store (mask[i], std::memory_order_relaxed);
}

void OrreryProcessor::handleIncoming (const MidiMessage& m, int sample, MidiBuffer& out)
{
    if (m.isNoteOn())
    {
        const int n = m.getNoteNumber();
        held[(size_t) n] = true;
        rebuildChord();
        if (follow->load() <= 0.5f && sound->load() > 0.5f)
        {
            int vi = -1;
            uint32 id = 0;
            startVoice (n, m.getFloatVelocity(), 0.0f, -1, vi, id);
        }
    }
    else if (m.isNoteOff())
    {
        const int n = m.getNoteNumber();
        held[(size_t) n] = false;
        rebuildChord();
        releaseKeyboardVoices (n);
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        noteOffAll (out, sample);
        held.fill (false);
        numHeld = 0;
        chordSize = 0;
        chordGraceSamples = 0;
        releaseKeyboardVoices (-1);

        if (m.isAllSoundOff())
        {
            for (auto& v : voices)
                v.fadeOut (0.006f);
            flushState = 1;
        }
    }
}

//==============================================================================
void OrreryProcessor::processFx (float* left, float* right, int numSamples)
{
    const float srf = (float) sr;

    // Echo time follows the host tempo.
    const double beats = aa::dsp::syncDivisionBeats ((int) echoTime->load());
    const float targetDelay = (float) jlimit (1.0, (double) echoL.getMaxDelay() - 4.0, beats * 60.0 / transport.bpm * sr);
    const float delaySmooth = aa::dsp::onePoleCoeff (0.12f, srf);

    const float reverbAmount = reverb->load() / 100.0f;
    if (std::abs (reverbAmount - lastReverbAmount) > 0.01f)
    {
        lastReverbAmount = reverbAmount;
        aa::dsp::FdnReverb::Params rp;
        rp.size = 0.82f;
        rp.decaySeconds = 2.4f + 3.6f * reverbAmount;
        rp.damping = 0.42f;
        rp.predelayMs = 24.0f;
        rp.modDepth = 0.45f;
        rp.modRate = 0.33f;
        rp.width = 1.0f;
        rp.lowCutHz = 140.0f;
        reverbFx.setParams (rp);
    }

    echoMix.setTarget (echo->load() / 100.0f);
    reverbMix.setTarget (reverbAmount);
    outGain.setTarget (aa::dsp::dbToGain (volume->load()));
    fbAmount.setTarget (feedback->load() / 100.0f);

    const float flushStep = 1.0f / (0.008f * srf);
    bool clearNow = false;

    // glue compressor: -10 dBFS threshold, 3:1, gentle attack/release
    constexpr float glueThreshold = 0.316f, glueSlope = 1.0f - 1.0f / 3.0f;
    const float glueAttack = aa::dsp::onePoleCoeff (0.004f, srf), glueRelease = aa::dsp::onePoleCoeff (0.25f, srf);
    const float glueGainSmooth = aa::dsp::onePoleCoeff (0.002f, srf);

    for (int i = 0; i < numSamples; ++i)
    {
        // struck resonators leave a tiny DC offset: block it before the effects
        float dl = dcBlockL.highpass (left[i]), dr = dcBlockR.highpass (right[i]);

        const float level = jmax (std::abs (dl), std::abs (dr));
        glueEnv += (level > glueEnv ? glueAttack : glueRelease) * (level - glueEnv);
        const float targetGain = glueEnv > glueThreshold ? std::pow (glueThreshold / glueEnv, glueSlope) : 1.0f;
        glueGain += glueGainSmooth * (targetGain - glueGain);
        dl *= glueGain;
        dr *= glueGain;

        echoDelaySamples += delaySmooth * (targetDelay - echoDelaySamples);
        const float el = echoL.read (echoDelaySamples);
        const float er = echoR.read (echoDelaySamples);
        const float fb = fbAmount.next();

        // Ping-pong: the left line hears the input plus the right line's echo, and vice versa.
        const float fbl = echoHighL.highpass (echoLowL.lowpass (er * fb));
        const float fbr = echoHighR.highpass (echoLowR.lowpass (el * fb));
        echoL.push (aa::dsp::undenormalise (0.5f * (dl + dr) + fbl));
        echoR.push (aa::dsp::undenormalise (fbr));

        const float em = echoMix.next() * 0.9f;
        const float wl = (el + 0.3f * er) * em;
        const float wr = (er + 0.3f * el) * em;

        float rl = 0.0f, rr = 0.0f;
        reverbFx.processSample (dl + wl * 0.7f, dr + wr * 0.7f, rl, rr);
        const float rm = reverbMix.next() * 1.1f;

        if (flushState == 1)
        {
            flushFade -= flushStep;
            if (flushFade <= 0.0f)
            {
                flushFade = 0.0f;
                flushState = 2;
                clearNow = true;
            }
        }
        else if (flushState == 2 && ! clearNow)
        {
            flushFade += flushStep;
            if (flushFade >= 1.0f)
            {
                flushFade = 1.0f;
                flushState = 0;
            }
        }

        const float g = outGain.next() * flushFade;
        const float ol = safety ((dl + wl + rl * rm) * g);
        const float orr = safety ((dr + wr + rr * rm) * g);
        left[i] = ol;
        right[i] = orr;
        meter.process (0.5f * (std::abs (ol) + std::abs (orr)));
    }

    if (clearNow)
    {
        echoL.clear();
        echoR.clear();
        reverbFx.reset();
        for (auto& v : voices)
            v.kill();
    }
}

//==============================================================================
void OrreryProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    midiOut.clear();
    midiOut.ensureSize (8192);

    if (numSamples <= 0 || ! prepared)
    {
        midi.clear();
        return;
    }

    transport.update (getPlayHead(), numSamples);

    if (resetRequested.exchange (false))
    {
        for (auto& v : voices)
            v.kill();
        echoL.clear();
        echoR.clear();
        reverbFx.reset();
    }

    if (flushRequested.exchange (false))
        noteOffAll (midiOut, 0);

    // ---- parameters ---------------------------------------------------------------
    const bool runFree = freeRun->load() > 0.5f;
    const bool followOn = follow->load() > 0.5f;
    const bool soundOn = sound->load() > 0.5f;
    const double speedMul = orrery::speedValue ((int) speed->load());
    const int scaleIndex = (int) scale->load();
    const int rootNote = (int) root->load();
    const float spreadAmt = spread->load() / 100.0f;
    bellSettings.material = material->load() / 100.0f;
    bellSettings.brightness = brightness->load() / 100.0f;
    bellSettings.decaySeconds = decay->load();

    struct Orbit
    {
        bool on;
        int beats, pulses, degree, octave;
        double offset;
        float velocity, chance, gate;
    };
    std::array<Orbit, orrery::numOrbits> orb;
    for (int i = 0; i < orrery::numOrbits; ++i)
    {
        const auto& p = orbitParams[(size_t) i];
        auto& o = orb[(size_t) i];
        o.on = p.on->load() > 0.5f;
        o.beats = jlimit (1, 16, (int) std::round (p.beats->load()));
        o.pulses = jlimit (1, 16, (int) std::round (p.pulses->load()));
        o.degree = jlimit (0, orrery::maxDegree, (int) std::round (p.note->load()));
        o.octave = jlimit (-2, 2, (int) std::round (p.octave->load()));
        o.offset = jlimit (0.0, 1.0, (double) p.offset->load() / 100.0);
        o.velocity = jlimit (0.01f, 1.0f, p.velocity->load() / 100.0f);
        o.chance = jlimit (0.0f, 1.0f, p.chance->load() / 100.0f);
        o.gate = jlimit (0.05f, 1.0f, p.gate->load() / 100.0f);

        // A planet that was switched off releases its notes straight away.
        if (! o.on && orbitWasOn[(size_t) i])
            for (int n = 0; n < 128; ++n)
                if (activeNotes[(size_t) n].on && activeNotes[(size_t) n].orbit == i)
                    endGeneratedNote (n, midiOut, 0);
        orbitWasOn[(size_t) i] = o.on;
    }

    // ---- musical position -----------------------------------------------------------
    const bool running = transport.hostPlaying || runFree;
    const double dp = transport.samplesToPpq (numSamples);
    // Free-running continues smoothly from wherever the planets are.
    const double p0 = transport.hostPlaying ? transport.ppqAtBlockStart : lastEndPpq;
    const double p1 = p0 + dp;
    double windowStart = p0;

    if (running)
    {
        const double tolerance = jmax (0.01, dp * 0.05);
        const bool jump = ! wasRunning || std::abs (p0 - lastEndPpq) > tolerance;
        if (jump)
        {
            if (wasRunning)
                noteOffAll (midiOut, 0); // loop / relocation: never leave anything hanging
            lastFirePpq.fill (-1.0e12);
            uiJumpCount.fetch_add (1);
        }
        else
        {
            windowStart = lastEndPpq; // windows tile exactly: nothing is skipped or doubled
        }
        lastEndPpq = p1;
    }
    else if (wasRunning)
    {
        noteOffAll (midiOut, 0);
    }
    wasRunning = running;

    // ---- find every pulse crossing inside this block ------------------------------
    numTriggers = 0;
    if (running)
    {
        for (int o = 0; o < orrery::numOrbits; ++o)
        {
            const auto& ob = orb[(size_t) o];
            if (! ob.on)
                continue;

            const double pulsesPerPpq = speedMul * (double) ob.pulses / (double) ob.beats;
            const double interval = 1.0 / pulsesPerPpq;
            const double uStart = windowStart * pulsesPerPpq - ob.offset;
            const double uEnd = p1 * pulsesPerPpq - ob.offset;

            for (double n = std::ceil (uStart); n < uEnd && numTriggers < maxTriggers; n += 1.0)
            {
                const double tp = (n + ob.offset) * interval;
                if (tp - lastFirePpq[(size_t) o] < 0.5 * interval)
                    continue;
                lastFirePpq[(size_t) o] = tp;

                const int s = jlimit (0, numSamples - 1, (int) std::floor ((tp - p0) / dp * (double) numSamples));
                const auto ni = (int64) n;
                const int pulse = (int) (((ni % ob.pulses) + ob.pulses) % ob.pulses);
                triggers[(size_t) numTriggers++] = { s, o, pulse, tp, interval };
            }
        }

        // stable insertion sort by sample position
        for (int i = 1; i < numTriggers; ++i)
        {
            const auto t = triggers[(size_t) i];
            int j = i - 1;
            while (j >= 0 && triggers[(size_t) j].sample > t.sample)
            {
                triggers[(size_t) j + 1] = triggers[(size_t) j];
                --j;
            }
            triggers[(size_t) j + 1] = t;
        }
    }

    // ---- audio targets ------------------------------------------------------------
    float* left = buffer.getWritePointer (0);
    float* right = nullptr;
    const bool mono = buffer.getNumChannels() < 2;
    if (! mono)
        right = buffer.getWritePointer (1);
    else if (monoScratch.getNumSamples() >= numSamples)
    {
        monoScratch.clear();
        right = monoScratch.getWritePointer (0);
    }
    const bool canRender = right != nullptr;

    auto fire = [&] (const Trigger& t, int sample)
    {
        const auto& ob = orb[(size_t) t.orbit];
        if (ob.chance < 1.0f && ! rng.chance (ob.chance))
        {
            noteEvents.push ({ t.orbit, t.pulse, -1, 0.0f, t.ppq }); // a skipped pulse still glints
            return;
        }

        const bool useChord = followOn && chordSize > 0;
        const int note = orrery::noteForDegree (ob.degree, ob.octave, scaleIndex, rootNote,
                                                useChord ? chord.data() : nullptr, useChord ? chordSize : 0);

        if (activeNotes[(size_t) note].on)
            endGeneratedNote (note, midiOut, sample);

        midiOut.addEvent (MidiMessage::noteOn (1, note, (uint8) jlimit (1, 127, roundToInt (ob.velocity * 127.0f))), sample);

        auto& a = activeNotes[(size_t) note];
        const double gateSamples = jmax (0.004 * sr, transport.ppqToSamples (t.intervalPpq) * ob.gate);
        a.on = true;
        a.orbit = t.orbit;
        a.endSample = sample + jmax ((int64) 1, (int64) gateSamples);
        a.voice = -1;
        a.voiceId = 0;
        ++numActiveNotes;

        if (soundOn && canRender)
            startVoice (note, ob.velocity, orrery::planets()[(size_t) t.orbit].pan * spreadAmt, t.orbit, a.voice, a.voiceId);

        noteEvents.push ({ t.orbit, t.pulse, note, ob.velocity, t.ppq });
    };

    // ---- sample-accurate event loop: render audio between events ---------------------
    int cursor = 0, ti = 0;
    auto inIt = midi.cbegin();
    const auto inEnd = midi.cend();

    for (;;)
    {
        int next = numSamples;
        if (ti < numTriggers)
            next = jmin (next, triggers[(size_t) ti].sample);
        if (inIt != inEnd)
            next = jmin (next, jlimit (0, numSamples - 1, (*inIt).samplePosition));
        if (numActiveNotes > 0)
            for (int p = 0; p < 128; ++p)
                if (activeNotes[(size_t) p].on && activeNotes[(size_t) p].endSample < next)
                    next = jmax (cursor, (int) activeNotes[(size_t) p].endSample);

        if (next > cursor)
        {
            if (canRender)
                renderVoices (left, right, cursor, next - cursor);
            cursor = next;
        }
        if (cursor >= numSamples)
            break;

        if (numActiveNotes > 0)
            for (int p = 0; p < 128; ++p)
                if (activeNotes[(size_t) p].on && activeNotes[(size_t) p].endSample <= cursor)
                    endGeneratedNote (p, midiOut, cursor);

        while (inIt != inEnd && jlimit (0, numSamples - 1, (*inIt).samplePosition) <= cursor)
        {
            handleIncoming ((*inIt).getMessage(), cursor, midiOut);
            ++inIt;
        }

        while (ti < numTriggers && triggers[(size_t) ti].sample <= cursor)
            fire (triggers[(size_t) ti++], cursor);
    }

    for (auto& a : activeNotes)
        if (a.on)
            a.endSample -= numSamples;

    if (numHeld == 0 && chordSize > 0 && chordGraceSamples > 0)
    {
        chordGraceSamples -= numSamples;
        if (chordGraceSamples <= 0)
            chordSize = 0;
    }
    publishChord();

    // ---- echo, reverb, volume -------------------------------------------------------
    if (canRender)
    {
        processFx (left, right, numSamples);
        if (mono)
            for (int i = 0; i < numSamples; ++i)
                left[i] = 0.5f * (left[i] + right[i]);
    }
    meter.publish();

    // ---- hand the MIDI back to the host and feed the UI -------------------------
    midi.swapWith (midiOut);

    uiPpq.store (lastEndPpq);
    uiWallMs.store (Time::getMillisecondCounterHiRes());
    uiBpm.store ((float) transport.bpm);
    uiRunState.store (transport.hostPlaying ? hostPlaying : (runFree ? freeRunning : stopped));
    uiBeatsPerBar.store (jmax (1, transport.timeSigNumerator * 4 / jmax (1, transport.timeSigDenominator)));
    uiLevel.store (meter.level.load());
}

void OrreryProcessor::processBlockBypassed (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    // Silent while bypassed, but never leave the instruments we drive with stuck notes.
    buffer.clear();
    midi.clear();
    noteOffAll (midi, 0);
    if (prepared)
        for (auto& v : voices)
            v.kill();
    held.fill (false);
    numHeld = 0;
    chordSize = 0;
    wasRunning = false; // resume as if the transport had jumped
}

//==============================================================================
int OrreryProcessor::currentNoteForOrbit (int orbit) const
{
    orbit = jlimit (0, orrery::numOrbits - 1, orbit);
    const auto& p = orbitParams[(size_t) orbit];

    int chordNotes[orrery::maxChord];
    int n = 0;
    for (int w = 0; w < 4; ++w)
    {
        const uint32 bits = uiChord[w].load (std::memory_order_relaxed);
        for (int b = 0; b < 32 && n < orrery::maxChord; ++b)
            if (bits & (1u << b))
                chordNotes[n++] = w * 32 + b;
    }

    return orrery::noteForDegree ((int) std::round (p.note->load()), (int) std::round (p.octave->load()),
                                  (int) scale->load(), (int) root->load(), n > 0 ? chordNotes : nullptr, n);
}

String OrreryProcessor::followedChordText() const
{
    StringArray names;
    for (int w = 0; w < 4; ++w)
    {
        const uint32 bits = uiChord[w].load (std::memory_order_relaxed);
        for (int b = 0; b < 32; ++b)
            if (bits & (1u << b))
                names.add (orrery::noteName (w * 32 + b));
    }
    return names.joinIntoString (" ");
}

AudioProcessorEditor* OrreryProcessor::createEditor()
{
    return new OrreryEditor (*this);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OrreryProcessor();
}
