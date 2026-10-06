#pragma once

#include <aakit/AdventureKit.h>

namespace gremlin
{
//==============================================================================
/** Everything the gremlin can do to a step. `none` lets the audio through untouched. */
enum class Fx : int { none = 0, stutter, reverse, tapeStop, halfSpeed, crush, gate, scatter };
constexpr int numFx = 8;       // including none
constexpr int numDiceFx = 7;   // the ones with a probability weight

enum SliceMode { slice2 = 0, slice4, slice8, slice16, sliceDice, sliceAccel };

inline const juce::StringArray& gridNames()
{
    static const juce::StringArray names { "1/4", "1/8", "1/16", "1/32" };
    return names;
}

inline double gridBeatsFor (int index)
{
    static const double beats[] = { 1.0, 0.5, 0.25, 0.125 };
    return beats[juce::jlimit (0, 3, index)];
}

inline int lockBarsFor (int index)
{
    static const int bars[] = { 0, 1, 2, 4 };
    return bars[juce::jlimit (0, 3, index)];
}

inline const char* fxName (Fx fx)
{
    switch (fx)
    {
        case Fx::stutter:   return "Stutter";
        case Fx::reverse:   return "Reverse";
        case Fx::tapeStop:  return "Tape Stop";
        case Fx::halfSpeed: return "Half Speed";
        case Fx::crush:     return "Bitcrush";
        case Fx::gate:      return "Gate";
        case Fx::scatter:   return "Scatter";
        case Fx::none:      break;
    }
    return "Clean";
}

//==============================================================================
/** The dice: how likely a step is to be glitched, and by what. */
struct DiceSettings
{
    float chaos = 0.3f;                               // 0..1 master probability
    std::array<float, numDiceFx> weights {};          // per effect, 0..1 (index = Fx - 1)
    int sliceMode = sliceDice;
    double gridBeats = 0.5;
    int stepsPerBar = 8;
};

/** What the gremlin decided to do with one step. */
struct Decision
{
    Fx fx = Fx::none;
    int slices = 4;              // repeats / chops per step (0 = accelerating roll)
    int scatterSteps = 1;        // how many steps back a scatter jumps
    uint32_t gateMask = 0xffffu; // which chops of a gate are open
    float variation = 1.0f;      // 0.75..1.25 flavour for crush depth / gate duty
    int span = 1;                // steps the effect occupies
};

inline uint32_t mixHash (uint32_t h)
{
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

inline uint32_t stepHash (uint32_t seed, int64_t barInPattern, int64_t stepInBar, int gridIndex)
{
    uint32_t h = mixHash (seed * 0x9E3779B1u + 0x7f4a7c15u);
    h = mixHash (h ^ ((uint32_t) barInPattern * 0x85EBCA77u + 0x165667B1u));
    h = mixHash (h ^ ((uint32_t) stepInBar * 0xC2B2AE3Du + (uint32_t) gridIndex * 0x27D4EB2Fu));
    return h == 0 ? 1u : h;
}

/** Tape stops and half-speed need a little room to be recognisable: at least an eighth note. */
inline int spanFor (Fx fx, double gridBeats)
{
    if (fx == Fx::tapeStop || fx == Fx::halfSpeed)
        return std::max (1, (int) std::ceil (0.5 / gridBeats - 1.0e-6));
    return 1;
}

inline int slicesFor (int sliceMode, float roll)
{
    switch (sliceMode)
    {
        case slice2:     return 2;
        case slice4:     return 4;
        case slice8:     return 8;
        case slice16:    return 16;
        case sliceAccel: return 0;
        default:         break;
    }
    return roll < 0.3f ? 2 : (roll < 0.72f ? 4 : (roll < 0.93f ? 8 : 16));
}

/** Rolls the dice for one step. Always draws the same amount of randomness so a step's fate
    stays put while the knobs move (more Chaos only ever adds glitches). */
inline Decision decide (aa::dsp::Rng& rng, const DiceSettings& s)
{
    const float roll = rng.next01();
    const float pick = rng.next01();
    const float sliceRoll = rng.next01();
    const float scatterRoll = rng.next01();
    const uint32_t maskBits = rng.nextU32();
    const float varRoll = rng.next01();

    Decision d;
    d.variation = 0.75f + 0.5f * varRoll;

    if (roll >= s.chaos)
        return d;

    float total = 0.0f;
    for (auto w : s.weights)
        total += std::max (0.0f, w);
    if (total <= 1.0e-4f)
        return d;

    const float target = pick * total;
    float acc = 0.0f;
    int chosen = -1, lastNonZero = 0;
    for (int i = 0; i < numDiceFx; ++i)
    {
        const float w = std::max (0.0f, s.weights[(size_t) i]);
        if (w <= 0.0f)
            continue;
        lastNonZero = i;
        acc += w;
        if (chosen < 0 && target < acc)
            chosen = i;
    }
    d.fx = (Fx) ((chosen < 0 ? lastNonZero : chosen) + 1);

    d.slices = slicesFor (s.sliceMode, sliceRoll);

    const int spb = std::max (1, s.stepsPerBar);
    const int range = std::max (1, 2 * spb - 1);
    int k = 1 + std::min (range - 1, (int) (scatterRoll * (float) range));
    if (spb > 1 && k % spb == 0)
        k -= 1;
    d.scatterSteps = std::max (1, k);

    d.gateMask = (maskBits & 0xffffu) | (maskBits >> 16) | 1u;
    d.span = spanFor (d.fx, s.gridBeats);
    return d;
}

/** Position of a step inside the locked pattern -> its deterministic decision. */
inline Decision lockedDecision (const DiceSettings& s, uint32_t seed, int lockBars, int gridIndex,
                                double stepPpq, double ppqPerBar)
{
    ppqPerBar = std::max (0.25, ppqPerBar);
    const auto bar = (int64_t) std::floor (stepPpq / ppqPerBar + 1.0e-9);
    const auto stepInBar = (int64_t) std::floor ((stepPpq - (double) bar * ppqPerBar) / s.gridBeats + 1.0e-6);
    const int64_t bars = std::max (1, lockBars);
    const int64_t barInPattern = ((bar % bars) + bars) % bars;
    aa::dsp::Rng rng (stepHash (seed, barInPattern, stepInBar, gridIndex));
    return decide (rng, s);
}

//==============================================================================
/** A step as reported to the UI. */
struct StepEvent
{
    double ppq = 0.0;
    float lengthPpq = 0.0f;
    int8_t fx = 0;
    bool forced = false;
    int8_t slices = 0;
};

//==============================================================================
/** The glitch engine: records the input into a circular buffer and, on every grid step,
    plays it back through whichever trick the dice (or a force pad) picked. Every jump in the
    playback position is covered by a short crossfade. Allocation-free after prepare(). */
class Engine
{
public:
    struct Settings
    {
        DiceSettings dice;
        int gridIndex = 1;
        float ratchet = 0.0f;     // semitones added per stutter repeat
        float crush = 0.55f;      // 0..1
        float smoothMs = 4.0f;    // crossfade length
        float mix = 1.0f;         // 0..1
        uint32_t seed = 1;
        int lockBars = 0;         // 0 = free
        std::array<bool, 4> force {}; // stutter, reverse, tape stop, half speed
    };

    struct TimeInfo
    {
        double ppq = 0.0, bpm = 120.0, ppqPerBar = 4.0;
    };

    void prepare (double sampleRate);
    void reset();
    void process (float* left, float* right, int numSamples, const Settings& settings, const TimeInfo& time);

    static Fx forceFx (int index)
    {
        static const Fx map[] = { Fx::stutter, Fx::reverse, Fx::tapeStop, Fx::halfSpeed };
        return map[juce::jlimit (0, 3, index)];
    }

    //==============================================================================
    // UI feed (written on the audio thread, read on the message thread)
    aa::dsp::SpscQueue<StepEvent, 512> events;
    std::atomic<int> activeFx { 0 };
    std::atomic<bool> activeForced { false };
    std::atomic<float> gateLevel { 1.0f }, tapeSpeed { 1.0f };
    std::atomic<int> repeatIndex { 0 };
    std::atomic<double> uiPpq { 0.0 }, uiBpm { 120.0 }, uiPpqPerBar { 4.0 };
    std::atomic<float> inLevel { 0.0f }, outLevel { 0.0f };
    std::atomic<uint32_t> stepCounter { 0 };

private:
    struct Voice
    {
        bool alive = false, held = false;
        Fx fx = Fx::none;
        double t0 = 0.0, n = 0.0, len = 1.0, cycleLen = 0.0;
        float fade = 0.0f, fadeInc = 0.0f;

        // repeat / chop schedule (stutter + gate)
        int slices = 4, repIdx = 0;
        double repStart = 0.0, repLen = 1.0, minRepLen = 1.0;
        double rate = 1.0, prevRate = 1.0, prevRepStart = 0.0;
        int xfLeft = 0, xfLen = 1;
        float ratchet = 0.0f;

        double offset = 0.0;      // scatter distance
        double rampLen = 1.0;     // tape stop
        uint32_t mask = 0xffffu;
        float duty = 0.5f;
        float holdLen = 1.0f, holdPhase = 0.0f, levels = 256.0f, heldL = 0.0f, heldR = 0.0f;
        float lpL = 0.0f, lpR = 0.0f;
        float lastGate = 1.0f, lastSpeed = 1.0f;
    };

    static constexpr int maxVoices = 6;

    void read (double pos, float& l, float& r) const;
    void decideStep (bool fromBoundary, const Settings& s);
    void startVoice (const Decision& d, bool held, double t0, double n0, const Settings& s);
    void renderVoice (Voice& v, float& l, float& r);
    double repLenFor (const Voice& v, int k) const;
    double rateFor (const Voice& v, int k) const;
    void advanceRepeats (Voice& v);
    void pushEvent (Fx fx, bool forced, int span, int slices);
    bool updateForce (const std::array<bool, 4>& held);

    std::vector<float> bufL, bufR;
    int64_t mask = 0, capacity = 0, writeCount = 0;

    std::array<Voice, maxVoices> voices;
    int active = -1;

    double sr = 44100.0;
    double samplesPerPpq = 22050.0, stepLen = 11025.0, smoothSamples = 176.0, ppqPerBar = 4.0;
    int64_t currentStep = std::numeric_limits<int64_t>::min();
    int lastGridIndex = -1;
    double stepStartAbs = 0.0, stepPpq = 0.0;
    int spanRemaining = 0;

    std::array<bool, 4> forceHeld {};
    std::array<uint32_t, 4> forceOrder {};
    uint32_t forceCounter = 0;
    Fx desiredForce = Fx::none;

    aa::dsp::Rng freeRng { 0x51ED270Bu };
    aa::dsp::Smoother mixSmoother;
    float inPeak = 0.0f, outPeak = 0.0f, peakRelease = 0.999f;
};
} // namespace gremlin
