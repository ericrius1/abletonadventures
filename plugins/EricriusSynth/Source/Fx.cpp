// ericriusSynth - the ten-effect rack.
// Copyright (C) Adventure Audio. Licensed under the GNU AGPLv3.
//
// Every effect runs in sub-blocks of 32 samples: parameters are smoothed once per sub-block (one-pole) and
// interpolated per sample where it matters (gains, delay times). Switching an effect off fades it to dry
// over 10 ms before it is skipped; switching it back on resets its state and fades it in.
// Nothing here allocates, locks or logs once prepare() has run, and the rack adds no latency.

#include "Fx.h"
#include "Filters.h"
#include <aakit/dsp/Reverb.h>
#include <juce_dsp/juce_dsp.h>

namespace ers::fx
{
juce::String kindName (Kind k)
{
    switch (k)
    {
        case Kind::hyper:      return "Hyper";
        case Kind::distortion: return "Distortion";
        case Kind::flanger:    return "Flanger";
        case Kind::phaser:     return "Phaser";
        case Kind::chorus:     return "Chorus";
        case Kind::delay:      return "Delay";
        case Kind::compressor: return "Compressor";
        case Kind::reverb:     return "Reverb";
        case Kind::eq:         return "EQ";
        case Kind::filter:     return "Filter";
        case Kind::count:      break;
    }
    return {};
}

juce::String kindPrefix (Kind k)
{
    switch (k)
    {
        case Kind::hyper:      return "hy";
        case Kind::distortion: return "di";
        case Kind::flanger:    return "fl";
        case Kind::phaser:     return "ph";
        case Kind::chorus:     return "ch";
        case Kind::delay:      return "dl";
        case Kind::compressor: return "comp";
        case Kind::reverb:     return "rv";
        case Kind::eq:         return "eq";
        case Kind::filter:     return "ff";
        case Kind::count:      break;
    }
    return {};
}

namespace
{
namespace ad = aa::dsp;

constexpr int blockLen = 32;

//==============================================================================
// Tempo-sync divisions for the delay (in quarter notes).
struct Division
{
    const char* name;
    double beats;
};

constexpr Division divisions[] = {
    { "1/32T", 1.0 / 12.0 }, { "1/32", 0.125 }, { "1/32D", 0.1875 },
    { "1/16T", 1.0 / 6.0 },  { "1/16", 0.25 },  { "1/16D", 0.375 },
    { "1/8T", 1.0 / 3.0 },   { "1/8", 0.5 },    { "1/8D", 0.75 },
    { "1/4T", 2.0 / 3.0 },   { "1/4", 1.0 },    { "1/4D", 1.5 },
    { "1/2T", 4.0 / 3.0 },   { "1/2", 2.0 },    { "1/2D", 3.0 },
    { "1/1", 4.0 },          { "2/1", 8.0 },
};
constexpr int numDivisions = (int) (sizeof (divisions) / sizeof (divisions[0]));
constexpr int quarterDivision = 10;

juce::StringArray divisionNames()
{
    juce::StringArray names;
    for (const auto& d : divisions)
        names.add (d.name);
    return names;
}

constexpr int phaserStageCounts[] = { 4, 6, 8, 12 };

//==============================================================================
std::vector<ParamSpec> buildSpecs()
{
    using U = aa::params::Unit;
    constexpr float noSkew = aa::params::noSkew;
    std::vector<ParamSpec> specs;

    auto add = [&specs] (Kind k, const char* idName, const char* name, const char* label, float min, float max,
                         float def, U unit = U::none, float skew = aa::params::noSkew, float step = 0.0f, bool mod = true)
    {
        ParamSpec p;
        p.id = kindPrefix (k) + idName;
        p.name = kindName (k) + " " + name;
        p.label = label;
        p.owner = k;
        p.min = min;
        p.max = max;
        p.def = def;
        p.unit = unit;
        p.skewCentre = skew;
        p.step = step;
        p.modulatable = mod;
        specs.push_back (p);
    };

    auto choice = [&specs] (Kind k, const char* idName, const char* name, const char* label,
                            const juce::StringArray& items, int def)
    {
        ParamSpec p;
        p.id = kindPrefix (k) + idName;
        p.name = kindName (k) + " " + name;
        p.label = label;
        p.owner = k;
        p.type = ParamSpec::Type::choice;
        p.min = 0.0f;
        p.max = (float) (items.size() - 1);
        p.def = (float) def;
        p.step = 1.0f;
        p.choices = items;
        p.modulatable = false;
        specs.push_back (p);
    };

    auto toggle = [&specs] (Kind k, const char* idName, const char* name, const char* label, bool def)
    {
        ParamSpec p;
        p.id = kindPrefix (k) + idName;
        p.name = kindName (k) + " " + name;
        p.label = label;
        p.owner = k;
        p.type = ParamSpec::Type::toggle;
        p.min = 0.0f;
        p.max = 1.0f;
        p.def = def ? 1.0f : 0.0f;
        p.step = 1.0f;
        p.modulatable = false;
        specs.push_back (p);
    };

    auto onToggle = [&toggle] (Kind k) { toggle (k, "On", "On", "ON", false); };

    // Hyper: unison chorus + dimension widener
    onToggle (Kind::hyper);
    add (Kind::hyper, "Rate", "Rate", "RATE", 0.05f, 5.0f, 0.6f, U::hz, 0.8f);
    add (Kind::hyper, "Detune", "Detune", "DETUNE", 0.0f, 100.0f, 30.0f, U::percent);
    choice (Kind::hyper, "Voices", "Voices", "VOICES", { "2", "3", "4", "5", "6", "7" }, 2);
    add (Kind::hyper, "DimSize", "Dimension Size", "DIM SIZE", 0.0f, 100.0f, 50.0f, U::percent);
    add (Kind::hyper, "DimMix", "Dimension Mix", "DIM MIX", 0.0f, 100.0f, 0.0f, U::percent);
    add (Kind::hyper, "Mix", "Mix", "MIX", 0.0f, 100.0f, 50.0f, U::percent);

    // Distortion
    onToggle (Kind::distortion);
    choice (Kind::distortion, "Mode", "Mode", "MODE",
            { "Tube", "Soft Clip", "Hard Clip", "Diode", "Linear Fold", "Sine Fold", "Zero-Square", "Downsample",
              "Asymmetric", "Rectify", "Bitcrush" }, 0);
    add (Kind::distortion, "Drive", "Drive", "DRIVE", 0.0f, 100.0f, 25.0f, U::percent);
    choice (Kind::distortion, "Filter", "Filter", "FILTER", { "Off", "Pre", "Post" }, 0);
    add (Kind::distortion, "FilterFreq", "Filter Freq", "FREQ", 20.0f, 20000.0f, 1000.0f, U::hz, 1000.0f);
    add (Kind::distortion, "FilterQ", "Filter Q", "Q", 0.3f, 10.0f, 0.8f, U::none, 1.5f);
    add (Kind::distortion, "Mix", "Mix", "MIX", 0.0f, 100.0f, 100.0f, U::percent);

    // Flanger
    onToggle (Kind::flanger);
    add (Kind::flanger, "Rate", "Rate", "RATE", 0.02f, 10.0f, 0.3f, U::hz, 1.0f);
    add (Kind::flanger, "Depth", "Depth", "DEPTH", 0.0f, 100.0f, 50.0f, U::percent);
    add (Kind::flanger, "Feedback", "Feedback", "FEEDBACK", -100.0f, 100.0f, 40.0f, U::percent);
    add (Kind::flanger, "Stereo", "Stereo Phase", "STEREO", 0.0f, 180.0f, 90.0f, U::none, noSkew, 1.0f);
    add (Kind::flanger, "Mix", "Mix", "MIX", 0.0f, 100.0f, 100.0f, U::percent);

    // Phaser
    onToggle (Kind::phaser);
    add (Kind::phaser, "Rate", "Rate", "RATE", 0.02f, 10.0f, 0.25f, U::hz, 1.0f);
    add (Kind::phaser, "Depth", "Depth", "DEPTH", 0.0f, 100.0f, 60.0f, U::percent);
    add (Kind::phaser, "Freq", "Frequency", "FREQ", 20.0f, 18000.0f, 800.0f, U::hz, 1000.0f);
    add (Kind::phaser, "Feedback", "Feedback", "FEEDBACK", -100.0f, 100.0f, 30.0f, U::percent);
    choice (Kind::phaser, "Stages", "Stages", "STAGES", { "4", "6", "8", "12" }, 1);
    add (Kind::phaser, "Stereo", "Stereo Phase", "STEREO", 0.0f, 180.0f, 90.0f, U::none, noSkew, 1.0f);
    add (Kind::phaser, "Mix", "Mix", "MIX", 0.0f, 100.0f, 100.0f, U::percent);

    // Chorus
    onToggle (Kind::chorus);
    add (Kind::chorus, "Rate", "Rate", "RATE", 0.02f, 10.0f, 0.8f, U::hz, 1.0f);
    add (Kind::chorus, "Delay", "Delay", "DELAY", 1.0f, 40.0f, 12.0f, U::ms, 10.0f);
    add (Kind::chorus, "Depth", "Depth", "DEPTH", 0.0f, 100.0f, 40.0f, U::percent);
    add (Kind::chorus, "Feedback", "Feedback", "FEEDBACK", 0.0f, 100.0f, 0.0f, U::percent);
    add (Kind::chorus, "LowCut", "Low Cut", "LOW CUT", 20.0f, 2000.0f, 80.0f, U::hz, 200.0f);
    add (Kind::chorus, "HighCut", "High Cut", "HIGH CUT", 1000.0f, 20000.0f, 12000.0f, U::hz, 6000.0f);
    add (Kind::chorus, "Mix", "Mix", "MIX", 0.0f, 100.0f, 50.0f, U::percent);

    // Delay
    onToggle (Kind::delay);
    choice (Kind::delay, "Mode", "Mode", "MODE", { "Normal", "Ping-Pong", "Tap->Delay" }, 0);
    toggle (Kind::delay, "Sync", "Sync", "SYNC", true);
    add (Kind::delay, "TimeL", "Time L", "TIME L", 1.0f, 2000.0f, 375.0f, U::ms, 300.0f);
    add (Kind::delay, "TimeR", "Time R", "TIME R", 1.0f, 2000.0f, 500.0f, U::ms, 300.0f);
    choice (Kind::delay, "SyncL", "Sync L", "SYNC L", divisionNames(), quarterDivision);
    choice (Kind::delay, "SyncR", "Sync R", "SYNC R", divisionNames(), quarterDivision);
    add (Kind::delay, "Feedback", "Feedback", "FEEDBACK", 0.0f, 100.0f, 40.0f, U::percent);
    add (Kind::delay, "LowCut", "Low Cut", "LOW CUT", 20.0f, 2000.0f, 80.0f, U::hz, 200.0f);
    add (Kind::delay, "HighCut", "High Cut", "HIGH CUT", 500.0f, 20000.0f, 10000.0f, U::hz, 4000.0f);
    add (Kind::delay, "Mix", "Mix", "MIX", 0.0f, 100.0f, 30.0f, U::percent);

    // Compressor
    onToggle (Kind::compressor);
    choice (Kind::compressor, "Mode", "Mode", "MODE", { "Single", "Multiband" }, 0);
    add (Kind::compressor, "Threshold", "Threshold", "THRESHOLD", -60.0f, 0.0f, -18.0f, U::db);
    add (Kind::compressor, "Ratio", "Ratio", "RATIO", 1.0f, 20.0f, 4.0f, U::ratio, 4.0f);
    add (Kind::compressor, "Attack", "Attack", "ATTACK", 0.1f, 100.0f, 5.0f, U::ms, 10.0f);
    add (Kind::compressor, "Release", "Release", "RELEASE", 5.0f, 1000.0f, 100.0f, U::ms, 120.0f);
    add (Kind::compressor, "Gain", "Gain", "GAIN", -12.0f, 24.0f, 0.0f, U::db);
    add (Kind::compressor, "Amount", "Multiband Amount", "AMOUNT", 0.0f, 100.0f, 70.0f, U::percent);
    add (Kind::compressor, "Mix", "Mix", "MIX", 0.0f, 100.0f, 100.0f, U::percent);

    // Reverb
    onToggle (Kind::reverb);
    choice (Kind::reverb, "Mode", "Mode", "MODE", { "Plate", "Hall", "Room" }, 1);
    add (Kind::reverb, "Size", "Size", "SIZE", 0.0f, 100.0f, 55.0f, U::percent, noSkew, 0.0f, false);
    add (Kind::reverb, "PreDelay", "Pre-Delay", "PREDELAY", 0.0f, 250.0f, 12.0f, U::ms, 50.0f);
    add (Kind::reverb, "Decay", "Decay", "DECAY", 0.2f, 20.0f, 2.8f, U::seconds, 3.0f);
    add (Kind::reverb, "Damping", "Damping", "DAMPING", 0.0f, 100.0f, 40.0f, U::percent);
    add (Kind::reverb, "LowCut", "Low Cut", "LOW CUT", 20.0f, 1000.0f, 90.0f, U::hz, 150.0f);
    add (Kind::reverb, "Width", "Width", "WIDTH", 0.0f, 100.0f, 100.0f, U::percent);
    add (Kind::reverb, "Mix", "Mix", "MIX", 0.0f, 100.0f, 25.0f, U::percent);

    // EQ
    onToggle (Kind::eq);
    add (Kind::eq, "LowFreq", "Low Freq", "LOW FREQ", 20.0f, 2000.0f, 120.0f, U::hz, 200.0f);
    add (Kind::eq, "LowGain", "Low Gain", "LOW GAIN", -18.0f, 18.0f, 0.0f, U::db);
    add (Kind::eq, "PeakFreq", "Peak Freq", "PEAK FREQ", 40.0f, 18000.0f, 1000.0f, U::hz, 1000.0f);
    add (Kind::eq, "PeakGain", "Peak Gain", "PEAK GAIN", -18.0f, 18.0f, 0.0f, U::db);
    add (Kind::eq, "PeakQ", "Peak Q", "PEAK Q", 0.2f, 10.0f, 0.8f, U::none, 1.2f);
    add (Kind::eq, "HighFreq", "High Freq", "HIGH FREQ", 500.0f, 20000.0f, 6000.0f, U::hz, 4000.0f);
    add (Kind::eq, "HighGain", "High Gain", "HIGH GAIN", -18.0f, 18.0f, 0.0f, U::db);

    // Filter
    onToggle (Kind::filter);
    choice (Kind::filter, "Type", "Type", "TYPE", filterTypeNames(), 0);
    add (Kind::filter, "Cutoff", "Cutoff", "CUTOFF", 20.0f, 20000.0f, 2000.0f, U::hz, 1000.0f);
    add (Kind::filter, "Resonance", "Resonance", "RES", 0.0f, 100.0f, 20.0f, U::percent);
    add (Kind::filter, "Drive", "Drive", "DRIVE", 0.0f, 100.0f, 0.0f, U::percent);
    add (Kind::filter, "Var", "Var", "VAR", 0.0f, 100.0f, 0.0f, U::percent);
    add (Kind::filter, "Mix", "Mix", "MIX", 0.0f, 100.0f, 100.0f, U::percent);

    return specs;
}
} // namespace

const std::vector<ParamSpec>& paramSpecs()
{
    static const std::vector<ParamSpec> specs = buildSpecs();
    return specs;
}

int specIndex (const juce::String& id)
{
    const auto& specs = paramSpecs();
    for (size_t i = 0; i < specs.size(); ++i)
        if (specs[i].id == id)
            return (int) i;
    return -1;
}

std::vector<int> specsFor (Kind k)
{
    std::vector<int> result;
    const auto& specs = paramSpecs();
    for (size_t i = 0; i < specs.size(); ++i)
        if (specs[i].owner == k)
            result.push_back ((int) i);
    return result;
}

Order defaultOrder()
{
    Order o {};
    for (int i = 0; i < numKinds; ++i)
        o[(size_t) i] = (Kind) i;
    return o;
}

namespace
{
//==============================================================================
// Small helpers

inline float sin2pi (float p) noexcept   // parabolic sin (2 pi p), p in [0, 1)
{
    const float x = (p < 0.5f ? p : p - 1.0f) * 2.0f;
    const float y = 4.0f * x * (1.0f - std::abs (x));
    return y * (0.775f + 0.225f * std::abs (y));
}

inline float wrap01 (float p) noexcept { return p - std::floor (p); }

/** Keeps feedback loops bounded: exactly linear below `knee` (clean repeats at normal levels), then a
    smooth cubic shoulder that saturates at knee + range. */
inline float guard (float x, float knee = 1.0f, float range = 1.0f) noexcept
{
    const float a = std::abs (x);
    if (a <= knee)
        return x;
    const float u = std::min (1.5f, (a - knee) / range);
    return std::copysign (knee + range * (u - (4.0f / 27.0f) * u * u * u), x);
}

/** Equal-power blend for effects whose wet is decorrelated from the dry (chorus, hyper). */
inline void equalPowerGains (float mix, float& dry, float& wet) noexcept
{
    // cheap quarter-sine pair (error < 0.2 %)
    const float t = ad::clamp01 (mix);
    wet = sin2pi (0.25f * t);
    dry = sin2pi (0.25f * (1.0f - t));
}

/** Keeps "dry + delayed loop" (flanger / phaser wet) at unity noise power for loop feedback f:
    the impulse response is 1, 1, f, f^2 ... so its power is 2 + f^2 / (1 - f^2). */
inline float combNorm (float f) noexcept
{
    const float f2 = std::min (0.99f, f * f);
    return 1.0f / std::sqrt (2.0f + f2 / (1.0f - f2));
}

/** Send-style blend for delay / reverb: dry stays at unity up to 50 %, wet reaches unity at 50 %. */
inline void sendGains (float mix, float& dry, float& wet) noexcept
{
    dry = std::min (1.0f, 2.0f * (1.0f - mix));
    wet = std::min (1.0f, 2.0f * mix);
}

int idx (const char* id)
{
    const int i = specIndex (id);
    jassert (i >= 0);
    return std::max (0, i);
}

/** The processor's values for one sub-block, clamped to each parameter's range. */
struct Params
{
    const float* values = nullptr;
    const ParamSpec* specs = nullptr;

    float get (int i) const noexcept
    {
        const auto& s = specs[i];
        const float v = values[i];
        return std::isfinite (v) ? juce::jlimit (s.min, s.max, v) : s.def;
    }
    float unit (int i) const noexcept { return get (i) * 0.01f; }   // percent -> 0..1
    int index (int i) const noexcept { return (int) std::lround (get (i)); }
    bool on (int i) const noexcept { return get (i) > 0.5f; }
};

/** One-pole smoothing advanced once per sub-block (Lerp spreads each step over its samples). */
struct Smooth
{
    float prev = 0.0f, cur = 0.0f, coeff = 1.0f;
    bool primed = false;

    void setTime (float sr, float seconds) { coeff = 1.0f - std::exp (-(float) blockLen / std::max (1.0f, seconds * sr)); }
    void reset() noexcept { primed = false; }

    float next (float target) noexcept
    {
        if (! primed)
        {
            prev = cur = target;
            primed = true;
            return cur;
        }
        prev = cur;
        cur += coeff * (target - cur);
        if (std::abs (target - cur) <= 1.0e-6f * std::max (1.0f, std::abs (target)))
            cur = target;
        return cur;
    }

    bool moving() const noexcept { return prev != cur; }
};

struct Lerp
{
    float v, d;
    Lerp (const Smooth& s, int n) noexcept : v (s.prev), d ((s.cur - s.prev) / (float) n) {}
    Lerp (float from, float to, int n) noexcept : v (from), d ((to - from) / (float) n) {}
    float next() noexcept { return v += d; }
};

//==============================================================================
struct Effect
{
    virtual ~Effect() = default;
    virtual void prepare (float sampleRate) = 0;
    virtual void reset() = 0;
    /** n <= blockLen, in place. */
    virtual void process (float* l, float* r, int n, const Params& p, const TempoInfo& tempo) noexcept = 0;

    float sr = 44100.0f;
};

//==============================================================================
/** Hyper: up to seven slowly drifting, detuned delay voices fanned across the stereo field, followed by
    "dimension": a decorrelated copy of the mid signal (four allpass diffusers) added to the side channel,
    which widens without touching the mono sum. */
struct Hyper final : Effect
{
    static constexpr int maxVoices = 7, numDiffusers = 4;
    static constexpr float rateMul[maxVoices] = { 1.0f, 1.19f, 0.83f, 1.41f, 0.69f, 1.61f, 0.57f };
    static constexpr float baseMs[maxVoices] = { 11.0f, 16.5f, 8.5f, 19.0f, 13.5f, 7.0f, 21.5f };
    static constexpr float diffuserMs[numDiffusers] = { 4.7f, 7.3f, 11.3f, 16.1f };

    const int iRate = idx ("hyRate"), iDetune = idx ("hyDetune"), iVoices = idx ("hyVoices"),
              iDimSize = idx ("hyDimSize"), iDimMix = idx ("hyDimMix"), iMix = idx ("hyMix");

    ad::DelayLine lineL, lineR, diffuser[numDiffusers];
    ad::OnePole dimHp;
    float phase[maxVoices] {};
    Smooth rate, detune, dimSize, dimMix, mix, gainL[maxVoices], gainR[maxVoices], srcL[maxVoices];

    void prepare (float s) override
    {
        sr = s;
        lineL.prepare ((int) (0.05f * sr) + 8);
        lineR.prepare ((int) (0.05f * sr) + 8);
        for (auto& d : diffuser)
            d.prepare ((int) (0.04f * sr) + 8);
        dimHp.setCutoff (150.0f, sr);

        rate.setTime (sr, 0.05f);
        detune.setTime (sr, 0.05f);
        dimSize.setTime (sr, 0.3f);
        dimMix.setTime (sr, 0.02f);
        mix.setTime (sr, 0.02f);
        for (int v = 0; v < maxVoices; ++v)
        {
            gainL[v].setTime (sr, 0.03f);
            gainR[v].setTime (sr, 0.03f);
            srcL[v].setTime (sr, 0.03f);
        }
    }

    void reset() override
    {
        lineL.clear();
        lineR.clear();
        for (auto& d : diffuser)
            d.clear();
        dimHp.reset();
        for (int v = 0; v < maxVoices; ++v)
        {
            phase[v] = (float) v / (float) maxVoices;
            gainL[v].reset();
            gainR[v].reset();
            srcL[v].reset();
        }
        for (auto* s : { &rate, &detune, &dimSize, &dimMix, &mix })
            s->reset();
    }

    void process (float* l, float* r, int n, const Params& p, const TempoInfo&) noexcept override
    {
        const int voices = 2 + juce::jlimit (0, maxVoices - 2, p.index (iVoices));
        const float rateHz = rate.next (p.get (iRate));
        const float det = detune.next (p.unit (iDetune));
        const float size = dimSize.next (p.unit (iDimSize));
        dimMix.next (p.unit (iDimMix));
        mix.next (p.unit (iMix));

        const float msToSamples = 0.001f * sr;
        const float detuneRatio = det * det * 0.0145f;   // peak pitch deviation, about +-25 cents at 100 %
        const float norm = std::sqrt (2.0f / (float) voices);   // equal-power pans: unity wet power

        float inc[maxVoices], amp[maxVoices], base[maxVoices];
        float gl[maxVoices], gld[maxVoices], gr[maxVoices], grd[maxVoices], sl[maxVoices], sld[maxVoices];
        int numActive = 0;

        for (int v = 0; v < maxVoices; ++v)
        {
            const float rv = rateHz * rateMul[v];
            inc[v] = rv / sr;
            // A sine-modulated delay of amplitude A detunes by up to 2 pi f A: pick A for the wanted detune.
            amp[v] = std::min (0.009f, detuneRatio / (ad::twoPi * rv)) * sr;
            base[v] = baseMs[v] * msToSamples;

            float tl = 0.0f, tr = 0.0f, src = 0.5f;
            if (v < voices)
            {
                const float pan = -1.0f + 2.0f * (float) v / (float) (voices - 1);
                const float a = (pan + 1.0f) * 0.25f * ad::pi;
                tl = std::cos (a) * norm;
                tr = std::sin (a) * norm;
                src = 0.5f - 0.5f * pan;   // left-panned voices mostly read the left input
            }
            gainL[v].next (tl);
            gainR[v].next (tr);
            srcL[v].next (src);

            const Lerp a (gainL[v], n), b (gainR[v], n), c (srcL[v], n);
            gl[v] = a.v; gld[v] = a.d;
            gr[v] = b.v; grd[v] = b.d;
            sl[v] = c.v; sld[v] = c.d;
            if (gainL[v].prev != 0.0f || gainL[v].cur != 0.0f || gainR[v].prev != 0.0f || gainR[v].cur != 0.0f)
                numActive = v + 1;
        }

        Lerp mixL (mix, n), dimL (dimMix, n);
        const float diffScale = (0.3f + 1.4f * size) * msToSamples;

        for (int i = 0; i < n; ++i)
        {
            const float xl = l[i], xr = r[i];
            lineL.push (xl);
            lineR.push (xr);

            float wl = 0.0f, wr = 0.0f;
            for (int v = 0; v < numActive; ++v)
            {
                phase[v] += inc[v];
                if (phase[v] >= 1.0f)
                    phase[v] -= 1.0f;

                gl[v] += gld[v];
                gr[v] += grd[v];
                sl[v] += sld[v];
                const float d = base[v] + amp[v] * (1.0f + sin2pi (phase[v]));
                const float src = sl[v] * lineL.read (d) + (1.0f - sl[v]) * lineR.read (d);
                wl += gl[v] * src;
                wr += gr[v] * src;
            }

            float dryGain, wetGain;
            equalPowerGains (mixL.next(), dryGain, wetGain);
            float ol = dryGain * xl + wetGain * wl;
            float orr = dryGain * xr + wetGain * wr;

            // Dimension: mid -> high-pass -> allpass diffusers -> side.
            float x = dimHp.highpass (0.5f * (ol + orr));
            for (int j = 0; j < numDiffusers; ++j)
            {
                const float dly = diffuser[j].read (diffuserMs[j] * diffScale);
                const float w = x + 0.55f * dly;
                diffuser[j].push (ad::undenormalise (w));
                x = dly - 0.55f * w;
            }
            const float side = 0.75f * dimL.next() * x;
            l[i] = ol + side;
            r[i] = orr - side;
        }
    }
};

//==============================================================================
/** Distortion: eleven shapers at 2x oversampling (polyphase IIR half-band filters). The dry/wet blend
    happens inside the oversampled domain, so dry and wet see identical filtering and stay phase aligned. */
struct Distortion final : Effect
{
    enum Mode { tube, soft, hard, diode, linFold, sinFold, zeroSquare, downsample, asym, rectify, bitcrush };

    const int iMode = idx ("diMode"), iDrive = idx ("diDrive"), iFilter = idx ("diFilter"),
              iFreq = idx ("diFilterFreq"), iQ = idx ("diFilterQ"), iMix = idx ("diMix");

    std::atomic<float>& meter;
    juce::dsp::Oversampling<float> os { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false };
    ad::Svf band[2];
    Smooth drive, logFreq, q, mix;
    float dc[2] {}, held[2] {}, holdPhase = 1.0f, duck = 1.0f, env = 0.0f;
    float dcCoeff = 0.001f, duckStep = 0.01f, envRelease = 0.999f;
    int mode = 0, filterMode = 0;
    bool fresh = true;

    explicit Distortion (std::atomic<float>& m) : meter (m) {}

    void prepare (float s) override
    {
        sr = s;
        os.initProcessing ((size_t) blockLen);
        const float osr = 2.0f * sr;
        dcCoeff = 1.0f - std::exp (-ad::twoPi * 12.0f / osr);
        duckStep = 1.0f / (0.004f * osr);
        envRelease = std::exp (-1.0f / (0.15f * sr));
        drive.setTime (sr, 0.03f);
        logFreq.setTime (sr, 0.03f);
        q.setTime (sr, 0.03f);
        mix.setTime (sr, 0.02f);
    }

    void reset() override
    {
        os.reset();
        for (auto& b : band)
            b.reset();
        dc[0] = dc[1] = held[0] = held[1] = 0.0f;
        holdPhase = 1.0f;
        duck = 1.0f;
        env = 0.0f;
        fresh = true;
        for (auto* s : { &drive, &logFreq, &q, &mix })
            s->reset();
    }

    static float shape (int m, float x) noexcept
    {
        switch (m)
        {
            case tube:       return ad::softClip (x + 0.2f) - 0.19766f;   // biased: warm even harmonics
            case soft:       return ad::softClip (x);
            case hard:       return juce::jlimit (-1.0f, 1.0f, x);
            case diode:      return x >= 0.0f ? 1.0f - std::exp (-x) : -0.6f * (1.0f - std::exp (x * (1.0f / 0.6f)));
            case linFold:
            {
                float t = (x + 1.0f) * 0.25f;   // triangle fold: identity inside +-1, folds back beyond
                t -= std::floor (t);
                return 1.0f - 4.0f * std::abs (t - 0.5f);
            }
            case sinFold:    return std::sin (x * (0.5f * ad::pi));
            case zeroSquare: return x >= 0.0f ? std::min (1.0f, x * x) : -std::min (1.0f, x * x);
            case asym:       return x >= 0.0f ? ad::softClip (x) : x / (1.0f - 4.0f * x);
            default:         return x;
        }
    }

    void process (float* l, float* r, int n, const Params& p, const TempoInfo&) noexcept override
    {
        const int wantMode = juce::jlimit (0, 10, p.index (iMode));
        const int wantFilter = juce::jlimit (0, 2, p.index (iFilter));
        if (fresh)
        {
            mode = wantMode;
            filterMode = wantFilter;
            fresh = false;
        }

        drive.next (p.unit (iDrive));
        const float f = std::min (0.45f * sr, std::exp2 (logFreq.next (std::log2 (p.get (iFreq)))));
        const float qq = q.next (p.get (iQ));
        mix.next (p.unit (iMix));
        for (auto& b : band)
            b.setCutoffQ (f, qq, 2.0f * sr);

        // Drive in dB (0..36) for the shapers, raw amount for downsample / bitcrush; make-up g^-0.4.
        const float g0 = ad::dbToGain (drive.prev * 36.0f), g1 = ad::dbToGain (drive.cur * 36.0f);
        float* chans[2] = { l, r };
        juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
        auto up = os.processSamplesUp (block);
        const int m = (int) up.getNumSamples();
        float* u[2] = { up.getChannelPointer (0), up.getChannelPointer (1) };

        Lerp gain (g0, g1, m), comp (std::pow (g0, -0.4f), std::pow (g1, -0.4f), m), amount (drive.prev, drive.cur, m),
             wet (mix.prev, mix.cur, m);

        for (int i = 0; i < m; ++i)
        {
            const float g = gain.next(), c = comp.next(), a = amount.next(), w = wet.next() * duck;
            float s[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                s[ch] = u[ch][i];
                if (filterMode == 1)
                    s[ch] = band[ch].process (s[ch]).bp * band[ch].k;
            }

            if (mode == downsample)
            {
                holdPhase += 1.0f / (1.0f + 63.0f * a * a);
                if (holdPhase >= 1.0f)
                {
                    holdPhase -= std::floor (holdPhase);
                    held[0] = s[0];
                    held[1] = s[1];
                }
                s[0] = held[0];
                s[1] = held[1];
            }
            else if (mode == rectify)
            {
                // Full-wave rectifier (octave up) with gentle drive; the DC blocker removes the offset.
                const float rg = 1.0f + 3.0f * a;
                for (auto& v : s)
                    v = ad::softClip (std::abs (v) * rg);
            }
            else if (mode == bitcrush)
            {
                const float levels = std::exp2 (15.0f - 13.5f * a), inv = 1.0f / levels;
                for (auto& v : s)
                    v = std::floor (v * levels + 0.5f) * inv;
            }
            else
            {
                for (auto& v : s)
                    v = shape (mode, v * g) * c;
            }

            for (int ch = 0; ch < 2; ++ch)
            {
                float y = s[ch];
                if (filterMode == 2)
                    y = band[ch].process (y).bp * band[ch].k;
                dc[ch] = ad::undenormalise (dc[ch] + dcCoeff * (y - dc[ch]));
                y -= dc[ch];
                const float x = u[ch][i];
                u[ch][i] = x + w * (y - x);
            }

            // Mode / filter changes duck the wet for a few ms instead of clicking.
            if (mode != wantMode || filterMode != wantFilter)
            {
                duck -= duckStep;
                if (duck <= 0.0f)
                {
                    duck = 0.0f;
                    mode = wantMode;
                    filterMode = wantFilter;
                    for (auto& b : band)
                        b.reset();
                }
            }
            else if (duck < 1.0f)
            {
                duck = std::min (1.0f, duck + duckStep);
            }
        }

        os.processSamplesDown (block);

        for (int i = 0; i < n; ++i)
            env = std::max (std::max (std::abs (l[i]), std::abs (r[i])), env * envRelease);
        meter.store (juce::jlimit (0.0f, 1.0f, drive.cur * std::min (1.0f, 2.0f * env) * mix.cur),
                     std::memory_order_relaxed);
    }
};

//==============================================================================
/** Flanger: LFO-swept short delay (0.25 .. 5.25 ms) with bipolar feedback; the wet is dry + delayed. */
struct Flanger final : Effect
{
    const int iRate = idx ("flRate"), iDepth = idx ("flDepth"), iFeedback = idx ("flFeedback"),
              iStereo = idx ("flStereo"), iMix = idx ("flMix");

    ad::DelayLine line[2];
    float phase = 0.0f;
    Smooth rate, depth, feedback, stereo, mix;

    void prepare (float s) override
    {
        sr = s;
        for (auto& d : line)
            d.prepare ((int) (0.02f * sr) + 8);
        for (auto* sm : { &rate, &depth, &feedback, &stereo, &mix })
            sm->setTime (sr, 0.03f);
    }

    void reset() override
    {
        for (auto& d : line)
            d.clear();
        phase = 0.0f;
        for (auto* sm : { &rate, &depth, &feedback, &stereo, &mix })
            sm->reset();
    }

    void process (float* l, float* r, int n, const Params& p, const TempoInfo&) noexcept override
    {
        const float inc = rate.next (p.get (iRate)) / sr;
        depth.next (p.unit (iDepth));
        feedback.next (p.get (iFeedback) * 0.0095f);   // +-95 %
        const float offset = stereo.next (p.get (iStereo)) / 360.0f;
        mix.next (p.unit (iMix));

        Lerp dep (depth, n), fb (feedback, n), wet (mix, n), nrm (combNorm (feedback.prev), combNorm (feedback.cur), n);
        const float msToSamples = 0.001f * sr;
        float* io[2] = { l, r };

        for (int i = 0; i < n; ++i)
        {
            phase += inc;
            if (phase >= 1.0f)
                phase -= 1.0f;

            const float d = dep.next(), f = fb.next(), m = wet.next();
            const float norm = nrm.next();
            for (int ch = 0; ch < 2; ++ch)
            {
                const float lfo = 0.5f + 0.5f * sin2pi (ch == 0 ? phase : wrap01 (phase + offset));
                const float x = io[ch][i];
                const float delayed = line[ch].read ((0.25f + 5.0f * d * lfo) * msToSamples);
                line[ch].push (ad::undenormalise (guard (x + f * delayed, 1.5f)));
                io[ch][i] = x + m * ((x + delayed) * norm - x);
            }
        }
    }
};

//==============================================================================
/** Phaser: 4..12 first-order allpasses swept by an LFO around Frequency, with bipolar feedback.
    Changing the stage count cross-fades between the two taps of the same chain. */
struct Phaser final : Effect
{
    static constexpr int maxStages = 12;

    const int iRate = idx ("phRate"), iDepth = idx ("phDepth"), iFreq = idx ("phFreq"), iFeedback = idx ("phFeedback"),
              iStages = idx ("phStages"), iStereo = idx ("phStereo"), iMix = idx ("phMix");

    float state[2][maxStages] {}, last[2] {}, G[2] {};
    float phase = 0.0f, stageFade = 1.0f, fadeStep = 0.01f;
    int stages = 6, prevStages = 6;
    bool fresh = true;
    Smooth rate, depth, logFreq, feedback, stereo, mix;

    void prepare (float s) override
    {
        sr = s;
        fadeStep = 1.0f / (0.02f * sr);
        for (auto* sm : { &rate, &depth, &logFreq, &feedback, &stereo, &mix })
            sm->setTime (sr, 0.03f);
    }

    void reset() override
    {
        for (auto& ch : state)
            std::fill (std::begin (ch), std::end (ch), 0.0f);
        last[0] = last[1] = 0.0f;
        phase = 0.0f;
        stageFade = 1.0f;
        fresh = true;
        for (auto* sm : { &rate, &depth, &logFreq, &feedback, &stereo, &mix })
            sm->reset();
    }

    void process (float* l, float* r, int n, const Params& p, const TempoInfo&) noexcept override
    {
        const int want = phaserStageCounts[juce::jlimit (0, 3, p.index (iStages))];
        if (fresh)
        {
            stages = prevStages = want;
            fresh = false;
        }
        else if (want != stages && stageFade >= 1.0f)
        {
            for (auto& ch : state)                      // stages that were idle start from silence
                for (int j = stages; j < maxStages; ++j)
                    ch[j] = 0.0f;
            prevStages = stages;
            stages = want;
            stageFade = 0.0f;
        }

        const float inc = rate.next (p.get (iRate)) / sr;
        const float dep = depth.next (p.unit (iDepth));
        const float lf = logFreq.next (std::log2 (p.get (iFreq)));
        feedback.next (p.get (iFeedback) * 0.0085f);    // +-85 %
        const float offset = stereo.next (p.get (iStereo)) / 360.0f;
        mix.next (p.unit (iMix));

        Lerp fb (feedback, n), wet (mix, n), nrm (combNorm (feedback.prev), combNorm (feedback.cur), n);
        const float maxHz = 0.45f * sr;
        float* io[2] = { l, r };

        for (int i = 0; i < n; ++i)
        {
            phase += inc;
            if (phase >= 1.0f)
                phase -= 1.0f;

            if ((i & 3) == 0)   // the sweep is slow: refresh the allpass coefficient every 4 samples
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float lfo = sin2pi (ch == 0 ? phase : wrap01 (phase + offset));
                    const float hz = juce::jlimit (20.0f, maxHz, std::exp2 (lf + dep * 2.5f * lfo));
                    const float g = std::tan (ad::pi * hz / sr);
                    G[ch] = g / (1.0f + g);
                }
            }

            const float f = fb.next(), m = wet.next(), norm = nrm.next();
            const int count = std::max (stages, stageFade < 1.0f ? prevStages : 0);

            for (int ch = 0; ch < 2; ++ch)
            {
                const float x = io[ch][i];
                float a = x + f * last[ch], tapNew = 0.0f, tapOld = 0.0f;
                float* s = state[ch];
                for (int j = 0; j < count; ++j)
                {
                    const float v = (a - s[j]) * G[ch];
                    const float lp = v + s[j];
                    s[j] = ad::undenormalise (lp + v);
                    a = 2.0f * lp - a;
                    if (j + 1 == stages)
                        tapNew = a;
                    if (j + 1 == prevStages)
                        tapOld = a;
                }
                const float ap = stageFade < 1.0f ? tapOld + stageFade * (tapNew - tapOld) : tapNew;
                last[ch] = guard (ap, 1.5f, 0.5f);
                io[ch][i] = x + m * ((x + ap) * norm - x);
            }

            if (stageFade < 1.0f)
                stageFade = std::min (1.0f, stageFade + fadeStep);
        }
    }
};

//==============================================================================
/** Chorus: two modulated taps per side (cross-coupled, quadrature LFOs), feedback, and a band-limited wet. */
struct Chorus final : Effect
{
    const int iRate = idx ("chRate"), iDelay = idx ("chDelay"), iDepth = idx ("chDepth"), iFeedback = idx ("chFeedback"),
              iLowCut = idx ("chLowCut"), iHighCut = idx ("chHighCut"), iMix = idx ("chMix");

    ad::DelayLine line[2];
    ad::Svf hp[2], lp[2];
    float phase = 0.0f, fbState[2] {};
    Smooth rate, delay, depth, feedback, logLow, logHigh, mix;

    void prepare (float s) override
    {
        sr = s;
        for (auto& d : line)
            d.prepare ((int) (0.07f * sr) + 8);
        for (auto* sm : { &rate, &delay, &depth, &feedback, &logLow, &logHigh, &mix })
            sm->setTime (sr, 0.04f);
    }

    void reset() override
    {
        for (auto& d : line)
            d.clear();
        for (int ch = 0; ch < 2; ++ch)
        {
            hp[ch].reset();
            lp[ch].reset();
            fbState[ch] = 0.0f;
        }
        phase = 0.0f;
        for (auto* sm : { &rate, &delay, &depth, &feedback, &logLow, &logHigh, &mix })
            sm->reset();
    }

    void process (float* l, float* r, int n, const Params& p, const TempoInfo&) noexcept override
    {
        const float inc = rate.next (p.get (iRate)) / sr;
        delay.next (p.get (iDelay));
        depth.next (p.unit (iDepth));
        feedback.next (p.unit (iFeedback) * 0.9f);
        const float low = std::exp2 (logLow.next (std::log2 (p.get (iLowCut))));
        const float high = std::exp2 (logHigh.next (std::log2 (p.get (iHighCut))));
        mix.next (p.unit (iMix));

        for (int ch = 0; ch < 2; ++ch)
        {
            hp[ch].setCutoffQ (low, 0.7071f, sr);
            lp[ch].setCutoffQ (high, 0.7071f, sr);
        }

        const float msToSamples = 0.001f * sr;
        Lerp base (delay.prev * msToSamples, delay.cur * msToSamples, n), dep (depth, n), fb (feedback, n), wet (mix, n);

        for (int i = 0; i < n; ++i)
        {
            phase += inc;
            if (phase >= 1.0f)
                phase -= 1.0f;

            const float b = base.next();
            const float amp = dep.next() * std::min (0.85f * b, 7.0f * msToSamples);
            const float f = fb.next(), m = wet.next();

            const float sA = sin2pi (phase), sB = sin2pi (wrap01 (phase + 0.25f));
            const float sC = sin2pi (wrap01 (phase + 0.5f)), sD = sin2pi (wrap01 (phase + 0.75f));

            float w[2];
            w[0] = 0.82f * line[0].read (b + amp * sA) + 0.57f * line[1].read (1.17f * b + amp * sC);
            w[1] = 0.82f * line[1].read (b + amp * sB) + 0.57f * line[0].read (1.17f * b + amp * sD);

            const float x[2] = { l[i], r[i] };
            for (int ch = 0; ch < 2; ++ch)
            {
                w[ch] = lp[ch].process (hp[ch].process (w[ch]).hp).lp;
                line[ch].push (ad::undenormalise (guard (x[ch] + f * w[ch])));
            }

            float dryGain, wetGain;
            equalPowerGains (m, dryGain, wetGain);
            l[i] = dryGain * x[0] + wetGain * w[0];
            r[i] = dryGain * x[1] + wetGain * w[1];
        }
    }
};

//==============================================================================
/** Delay: Normal (L/R), Ping-Pong (mono in, bouncing L -> R) and Tap->Delay (the right echo follows each
    left echo by Time R). Times glide (rate-limited one-pole) so changes never zipper; the band filters
    sit in the feedback loop so repeats darken and thin out. */
struct Delay final : Effect
{
    enum Mode { normal, pingPong, tapDelay };

    const int iMode = idx ("dlMode"), iSync = idx ("dlSync"), iTimeL = idx ("dlTimeL"), iTimeR = idx ("dlTimeR"),
              iSyncL = idx ("dlSyncL"), iSyncR = idx ("dlSyncR"), iFeedback = idx ("dlFeedback"),
              iLowCut = idx ("dlLowCut"), iHighCut = idx ("dlHighCut"), iMix = idx ("dlMix");

    ad::DelayLine line[2];
    ad::Svf hp[2], lp[2];
    float time[2] {}, maxDelay = 1000.0f, slew = 0.001f, duck = 1.0f, duckStep = 0.01f;
    int mode = normal;
    bool fresh = true;
    Smooth feedback, logLow, logHigh, mix;

    void prepare (float s) override
    {
        sr = s;
        for (auto& d : line)
            d.prepare ((int) (5.0f * sr) + 8);
        maxDelay = (float) (line[0].getMaxDelay() - 4);
        slew = ad::onePoleCoeff (0.1f, sr);
        duckStep = 1.0f / (0.01f * sr);
        for (auto* sm : { &feedback, &logLow, &logHigh, &mix })
            sm->setTime (sr, 0.03f);
    }

    void reset() override
    {
        for (auto& d : line)
            d.clear();
        for (int ch = 0; ch < 2; ++ch)
        {
            hp[ch].reset();
            lp[ch].reset();
        }
        duck = 1.0f;
        fresh = true;
        for (auto* sm : { &feedback, &logLow, &logHigh, &mix })
            sm->reset();
    }

    void process (float* l, float* r, int n, const Params& p, const TempoInfo& tempo) noexcept override
    {
        const int wantMode = juce::jlimit (0, 2, p.index (iMode));
        const double bpm = std::isfinite (tempo.bpm) ? juce::jlimit (20.0, 999.0, tempo.bpm) : 120.0;
        const bool sync = p.on (iSync);

        float target[2];
        for (int ch = 0; ch < 2; ++ch)
        {
            const double seconds = sync ? divisions[juce::jlimit (0, numDivisions - 1, p.index (ch == 0 ? iSyncL : iSyncR))].beats * 60.0 / bpm
                                        : 0.001 * (double) p.get (ch == 0 ? iTimeL : iTimeR);
            target[ch] = juce::jlimit (2.0f, maxDelay, (float) (seconds * sr));
        }

        if (fresh)
        {
            time[0] = target[0];
            time[1] = target[1];
            mode = wantMode;
            fresh = false;
        }

        feedback.next (p.unit (iFeedback) * 0.98f);
        const float low = std::exp2 (logLow.next (std::log2 (p.get (iLowCut))));
        const float high = std::exp2 (logHigh.next (std::log2 (p.get (iHighCut))));
        mix.next (p.unit (iMix));
        for (int ch = 0; ch < 2; ++ch)
        {
            hp[ch].setCutoffQ (low, 0.7071f, sr);
            lp[ch].setCutoffQ (high, 0.7071f, sr);
        }

        Lerp fb (feedback, n), wet (mix, n);
        constexpr float maxGlide = 0.75f;   // samples per sample: pitch never bends more than ~ +-an octave

        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
                time[ch] += juce::jlimit (-maxGlide, maxGlide, slew * (target[ch] - time[ch]));

            const float f = fb.next();
            const float xl = l[i], xr = r[i], mono = 0.5f * (xl + xr);
            const float wl = line[0].read (time[0]);
            const float wr = mode == tapDelay ? line[0].read (std::min (maxDelay, time[0] + time[1])) : line[1].read (time[1]);
            const float fl = lp[0].process (hp[0].process (wl).hp).lp;
            const float fr = lp[1].process (hp[1].process (wr).hp).lp;

            switch (mode)
            {
                case pingPong:
                    line[0].push (ad::undenormalise (guard (mono + f * fr)));
                    line[1].push (ad::undenormalise (guard (f * fl)));
                    break;
                case tapDelay:
                    line[0].push (ad::undenormalise (guard (mono + f * fl)));
                    line[1].push (0.0f);
                    break;
                default:
                    line[0].push (ad::undenormalise (guard (xl + f * fl)));
                    line[1].push (ad::undenormalise (guard (xr + f * fr)));
                    break;
            }

            // Mode changes re-route the loop: dip the echoes for 10 ms rather than click.
            if (mode != wantMode)
            {
                duck -= duckStep;
                if (duck <= 0.0f)
                {
                    duck = 0.0f;
                    mode = wantMode;
                }
            }
            else if (duck < 1.0f)
            {
                duck = std::min (1.0f, duck + duckStep);
            }

            float dryGain, wetGain;
            sendGains (wet.next(), dryGain, wetGain);
            wetGain *= duck;
            l[i] = xl * dryGain + fl * wetGain;
            r[i] = xr * dryGain + fr * wetGain;
        }
    }
};

//==============================================================================
/** Compressor. Single: stereo-linked feed-forward compressor with a 6 dB soft knee.
    Multiband: three bands (Linkwitz-Riley 4th order at 120 Hz and 2.5 kHz), each compressed downward above
    Threshold and upward below Threshold - 18 dB, scaled by Amount - the familiar "OTT" style. */
struct Compressor final : Effect
{
    const int iMode = idx ("compMode"), iThreshold = idx ("compThreshold"), iRatio = idx ("compRatio"),
              iAttack = idx ("compAttack"), iRelease = idx ("compRelease"), iGain = idx ("compGain"),
              iAmount = idx ("compAmount"), iMix = idx ("compMix");

    struct Detector
    {
        float env = 0.0f, gainDb = 0.0f;
    };

    std::atomic<float>& meter;
    Detector single, bands[3];
    ad::Svf split1[2], low2[2], high2[2], split2[2], mid2[2], top2[2], lowAllpass[2];
    Smooth threshold, ratio, makeup, amount, mix;
    float envRelease[4] {}, fadeStep = 0.001f, modeFade = 1.0f;
    int mode = 0, prevMode = 0;
    bool fresh = true;

    static constexpr float bandTime[3] = { 1.6f, 1.0f, 0.6f };   // per-band attack / release scaling

    explicit Compressor (std::atomic<float>& m) : meter (m) {}

    void prepare (float s) override
    {
        sr = s;
        constexpr float bw = 0.70710678f;
        for (int ch = 0; ch < 2; ++ch)
        {
            split1[ch].setCutoffQ (120.0f, bw, sr);
            low2[ch].setCutoffQ (120.0f, bw, sr);
            high2[ch].setCutoffQ (120.0f, bw, sr);
            split2[ch].setCutoffQ (2500.0f, bw, sr);
            mid2[ch].setCutoffQ (2500.0f, bw, sr);
            top2[ch].setCutoffQ (2500.0f, bw, sr);
            lowAllpass[ch].setCutoffQ (2500.0f, bw, sr);
        }
        const float envMs[4] = { 40.0f, 12.0f, 6.0f, 15.0f };   // peak-hold release: low, mid, high, single
        for (int b = 0; b < 4; ++b)
            envRelease[b] = std::exp (-1.0f / (envMs[b] * 0.001f * sr));
        fadeStep = 1.0f / (0.02f * sr);
        for (auto* sm : { &threshold, &ratio, &makeup, &amount, &mix })
            sm->setTime (sr, 0.03f);
    }

    void resetSingle() noexcept { single = {}; }

    void resetMultiband() noexcept
    {
        for (auto& b : bands)
            b = {};
        for (int ch = 0; ch < 2; ++ch)
            for (auto* f : { &split1[ch], &low2[ch], &high2[ch], &split2[ch], &mid2[ch], &top2[ch], &lowAllpass[ch] })
                f->reset();
    }

    void reset() override
    {
        resetSingle();
        resetMultiband();
        modeFade = 1.0f;
        fresh = true;
        for (auto* sm : { &threshold, &ratio, &makeup, &amount, &mix })
            sm->reset();
    }

    static float downward (float levelDb, float thr, float ratio) noexcept
    {
        constexpr float knee = 6.0f;
        const float over = levelDb - thr;
        const float slope = 1.0f / ratio - 1.0f;
        if (over <= -0.5f * knee)
            return 0.0f;
        if (over >= 0.5f * knee)
            return slope * over;
        const float t = over + 0.5f * knee;
        return slope * t * t / (2.0f * knee);
    }

    static float upward (float levelDb, float thr, float ratio) noexcept
    {
        const float under = thr - levelDb;
        if (under <= 0.0f)
            return 0.0f;
        const float gate = ad::clamp01 ((levelDb + 80.0f) * 0.05f);   // leave silence and the noise floor alone
        return std::min (24.0f, under * (1.0f - 1.0f / ratio)) * gate;
    }

    void process (float* l, float* r, int n, const Params& p, const TempoInfo&) noexcept override
    {
        const int wantMode = juce::jlimit (0, 1, p.index (iMode));
        if (fresh)
        {
            mode = prevMode = wantMode;
            fresh = false;
        }
        else if (wantMode != mode && modeFade >= 1.0f)
        {
            prevMode = mode;
            mode = wantMode;
            modeFade = 0.0f;
            if (mode == 0)
                resetSingle();
            else
                resetMultiband();
        }

        const float thr = threshold.next (p.get (iThreshold));
        const float rat = ratio.next (p.get (iRatio));
        makeup.next (p.get (iGain));
        const float amt = amount.next (p.unit (iAmount));
        mix.next (p.unit (iMix));

        const float atkBase = p.get (iAttack) * 0.001f, relBase = p.get (iRelease) * 0.001f;
        float atk[4], rel[4];
        for (int b = 0; b < 3; ++b)
        {
            atk[b] = ad::onePoleCoeff (atkBase * bandTime[b], sr);
            rel[b] = ad::onePoleCoeff (relBase * bandTime[b], sr);
        }
        atk[3] = ad::onePoleCoeff (atkBase, sr);
        rel[3] = ad::onePoleCoeff (relBase, sr);

        const float upThr = thr - 18.0f;
        Lerp gain (ad::dbToGain (makeup.prev), ad::dbToGain (makeup.cur), n), wet (mix, n);
        const bool runSingle = mode == 0 || (modeFade < 1.0f && prevMode == 0);
        const bool runMulti = mode == 1 || (modeFade < 1.0f && prevMode == 1);
        float maxReduction = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float xl = l[i], xr = r[i];
            float sl = 0.0f, sR = 0.0f, ml = 0.0f, mr = 0.0f;

            if (runSingle)
            {
                auto& d = single;
                const float peak = std::max (std::abs (xl), std::abs (xr));
                d.env = ad::undenormalise (peak > d.env ? peak : d.env * envRelease[3]);
                const float target = downward (ad::gainToDb (d.env), thr, rat);
                d.gainDb += (target < d.gainDb ? atk[3] : rel[3]) * (target - d.gainDb);
                const float g = std::exp (d.gainDb * 0.11512925f);
                sl = xl * g;
                sR = xr * g;
                if (mode == 0)
                    maxReduction = std::max (maxReduction, -d.gainDb);
            }

            if (runMulti)
            {
                float band[3][2];
                const float x[2] = { xl, xr };
                for (int ch = 0; ch < 2; ++ch)
                {
                    const auto a = split1[ch].process (x[ch]);
                    const float lo = low2[ch].lowpass (a.lp);
                    const float rest = high2[ch].highpass (a.hp);
                    const auto b = split2[ch].process (rest);
                    band[1][ch] = mid2[ch].lowpass (b.lp);
                    band[2][ch] = top2[ch].highpass (b.hp);
                    const auto ap = lowAllpass[ch].process (lo);   // phase-match the low band to the 2.5 kHz split
                    band[0][ch] = ap.lp - lowAllpass[ch].k * ap.bp + ap.hp;
                }

                for (int b = 0; b < 3; ++b)
                {
                    auto& d = bands[b];
                    const float peak = std::max (std::abs (band[b][0]), std::abs (band[b][1]));
                    d.env = ad::undenormalise (peak > d.env ? peak : d.env * envRelease[b]);
                    const float level = ad::gainToDb (d.env);
                    const float target = amt * (downward (level, thr, rat) + upward (level, upThr, rat));
                    d.gainDb += (target < d.gainDb ? atk[b] : rel[b]) * (target - d.gainDb);
                    const float g = std::exp (d.gainDb * 0.11512925f);
                    ml += band[b][0] * g;
                    mr += band[b][1] * g;
                    if (mode == 1)
                        maxReduction = std::max (maxReduction, -d.gainDb);
                }
            }

            float yl, yr;
            if (modeFade < 1.0f)
            {
                const float t = modeFade;   // 0 = previous mode, 1 = current mode
                const float curL = mode == 0 ? sl : ml, curR = mode == 0 ? sR : mr;
                const float oldL = mode == 0 ? ml : sl, oldR = mode == 0 ? mr : sR;
                yl = oldL + t * (curL - oldL);
                yr = oldR + t * (curR - oldR);
                modeFade = std::min (1.0f, modeFade + fadeStep);
            }
            else
            {
                yl = mode == 0 ? sl : ml;
                yr = mode == 0 ? sR : mr;
            }

            const float g = gain.next(), m = wet.next();
            l[i] = xl + m * (yl * g - xl);
            r[i] = xr + m * (yr * g - xr);
        }

        meter.store (std::max (0.0f, maxReduction), std::memory_order_relaxed);
    }
};

//==============================================================================
/** Reverb: the kit's 8-line FDN. The modes are voicings of its parameters (plate: compact, bright and
    shimmering; hall: big and slowly modulated; room: small and darker). Mapped values glide slowly so
    mode / size changes morph rather than click. */
struct Reverb final : Effect
{
    const int iMode = idx ("rvMode"), iSize = idx ("rvSize"), iPreDelay = idx ("rvPreDelay"), iDecay = idx ("rvDecay"),
              iDamping = idx ("rvDamping"), iLowCut = idx ("rvLowCut"), iWidth = idx ("rvWidth"), iMix = idx ("rvMix");

    ad::FdnReverb fdn;
    ad::FdnReverb::Params current;
    Smooth size, predelay, decay, damping, modDepth, modRate, logLowCut, width, mix;
    float wetL[blockLen] {}, wetR[blockLen] {};
    bool dirty = true;

    void prepare (float s) override
    {
        sr = s;
        fdn.prepare (sr);
        size.setTime (sr, 0.5f);
        predelay.setTime (sr, 0.2f);
        modDepth.setTime (sr, 0.3f);
        modRate.setTime (sr, 0.3f);
        for (auto* sm : { &decay, &damping, &logLowCut, &width, &mix })
            sm->setTime (sr, 0.05f);
    }

    void reset() override
    {
        fdn.reset();
        dirty = true;
        for (auto* sm : { &size, &predelay, &decay, &damping, &modDepth, &modRate, &logLowCut, &width, &mix })
            sm->reset();
    }

    void process (float* l, float* r, int n, const Params& p, const TempoInfo&) noexcept override
    {
        float sz = p.unit (iSize), dmp = p.unit (iDamping), md = 0.55f, mr = 0.35f;
        switch (juce::jlimit (0, 2, p.index (iMode)))
        {
            case 0:  sz = 0.15f + 0.5f * sz;  dmp *= 0.6f;               md = 0.25f; mr = 0.9f;  break;   // plate
            case 2:  sz = 0.05f + 0.35f * sz; dmp = 0.15f + 0.85f * dmp; md = 0.12f; mr = 0.6f;  break;   // room
            default: sz = 0.45f + 0.55f * sz;                            md = 0.55f; mr = 0.35f; break;   // hall
        }

        ad::FdnReverb::Params np;
        np.size = size.next (sz);
        np.decaySeconds = decay.next (p.get (iDecay));
        np.damping = damping.next (dmp);
        np.predelayMs = predelay.next (p.get (iPreDelay));
        np.modDepth = modDepth.next (md);
        np.modRate = modRate.next (mr);
        np.lowCutHz = std::exp2 (logLowCut.next (std::log2 (p.get (iLowCut))));
        np.width = width.next (p.unit (iWidth));
        mix.next (p.unit (iMix));

        if (dirty || np.size != current.size || np.decaySeconds != current.decaySeconds || np.damping != current.damping
            || np.predelayMs != current.predelayMs || np.modDepth != current.modDepth || np.modRate != current.modRate
            || np.lowCutHz != current.lowCutHz || np.width != current.width)
        {
            current = np;
            fdn.setParams (np);
            dirty = false;
        }

        fdn.process (l, r, wetL, wetR, n);

        Lerp wet (mix, n);
        for (int i = 0; i < n; ++i)
        {
            float dryGain, wetGain;
            sendGains (wet.next(), dryGain, wetGain);
            l[i] = l[i] * dryGain + wetL[i] * wetGain;
            r[i] = r[i] * dryGain + wetR[i] * wetGain;
        }
    }
};

//==============================================================================
/** EQ: low shelf, peak and high shelf (RBJ cookbook biquads, transposed direct form II in double).
    Parameters glide; coefficients are recomputed per sub-block while they move and interpolated per sample. */
struct Eq final : Effect
{
    const int iLowFreq = idx ("eqLowFreq"), iLowGain = idx ("eqLowGain"), iPeakFreq = idx ("eqPeakFreq"),
              iPeakGain = idx ("eqPeakGain"), iPeakQ = idx ("eqPeakQ"), iHighFreq = idx ("eqHighFreq"),
              iHighGain = idx ("eqHighGain");

    struct Biquad
    {
        double c[5] { 1.0, 0.0, 0.0, 0.0, 0.0 };   // b0 b1 b2 a1 a2 (a0 normalised)
        double z1[2] {}, z2[2] {};
    };

    Biquad bq[3];
    Smooth logFreq[3], gainDb[3], logQ;
    bool fresh = true;

    void prepare (float s) override
    {
        sr = s;
        for (int b = 0; b < 3; ++b)
        {
            logFreq[b].setTime (sr, 0.03f);
            gainDb[b].setTime (sr, 0.03f);
        }
        logQ.setTime (sr, 0.03f);
    }

    void reset() override
    {
        for (auto& b : bq)
            for (int ch = 0; ch < 2; ++ch)
                b.z1[ch] = b.z2[ch] = 0.0;
        for (int b = 0; b < 3; ++b)
        {
            logFreq[b].reset();
            gainDb[b].reset();
        }
        logQ.reset();
        fresh = true;
    }

    static void design (int type, double f, double db, double q, double sampleRate, double* out) noexcept
    {
        const double A = std::pow (10.0, db / 40.0);
        const double w0 = 2.0 * juce::MathConstants<double>::pi * f / sampleRate;
        const double cw = std::cos (w0), sw = std::sin (w0);
        double b0, b1, b2, a0, a1, a2;

        if (type == 1)   // peak
        {
            const double alpha = sw / (2.0 * q);
            b0 = 1.0 + alpha * A;
            b1 = -2.0 * cw;
            b2 = 1.0 - alpha * A;
            a0 = 1.0 + alpha / A;
            a1 = -2.0 * cw;
            a2 = 1.0 - alpha / A;
        }
        else             // shelves, slope S = 1
        {
            const double sa = 2.0 * std::sqrt (A) * (sw / 2.0 * std::sqrt (2.0));
            const double sign = type == 0 ? 1.0 : -1.0;
            b0 = A * ((A + 1.0) - sign * (A - 1.0) * cw + sa);
            b1 = sign * 2.0 * A * ((A - 1.0) - sign * (A + 1.0) * cw);
            b2 = A * ((A + 1.0) - sign * (A - 1.0) * cw - sa);
            a0 = (A + 1.0) + sign * (A - 1.0) * cw + sa;
            a1 = -sign * 2.0 * ((A - 1.0) + sign * (A + 1.0) * cw);
            a2 = (A + 1.0) + sign * (A - 1.0) * cw - sa;
        }

        out[0] = b0 / a0;
        out[1] = b1 / a0;
        out[2] = b2 / a0;
        out[3] = a1 / a0;
        out[4] = a2 / a0;
    }

    void process (float* l, float* r, int n, const Params& p, const TempoInfo&) noexcept override
    {
        const int fIdx[3] = { iLowFreq, iPeakFreq, iHighFreq };
        const int gIdx[3] = { iLowGain, iPeakGain, iHighGain };
        const float maxHz = 0.45f * sr;
        const double q = std::exp2 (logQ.next (std::log2 (p.get (iPeakQ))));
        const bool qMoving = logQ.moving();

        double inc[3][5] {};
        bool ramp[3] {};
        for (int b = 0; b < 3; ++b)
        {
            const float f = std::min (maxHz, std::exp2 (logFreq[b].next (std::log2 (p.get (fIdx[b])))));
            const float g = gainDb[b].next (p.get (gIdx[b]));
            if (fresh || logFreq[b].moving() || gainDb[b].moving() || (b == 1 && qMoving))
            {
                double target[5];
                design (b, f, g, q, sr, target);
                for (int k = 0; k < 5; ++k)
                {
                    if (fresh)
                        bq[b].c[k] = target[k];
                    inc[b][k] = (target[k] - bq[b].c[k]) / (double) n;
                }
                ramp[b] = ! fresh;
            }
        }
        fresh = false;

        float* io[2] = { l, r };
        for (int i = 0; i < n; ++i)
        {
            for (int b = 0; b < 3; ++b)
            {
                auto& f = bq[b];
                if (ramp[b])
                    for (int k = 0; k < 5; ++k)
                        f.c[k] += inc[b][k];

                for (int ch = 0; ch < 2; ++ch)
                {
                    const double x = io[ch][i];
                    const double y = f.c[0] * x + f.z1[ch];
                    f.z1[ch] = f.c[1] * x - f.c[3] * y + f.z2[ch];
                    f.z2[ch] = f.c[2] * x - f.c[4] * y;
                    io[ch][i] = (float) y;
                }
            }
        }
    }
};

//==============================================================================
/** Filter: one VoiceFilter (any of the 18 types), with smoothed controls. */
struct FilterFx final : Effect
{
    const int iType = idx ("ffType"), iCutoff = idx ("ffCutoff"), iRes = idx ("ffResonance"),
              iDrive = idx ("ffDrive"), iVar = idx ("ffVar"), iMix = idx ("ffMix");

    VoiceFilter filter;
    Smooth logCutoff, res, drive, var, mix;

    void prepare (float s) override
    {
        sr = s;
        filter.prepare (sr);
        for (auto* sm : { &logCutoff, &res, &drive, &var, &mix })
            sm->setTime (sr, 0.02f);
    }

    void reset() override
    {
        filter.reset();
        for (auto* sm : { &logCutoff, &res, &drive, &var, &mix })
            sm->reset();
    }

    void process (float* l, float* r, int n, const Params& p, const TempoInfo&) noexcept override
    {
        const auto type = (FilterType) juce::jlimit (0, (int) FilterType::count - 1, p.index (iType));
        filter.setParams (type, std::exp2 (logCutoff.next (std::log2 (p.get (iCutoff)))), res.next (p.unit (iRes)),
                          drive.next (p.unit (iDrive)), var.next (p.unit (iVar)), mix.next (p.unit (iMix)));
        filter.process (l, r, n);
    }
};

//==============================================================================
uint64_t packOrder (const Order& o) noexcept
{
    uint64_t v = 0;
    for (int i = 0; i < numKinds; ++i)
        v |= (uint64_t) o[(size_t) i] << (4 * i);
    return v;
}

Order unpackOrder (uint64_t v) noexcept
{
    Order o {};
    for (int i = 0; i < numKinds; ++i)
        o[(size_t) i] = (Kind) ((v >> (4 * i)) & 0xf);
    return o;
}
} // namespace

//==============================================================================
struct FxRack::Impl
{
    explicit Impl (FxRack& owner)
    {
        fx[(size_t) Kind::hyper] = std::make_unique<Hyper>();
        fx[(size_t) Kind::distortion] = std::make_unique<Distortion> (owner.distortionDrive);
        fx[(size_t) Kind::flanger] = std::make_unique<Flanger>();
        fx[(size_t) Kind::phaser] = std::make_unique<Phaser>();
        fx[(size_t) Kind::chorus] = std::make_unique<Chorus>();
        fx[(size_t) Kind::delay] = std::make_unique<Delay>();
        fx[(size_t) Kind::compressor] = std::make_unique<Compressor> (owner.compressorGainReductionDb);
        fx[(size_t) Kind::reverb] = std::make_unique<Reverb>();
        fx[(size_t) Kind::eq] = std::make_unique<Eq>();
        fx[(size_t) Kind::filter] = std::make_unique<FilterFx>();

        for (int k = 0; k < numKinds; ++k)
            onIndex[(size_t) k] = idx ((kindPrefix ((Kind) k) + "On").toRawUTF8());
    }

    std::array<std::unique_ptr<Effect>, numKinds> fx;
    std::array<int, numKinds> onIndex {};
    std::array<float, numKinds> fade {};
    std::array<bool, numKinds> running {};
    std::atomic<uint64_t> order { packOrder (defaultOrder()) };
    const ParamSpec* specs = paramSpecs().data();
    float fadeStep = 1.0f / 441.0f;
    float dry[2][blockLen] {};
};

FxRack::FxRack() : impl (std::make_unique<Impl> (*this))
{
    prepare (44100.0, 512);
}

FxRack::~FxRack() = default;

void FxRack::prepare (double sampleRate, int /*maxBlockSize: the rack works in 32-sample sub-blocks*/)
{
    const float sr = (float) juce::jlimit (8000.0, 768000.0, sampleRate);
    for (auto& e : impl->fx)
        e->prepare (sr);
    impl->fadeStep = 1.0f / (0.01f * sr);
    reset();
}

void FxRack::reset()
{
    for (int k = 0; k < numKinds; ++k)
    {
        impl->fx[(size_t) k]->reset();
        impl->running[(size_t) k] = false;
        impl->fade[(size_t) k] = 0.0f;
    }
    compressorGainReductionDb.store (0.0f);
    distortionDrive.store (0.0f);
}

void FxRack::setOrder (const Order& newOrder) noexcept
{
    std::array<bool, numKinds> seen {};
    for (auto k : newOrder)
    {
        const int i = (int) k;
        if (i < 0 || i >= numKinds || seen[(size_t) i])
            return;   // not a permutation: ignore
        seen[(size_t) i] = true;
    }
    impl->order.store (packOrder (newOrder), std::memory_order_relaxed);
}

void FxRack::process (float* left, float* right, int numSamples, const float* values, const TempoInfo& tempo) noexcept
{
    if (left == nullptr || right == nullptr || values == nullptr || numSamples <= 0)
        return;

    auto& s = *impl;
    const Order ord = unpackOrder (s.order.load (std::memory_order_relaxed));
    const Params p { values, s.specs };

    for (int pos = 0; pos < numSamples; pos += blockLen)
    {
        const int n = std::min (blockLen, numSamples - pos);
        float* l = left + pos;
        float* r = right + pos;

        for (auto kind : ord)
        {
            const auto k = (size_t) kind;
            const bool on = p.on (s.onIndex[k]);
            if (! on && ! s.running[k])
                continue;

            auto& e = *s.fx[k];
            if (on && ! s.running[k])
            {
                e.reset();
                s.running[k] = true;
                s.fade[k] = 0.0f;
            }

            if (on && s.fade[k] >= 1.0f)
            {
                e.process (l, r, n, p, tempo);
                continue;
            }

            // Fading in or out: blend against the dry input.
            std::copy (l, l + n, s.dry[0]);
            std::copy (r, r + n, s.dry[1]);
            e.process (l, r, n, p, tempo);

            float f = s.fade[k];
            const float step = on ? s.fadeStep : -s.fadeStep;
            for (int i = 0; i < n; ++i)
            {
                f = juce::jlimit (0.0f, 1.0f, f + step);
                l[i] = s.dry[0][i] + f * (l[i] - s.dry[0][i]);
                r[i] = s.dry[1][i] + f * (r[i] - s.dry[1][i]);
            }
            s.fade[k] = f;
            if (f <= 0.0f)
                s.running[k] = false;
        }

        // Safety net: should anything ever go non-finite, clear the rack rather than emit garbage.
        float energy = 0.0f;
        for (int i = 0; i < n; ++i)
            energy += l[i] * l[i] + r[i] * r[i];
        if (! std::isfinite (energy))
        {
            reset();
            std::fill (l, l + n, 0.0f);
            std::fill (r, r + n, 0.0f);
        }
    }

    if (! s.running[(size_t) Kind::compressor])
        compressorGainReductionDb.store (0.0f, std::memory_order_relaxed);
    if (! s.running[(size_t) Kind::distortion])
        distortionDrive.store (0.0f, std::memory_order_relaxed);
}
} // namespace ers::fx
