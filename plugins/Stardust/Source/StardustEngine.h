#pragma once

#include <aakit/AdventureKit.h>

namespace stardust
{
constexpr int maxPolyphony = 16;
constexpr int numVoiceSlots = maxPolyphony + 4; // spare slots let stolen voices fade out without clicks
constexpr int maxUnison = 7;
constexpr int maxGlints = 32;
constexpr int controlInterval = 8;              // samples between control-rate updates
constexpr int maxChunk = 256;                   // largest render call
constexpr float pitchBendRange = 2.0f;          // semitones

enum class VoiceMode { poly = 0, mono, legato };
enum class FilterType { lowpass = 0, bandpass, highpass };

/** LFO tempo-sync divisions (in quarter notes). Slow ones first: this is a pad synth. */
inline const juce::StringArray& lfoDivisionNames()
{
    static const juce::StringArray names { "4 bars", "2 bars", "1/1", "1/2", "1/4D", "1/4",
                                           "1/4T", "1/8D", "1/8", "1/8T", "1/16", "1/32" };
    return names;
}

inline double lfoDivisionBeats (int index)
{
    static const double beats[] = { 16.0, 8.0, 4.0, 2.0, 1.5, 1.0, 2.0 / 3.0, 0.75, 0.5, 1.0 / 3.0, 0.25, 0.125 };
    return beats[juce::jlimit (0, 11, index)];
}

/** A per-block snapshot of every parameter, in real units. */
struct Settings
{
    float shapeA = 2.0f, detune = 0.3f, spread = 0.8f, levelA = 0.8f;
    int unison = 7, octaveA = 0;
    float shapeB = 1.0f, levelB = 0.4f, fineB = 0.0f;
    int octaveB = 0, semiB = 0;
    float subLevel = 0.2f, noiseLevel = 0.0f;

    FilterType filterType = FilterType::lowpass;
    float cutoff = 2400.0f, resonance = 0.2f, drive = 0.15f, envAmount = 0.3f, keyTrack = 0.5f;
    float fAttack = 0.5f, fDecay = 2.0f, fSustain = 0.4f, fRelease = 2.0f;
    float aAttack = 0.4f, aDecay = 1.0f, aSustain = 0.8f, aRelease = 2.0f;
    float velocity = 0.5f;

    int lfoShape = 0, lfoDivision = 5;
    bool lfoSync = false;
    float lfoRate = 0.3f, lfoPitch = 0.0f, lfoCutoff = 0.0f, lfoShapeMod = 0.0f;

    float twinkle = 0.35f, drift = 0.3f, gravity = 0.0f;

    VoiceMode voiceMode = VoiceMode::poly;
    float glide = 0.0f; // seconds
};

struct NoteEvent { int note = 60; float velocity = 1.0f; };
struct GlintEvent { float pan = 0.0f, height = 0.5f, level = 1.0f, life = 1.0f; };

//==============================================================================
/** Band-limited oscillator shapes. Every wave is phase-aligned with sin(2 pi p) so the
    continuous morph never cancels its own fundamental. */
namespace osc
{
    inline float sin2pi (float p)
    {
        float x = p - 0.5f;                    // sin (2 pi p) = -sin (2 pi x), x in [-0.5, 0.5)
        if (x > 0.25f) x = 0.5f - x;
        else if (x < -0.25f) x = -0.5f - x;
        const float t = x * aa::dsp::twoPi;     // [-pi/2, pi/2]
        const float t2 = t * t;
        const float s = t * (1.0f + t2 * (-0.16666667f + t2 * (0.0083333333f + t2 * (-0.00019841270f + t2 * 2.7557319e-6f))));
        return -s;
    }

    /** 2-sample polyBLEP residual for a unit-height (from -1 to +1 = 2) step. */
    inline float blep (float t, float dt)
    {
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0f;
        }
        if (t > 1.0f - dt)
        {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }
        return 0.0f;
    }

    /** polyBLAMP residual (integrated BLEP) for a slope change, |tau| < 1 sample. */
    inline float blampAt (float distance, float dt)
    {
        const float tau = std::abs (distance) / dt;
        if (tau >= 1.0f)
            return 0.0f;
        const float u = 1.0f - tau;
        return u * u * u * (1.0f / 6.0f);
    }

    inline float wrapHalf (float d) { return d >= 0.5f ? d - 1.0f : (d < -0.5f ? d + 1.0f : d); }

    inline float saw (float p, float dt) { return 1.0f - 2.0f * p + blep (p, dt); }

    inline float square (float p, float dt)
    {
        float v = p < 0.5f ? 1.0f : -1.0f;
        v += blep (p, dt);
        float t2 = p - 0.5f;
        if (t2 < 0.0f) t2 += 1.0f;
        return v - blep (t2, dt);
    }

    inline float triangle (float p, float dt)
    {
        float v = p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f);
        const float k = 8.0f * dt;
        v -= k * blampAt (wrapHalf (p - 0.25f), dt);
        v += k * blampAt (wrapHalf (p - 0.75f), dt);
        return v;
    }

    inline float wave (int index, float p, float dt)
    {
        switch (index)
        {
            case 0: return sin2pi (p);
            case 1: return triangle (p, dt);
            case 2: return saw (p, dt);
            default: return square (p, dt);
        }
    }

    /** 0 = sine, 1 = triangle, 2 = saw, 3 = square, continuous in between. */
    inline float morph (float shape, float p, float dt)
    {
        int i = (int) shape;
        if (i > 2) i = 2;
        const float f = shape - (float) i;
        const float a = wave (i, p, dt);
        if (f < 0.0005f)
            return a;
        return a + (wave (i + 1, p, dt) - a) * f;
    }
} // namespace osc

//==============================================================================
/** The Stardust synth voice engine: voices, glide, drift, gravity, twinkle glints and the LFO.
    Effects (chorus, reverb, volume) live in the processor. */
class Engine
{
public:
    void prepare (double sampleRate);
    void reset();

    void setSettings (const Settings& s);
    void setTransport (double ppqAtBlockStart, double bpm);
    void handleMidi (const juce::MidiMessage& m);

    /** Renders n <= maxChunk samples. Overwrites the four output buffers. */
    void render (float* synthL, float* synthR, float* glintL, float* glintR, int n);

    /** True once after an all-sound-off: the processor should flush its effect tails. */
    bool takeFlushRequest() noexcept { const bool f = flushRequested; flushRequested = false; return f; }

    // ---- UI feed (audio thread writes, UI reads) ---------------------------------
    aa::dsp::SpscQueue<NoteEvent, 128> noteEvents;
    aa::dsp::SpscQueue<GlintEvent, 128> glintEvents;
    std::atomic<int> activeVoiceCount { 0 };
    std::atomic<float> lfoValue { 0.0f }, lfoPhaseOut { 0.0f }, ampEnvLevel { 0.0f }, filterEnvLevel { 0.0f };

private:
    struct Voice
    {
        bool active = false, gate = false, sustained = false, fadingOut = false;
        int note = -1;
        float velocity = 0.0f, velGain = 1.0f, envDepth = 1.0f;
        uint32_t order = 0;

        float pitch = 60.0f, targetPitch = 60.0f, baseHz = 261.6f, noteDetune = 0.0f;
        float timeOn = 0.0f;

        std::array<float, maxUnison> phaseA {}, uniW {}, uniDriftPhase {}, uniDriftRate {};
        float phaseB = 0.0f, phaseSub = 0.0f;
        float wB = 0.0f, wSub = 0.0f, wNoise = 0.0f;

        aa::dsp::Adsr amp, filt;
        float ic1L = 0.0f, ic2L = 0.0f, ic1R = 0.0f, ic2R = 0.0f;
        float gPrev = 0.1f;

        float fade = 1.0f, fadeStep = 0.0f;

        aa::dsp::Rng rng;
        float driftPhase = 0.0f, driftRate = 0.3f, driftFrom = 0.0f, driftTo = 0.0f;
        float cutDriftFrom = 0.0f, cutDriftTo = 0.0f;
        float noiseLp = 0.0f;
    };

    struct Glint
    {
        bool active = false, attacking = true;
        float phase = 0.0f, dt = 0.0f, env = 0.0f, attackCoeff = 0.01f, decayMul = 0.9999f;
        float level = 0.0f, panL = 0.7f, panR = 0.7f;
        float tremPhase = 0.0f, tremDt = 0.0f, tremDepth = 0.0f;
    };

    /** One-pole smoothed control value, stepped at control rate. */
    struct Ctl
    {
        float value = 0.0f, target = 0.0f;
        void snap (float v) { value = target = v; }
        void step (float c) { value += c * (target - value); }
    };

    // MIDI
    void noteOn (int note, float velocity);
    void noteOff (int note);
    void sustainPedal (bool down);
    void allNotesOff();
    void allSoundOff();

    Voice* allocateVoice();
    void startVoice (Voice& v, int note, float velocity, float startPitch, bool retrigger);
    void releaseVoice (Voice& v);
    void beginFade (Voice& v, float seconds);
    Voice* monoVoice();
    float glideStartPitch (int note) const;

    // rendering
    void updateControls (int len);
    void renderVoice (Voice& v, float* outL, float* outR, int len);
    void spawnGlints (int len);
    void renderGlints (float* outL, float* outR, int len);
    void applyEnvelopeSettings (Voice& v) const;

    float sr = 44100.0f;
    Settings settings;
    bool settingsInit = false;
    std::array<Voice, numVoiceSlots> voices;
    std::array<Glint, maxGlints> glints;
    uint32_t orderCounter = 0;
    aa::dsp::Rng rng { 0x5EED1234u };

    // held-note stack for mono / legato
    std::array<int, 32> heldNotes {};
    std::array<float, 32> heldVelocity {};
    int numHeld = 0;
    int monoIndex = -1;
    float lastNotePitch = -1.0f;
    bool sustainDown = false;
    VoiceMode currentMode = VoiceMode::poly;

    // smoothed controls
    Ctl shapeA, shapeB, detune, spread, levelA, levelB, subLevel, noiseLevel;
    Ctl cutoffOct, resonance, drive, envAmount, keyTrack;
    Ctl lfoPitch, lfoCutoff, lfoShapeMod, twinkle, drift, gravity, bend, modWheel;
    float bendTarget = 0.0f, modWheelTarget = 0.0f;

    // per control-block derived values
    float shapeA0 = 2.0f, shapeA1 = 2.0f, shapeB0 = 1.0f, shapeB1 = 1.0f;
    std::array<float, maxUnison> uniTarget {}, uniOffset {}, uniPanL {}, uniPanR {};
    float pitchMod = 0.0f, cutMod = 0.0f, filterK = 1.0f, bpNorm = 1.0f, inputComp = 1.0f;
    float drivePre = 0.35f, drivePost = 1.0f / 0.35f;
    float octAMul = 1.0f, bRatio = 1.0f;
    float ctlCoeff = 0.1f, uniCoeff = 0.2f, glideCoeff = 1.0f;

    // LFO + vibrato
    float lfoPhase = 0.0f, lfoHeld = 0.0f, lfoPrevHeld = 0.0f, lfoSmoothed = 0.0f;
    float vibPhase = 0.0f;
    double blockPpq = 0.0, blockBpm = 120.0;
    int samplesIntoBlock = 0;

    float polyGain = 1.0f;
    bool flushRequested = false;
    int controlCountdown = 0;
};
} // namespace stardust
