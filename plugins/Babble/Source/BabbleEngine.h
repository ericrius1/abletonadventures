#pragma once

#include <aakit/AdventureKit.h>
#include "BabbleData.h"

namespace babble
{
/** Parameter snapshot, in real units, handed to the engine once per block. */
struct Settings
{
    float vowel = 0.0f;           // 0..4 (A E I O U)
    int voiceType = alto;
    float shift = 0.0f;           // formant shift, semitones
    float breath = 0.12f;         // 0..1
    float brightness = 0.55f;     // 0..1
    float vibRate = 5.2f;         // Hz
    float vibDepth = 0.3f;        // 0..1
    float vibDelay = 0.4f;        // seconds
    float glide = 0.0f;           // seconds
    float babble = 0.4f;          // 0..1
    float babbleRate = 4.0f;      // Hz
    bool babbleSync = false;
    int babbleDivision = 5;
    int choir = 3;
    float detune = 0.35f;         // 0..1
    float attack = 0.06f;         // seconds
    float release = 0.5f;         // seconds
    float chorus = 0.25f;         // 0..1
    float reverb = 0.3f;          // 0..1
    float volumeDb = 0.0f;
};

/** Events for the editor (note-ons make the head bob, syllables fill the speech bubble). */
struct UiEvent
{
    enum Type { noteOn = 0, syllable = 1 };
    int type = noteOn;
    int a = 0;          // note number / consonant index
    int b = 0;          // vowel index for syllables
    float value = 0.0f; // velocity
};

/** The singing engine: 8 voices x up to 4 choir singers, each a band-limited glottal source plus
    breath noise through a parallel bank of 5 formant resonators, with a global "babbler" that
    turns held notes into gibberish syllables. Allocation-free after prepare(). */
class Engine
{
public:
    void prepare (double sampleRate);
    void reset();

    void setSettings (const Settings& s) { settings = s; }
    void handleMidi (const juce::MidiMessage& m);

    /** Renders and *overwrites* numSamples into left/right (right may be nullptr for mono). */
    void render (float* left, float* right, int numSamples, const aa::dsp::Transport& transport, int blockOffset);

    /** Publishes the per-block UI values (call once at the end of processBlock). */
    void publishUi();

    // ---- UI feed (written on the audio thread, read by the editor) -----------------
    std::atomic<float> uiVowel { 0.0f };     // current sung vowel 0..4 (includes babble)
    std::atomic<float> uiOpen { 0.0f };      // how open the mouth is 0..1
    std::atomic<float> uiLevel { 0.0f };     // output peak
    std::atomic<float> uiVibrato { 0.0f };   // signed vibrato excursion, roughly -1..1
    std::atomic<float> uiLips { 0.0f };      // lip closure for b/m/p/w 0..1
    std::atomic<int> uiActiveNotes { 0 };
    aa::dsp::SpscQueue<UiEvent, 128> uiEvents;

private:
    struct Sub
    {
        float phase = 0.0f, dt = 0.0f;
        float gain = 0.0f;
        float panL = 0.7071f, panR = 0.7071f;
        float vibPhase = 0.0f, vibRateMul = 1.0f;
        aa::dsp::Lfo drift, jitter;
        float driftRate = 1.0f, jitterRate = 10.0f;
    };

    struct Voice
    {
        int note = -1;
        float velocity = 0.0f, velGain = 0.0f;
        bool active = false, held = false, sustained = false, pendingOff = false;
        int gateCountdown = 0;  // minimum note length, so ultra-short notes still speak
        uint32_t order = 0;
        float pitch = 60.0f, target = 60.0f, age = 0.0f;
        aa::dsp::Adsr env;
        float kill = 1.0f, killTarget = 1.0f;
        std::array<Sub, maxChoir> subs;
        std::array<aa::dsp::Svf, numFormants> bankL, bankR;
        std::array<float, numFormants> srcGain {}, noiseGain {};
        float tiltCoeff = 1.0f, tiltL = 0.0f, tiltR = 0.0f;
        float uniNorm = 1.0f;
        float vibValue = 0.0f;
        aa::dsp::Rng rng;
    };

    /** Formant frame shared by all voices for one control tick. */
    struct Frame
    {
        std::array<float, numFormants> freq {}, bw {}, gain {};
    };

    void controlTick (const aa::dsp::Transport& transport, int samplePos);
    void babbleTick (const aa::dsp::Transport& transport, int samplePos);
    void startSyllable (bool fresh);
    void voiceTick (Voice& v);
    void renderVoice (Voice& v, float* outL, float* outR, int n, float gapStart, float gapStep);
    void renderChunk (float* left, float* right, int n);

    void noteOn (int note, float velocity);
    void noteOff (int note);
    void setSustain (bool down);
    void releaseAll();
    void killAll();
    int pickVoice (int note);

    Settings settings;
    double sr = 44100.0;
    int tickLen = 8, samplesUntilTick = 0;
    float tickSeconds = 8.0f / 44100.0f, tickRate = 44100.0f / 8.0f;

    std::array<Voice, maxVoices> voices;
    uint32_t orderCounter = 0;
    float lastNotePitch = -1.0f, glideSource = -1.0f;
    int64_t sampleClock = 0, lastNoteOnClock = -1000000;
    bool sustainDown = false;
    float bendTarget = 0.0f, bend = 0.0f, modWheelTarget = 0.0f, modWheel = 0.0f;

    // Smoothed globals (control rate)
    float vowelS = 0.0f, shiftS = 0.0f, brightS = 0.55f, breathS = 0.12f, robotS = 0.0f;
    std::array<float, numFormants> logFreqS {}, bwS {}, dbS {};
    bool firstTick = true;
    Frame frame;

    // Babbler
    aa::dsp::Rng rng { 0x5EED1234u };
    float sylPhase = 0.0f, sylLength = 0.25f, sylTime = 1.0f, closeLen = 0.05f, depthEff = 0.0f;
    int consonant = 0, lastConsonant = -1;
    int randomVowel = 0, prevRandomVowel = 0;
    double lastSyncIndex = -1.0e9;
    bool forceSyllable = false, wasBabbling = false;
    float vowelTarget = 0.0f;
    float inflectTarget = 0.0f, inflect = 0.0f, accent = 1.0f;
    float gap = 1.0f, gapTarget = 1.0f;
    float locusEnv = 0.0f, murmurEnv = 0.0f, noiseEnv = 0.0f, aspEnv = 0.0f, lipsEnv = 0.0f;

    // Activity / bus
    float activity = 0.0f, gateActivity = 0.0f;
    std::array<float, 64> chunkL {}, chunkR {};
    aa::dsp::Svf consonantFilterL, consonantFilterR;
    float consonantNoiseNorm = 0.0f, consonantNoiseLevel = 0.0f, consonantNoiseStep = 0.0f;
    float murmurCoeff = 1.0f, murmurL = 0.0f, murmurR = 0.0f;
    float crushPhase = 0.0f, crushL = 0.0f, crushR = 0.0f;
    float dcL = 0.0f, dcR = 0.0f, dcCoeff = 0.0f;
    aa::dsp::Smoother volume;
    aa::dsp::StereoChorus chorus;
    aa::dsp::FdnReverb reverb;
    float lastReverbAmount = -1.0f;
    float reverbMix = 0.0f, chorusMix = 0.0f;

    // UI accumulators
    float peak = 0.0f;
};
} // namespace babble
