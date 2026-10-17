// ericriusSynth - the multimode voice filter.
// Copyright (C) Adventure Audio. Licensed under the GNU AGPLv3.
//
// Everything here is textbook DSP: TPT / zero-delay-feedback state variable and ladder structures
// (Zavalishin, "The Art of VA Filter Design"), feedback combs, first-order allpass phasers and parallel
// band-pass formant banks.
//
// Coefficients are computed once per control block (setParams) and glide linearly to their new values
// over the next 32 samples (in 4-sample steps; drive and mix glide per sample). Switching type cross-fades
// from a frozen copy of the old filter, so nothing ever pops. Every feedback path is soft-limited, which
// keeps all types bounded at any setting.

#include "Filters.h"

namespace ers
{
juce::StringArray filterTypeNames()
{
    return { "Low 12", "Low 24", "Ladder", "Diode", "High 12", "High 24", "Band 12", "Band 24", "Notch", "Peak",
             "Comb +", "Comb -", "Flanger", "Phaser", "Formant", "Ring Mod", "Downsample", "Wasp" };
}

namespace
{
using aa::dsp::pi;
using aa::dsp::softClip;
using aa::dsp::undenormalise;

constexpr int rampLength = 32;     // one control block: coefficients glide over this many samples
constexpr int maxCoeffs = 10;
constexpr int chunkSize = 64;      // internal processing chunk (stack buffers)
constexpr int numPhaserStages = 6;
constexpr int numFormants = 3;
constexpr float minCutoff = 20.0f;

/** Cubic soft clipper saturating at +-level (an intentional, gritty curve). */
inline float limit (float x, float level, float invLevel) noexcept
{
    const float t = juce::jlimit (-1.5f, 1.5f, x * invLevel);
    return level * (t - (4.0f / 27.0f) * t * t * t);
}

/** Safety limiter for feedback paths: exactly linear below `knee` (so resonances are untouched at normal
    levels - even tiny per-sample losses add up inside a high-Q loop), then a smooth shoulder that
    saturates at knee + range. */
inline float guard (float x, float knee, float range, float invRange) noexcept
{
    const float a = std::abs (x);
    if (a <= knee)
        return x;
    const float u = std::min (1.5f, (a - knee) * invRange);
    return std::copysign (knee + range * (u - (4.0f / 27.0f) * u * u * u), x);
}

/** Parabolic approximation of sin (2 pi p) for p in [0, 1) (error < 0.1 %). */
inline float sin2pi (float p) noexcept
{
    const float x = (p < 0.5f ? p : p - 1.0f) * 2.0f;
    const float y = 4.0f * x * (1.0f - std::abs (x));
    return y * (0.775f + 0.225f * std::abs (y));
}

inline float prewarp (float hz, float sr) noexcept { return std::tan (pi * hz / sr); }

//==============================================================================
/** N coefficients that glide linearly to new targets over one control block, in steps of S samples
    (the filter coefficients use 8 steps of 4 samples: smooth, and a quarter of the recalculation). */
template <int N, int S>
struct Ramp
{
    static constexpr int numSteps = rampLength / S;
    float cur[N] {}, inc[N] {}, tgt[N] {};
    int remaining = 0;   // samples left in the glide

    void snap (const float* t) noexcept
    {
        for (int i = 0; i < N; ++i)
        {
            cur[i] = tgt[i] = t[i];
            inc[i] = 0.0f;
        }
        remaining = 0;
    }

    void glide (const float* t) noexcept
    {
        if (remaining == 0 && std::equal (t, t + N, tgt))
            return;   // nothing to do: keep the derived coefficients

        for (int i = 0; i < N; ++i)
        {
            tgt[i] = t[i];
            inc[i] = (t[i] - cur[i]) * (1.0f / (float) numSteps);
        }
        remaining = rampLength;
    }

    /** Advances one sample; returns true if the coefficients moved. */
    bool tick() noexcept
    {
        if (remaining <= 0)
            return false;
        if (--remaining % S != S - 1)
            return false;

        if (remaining < S)
            for (int i = 0; i < N; ++i)
                cur[i] = tgt[i];
        else
            for (int i = 0; i < N; ++i)
                cur[i] += inc[i];
        return true;
    }

    bool active (int index) const noexcept { return cur[index] != 0.0f || tgt[index] != 0.0f; }
};

//==============================================================================
/** Filter types sharing a state layout. Changing type inside a group keeps the state (and cross-fades);
    entering a new group starts it from silence. */
enum class Group { svf12, svf24, ladder, diode, delay, phaser, formant, ring, downsample, wasp };

Group groupOf (FilterType t) noexcept
{
    switch (t)
    {
        case FilterType::lp12: case FilterType::hp12: case FilterType::bp12:
        case FilterType::notch: case FilterType::peak:      return Group::svf12;
        case FilterType::lp24: case FilterType::hp24:
        case FilterType::bp24:                              return Group::svf24;
        case FilterType::ladder:                            return Group::ladder;
        case FilterType::diode:                             return Group::diode;
        case FilterType::combPlus: case FilterType::combMinus:
        case FilterType::flanger:                           return Group::delay;
        case FilterType::phaser:                            return Group::phaser;
        case FilterType::formant:                           return Group::formant;
        case FilterType::ring:                              return Group::ring;
        case FilterType::downsample:                        return Group::downsample;
        case FilterType::wasp: case FilterType::count:      break;
    }
    return Group::wasp;
}

//==============================================================================
// Shared building blocks

/** TPT SVF coefficients (Simper's formulation) derived from g = tan (pi fc / sr) and damping k = 1 / Q. */
struct SvfK
{
    float k = 1.0f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
};

inline SvfK svfK (float g, float k) noexcept
{
    const float a1 = 1.0f / (1.0f + g * (g + k));
    return { k, a1, g * a1, g * g * a1 };
}

/** One SVF step; returns the low-pass and writes the band-pass. The band integrator is soft-limited,
    which tames high resonance without touching normal levels. */
inline float svfStep (const SvfK& c, float x, float* s, float& bp) noexcept
{
    const float v3 = x - s[1];
    const float v1 = c.a1 * s[0] + c.a2 * v3;
    const float v2 = s[1] + c.a2 * s[0] + c.a3 * v3;
    s[0] = guard (2.0f * v1 - s[0], 1.5f, 1.0f, 1.0f);
    s[1] = 2.0f * v2 - s[1];
    bp = v1;
    return v2;
}

/** "Fat" (var on the classic types): a low-shelf lift (~+8 dB under 160 Hz at full) into a soft saturator. */
struct FatK
{
    float amount = 0.0f, drive = 1.0f, norm = 1.0f;
};

inline FatK fatK (float f) noexcept
{
    const float d = 1.0f + 2.0f * f;
    return { f, d, 1.0f / std::sqrt (d) };
}

inline float fatten (float x, float& z, const FatK& f, float aLow) noexcept
{
    z += aLow * (x - z);
    const float y = x + 1.5f * f.amount * z;
    return y + f.amount * (limit (y * f.drive, 1.5f, 1.0f / 1.5f) * f.norm - y);
}

//==============================================================================
/** Formant tables: the classic average adult vowel formants (Peterson & Barney style), A E I O U. */
constexpr float vowelHz[5][numFormants] = {
    { 730.0f, 1090.0f, 2440.0f },   // A
    { 530.0f, 1840.0f, 2480.0f },   // E
    { 270.0f, 2290.0f, 3010.0f },   // I
    { 570.0f, 840.0f, 2410.0f },    // O
    { 300.0f, 870.0f, 2240.0f },    // U
};
constexpr float vowelGain[5][numFormants] = {
    { 1.0f, 0.50f, 0.28f },
    { 1.0f, 0.40f, 0.32f },
    { 1.0f, 0.25f, 0.22f },
    { 1.0f, 0.55f, 0.12f },
    { 1.0f, 0.30f, 0.10f },
};
constexpr float formantBw[numFormants] = { 80.0f, 100.0f, 130.0f };

//==============================================================================
/** Computes the coefficient targets of a type (layouts documented next to each run function). */
void computeTargets (FilterType type, float fc, float reso, float var, float sr, float maxDelay, float* t) noexcept
{
    std::fill (t, t + maxCoeffs, 0.0f);
    const float maxHz = 0.45f * sr;
    auto clampHz = [maxHz] (float hz) { return juce::jlimit (minCutoff, maxHz, hz); };
    const float g = prewarp (fc, sr);

    switch (type)
    {
        case FilterType::lp12:
        case FilterType::hp12:
        case FilterType::bp12:
            t[0] = g;
            t[1] = 2.0f * std::pow (40.0f, -reso);        // Q 0.5 .. 20
            t[2] = 1.0f + 1.5f * reso;                    // band-pass peak gain
            t[3] = var;
            break;

        case FilterType::notch:
            t[0] = g;
            t[1] = 2.5f * std::pow (25.0f, -reso);        // Q 0.4 .. 10: resonance narrows the notch
            t[3] = var;
            break;

        case FilterType::peak:
            t[0] = g;
            t[1] = 1.0f / 1.4f;                           // fixed Q
            t[2] = std::pow (10.0f, reso * 18.0f / 20.0f) - 1.0f;   // 0 .. +18 dB boost
            t[3] = var;
            break;

        case FilterType::lp24:
        case FilterType::hp24:
            t[0] = g;
            t[1] = 1.0f / 0.54f;                          // first stage: no peaking
            t[2] = 1.25f * std::pow (25.0f, -reso);       // second stage: Q 0.8 .. 20
            t[3] = var;
            break;

        case FilterType::bp24:
            t[0] = g;
            t[1] = 1.0f / 0.7f;
            t[2] = (1.0f / 0.7f) * std::pow (28.0f, -reso);
            t[3] = var;
            t[4] = 1.0f + 1.5f * reso;
            break;

        case FilterType::ladder:
            t[0] = g;
            t[1] = 4.4f * reso;                           // feedback; self-oscillates from ~0.91
            t[2] = var;
            break;

        case FilterType::diode:
        {
            // Four mismatched poles, spread the way a diode ladder's are: the resonance (180 degrees of
            // loop phase) lands on fc and is softer and thinner than the ladder's.
            constexpr float spread[4] = { 0.42f, 0.84f, 1.35f, 2.0f };
            for (int i = 0; i < 4; ++i)
            {
                const float gi = prewarp (clampHz (fc * spread[i]), sr);
                t[i] = gi / (1.0f + gi);
            }
            t[4] = 6.0f * reso;                           // loop gain at fc is ~0.18: oscillates from ~0.93
            t[5] = var;
            break;
        }

        case FilterType::combPlus:
        case FilterType::combMinus:
        case FilterType::flanger:
        {
            const float dampHz = 18000.0f * std::pow (300.0f / 18000.0f, var);
            const float a = var < 0.001f ? 1.0f : 1.0f - std::exp (-2.0f * pi * std::min (dampHz, maxHz) / sr);
            const float fb = type == FilterType::combPlus ? 0.97f * reso
                           : (type == FilterType::combMinus ? -0.97f * reso : 0.95f * reso);
            const float period = sr / fc;

            // The damping low-pass delays the loop and the 5 Hz DC blocker advances it: correct the line
            // length by both at the lowest peak so keytracked combs stay in tune.
            const float w0 = 2.0f * pi / period * (type == FilterType::combMinus ? 0.5f : 1.0f);
            const float p = 1.0f - a;
            const float lpDelay = std::atan2 (p * std::sin (w0), 1.0f - p * std::cos (w0)) / w0;
            const float hpLead = std::atan (2.0f * pi * 5.0f / sr / w0) / w0;

            t[0] = juce::jlimit (2.0f, maxDelay, period - lpDelay + hpLead);
            t[1] = fb;
            t[2] = a;
            if (type == FilterType::flanger)
            {
                t[4] = t[5] = 0.5f * (1.0f - 0.45f * fb);  // delayed + dry: static flange notches
            }
            else
            {
                t[3] = std::min (1.0f, 1.3f * std::sqrt (1.0f - fb * fb));   // keeps the level roughly even
            }
            break;
        }

        case FilterType::phaser:
        {
            const float spread = 1.0f + 7.0f * var;       // outer stages up to 3 octaves either side
            for (int i = 0; i < numPhaserStages; ++i)
            {
                const float pos = ((float) i - 0.5f * (float) (numPhaserStages - 1)) / (0.5f * (float) (numPhaserStages - 1));
                const float gi = prewarp (clampHz (fc * std::pow (spread, pos)), sr);
                t[i] = gi / (1.0f + gi);
            }
            t[6] = 0.92f * reso;
            t[7] = 1.3f * (1.0f - 0.45f * t[6]);
            break;
        }

        case FilterType::formant:
        {
            const float pos = var * 4.0f;
            const int v0 = std::min (3, (int) pos);
            const float fr = pos - (float) v0;
            const float shift = std::sqrt (fc / 1000.0f);
            const float bwScale = 2.2f - 1.8f * reso;
            const float makeup = 3.8f / std::sqrt (bwScale);   // narrow bands pass little energy: make it up
            for (int j = 0; j < numFormants; ++j)
            {
                const float f = juce::jlimit (40.0f, maxHz, aa::dsp::lerp (vowelHz[v0][j], vowelHz[v0 + 1][j], fr) * shift);
                const float bw = formantBw[j] * bwScale * (0.6f + 0.4f * shift);
                t[j] = prewarp (f, sr);
                t[numFormants + j] = 1.0f / juce::jlimit (0.7f, 40.0f, f / bw);
                t[2 * numFormants + j] = aa::dsp::lerp (vowelGain[v0][j], vowelGain[v0 + 1][j], fr) * makeup;
            }
            break;
        }

        case FilterType::ring:
            t[0] = fc / sr;
            t[1] = var;
            break;

        case FilterType::downsample:
            t[0] = std::min (1.0f, 2.0f * fc / sr);
            t[1] = std::exp2 (15.0f - 13.5f * var);       // half the quantiser levels: 16 bits .. ~2.5 bits
            t[2] = g;
            t[3] = 1.0f / (0.6f + 4.0f * reso);
            t[4] = std::min (1.0f, 4.0f * reso);
            break;

        case FilterType::wasp:
            t[0] = g;
            t[1] = 1.0f / (0.6f * std::pow (20.0f, reso));   // Q 0.6 .. 12
            t[2] = std::max (0.0f, 1.0f - 2.0f * var);
            t[3] = 1.0f - std::abs (2.0f * var - 1.0f);
            t[4] = std::max (0.0f, 2.0f * var - 1.0f);
            break;

        case FilterType::count:
            break;
    }
}

//==============================================================================
/** One complete filter: the active type's coefficients plus the state of every type. Trivially copyable,
    so a type change can freeze a copy and cross-fade from it. The comb delay lines live outside. */
struct Core
{
    FilterType type = FilterType::lp12;
    Ramp<maxCoeffs, 4> ramp;

    float fatZ[2] {};
    float svf[2][2][2] {};             // [stage][channel][band, low]
    float lad[2][4] {};
    float dio[2][4] {};
    float dioDc[2] {};
    float damp[2] {}, dc[2] {};
    float ph[2][numPhaserStages] {}, phLast[2] {};
    float fm[numFormants][2][2] {};
    float ringPhase = 0.0f;
    float dsPhase = 1.0f, dsHeld[2] {}, dsLp[2][2] {};
    float wasp[2][2] {}, waspDc[2] {};

    /** Flushes tiny values out of every feedback state. Run once per chunk (64 samples): far too short
        for a state to decay from 1e-15 into the denormal range, and it keeps the per-sample loops lean. */
    void flushDenormals() noexcept
    {
        auto flush = [] (float* p, size_t count)
        {
            for (size_t i = 0; i < count; ++i)
                p[i] = undenormalise (p[i]);
        };
        flush (fatZ, 2);
        flush (&svf[0][0][0], 8);
        flush (&lad[0][0], 8);
        flush (&dio[0][0], 8);
        flush (dioDc, 2);
        flush (damp, 2);
        flush (dc, 2);
        flush (&ph[0][0], 2 * numPhaserStages);
        flush (phLast, 2);
        flush (&fm[0][0][0], (size_t) numFormants * 4);
        flush (&dsLp[0][0], 4);
        flush (&wasp[0][0], 4);
        flush (waspDc, 2);
    }

    void resetState() noexcept
    {
        const auto t = type;
        const auto r = ramp;
        *this = Core();
        type = t;
        ramp = r;
    }
};

struct Ctx
{
    aa::dsp::DelayLine* delay = nullptr;   // two lines (left, right)
    float aFat = 0.02f, aDc = 0.0007f;
};

using In = const float* const*;
using Out = float* const*;

//==============================================================================
// Coefficients: 0 g, 1 k, 2 gain (band / peak), 3 fat
template <FilterType type>
void runSvf12 (Core& c, const Ctx& ctx, In in, Out out, int n) noexcept
{
    auto& r = c.ramp;
    SvfK k;
    FatK fat;
    float gain = 0.0f;
    auto derive = [&]
    {
        k = svfK (r.cur[0], r.cur[1]);
        fat = fatK (r.cur[3]);
        gain = r.cur[2];
    };
    derive();
    const bool useFat = r.active (3);

    for (int i = 0; i < n; ++i)
    {
        if (r.tick())
            derive();

        for (int ch = 0; ch < 2; ++ch)
        {
            float x = in[ch][i];
            if (useFat)
                x = fatten (x, c.fatZ[ch], fat, ctx.aFat);

            float bp;
            const float lp = svfStep (k, x, c.svf[0][ch], bp);

            if constexpr (type == FilterType::lp12)         out[ch][i] = lp;
            else if constexpr (type == FilterType::hp12)    out[ch][i] = x - k.k * bp - lp;
            else if constexpr (type == FilterType::bp12)    out[ch][i] = k.k * bp * gain;
            else if constexpr (type == FilterType::notch)   out[ch][i] = x - k.k * bp;
            else                                            out[ch][i] = guard (x + gain * k.k * bp, 2.5f, 1.5f, 1.0f / 1.5f);
        }
    }
}

// Coefficients: 0 g, 1 k first stage, 2 k second stage, 3 fat, 4 band gain
template <FilterType type>
void runSvf24 (Core& c, const Ctx& ctx, In in, Out out, int n) noexcept
{
    auto& r = c.ramp;
    SvfK k1, k2;
    FatK fat;
    float gain = 0.0f;
    auto derive = [&]
    {
        k1 = svfK (r.cur[0], r.cur[1]);
        k2 = svfK (r.cur[0], r.cur[2]);
        fat = fatK (r.cur[3]);
        gain = r.cur[4];
    };
    derive();
    const bool useFat = r.active (3);

    for (int i = 0; i < n; ++i)
    {
        if (r.tick())
            derive();

        for (int ch = 0; ch < 2; ++ch)
        {
            float x = in[ch][i];
            if (useFat)
                x = fatten (x, c.fatZ[ch], fat, ctx.aFat);

            float bp1, bp2;
            const float lp1 = svfStep (k1, x, c.svf[0][ch], bp1);

            if constexpr (type == FilterType::lp24)
            {
                out[ch][i] = svfStep (k2, lp1, c.svf[1][ch], bp2);
            }
            else if constexpr (type == FilterType::hp24)
            {
                const float h = x - k1.k * bp1 - lp1;
                const float lp2 = svfStep (k2, h, c.svf[1][ch], bp2);
                out[ch][i] = h - k2.k * bp2 - lp2;
            }
            else
            {
                svfStep (k2, k1.k * bp1, c.svf[1][ch], bp2);
                out[ch][i] = k2.k * bp2 * gain;
            }
        }
    }
}

// Coefficients: 0 g, 1 feedback k (0 .. 4.4), 2 fat
void runLadder (Core& c, const Ctx& ctx, In in, Out out, int n) noexcept
{
    auto& r = c.ramp;
    float G = 0, G0 = 0, beta = 1, w[4] {}, inGain = 1;
    FatK fat;
    auto derive = [&]
    {
        const float g = r.cur[0];
        G = g / (1.0f + g);
        beta = 1.0f - G;
        // Zero-delay feedback: y4 = G^4 u + sum (G^(3-j) beta) s[j]  (each TPT one-pole: y = G x + (1 - G) s),
        // u = (x (1 + 0.6 k) - k y4) / (1 + k G^4). The 0.6 k term is bass compensation. Everything constant
        // is folded into the weights; the 0.5 / 2 of the saturator too (G0 = 2 G).
        const float kk = r.cur[1];
        const float scale = 0.5f / (1.0f + kk * G * G * G * G);
        w[3] = kk * scale * beta;
        w[2] = w[3] * G;
        w[1] = w[2] * G;
        w[0] = w[1] * G;
        inGain = (1.0f + 0.6f * kk) * scale;
        G0 = 2.0f * G;
        fat = fatK (r.cur[2]);
    };
    derive();
    const bool useFat = r.active (2);

    for (int i = 0; i < n; ++i)
    {
        if (r.tick())
            derive();

        for (int ch = 0; ch < 2; ++ch)
        {
            float x = in[ch][i];
            if (useFat)
                x = fatten (x, c.fatZ[ch], fat, ctx.aFat);

            float* s = c.lad[ch];
            // Zero-delay feedback: solve the loop linearly, then saturate the stage input (keeps
            // self-oscillation bounded and gives the classic squash).
            const float S = (w[0] * s[0] + w[1] * s[1]) + (w[2] * s[2] + w[3] * s[3]);
            const float u = softClip (x * inGain - S);   // stage input / 2, saturated

            const float y0 = G0 * u + beta * s[0];
            const float y1 = G * y0 + beta * s[1];
            const float y2 = G * y1 + beta * s[2];
            const float y3 = G * y2 + beta * s[3];
            s[0] = 2.0f * y0 - s[0];
            s[1] = 2.0f * y1 - s[1];
            s[2] = 2.0f * y2 - s[2];
            s[3] = 2.0f * y3 - s[3];
            out[ch][i] = y3;
        }
    }
}

// Coefficients: 0..3 G per stage, 4 feedback k (0 .. 6), 5 fat
void runDiode (Core& c, const Ctx& ctx, In in, Out out, int n) noexcept
{
    auto& r = c.ramp;
    float G[4] {}, B[4] {}, w[4] {}, inGain = 1;
    FatK fat;
    auto derive = [&]
    {
        for (int j = 0; j < 4; ++j)
        {
            G[j] = r.cur[j];
            B[j] = 1.0f - G[j];
        }
        // y4 = G0 G1 G2 G3 u + sum w[j] s[j]   (TPT one-poles: y = G x + (1 - G) s);
        // u = (x (1 + 0.35 k) - k y4) / (1 + k G0 G1 G2 G3), constants folded into the weights.
        const float kk = r.cur[4];
        const float invDen = 1.0f / (1.0f + kk * G[0] * G[1] * G[2] * G[3]);
        w[3] = kk * invDen * (1.0f - G[3]);
        w[2] = kk * invDen * G[3] * (1.0f - G[2]);
        w[1] = kk * invDen * G[3] * G[2] * (1.0f - G[1]);
        w[0] = kk * invDen * G[3] * G[2] * G[1] * (1.0f - G[0]);
        inGain = (1.0f + 0.35f * kk) * invDen;     // only partial bass compensation: acid stays lean
        fat = fatK (r.cur[5]);
    };
    derive();
    const bool useFat = r.active (5);

    for (int i = 0; i < n; ++i)
    {
        if (r.tick())
            derive();

        for (int ch = 0; ch < 2; ++ch)
        {
            float x = in[ch][i];
            if (useFat)
                x = fatten (x, c.fatZ[ch], fat, ctx.aFat);

            float* s = c.dio[ch];
            const float S = (w[0] * s[0] + w[1] * s[1]) + (w[2] * s[2] + w[3] * s[3]);
            float u = x * inGain - S;
            // Lopsided "diode" saturation (branch-free): even harmonics and a squelchier resonance.
            const float level = 1.0f + std::copysign (0.2f, u);                 // 1.2 / 0.8
            const float invLevel = 1.0416667f - std::copysign (0.2083333f, u);  // 1 / level
            u = level * softClip (u * invLevel);

            const float y0 = G[0] * u + B[0] * s[0];
            const float y1 = G[1] * y0 + B[1] * s[1];
            const float y2 = G[2] * y1 + B[2] * s[2];
            const float y = G[3] * y2 + B[3] * s[3];
            s[0] = 2.0f * y0 - s[0];
            s[1] = 2.0f * y1 - s[1];
            s[2] = 2.0f * y2 - s[2];
            s[3] = 2.0f * y - s[3];

            c.dioDc[ch] += ctx.aDc * (y - c.dioDc[ch]);
            out[ch][i] = y - c.dioDc[ch];
        }
    }
}

// Coefficients: 0 delay (samples), 1 feedback, 2 damping coefficient, 3 loop gain, 4 delayed gain, 5 dry gain
void runComb (Core& c, const Ctx& ctx, In in, Out out, int n) noexcept
{
    auto& r = c.ramp;
    for (int i = 0; i < n; ++i)
    {
        r.tick();
        const float d = r.cur[0], fb = r.cur[1], a = r.cur[2], wa = r.cur[3], wb = r.cur[4], wc = r.cur[5];

        for (int ch = 0; ch < 2; ++ch)
        {
            const float x = in[ch][i];
            auto& line = ctx.delay[ch];
            const float w = line.read (d);
            c.damp[ch] += a * (w - c.damp[ch]);
            c.dc[ch] += ctx.aDc * (c.damp[ch] - c.dc[ch]);
            const float v = c.damp[ch] - c.dc[ch];
            const float s = x + fb * v;
            line.push (undenormalise (guard (s, 1.5f, 1.0f, 1.0f)));
            out[ch][i] = wa * s + wb * v + wc * x;
        }
    }
}

// Coefficients: 0..5 allpass G per stage, 6 feedback, 7 output gain
void runPhaser (Core& c, const Ctx&, In in, Out out, int n) noexcept
{
    auto& r = c.ramp;
    // TPT allpass (2 lp - x) rearranged so the signal path is one multiply-add per stage:
    //   out = (2G - 1) x + 2 (1 - G) s,   s' = 2G x + 2 (1 - G) s - s
    float c1[numPhaserStages], c2[numPhaserStages], c3[numPhaserStages];
    auto derive = [&]
    {
        for (int j = 0; j < numPhaserStages; ++j)
        {
            c1[j] = 2.0f * r.cur[j] - 1.0f;
            c2[j] = 2.0f * (1.0f - r.cur[j]);
            c3[j] = 2.0f * r.cur[j];
        }
    };
    derive();

    for (int i = 0; i < n; ++i)
    {
        if (r.tick())
            derive();

        const float fb = r.cur[6], norm = r.cur[7];
        for (int ch = 0; ch < 2; ++ch)
        {
            const float x = in[ch][i];
            float a = x + fb * c.phLast[ch];
            float* s = c.ph[ch];
            for (int j = 0; j < numPhaserStages; ++j)
            {
                const float bs = c2[j] * s[j];
                s[j] = c3[j] * a + bs - s[j];
                a = c1[j] * a + bs;
            }
            c.phLast[ch] = guard (a, 1.5f, 0.5f, 2.0f);
            out[ch][i] = 0.5f * (x + a) * norm;
        }
    }
}

// Coefficients: 0..2 g, 3..5 k, 6..8 gain per formant
void runFormant (Core& c, const Ctx&, In in, Out out, int n) noexcept
{
    auto& r = c.ramp;
    SvfK k[numFormants];
    float gk[numFormants];
    auto derive = [&]
    {
        for (int j = 0; j < numFormants; ++j)
        {
            k[j] = svfK (r.cur[j], r.cur[numFormants + j]);
            gk[j] = r.cur[2 * numFormants + j] * r.cur[numFormants + j];
        }
    };
    derive();

    for (int i = 0; i < n; ++i)
    {
        if (r.tick())
            derive();

        for (int ch = 0; ch < 2; ++ch)
        {
            const float x = in[ch][i];
            float y = 0.0f;
            for (int j = 0; j < numFormants; ++j)
            {
                float bp;
                svfStep (k[j], x, c.fm[j][ch], bp);
                y += gk[j] * bp;
            }
            out[ch][i] = y;
        }
    }
}

// Coefficients: 0 phase increment, 1 shape (sine -> square)
void runRing (Core& c, const Ctx&, In in, Out out, int n) noexcept
{
    auto& r = c.ramp;
    for (int i = 0; i < n; ++i)
    {
        r.tick();
        c.ringPhase += r.cur[0];
        if (c.ringPhase >= 1.0f)
            c.ringPhase -= 1.0f;

        const float s = sin2pi (c.ringPhase);
        const float m = s + r.cur[1] * (softClip (4.0f * s) - s);
        out[0][i] = in[0][i] * m;
        out[1][i] = in[1][i] * m;
    }
}

// Coefficients: 0 hold increment, 1 quantiser half-levels, 2 g, 3 k, 4 low-pass mix
void runDownsample (Core& c, const Ctx&, In in, Out out, int n) noexcept
{
    auto& r = c.ramp;
    SvfK k;
    float invQ = 1.0f;
    auto derive = [&]
    {
        k = svfK (r.cur[2], r.cur[3]);
        invQ = 1.0f / std::max (1.0f, r.cur[1]);
    };
    derive();
    const bool useLp = r.active (4);

    for (int i = 0; i < n; ++i)
    {
        if (r.tick())
            derive();

        c.dsPhase += r.cur[0];
        if (c.dsPhase >= 1.0f)
        {
            c.dsPhase -= 1.0f;
            const float q = r.cur[1];
            c.dsHeld[0] = std::floor (in[0][i] * q + 0.5f) * invQ;
            c.dsHeld[1] = std::floor (in[1][i] * q + 0.5f) * invQ;
        }

        for (int ch = 0; ch < 2; ++ch)
        {
            float y = c.dsHeld[ch];
            if (useLp)
            {
                float bp;
                const float lp = svfStep (k, y, c.dsLp[ch], bp);
                y += r.cur[4] * (lp - y);
            }
            out[ch][i] = y;
        }
    }
}

// Coefficients: 0 g, 1 k, 2 low, 3 band, 4 high mix
void runWasp (Core& c, const Ctx& ctx, In in, Out out, int n) noexcept
{
    auto& r = c.ramp;
    SvfK k;
    auto derive = [&] { k = svfK (r.cur[0], r.cur[1]); };
    derive();

    for (int i = 0; i < n; ++i)
    {
        if (r.tick())
            derive();

        const float cl = r.cur[2], cb = 1.5f * r.cur[3] * k.k, chp = r.cur[4];
        for (int ch = 0; ch < 2; ++ch)
        {
            const float x = in[ch][i];
            float* s = c.wasp[ch];
            const float v3 = x - s[1];
            const float v1 = k.a1 * s[0] + k.a2 * v3;
            const float v2 = s[1] + k.a2 * s[0] + k.a3 * v3;

            // CMOS-style integrators: the band state clips early and lopsided, so the resonance snarls.
            const float b = 2.0f * v1 - s[0];
            s[0] = b > 0.0f ? limit (b, 1.0f, 1.0f) : limit (b, 1.4f, 1.0f / 1.4f);
            s[1] = limit (2.0f * v2 - s[1], 2.5f, 0.4f);

            const float hp = x - k.k * v1 - v2;
            const float y = limit (cl * v2 + cb * v1 + chp * hp, 1.6f, 1.0f / 1.6f);
            c.waspDc[ch] += ctx.aDc * (y - c.waspDc[ch]);
            out[ch][i] = y - c.waspDc[ch];
        }
    }
}

void runCore (Core& c, const Ctx& ctx, In in, Out out, int n) noexcept
{
    switch (c.type)
    {
        case FilterType::lp12:       runSvf12<FilterType::lp12> (c, ctx, in, out, n); break;
        case FilterType::hp12:       runSvf12<FilterType::hp12> (c, ctx, in, out, n); break;
        case FilterType::bp12:       runSvf12<FilterType::bp12> (c, ctx, in, out, n); break;
        case FilterType::notch:      runSvf12<FilterType::notch> (c, ctx, in, out, n); break;
        case FilterType::peak:       runSvf12<FilterType::peak> (c, ctx, in, out, n); break;
        case FilterType::lp24:       runSvf24<FilterType::lp24> (c, ctx, in, out, n); break;
        case FilterType::hp24:       runSvf24<FilterType::hp24> (c, ctx, in, out, n); break;
        case FilterType::bp24:       runSvf24<FilterType::bp24> (c, ctx, in, out, n); break;
        case FilterType::ladder:     runLadder (c, ctx, in, out, n); break;
        case FilterType::diode:      runDiode (c, ctx, in, out, n); break;
        case FilterType::combPlus:
        case FilterType::combMinus:
        case FilterType::flanger:    runComb (c, ctx, in, out, n); break;
        case FilterType::phaser:     runPhaser (c, ctx, in, out, n); break;
        case FilterType::formant:    runFormant (c, ctx, in, out, n); break;
        case FilterType::ring:       runRing (c, ctx, in, out, n); break;
        case FilterType::downsample: runDownsample (c, ctx, in, out, n); break;
        case FilterType::wasp:       runWasp (c, ctx, in, out, n); break;
        case FilterType::count:      break;
    }
    c.flushDenormals();
}
} // namespace

//==============================================================================
struct VoiceFilter::Impl
{
    float sr = 44100.0f;
    aa::dsp::DelayLine delay[2];
    float maxDelay = 2.0f;
    Ctx ctx;

    Core active, fading;
    int fadeRemaining = 0, fadeLength = 128;
    float fadeInv = 1.0f / 128.0f;

    Ramp<4, 1> global;   // per sample: 0 drive pre-gain, 1 drive make-up, 2 drive blend, 3 mix
    bool needsSnap = true, delayDirty = false;

    void prepare (float sampleRate)
    {
        sr = std::max (8000.0f, sampleRate);
        const int maxSamples = (int) std::ceil (sr / minCutoff) + 8;
        for (auto& d : delay)
            d.prepare (maxSamples);
        maxDelay = (float) std::min (maxSamples, delay[0].getMaxDelay() - 2);

        ctx.delay = delay;
        ctx.aFat = 1.0f - std::exp (-2.0f * pi * 160.0f / sr);
        ctx.aDc = 1.0f - std::exp (-2.0f * pi * 5.0f / sr);
        fadeLength = juce::jlimit (32, 1024, (int) (0.004f * sr));
        fadeInv = 1.0f / (float) fadeLength;
        reset();
    }

    void clearDelay()
    {
        if (delayDirty)
            for (auto& d : delay)
                d.clear();
        delayDirty = false;
    }

    void reset()
    {
        active.resetState();
        fading.resetState();
        fadeRemaining = 0;
        clearDelay();
        needsSnap = true;
    }

    static void driveTargets (float drive, float mix, float* t)
    {
        const float pre = std::pow (10.0f, drive * 24.0f / 20.0f);   // up to +24 dB into the saturator
        t[0] = pre;
        t[1] = 0.5f / softClip (0.5f * pre);                       // keeps a half-scale signal at half scale
        t[2] = std::min (1.0f, drive * 10.0f);                     // fades the saturator in over the first 10 %
        t[3] = mix;
    }
};

//==============================================================================
VoiceFilter::VoiceFilter() : impl (std::make_unique<Impl>())
{
    impl->prepare (44100.0f);
}

VoiceFilter::~VoiceFilter() = default;
VoiceFilter::VoiceFilter (VoiceFilter&&) noexcept = default;
VoiceFilter& VoiceFilter::operator= (VoiceFilter&&) noexcept = default;

void VoiceFilter::prepare (float sampleRate)
{
    if (impl != nullptr)
        impl->prepare (sampleRate);
}

void VoiceFilter::reset()
{
    if (impl != nullptr)
        impl->reset();
}

void VoiceFilter::setParams (FilterType type, float cutoffHz, float reso, float drive, float var, float mix)
{
    if (impl == nullptr)
        return;

    auto& s = *impl;
    if ((int) type < 0 || type >= FilterType::count)
        type = FilterType::lp12;

    auto unit = [] (float v, float fallback) { return std::isfinite (v) ? aa::dsp::clamp01 (v) : fallback; };
    reso = unit (reso, 0.0f);
    drive = unit (drive, 0.0f);
    var = unit (var, 0.0f);
    mix = unit (mix, 1.0f);
    const float fc = std::isfinite (cutoffHz) ? juce::jlimit (minCutoff, 0.45f * s.sr, cutoffHz) : 1000.0f;

    float t[maxCoeffs], gt[4];
    computeTargets (type, fc, reso, var, s.sr, s.maxDelay, t);
    Impl::driveTargets (drive, mix, gt);

    if (s.needsSnap)
    {
        s.active.type = type;
        s.active.resetState();
        s.active.ramp.snap (t);
        s.global.snap (gt);
        s.fadeRemaining = 0;
        s.needsSnap = false;
        return;
    }

    s.global.glide (gt);

    if (type == s.active.type)
    {
        s.active.ramp.glide (t);
        return;
    }

    const auto oldGroup = groupOf (s.active.type);
    const auto newGroup = groupOf (type);

    if (oldGroup == Group::delay && newGroup == Group::delay)
    {
        // Comb +, comb - and flanger share one structure: just glide the coefficients across.
        s.active.type = type;
        s.active.ramp.glide (t);
        return;
    }

    // Freeze the old filter and cross-fade from it. (A change during a cross-fade restarts it from the
    // current filter; the earlier, mostly faded copy is dropped.)
    s.fading = s.active;
    s.fadeRemaining = s.fadeLength;
    s.active.type = type;

    if (newGroup != oldGroup)
    {
        s.active.resetState();
        if (newGroup == Group::delay)
            s.clearDelay();
    }

    s.active.ramp.snap (t);
}

void VoiceFilter::process (float* left, float* right, int numSamples)
{
    if (impl == nullptr || left == nullptr || right == nullptr)
        return;

    auto& s = *impl;
    if (groupOf (s.active.type) == Group::delay || (s.fadeRemaining > 0 && groupOf (s.fading.type) == Group::delay))
        s.delayDirty = true;

    while (numSamples > 0)
    {
        const int n = std::min (numSamples, chunkSize);
        float driven[2][chunkSize], wet[2][chunkSize], mix[chunkSize];

        // Drive: up to +24 dB into a soft clipper, level compensated; faded in so drive = 0 stays clean.
        auto& g = s.global;
        const bool useDrive = g.active (2);
        for (int i = 0; i < n; ++i)
        {
            g.tick();
            mix[i] = g.cur[3];
            if (useDrive)
            {
                const float pre = g.cur[0], comp = g.cur[1], blend = g.cur[2];
                const float l = left[i], r = right[i];
                driven[0][i] = l + blend * (softClip (l * pre) * comp - l);
                driven[1][i] = r + blend * (softClip (r * pre) * comp - r);
            }
        }

        const float* in[2] = { useDrive ? driven[0] : left, useDrive ? driven[1] : right };
        float* out[2] = { wet[0], wet[1] };
        runCore (s.active, s.ctx, in, out, n);

        if (s.fadeRemaining > 0)
        {
            float old[2][chunkSize];
            float* oldOut[2] = { old[0], old[1] };
            runCore (s.fading, s.ctx, in, oldOut, n);

            for (int i = 0; i < n && s.fadeRemaining > 0; ++i, --s.fadeRemaining)
            {
                const float w = (float) s.fadeRemaining * s.fadeInv;   // weight of the old filter
                wet[0][i] += w * (old[0][i] - wet[0][i]);
                wet[1][i] += w * (old[1][i] - wet[1][i]);
            }
        }

        for (int i = 0; i < n; ++i)
        {
            left[i] += mix[i] * (wet[0][i] - left[i]);
            right[i] += mix[i] * (wet[1][i] - right[i]);
        }

        left += n;
        right += n;
        numSamples -= n;
    }
}
} // namespace ers
