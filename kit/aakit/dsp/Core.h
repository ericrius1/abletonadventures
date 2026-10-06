#pragma once

// Header-only DSP building blocks shared by the plugins.
// Everything here is allocation-free once prepared and safe to use on the audio thread.

#include <juce_core/juce_core.h>
#include <cmath>
#include <cstdint>
#include <vector>
#include <array>
#include <atomic>
#include <algorithm>

namespace aa::dsp
{
constexpr float pi = 3.14159265358979323846f;
constexpr float twoPi = 2.0f * pi;

inline float dbToGain (float db) { return db <= -96.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g)  { return g <= 0.0000159f ? -96.0f : 20.0f * std::log10 (g); }
inline float midiToHz (float note) { return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f); }
inline float semitonesToRatio (float st) { return std::pow (2.0f, st / 12.0f); }
inline float clamp01 (float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
inline float lerp (float a, float b, float t) { return a + (b - a) * t; }

/** Cheap tanh-like soft clipper (Padé), accurate enough for saturation. */
inline float softClip (float x)
{
    x = juce::jlimit (-3.0f, 3.0f, x);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/** Flush denormals / tiny values to zero (for feedback paths). */
inline float undenormalise (float x) { return std::abs (x) < 1.0e-15f ? 0.0f : x; }

//==============================================================================
/** Fast, deterministic xorshift random generator (audio-thread friendly). */
struct Rng
{
    uint32_t state = 0x9E3779B9u;

    explicit Rng (uint32_t seed = 0x9E3779B9u) : state (seed ? seed : 1u) {}
    void seed (uint32_t s) { state = s ? s : 1u; }

    uint32_t nextU32()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    float next01() { return (float) (nextU32() >> 8) * (1.0f / 16777216.0f); }
    float nextBipolar() { return next01() * 2.0f - 1.0f; }
    int nextInt (int maxExclusive) { return maxExclusive <= 0 ? 0 : (int) (nextU32() % (uint32_t) maxExclusive); }
    bool chance (float probability) { return next01() < probability; }
};

//==============================================================================
/** Linear parameter smoother (per-sample ramps toward a target). */
struct Smoother
{
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int countdown = 0, rampLength = 64;

    void reset (double sampleRate, double rampSeconds, float value)
    {
        rampLength = std::max (1, (int) (sampleRate * rampSeconds));
        current = target = value;
        countdown = 0;
        step = 0.0f;
    }
    void setTarget (float t)
    {
        if (t == target)
            return;
        target = t;
        countdown = rampLength;
        step = (target - current) / (float) rampLength;
    }
    void snap (float t) { current = target = t; countdown = 0; }
    float next()
    {
        if (countdown > 0)
        {
            current += step;
            if (--countdown == 0)
                current = target;
        }
        return current;
    }
    bool isSmoothing() const { return countdown > 0; }
};

//==============================================================================
/** One-pole lowpass / highpass. */
struct OnePole
{
    float a = 0.0f, z = 0.0f;
    void setCutoff (float hz, float sampleRate)
    {
        hz = juce::jlimit (1.0f, sampleRate * 0.49f, hz);
        a = 1.0f - std::exp (-twoPi * hz / sampleRate);
    }
    void reset (float v = 0.0f) { z = v; }
    float lowpass (float x) { z += a * (x - z); return z; }
    float highpass (float x) { z += a * (x - z); return x - z; }
};

/** One-pole smoothing coefficient for a time constant in seconds. */
inline float onePoleCoeff (float seconds, float sampleRate)
{
    return seconds <= 0.0f ? 1.0f : 1.0f - std::exp (-1.0f / (seconds * sampleRate));
}

//==============================================================================
/** Topology-preserving-transform state variable filter (Zavalishin / Simper). Stable under fast
    modulation, gives LP/BP/HP/notch outputs simultaneously. */
struct Svf
{
    float g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;

    struct Out { float lp, bp, hp; };

    void reset() { ic1 = ic2 = 0.0f; }

    /** resonance 0..1 maps to Q ~0.5..25 */
    void setCutoffRes (float hz, float resonance, float sampleRate)
    {
        hz = juce::jlimit (10.0f, sampleRate * 0.47f, hz);
        g = std::tan (pi * hz / sampleRate);
        const float q = 0.5f + resonance * resonance * 24.5f;
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void setCutoffQ (float hz, float q, float sampleRate)
    {
        hz = juce::jlimit (10.0f, sampleRate * 0.47f, hz);
        g = std::tan (pi * hz / sampleRate);
        k = 1.0f / std::max (0.05f, q);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    Out process (float x)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = undenormalise (2.0f * v1 - ic1);
        ic2 = undenormalise (2.0f * v2 - ic2);
        return { v2, v1, x - k * v1 - v2 };
    }

    float lowpass (float x) { return process (x).lp; }
    float bandpass (float x) { return process (x).bp; }
    float highpass (float x) { return process (x).hp; }
};

//==============================================================================
/** ADSR with exponential-ish segments that feel natural (fast attack curve, RC decay/release). */
struct Adsr
{
    enum class Stage { idle, attack, decay, sustain, release };

    float sampleRate = 44100.0f;
    float attackRate = 0.0f, decayCoeff = 0.0f, releaseCoeff = 0.0f, sustain = 1.0f;
    float value = 0.0f;
    Stage stage = Stage::idle;

    void setSampleRate (float sr) { sampleRate = sr; }

    void set (float attackS, float decayS, float sustainLevel, float releaseS)
    {
        attackRate = 1.0f / std::max (1.0f, attackS * sampleRate);
        decayCoeff = onePoleCoeff (std::max (0.001f, decayS) * 0.33f, sampleRate);
        releaseCoeff = onePoleCoeff (std::max (0.001f, releaseS) * 0.33f, sampleRate);
        sustain = clamp01 (sustainLevel);
    }

    void noteOn() { stage = Stage::attack; }
    void noteOff() { if (stage != Stage::idle) stage = Stage::release; }
    void kill() { stage = Stage::idle; value = 0.0f; }
    bool isActive() const { return stage != Stage::idle; }
    bool isReleasing() const { return stage == Stage::release; }

    float process()
    {
        switch (stage)
        {
            case Stage::idle: return 0.0f;
            case Stage::attack:
                value += attackRate * (1.05f - value * 0.05f);
                if (value >= 1.0f) { value = 1.0f; stage = Stage::decay; }
                break;
            case Stage::decay:
                value += decayCoeff * (sustain - value);
                if (std::abs (value - sustain) < 0.0005f) { value = sustain; stage = Stage::sustain; }
                break;
            case Stage::sustain: value = sustain; break;
            case Stage::release:
                value += releaseCoeff * (0.0f - value);
                if (value < 0.0002f) { value = 0.0f; stage = Stage::idle; }
                break;
        }
        return value;
    }
};

//==============================================================================
/** Simple decaying envelope for percussive sounds: instant attack, exponential decay. */
struct DecayEnv
{
    float value = 0.0f, coeff = 0.999f;
    void trigger (float level = 1.0f) { value = level; }
    void setDecay (float seconds, float sampleRate)
    {
        // time to fall by 60 dB
        coeff = std::exp (-6.9078f / std::max (1.0f, seconds * sampleRate));
    }
    float process() { const float v = value; value *= coeff; if (value < 1.0e-5f) value = 0.0f; return v; }
    bool isActive() const { return value > 0.0f; }
};

//==============================================================================
/** Circular delay line with Hermite (4-point) interpolated reads. */
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        int size = 1;
        while (size < maxDelaySamples + 4)
            size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writePos = 0;
    }

    void clear() { std::fill (buffer.begin(), buffer.end(), 0.0f); }
    int getMaxDelay() const { return mask - 3; }

    void push (float x)
    {
        buffer[(size_t) writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    /** Read `delay` samples behind the most recent write (delay >= 1). */
    float read (float delay) const
    {
        delay = juce::jlimit (1.0f, (float) (mask - 3), delay);
        const float readPos = (float) writePos - delay;
        const int i = (int) std::floor (readPos);
        const float f = readPos - (float) i;

        const float xm1 = buffer[(size_t) ((i - 1) & mask)];
        const float x0 = buffer[(size_t) (i & mask)];
        const float x1 = buffer[(size_t) ((i + 1) & mask)];
        const float x2 = buffer[(size_t) ((i + 2) & mask)];

        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    /** Integer read, no interpolation. */
    float readInt (int delay) const { return buffer[(size_t) ((writePos - delay) & mask)]; }

private:
    std::vector<float> buffer;
    int mask = 0, writePos = 0;
};

//==============================================================================
/** Band-limited oscillator using PolyBLEP. Phase in [0,1). */
struct BlepOsc
{
    float phase = 0.0f;

    static float polyBlep (float t, float dt)
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

    float saw (float dt) const { return 2.0f * phase - 1.0f - polyBlep (phase, dt); }

    float square (float dt, float pw = 0.5f) const
    {
        float v = phase < pw ? 1.0f : -1.0f;
        v += polyBlep (phase, dt);
        float t2 = phase - pw;
        if (t2 < 0.0f) t2 += 1.0f;
        v -= polyBlep (t2, dt);
        return v;
    }

    float sine() const { return std::sin (twoPi * phase); }

    float triangle() const
    {
        // naive triangle is fine (harmonics fall off at 12 dB/oct)
        return 1.0f - 4.0f * std::abs (phase - 0.5f);
    }

    void advance (float dt)
    {
        phase += dt;
        if (phase >= 1.0f)
            phase -= 1.0f;
    }
};

/** Morphing wave: 0 = sine, 1 = triangle, 2 = saw, 3 = square (continuous in between). */
inline float morphWave (const BlepOsc& o, float dt, float shape)
{
    shape = juce::jlimit (0.0f, 3.0f, shape);
    const int i = std::min (2, (int) shape);
    const float f = shape - (float) i;
    auto wave = [&] (int idx)
    {
        switch (idx)
        {
            case 0: return o.sine();
            case 1: return o.triangle();
            case 2: return o.saw (dt);
            default: return o.square (dt);
        }
    };
    if (f < 0.0005f)
        return wave (i);
    return wave (i) * (1.0f - f) + wave (i + 1) * f;
}

//==============================================================================
/** LFO with a few classic shapes. */
struct Lfo
{
    enum Shape { sine = 0, triangle, saw, square, random, smoothRandom };
    float phase = 0.0f, held = 0.0f, prevHeld = 0.0f;
    Rng rng { 12345u };

    float process (float rateHz, float sampleRate, int shape)
    {
        phase += rateHz / sampleRate;
        if (phase >= 1.0f)
        {
            phase -= std::floor (phase);
            prevHeld = held;
            held = rng.nextBipolar();
        }
        switch (shape)
        {
            case sine: return std::sin (twoPi * phase);
            case triangle: return 1.0f - 4.0f * std::abs (phase - 0.5f);
            case saw: return 1.0f - 2.0f * phase;
            case square: return phase < 0.5f ? 1.0f : -1.0f;
            case random: return held;
            case smoothRandom:
            {
                const float t = phase * phase * (3.0f - 2.0f * phase);
                return prevHeld + (held - prevHeld) * t;
            }
            default: return 0.0f;
        }
    }
};

//==============================================================================
/** Lock-free single-producer/single-consumer queue for audio->UI events. */
template <typename T, int Capacity>
class SpscQueue
{
public:
    bool push (const T& item)
    {
        const int w = writeIndex.load (std::memory_order_relaxed);
        const int next = (w + 1) % Capacity;
        if (next == readIndex.load (std::memory_order_acquire))
            return false; // full: drop
        items[(size_t) w] = item;
        writeIndex.store (next, std::memory_order_release);
        return true;
    }

    bool pop (T& item)
    {
        const int r = readIndex.load (std::memory_order_relaxed);
        if (r == writeIndex.load (std::memory_order_acquire))
            return false;
        item = items[(size_t) r];
        readIndex.store ((r + 1) % Capacity, std::memory_order_release);
        return true;
    }

private:
    std::array<T, (size_t) Capacity> items {};
    std::atomic<int> writeIndex { 0 }, readIndex { 0 };
};

//==============================================================================
/** Peak follower for meters (UI reads `level`). */
struct PeakMeter
{
    std::atomic<float> level { 0.0f };
    float current = 0.0f, release = 0.9995f;

    void prepare (float sampleRate) { release = std::exp (-1.0f / (0.25f * sampleRate)); current = 0.0f; }
    void process (float x)
    {
        const float a = std::abs (x);
        current = a > current ? a : current * release;
    }
    void publish() { level.store (current, std::memory_order_relaxed); }
};
} // namespace aa::dsp
