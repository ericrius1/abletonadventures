#pragma once

#include "Modulation.h"
#include "Wavetable.h"
#include "WavetableFactory.h"
#include "Filters.h"
#include <aakit/dsp/Core.h>

namespace ers
{
/** What the oscillators read from. Owned by the processor; swapped between blocks. */
struct EngineTables
{
    std::array<const Wavetable*, numOscs> osc {};
    const NoiseSource* noise = nullptr;
    std::array<const float*, numLfos> lfo {};    // lfoTableSize + 1 samples each, values 0..1
};

/** Per-block inputs from the processor. */
struct EngineBlock
{
    const float* values = nullptr;        // vp::count real (denormalised) parameter values
    const float* normalised = nullptr;    // the same values normalised 0..1
    const ModRoute* routes = nullptr;
    int numRoutes = 0;
    double bpm = 120.0;
    double ppq = 0.0;
    bool playing = false;
};

//==============================================================================
/** AHDSR envelope with curved segments, advanced once per control block. */
struct Envelope
{
    enum class Stage { idle, attack, hold, decay, sustain, release };

    Stage stage = Stage::idle;
    float level = 0.0f;
    float from = 0.0f;
    float t = 0.0f;

    void noteOn() noexcept   { stage = Stage::attack; from = level; t = 0.0f; }
    void noteOff() noexcept  { if (stage != Stage::idle && stage != Stage::release) { stage = Stage::release; from = level; t = 0.0f; } }
    void kill() noexcept     { stage = Stage::idle; level = 0.0f; }
    bool isActive() const noexcept { return stage != Stage::idle; }

    /** p: attack, hold, decay (seconds), sustain (0..1), release (s), attack/decay/release curves (-1..1). */
    void advance (float seconds, const float* p) noexcept;
};

/** One LFO instance (per voice, plus one global instance per LFO for "Free" mode and the FX). */
struct LfoState
{
    double phase = 0.0;
    float value = 0.0f;         // smoothed output 0..1 (already scaled by delay / rise)
    float raw = 0.0f;
    float elapsed = 0.0f;       // seconds since trigger
    bool finished = false;      // envelope mode reached the end
    bool primed = false;
};

//==============================================================================
class Engine
{
public:
    Engine();

    void prepare (double sampleRate);
    void reset();

    /** The ranges of the voice parameters, used to apply modulation in normalised space. */
    void setRanges (const std::vector<juce::NormalisableRange<float>>& vpRanges) { ranges = vpRanges; }

    void setTables (const EngineTables& t) noexcept { tables = t; }
    void beginBlock (const EngineBlock& block) noexcept;
    void handleMidi (const juce::MidiMessage& m) noexcept;

    /** Renders up to controlBlock samples (adds nothing - overwrites left/right). */
    void render (float* left, float* right, int numSamples) noexcept;

    /** Modulation offset (normalised) for an FX parameter this block, from global sources. */
    float getFxModulation (int fxIndex) const noexcept { return fxMod[(size_t) fxIndex]; }
    bool hasFxModulation() const noexcept { return anyFxMod; }

    // ---- UI feed (written on the audio thread, read by the editor) ---------------------
    struct UiFeed
    {
        std::atomic<int> activeVoices { 0 };
        std::array<std::atomic<float>, numEnvs> envLevel {};
        std::array<std::atomic<float>, numLfos> lfoPhase {};
        std::array<std::atomic<float>, numLfos> lfoValue {};
        std::array<std::atomic<float>, numOscs> oscPosition {};      // modulated, 0..1
        std::array<std::atomic<float>, numOscs> oscWarp {};          // modulated warp amount 0..1
        std::unique_ptr<std::atomic<float>[]> destMod;                 // per modDests(): current normalised offset
        int numDests = 0;
        std::atomic<uint32_t> heldNotes[4] {};                         // bitset of sounding notes (for the keyboard)
    } ui;

    static constexpr int numBuses = 3;  // filter 1, filter 2, direct

private:
    struct OscVoice
    {
        std::array<float, maxUnison> phase {};
        std::array<float, maxUnison> last {};     // previous output per unison voice (self-FM)
    };

    struct Voice
    {
        int note = -1;
        int channel = 1;
        float velocity = 0.0f;
        float releaseVelocity = 0.0f;
        float randomValue = 0.0f;
        float alternate = 1.0f;
        float polyPressure = 0.0f;
        bool gate = false;        // key held (or sustained)
        bool sustained = false;   // released while the sustain pedal was down
        bool active = false;
        uint64_t age = 0;

        float pitch = 60.0f;      // current (glided) pitch in semitones
        float glideFrom = 60.0f;
        float glideTarget = 60.0f;
        float glideT = 1.0f;

        std::array<OscVoice, numOscs> osc;
        float subPhase = 0.0f;
        double noisePos = 0.0;
        bool noiseDone = false;

        std::array<Envelope, numEnvs> env;
        std::array<LfoState, numLfos> lfo;
        std::array<VoiceFilter, numFilters> filter;

        std::array<float, numModSources> src {};
        std::array<float, vp::count> v {};          // this block's modulated parameter values
        std::array<std::array<float, controlBlock>, numOscs + 2> mono {};  // per-source mono output (A, B, C, sub, noise)

        float ampLevel = 0.0f;   // env 1 * velocity at the end of the last block
        float fade = 1.0f;       // steal / kill fade
        bool stealing = false;
        int pendingNote = -1;
        float pendingVelocity = 0.0f;
        int pendingChannel = 1;
    };

    void noteOn (int channel, int note, float velocity) noexcept;
    void noteOff (int note, float velocity) noexcept;
    void allNotesOff (bool immediate) noexcept;
    void startVoice (Voice& v, int channel, int note, float velocity, bool retrigger) noexcept;
    Voice* findVoiceToSteal() noexcept;
    Voice* newestVoice() noexcept;
    int countGated() const noexcept;
    void releaseVoice (Voice& v, float velocity) noexcept;

    void updateGlobalLfos (int numSamples) noexcept;
    void advanceLfo (LfoState& s, int index, const float* vals, float seconds, bool global) noexcept;
    float lfoRateHz (int index, const float* vals) const noexcept;
    void computeVoiceSources (Voice& v) noexcept;
    void applyModulation (Voice& v) noexcept;
    void renderVoice (Voice& v, float* outL, float* outR, int n) noexcept;
    void renderOsc (Voice& v, int o, float basePitch, int n) noexcept;
    void renderSub (Voice& v, float basePitch, int n) noexcept;
    void renderNoise (Voice& v, int n) noexcept;
    void addToBus (int route, const float* l, const float* r, int n) noexcept;
    float modSourceValue (const std::array<float, numModSources>& src, const ModRoute& r) const noexcept;

    float sr = 44100.0f;
    EngineTables tables;
    std::vector<juce::NormalisableRange<float>> ranges;

    const float* base = nullptr;
    const float* baseNorm = nullptr;
    std::array<ModRoute, maxModSlots> routes {};
    int numRoutes = 0;
    bool routesTouchVoices = false;
    double bpm = 120.0, ppq = 0.0;
    bool playing = false;

    std::array<Voice, maxVoices> voices;
    uint64_t ageCounter = 0;
    float alternateState = 1.0f;
    aa::dsp::Rng rng { 0x5EED1234u };

    // performance state
    float pitchBend = 0.0f;      // -1..1
    float modWheel = 0.0f;
    float channelPressure = 0.0f;
    bool sustainPedal = false;
    std::array<int, 128> heldStack {};
    int heldCount = 0;
    float lastNotePitch = -1.0f;

    std::array<LfoState, numLfos> globalLfo;
    std::array<float, numModSources> globalSrc {};
    std::vector<float> fxMod;
    bool anyFxMod = false;

    // scratch buses (per voice, reused)
    std::array<std::array<float, controlBlock>, numBuses * 2> bus {};
    std::array<float, controlBlock> tmpL {}, tmpR {};

    int uiCounter = 0;
};
} // namespace ers
