#pragma once

#include <aakit/AdventureKit.h>

/** Small DSP pieces that make up the Tape Dreams machine. Everything is allocation-free
    once prepared and safe on the audio thread. */
namespace tape
{
using aa::dsp::pi;
using aa::dsp::twoPi;

//==============================================================================
/** Transposed direct form II biquad with RBJ cookbook designs. */
struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void reset() { z1 = z2 = 0.0f; }

    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = aa::dsp::undenormalise (b1 * x - a1 * y + z2);
        z2 = aa::dsp::undenormalise (b2 * x - a2 * y);
        return y;
    }

    /** High shelf. A shelf with +g dB and one with -g dB at the same frequency are exact inverses. */
    void setHighShelf (float sampleRate, float hz, float gainDb, float q = 0.707f)
    {
        const float A = std::pow (10.0f, gainDb / 40.0f);
        const float w0 = twoPi * juce::jlimit (10.0f, sampleRate * 0.45f, hz) / sampleRate;
        const float cw = std::cos (w0), alpha = std::sin (w0) / (2.0f * q);
        const float sA = 2.0f * std::sqrt (A) * alpha;
        const float a0 = (A + 1.0f) - (A - 1.0f) * cw + sA;
        b0 = A * ((A + 1.0f) + (A - 1.0f) * cw + sA) / a0;
        b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw) / a0;
        b2 = A * ((A + 1.0f) + (A - 1.0f) * cw - sA) / a0;
        a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cw) / a0;
        a2 = ((A + 1.0f) - (A - 1.0f) * cw - sA) / a0;
    }

    void setPeak (float sampleRate, float hz, float q, float gainDb)
    {
        const float A = std::pow (10.0f, gainDb / 40.0f);
        const float w0 = twoPi * juce::jlimit (10.0f, sampleRate * 0.45f, hz) / sampleRate;
        const float cw = std::cos (w0), alpha = std::sin (w0) / (2.0f * q);
        const float a0 = 1.0f + alpha / A;
        b0 = (1.0f + alpha * A) / a0;
        b1 = -2.0f * cw / a0;
        b2 = (1.0f - alpha * A) / a0;
        a1 = -2.0f * cw / a0;
        a2 = (1.0f - alpha / A) / a0;
    }
};

//==============================================================================
/** Circular delay line with 16-tap Kaiser-windowed-sinc fractional reads: flat to ~18 kHz at
    any fractional position, so wow/flutter modulation doesn't dull or "shimmer" the top end
    the way 4-point polynomial interpolation does. The integer and fractional parts of the read
    position are kept separate so precision doesn't depend on the buffer size. */
class SincDelayLine
{
public:
    static constexpr int taps = 16, half = 8, phases = 1024;

    void prepare (int maxDelaySamples)
    {
        int size = 1;
        while (size < maxDelaySamples + taps + 8)
            size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writePos = 0;
        (void) table();
    }

    void clear() { std::fill (buffer.begin(), buffer.end(), 0.0f); }

    void push (float x)
    {
        buffer[(size_t) writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    /** Exact integer read (delay >= 1). Lines up with read() at integer delays. */
    float readInt (int delay) const { return buffer[(size_t) ((writePos - delay) & mask)]; }

    /** Read `delay` samples behind the most recent write. */
    float read (float delay) const
    {
        delay = juce::jlimit ((float) half + 1.0f, (float) (mask - taps - 4), delay);
        const float fl = std::floor (delay);
        const float df = delay - fl;
        // read position = (writePos - fl - 1) + (1 - df)
        int i = writePos - (int) fl - 1;
        int q = (int) ((1.0f - df) * (float) phases + 0.5f);
        if (q >= phases)
        {
            q -= phases;
            ++i;
        }
        const float* c = table().data() + (size_t) q * taps;
        const int start = i - (half - 1);
        float sum = 0.0f;
        for (int k = 0; k < taps; ++k)
            sum += buffer[(size_t) ((start + k) & mask)] * c[k];
        return sum;
    }

private:
    static const std::vector<float>& table()
    {
        static const std::vector<float> t = []
        {
            std::vector<float> v ((size_t) (phases + 1) * taps);
            const double beta = 5.0, fc = 0.97;
            auto bessel0 = [] (double x)
            {
                double sum = 1.0, term = 1.0;
                for (int k = 1; k < 30; ++k)
                {
                    term *= (x / (2.0 * k)) * (x / (2.0 * k));
                    sum += term;
                }
                return sum;
            };
            const double i0b = bessel0 (beta);
            for (int q = 0; q <= phases; ++q)
            {
                const double f = (double) q / phases;
                double norm = 0.0;
                for (int k = 0; k < taps; ++k)
                {
                    const double tt = f - (double) (k - (half - 1));
                    const double x = tt / (double) half;
                    const double w = std::abs (x) >= 1.0 ? 0.0 : bessel0 (beta * std::sqrt (1.0 - x * x)) / i0b;
                    const double arg = juce::MathConstants<double>::pi * fc * tt;
                    const double sinc = std::abs (arg) < 1.0e-9 ? 1.0 : std::sin (arg) / arg;
                    const double h = fc * sinc * w;
                    v[(size_t) q * taps + (size_t) k] = (float) h;
                    norm += h;
                }
                for (int k = 0; k < taps; ++k)
                    v[(size_t) q * taps + (size_t) k] = (float) (v[(size_t) q * taps + (size_t) k] / norm);
            }
            return v;
        }();
        return t;
    }

    std::vector<float> buffer;
    int mask = 0, writePos = 0;
};

//==============================================================================
/** Random wander: smoothstep-interpolated random points. The derivative is continuous, which
    matters because the derivative of a delay modulation is what we hear as pitch. */
struct SmoothRandom
{
    float phase = 1.0f, from = 0.0f, to = 0.0f;
    aa::dsp::Rng rng;

    explicit SmoothRandom (uint32_t seed = 1u) : rng (seed) {}

    void reset (uint32_t seed)
    {
        rng.seed (seed);
        from = 0.0f;
        to = rng.nextBipolar();
        phase = 0.0f;
    }

    float next (float increment)
    {
        phase += increment;
        if (phase >= 1.0f)
        {
            phase -= std::floor (phase);
            from = to;
            to = rng.nextBipolar();
        }
        const float t = phase * phase * (3.0f - 2.0f * phase);
        return from + (to - from) * t;
    }
};

//==============================================================================
/** Generates the transport speed irregularities as delay offsets (in samples).
    Wow: slow, wandering pitch drift. Flutter: faster, noisy capstan wobble.
    Most of the motion is shared (it's the same tape), with a little per-channel azimuth wander. */
class WowFlutter
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        wowPhase = 0.13f;
        fl1 = 0.0f;
        fl2 = 0.37f;
        wowRate.reset (11u);
        wowWander.reset (23u);
        wowAmp.reset (37u);
        sideL.reset (41u);
        sideR.reset (59u);
        flutterNoise.reset (71u);
        flutterAmp.reset (83u);
    }

    /** Depths are peak delay excursions in samples. */
    void process (float wowDepth, float flutterDepth, float& outL, float& outR)
    {
        const float inv = 1.0f / sr;

        // --- wow: a slightly irregular ~0.5 Hz swing plus a slower drift
        const float rateWander = wowRate.next (0.11f * inv);
        wowPhase += (0.62f + 0.2f * rateWander) * inv;
        if (wowPhase >= 1.0f)
            wowPhase -= 1.0f;
        const float amp = 0.8f + 0.2f * wowAmp.next (0.2f * inv);
        const float common = 0.62f * amp * std::sin (twoPi * wowPhase) + 0.3f * wowWander.next (0.45f * inv);
        const float wl = common + 0.025f * sideL.next (0.7f * inv);
        const float wr = common + 0.025f * sideR.next (0.63f * inv);

        // --- flutter: capstan + pinch roller rotation and some scrape noise
        fl1 += 6.7f * inv;
        if (fl1 >= 1.0f) fl1 -= 1.0f;
        fl2 += 11.3f * inv;
        if (fl2 >= 1.0f) fl2 -= 1.0f;
        const float fAmp = 0.75f + 0.25f * flutterAmp.next (1.3f * inv);
        const float fl = fAmp * (0.45f * fastSin (fl1) + 0.18f * fastSin (fl2)) + 0.32f * flutterNoise.next (19.0f * inv);

        outL = wowDepth * wl + flutterDepth * fl;
        outR = wowDepth * wr + flutterDepth * fl;
    }

    /** Normalised wow LFO position (for the UI), roughly -1..1. */
    float lastPhase() const { return wowPhase; }

private:
    static float fastSin (float phase)
    {
        // parabolic sine approximation, phase in [0, 1)
        const float x = phase * 2.0f - 1.0f;            // -1..1
        const float y = 4.0f * x * (1.0f - std::abs (x)); // ~ -sin(pi x)
        return -(y * (0.775f + 0.225f * std::abs (y)));
    }

    float sr = 44100.0f;
    float wowPhase = 0.0f, fl1 = 0.0f, fl2 = 0.0f;
    SmoothRandom wowRate { 11u }, wowWander { 23u }, wowAmp { 37u }, sideL { 41u }, sideR { 59u };
    SmoothRandom flutterNoise { 71u }, flutterAmp { 83u };
};

//==============================================================================
/** "Crinkle": occasional random warps, like a creased or stretched bit of tape passing the head.
    Produces a smooth delay bump (pitch bends one way then the other) with a little scrape on top. */
class Crinkle
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        active = false;
        rng.seed (0xC71A4u);
        cooldown = 0;
        shape = 0.0f;
    }

    /** amount 0..1, maxDepth in samples. Returns the delay offset in samples. */
    float process (float amount, float maxDepth)
    {
        if (! active)
        {
            shape = 0.0f;
            if (cooldown > 0)
                --cooldown;
            else if (amount > 0.001f)
            {
                const float rate = 0.08f + 1.1f * amount * amount; // events per second
                if (rng.next01() < rate / sr)
                {
                    active = true;
                    phase = 0.0f;
                    const float dur = 0.13f + 0.32f * rng.next01();
                    inc = 1.0f / (dur * sr);
                    depth = maxDepth * amount * (0.45f + 0.55f * rng.next01()) * (rng.chance (0.5f) ? 1.0f : -1.0f);
                    scrapeRate = (18.0f + 22.0f * rng.next01()) / sr;
                    scrapePhase = 0.0f;
                }
            }
            return 0.0f;
        }

        phase += inc;
        if (phase >= 1.0f)
        {
            active = false;
            cooldown = (int) (0.12f * sr);
            shape = 0.0f;
            return 0.0f;
        }

        shape = 0.5f - 0.5f * std::cos (twoPi * phase);
        scrapePhase += scrapeRate;
        if (scrapePhase >= 1.0f)
            scrapePhase -= 1.0f;
        return depth * shape * (1.0f + 0.035f * std::sin (twoPi * scrapePhase));
    }

    /** 0..1 envelope of the current warp (used to duck/darken a touch). */
    float envelope() const { return shape; }

private:
    float sr = 44100.0f, phase = 0.0f, inc = 0.0f, depth = 0.0f, shape = 0.0f;
    float scrapePhase = 0.0f, scrapeRate = 0.0f;
    bool active = false;
    int cooldown = 0;
    aa::dsp::Rng rng { 0xC71A4u };
};

//==============================================================================
/** "Wear": random dropouts (oxide shedding, the tape losing contact with the head) plus a gentle
    uneven-oxide level flicker. Returns a 0..1 "dip" amount. */
class Dropouts
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        env = target = 0.0f;
        hold = 0;
        rng.seed (0xD20Fu);
        flicker.reset (97u);
        attack = aa::dsp::onePoleCoeff (0.004f, sr);
        release = aa::dsp::onePoleCoeff (0.022f, sr);
    }

    float process (float wear)
    {
        if (hold > 0)
        {
            if (--hold == 0)
                target = 0.0f;
        }
        else if (wear > 0.001f)
        {
            const float rate = 0.12f + 2.6f * wear * std::sqrt (wear); // per second
            if (rng.next01() < rate / sr)
            {
                target = (0.25f + 0.6f * wear) * (0.45f + 0.55f * rng.next01());
                hold = (int) ((0.02f + 0.11f * rng.next01()) * sr);
            }
        }

        env += (target > env ? attack : release) * (target - env);
        const float f = 0.5f + 0.5f * flicker.next (7.0f / sr);
        return juce::jlimit (0.0f, 0.95f, env + wear * 0.09f * f);
    }

    float level() const { return env; }

private:
    float sr = 44100.0f, env = 0.0f, target = 0.0f, attack = 0.1f, release = 0.01f;
    int hold = 0;
    aa::dsp::Rng rng { 0xD20Fu };
    SmoothRandom flicker { 97u };
};

//==============================================================================
/** Program-dependent tape compression with a soft knee and automatic makeup. */
class Squash
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        env = 0.0f;
        attack = aa::dsp::onePoleCoeff (0.004f, sr);
        release = aa::dsp::onePoleCoeff (0.16f, sr);
        setAmount (0.0f);
    }

    void reset() { env = 0.0f; }

    void setAmount (float amount)
    {
        amt = amount;
        threshold = -8.0f - 22.0f * amount;
        ratio = 1.0f + 3.0f * amount;
        slope = 1.0f / ratio - 1.0f;
        makeup = -0.85f * gainReductionDb (-14.0f);
    }

    /** Returns the gain to apply for this detector sample. */
    float process (float detector)
    {
        env += (detector > env ? attack : release) * (detector - env);
        if (amt < 0.0005f)
            return 1.0f;
        const float levelDb = aa::dsp::gainToDb (env);
        return aa::dsp::dbToGain (gainReductionDb (levelDb) + makeup);
    }

private:
    float gainReductionDb (float levelDb) const
    {
        const float over = levelDb - threshold;
        if (2.0f * over <= -knee)
            return 0.0f;
        if (2.0f * over < knee)
        {
            const float t = over + knee * 0.5f;
            return slope * t * t / (2.0f * knee);
        }
        return slope * over;
    }

    float sr = 44100.0f, env = 0.0f, attack = 0.1f, release = 0.01f;
    float amt = 0.0f, threshold = 0.0f, ratio = 1.0f, slope = 0.0f, makeup = 0.0f;
    static constexpr float knee = 12.0f;
};

//==============================================================================
/** The tape saturator curve: asymmetric soft clip. Drive gain g and bias b grow with Drive;
    `compensation` keeps a typical (-12 dBFS) signal at the same loudness whatever the drive. */
struct Saturator
{
    static float driveGain (float drive) { return aa::dsp::dbToGain (24.0f * drive); }
    static float bias (float drive)      { return 0.22f * drive; }

    static float shape (float x, float g, float b, float offset)
    {
        return aa::dsp::softClip (g * x + b) - offset;
    }

    /** Builds a table of loudness compensation gains over the drive range. */
    static std::array<float, 65> makeCompensationTable()
    {
        std::array<float, 65> t {};
        constexpr int n = 512;
        const float amp = 0.3f;
        for (size_t k = 0; k < t.size(); ++k)
        {
            const float drive = (float) k / (float) (t.size() - 1);
            const float g = driveGain (drive), b = bias (drive), off = aa::dsp::softClip (b);
            double sum = 0.0, sumSq = 0.0, inSq = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const float x = amp * std::sin (twoPi * (float) i / (float) n);
                const float y = shape (x, g, b, off);
                sum += y;
                sumSq += (double) y * y;
                inSq += (double) x * x;
            }
            const double mean = sum / n;
            const double outRms = std::sqrt (juce::jmax (1.0e-12, sumSq / n - mean * mean));
            t[k] = (float) (std::sqrt (inSq / n) / outRms);
        }
        return t;
    }
};
} // namespace tape
