#include "WavetableFactory.h"
#include <aakit/dsp/Core.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <complex>

using namespace juce;

// Every table and noise source below is generated from scratch with the maths in this file
// (additive spectra, FM, phase distortion, folding, formant filters). Nothing is sampled.

namespace ers
{
namespace
{
    using cfloat = std::complex<float>;
    using cdouble = std::complex<double>;
    using aa::dsp::Rng;

    constexpr double twoPi = MathConstants<double>::twoPi;
    constexpr float pi = MathConstants<float>::pi;
    constexpr int maxH = wtSize / 2;

    //==============================================================================
    // Small shape helpers. Phase u is in cycles; every shape has period 1.

    inline double frac (double x)   // x - floor (x), without a libm call
    {
        const double f = x - (double) (int64) x;
        return f < 0.0 ? f + 1.0 : f;
    }
    inline float saw (double u) { return (float) (2.0 * frac (u) - 1.0); }                 // rising ramp
    inline float sine (double u) { return (float) std::sin (twoPi * u); }
    inline float tri (double u)                                                           // sine-aligned
    {
        const double f = frac (u + 0.25);
        return (float) (f < 0.5 ? 4.0 * f - 1.0 : 3.0 - 4.0 * f);
    }
    inline float pulse (double u, double duty) { return frac (u) < duty ? 1.0f : -1.0f; }
    inline float clamp01 (float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
    inline float smooth01 (float x) { x = clamp01 (x); return x * x * (3.0f - 2.0f * x); }
    inline float lerpf (float a, float b, float t) { return a + (b - a) * t; }
    inline float expMap (float lo, float hi, float p) { return lo * std::pow (hi / lo, p); }

    /** Triangle whose apex sits at r (0.5 = triangle, 1 = rising ramp), from -1 at u=0. */
    inline float skewTri (double u, double r)
    {
        const double f = frac (u);
        if (f < r || r >= 1.0)
            return (float) (-1.0 + 2.0 * f / r);
        return (float) (1.0 - 2.0 * (f - r) / (1.0 - r));
    }

    /** Triangle wavefolder: identity inside [-1, 1], folds back beyond. */
    inline float fold (float v)
    {
        const float m = v + 1.0f;
        const float w = m - 4.0f * std::floor (m * 0.25f);
        return 1.0f - std::abs (w - 2.0f);
    }

    /** Position over evenly spaced keyframes: segment index and blend. hold > 0 parks on each key for
        that fraction at both ends of a segment, so exact keys are reachable. */
    struct Seg { int index; float t; };

    Seg segment (float p, int numKeys, float hold = 0.0f)
    {
        const float x = clamp01 (p) * (float) (numKeys - 1);
        const int i = jmin ((int) x, numKeys - 2);
        float t = x - (float) i;
        if (hold > 0.0f)
            t = smooth01 ((t - hold) / (1.0f - 2.0f * hold));
        return { i, t };
    }

    /** Remove DC, normalise to peak 1. */
    void finishFrame (float* x)
    {
        double sum = 0.0;
        for (int i = 0; i < wtSize; ++i)
            sum += x[i];
        const auto mean = (float) (sum / wtSize);

        float peak = 0.0f;
        for (int i = 0; i < wtSize; ++i)
        {
            x[i] -= mean;
            peak = jmax (peak, std::abs (x[i]));
        }

        if (peak > 1.0e-9f)
            FloatVectorOperations::multiply (x, 1.0f / peak, wtSize);
    }

    /** One-pole filters run around the cycle until they settle, so the result stays periodic. */
    void cyclicLowpass (float* x, float cutoffHarmonics)
    {
        const float c = 1.0f - std::exp (-2.0f * pi * cutoffHarmonics / (float) wtSize);
        float z = 0.0f;
        for (int pass = 0; pass < 4; ++pass)
            for (int i = 0; i < wtSize; ++i)
            {
                z += c * (x[i] - z);
                if (pass == 3)
                    x[i] = z;
            }
    }

    void cyclicHighpass (float* x, float cutoffHarmonics)
    {
        std::vector<float> in (x, x + wtSize);
        const float a = std::exp (-2.0f * pi * cutoffHarmonics / (float) wtSize);
        float y = 0.0f, prev = in[wtSize - 1];
        for (int pass = 0; pass < 6; ++pass)
            for (int i = 0; i < wtSize; ++i)
            {
                y = a * (y + in[(size_t) i] - prev);
                prev = in[(size_t) i];
                if (pass == 5)
                    x[i] = y;
            }
    }

    /** Removes the jump between the last and first sample with a linear ramp (for iterated shapes). */
    void closeLoop (float* x, float next)
    {
        const float mismatch = next - x[0];
        for (int i = 0; i < wtSize; ++i)
            x[i] -= mismatch * (float) i / (float) wtSize;
    }

    //==============================================================================
    /** Additive renderer. coef[h] is harmonic h as a complex amplitude: the frame is
        sum_h Im (coef[h] * e^(i 2 pi h t)), i.e. |c| sin (2 pi h t + arg c). One inverse FFT per frame. */
    struct Additive
    {
        dsp::FFT fft { 11 };
        std::vector<cfloat> coef;
        std::vector<float> buf;

        Additive() : coef ((size_t) maxH + 1), buf ((size_t) wtSize * 2) {}

        void clear() { std::fill (coef.begin(), coef.end(), cfloat()); }
        void set (int h, float amp, float phase) { coef[(size_t) h] = std::polar (amp, phase); }

        void render (float* out)
        {
            const float scale = 0.5f * (float) wtSize;
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int h = 1; h < maxH; ++h)
            {
                buf[(size_t) (2 * h)] = scale * coef[(size_t) h].imag();
                buf[(size_t) (2 * h + 1)] = -scale * coef[(size_t) h].real();
            }
            fft.performRealOnlyInverseTransform (buf.data());
            std::copy (buf.begin(), buf.begin() + wtSize, out);
        }
    };

    /** Saw harmonic h (rising ramp) as a complex amplitude. */
    inline cfloat sawHarmonic (int h) { return { -1.0f / (float) h, 0.0f }; }

    /** Cascade of 2-pole resonances (a simple all-pole formant filter), at frequency f. */
    cdouble formantResponse (double f, const float* freqs, const float* bws, int n)
    {
        cdouble h (1.0, 0.0);
        for (int k = 0; k < n; ++k)
        {
            const double F = freqs[k], B = bws[k];
            h *= (F * F) / cdouble (F * F - f * f, B * f);
        }
        return h;
    }

    /** Resonant 4-pole lowpass (ladder-like) response at w = f / cutoff, unity gain at DC. */
    cdouble ladderResponse (double w, double k)
    {
        const cdouble s (1.0, w);
        return (1.0 + k) / (s * s * s * s + k);
    }

    inline float bump (float x, float centre, float width) // gaussian
    {
        const float d = (x - centre) / width;
        return std::exp (-0.5f * d * d);
    }

    //==============================================================================
    struct Builder
    {
        explicit Builder (int n) : numFrames (n), frames ((size_t) n * wtSize, 0.0f) {}

        float* frame (int f) { return frames.data() + (size_t) f * wtSize; }
        float pos (int f) const { return numFrames > 1 ? (float) f / (float) (numFrames - 1) : 0.0f; }

        /** fn (p) returns the frame's shape as a function of phase t (0..1); p = table position 0..1.
            Per-frame maths goes in fn, per-sample maths in the returned callable. */
        template <typename Fn>
        void eachFrame (Fn&& fn)
        {
            for (int f = 0; f < numFrames; ++f)
            {
                const auto shape = fn (pos (f));
                float* out = frame (f);
                for (int i = 0; i < wtSize; ++i)
                    out[i] = shape ((double) i / (double) wtSize);
            }
        }

        /** fn (p, additive) fills additive.coef; the frame is rendered from it. */
        template <typename Fn>
        void eachSpectrum (Fn&& fn)
        {
            for (int f = 0; f < numFrames; ++f)
            {
                add.clear();
                fn (pos (f), add);
                add.render (frame (f));
            }
        }

        int numFrames;
        std::vector<float> frames;
        Additive add;
    };

    //==============================================================================
    // Basic

    void basicShapes (Builder& b)
    {
        // sine -> triangle -> saw -> square -> narrow pulse; 65 frames put every key exactly on a frame
        b.eachFrame ([] (float p)
        {
            const auto s = segment (p, 5);
            const double q = s.t;
            const double apex = 0.5 + 0.5 * q, duty = 0.5 * std::pow (0.1, q);
            return [s, q, apex, duty] (double t)
            {
                const double u = t + 0.5;
                switch (s.index)
                {
                    case 0:  return lerpf (sine (t), tri (t), (float) q);
                    case 1:  return skewTri (t + 0.5 * apex, apex);
                    case 2:  return saw (u) - (float) q * saw (u + 0.5);
                    default: return saw (u) - saw (u + duty);
                }
            };
        });
    }

    void pulseWidth (Builder& b)
    {
        b.eachFrame ([] (float p)
        {
            const double duty = 0.5 * std::pow (0.04, (double) p);   // 50% -> 2%
            return [duty] (double t) { return pulse (t, duty); };
        });
    }

    void harmonicLadder (Builder& b)
    {
        // harmonics fade in one by one, slowly at first: 1 -> 96
        b.eachSpectrum ([] (float p, Additive& a)
        {
            const float count = expMap (1.0f, 96.0f, p);
            for (int h = 1; h <= 97; ++h)
            {
                const float g = smooth01 (count - (float) h + 1.0f);
                if (g > 0.0f)
                    a.set (h, g * std::pow ((float) h, -0.8f), 0.0f);
            }
        });
    }

    void hollowFade (Builder& b)
    {
        // square -> sine: odd harmonics under a falling 2-pole cutoff with a soft resonant hump
        b.eachSpectrum ([] (float p, Additive& a)
        {
            const float fc = expMap (300.0f, 0.9f, p);
            for (int h = 1; h < maxH; h += 2)
            {
                const float w = (float) h / fc;
                const float lp = 1.0f / std::sqrt (1.0f + w * w * w * w);
                const float hump = 1.0f + 0.9f * bump (std::log2 ((float) h / fc), 0.0f, 0.35f);
                a.set (h, lp * hump / (float) h, 0.0f);
            }
        });
    }

    //==============================================================================
    // Analog

    void sawSwarm (Builder& b)
    {
        // seven saws drifting against each other at whole-cycle rates: the last frame wraps to the first
        static constexpr int voices = 7;
        const int rates[voices] = { 0, 1, -1, 2, -2, 3, -3 };
        const float gains[voices] = { 1.0f, 0.85f, 0.85f, 0.7f, 0.7f, 0.55f, 0.55f };
        double offsets[voices];
        Rng rng (0x5A3517u);
        for (auto& o : offsets)
            o = rng.next01();

        b.eachFrame ([&] (float p)
        {
            std::array<double, voices> shift {};
            for (int v = 0; v < voices; ++v)
                shift[(size_t) v] = offsets[v] + rates[v] * (double) p;

            return [shift, &gains] (double t)
            {
                float sum = 0.0f;
                for (int v = 0; v < voices; ++v)
                    sum += gains[v] * saw (t + shift[(size_t) v]);
                return sum;
            };
        });
    }

    void syncSweep (Builder& b)
    {
        b.eachFrame ([] (float p)
        {
            const double ratio = std::pow (8.0, (double) p);   // slave 1x -> 8x
            const double window = 0.25 * p;                     // the slave fades a touch over the master cycle
            return [ratio, window] (double t) { return saw (t * ratio) * (float) (1.0 - window * t); };
        });
    }

    void resonantSweep (Builder& b)
    {
        b.eachSpectrum ([] (float p, Additive& a)
        {
            const double cutoff = expMap (1.5f, 260.0f, p);
            for (int h = 1; h < maxH; ++h)
            {
                const auto c = cdouble (sawHarmonic (h)) * ladderResponse ((double) h / cutoff, 3.0);
                if (std::abs (c) < 1.0e-7)
                    break;
                a.coef[(size_t) h] = cfloat ((float) c.real(), (float) c.imag());
            }
        });
    }

    void westFold (Builder& b)
    {
        b.eachFrame ([] (float p)
        {
            const float gain = 0.95f * (1.0f + 6.0f * std::pow (p, 1.3f));
            const float bias = 0.22f * p;
            return [gain, bias] (double t) { return fold (gain * (sine (t) + bias)); };
        });
    }

    void vintagePulse (Builder& b)
    {
        // PWM through AC coupling (droop) and a soft edge, like an old discrete oscillator
        for (int f = 0; f < b.numFrames; ++f)
        {
            const float p = b.pos (f);
            float* x = b.frame (f);
            const double duty = lerpf (0.5f, 0.08f, p);
            for (int i = 0; i < wtSize; ++i)
                x[i] = pulse ((double) i / wtSize, duty);
            cyclicHighpass (x, lerpf (0.15f, 0.5f, p));
            cyclicLowpass (x, lerpf (110.0f, 45.0f, p));
        }
    }

    //==============================================================================
    // Digital

    void phaseRes (Builder& b)
    {
        // phase-distortion style resonance: a cosine burst under a falling ramp window
        b.eachFrame ([] (float p)
        {
            const double ratio = std::pow (16.0, (double) p);
            return [ratio] (double t) { return (float) ((1.0 - t) * (0.5 - 0.5 * std::cos (twoPi * ratio * t))); };
        });
    }

    void bitCrush (Builder& b)
    {
        // fewer amplitude levels and fewer steps per cycle at the same time, blended between integers
        b.eachFrame ([] (float p)
        {
            const float levels = std::pow (2.0f, 6.0f - 4.8f * p);
            const float steps = std::pow (2.0f, 9.0f - 5.5f * p);
            const float l0 = std::floor (levels), lw = levels - l0;
            const float s0 = std::floor (steps), sw = steps - s0;

            return [l0, lw, s0, sw] (double t)
            {
                auto quant = [] (float x, float numLevels)
                {
                    const float half = numLevels * 0.5f;
                    return std::round (x * half) / half;
                };
                auto held = [t] (float numSteps)
                {
                    const double u = std::floor (t * numSteps) / numSteps;
                    return sine (u) * 0.8f + 0.2f * sine (2.0 * u + 0.1);
                };

                const float h0 = held (s0), h1 = held (s0 + 1.0f);
                const float a = lerpf (quant (h0, l0), quant (h0, l0 + 1.0f), lw);
                const float c = lerpf (quant (h1, l0), quant (h1, l0 + 1.0f), lw);
                return lerpf (a, c, sw);
            };
        });
    }

    float chipKey (int k, double t)
    {
        switch (k)
        {
            case 0:  return std::floor ((tri (t) * 0.5f + 0.5f) * 15.999f) / 7.5f - 1.0f;   // 4-bit triangle
            case 1:  return pulse (t, 0.5);
            case 2:  return pulse (t, 0.25);
            case 3:  return pulse (t, 0.125);
            default: return (float) std::floor (frac (t) * 16.0) / 7.5f - 1.0f;           // 4-bit saw
        }
    }

    void chipPulses (Builder& b)
    {
        // 4-bit triangle -> 50% -> 25% -> 12.5% -> 4-bit saw, parked on each key
        b.eachFrame ([] (float p)
        {
            const auto s = segment (p, 5, 0.25f);
            return [s] (double t) { return lerpf (chipKey (s.index, t), chipKey (s.index + 1, t), s.t); };
        });
    }

    void ringSweep (Builder& b)
    {
        b.eachFrame ([] (float p)
        {
            const float ratio = 2.0f + 14.0f * p;
            const float r0 = std::floor (ratio), w = ratio - r0;
            const float depth = 0.4f + 0.6f * p;
            return [r0, w, depth] (double t)
            {
                auto ring = [t, depth] (float r) { return sine (t) * ((1.0f - depth) + depth * sine (r * t)); };
                return lerpf (ring (r0), ring (r0 + 1.0f), w);
            };
        });
    }

    //==============================================================================
    // Vocal

    struct Vowel { float f1, f2, f3; };

    /** Glottal-ish harmonic source (tilt in 1/h^tilt) through five formants, f0 = nominal pitch in Hz. */
    void vowelSpectrum (Additive& a, const float* freqs, const float* bws, float f0, float tilt,
                        const float* phases = nullptr, int maxHarm = 200)
    {
        for (int h = 1; h <= maxHarm; ++h)
        {
            const auto r = formantResponse ((double) (f0 * (float) h), freqs, bws, 5);
            const float amp = (float) std::abs (r) * std::pow ((float) h, -tilt);
            const float ph = (float) std::arg (r) + (phases != nullptr ? phases[h] : 0.0f);
            a.set (h, amp, ph);
        }
    }

    Vowel mixVowels (const Vowel& a, const Vowel& b, float t)
    {
        auto lg = [t] (float x, float y) { return std::exp (lerpf (std::log (x), std::log (y), t)); };
        return { lg (a.f1, b.f1), lg (a.f2, b.f2), lg (a.f3, b.f3) };
    }

    void vowelGlide (Builder& b)
    {
        // A - E - I - O - U (adult voice, nominal pitch 110 Hz)
        const Vowel keys[] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 280, 2250, 2950 },
                               { 570, 850, 2410 }, { 310, 870, 2250 } };
        b.eachSpectrum ([&] (float p, Additive& a)
        {
            const auto s = segment (p, 5, 0.15f);
            const auto v = mixVowels (keys[s.index], keys[s.index + 1], s.t);
            const float freqs[5] = { v.f1, v.f2, v.f3, 3400.0f, 4300.0f };
            const float bws[5] = { 70.0f, 95.0f, 140.0f, 200.0f, 260.0f };
            vowelSpectrum (a, freqs, bws, 110.0f, 0.9f);
        });
    }

    void choirAir (Builder& b)
    {
        // soft ensemble "oo -> oh -> ah -> ae", higher voices; harmonic phases drift across the table
        // so sweeping the position shimmers like several singers
        const Vowel keys[] = { { 340, 900, 2650 }, { 480, 880, 2750 }, { 820, 1250, 2850 }, { 700, 1650, 2750 } };
        Rng rng (0xC401Au);
        std::vector<float> base ((size_t) maxH + 1), drift ((size_t) maxH + 1), phases ((size_t) maxH + 1);
        for (int h = 0; h <= maxH; ++h)
        {
            base[(size_t) h] = rng.next01() * 2.0f * pi;
            drift[(size_t) h] = (rng.nextBipolar() * 0.6f) * 2.0f * pi * jmin (1.0f, (float) h / 6.0f);
        }

        b.eachSpectrum ([&] (float p, Additive& a)
        {
            const auto s = segment (p, 4, 0.1f);
            const auto v = mixVowels (keys[s.index], keys[s.index + 1], s.t);
            const float freqs[5] = { v.f1, v.f2, v.f3, 3700.0f, 4700.0f };
            const float bws[5] = { 90.0f, 120.0f, 180.0f, 260.0f, 320.0f };
            for (int h = 1; h <= maxH; ++h)
                phases[(size_t) h] = base[(size_t) h] + drift[(size_t) h] * p;
            vowelSpectrum (a, freqs, bws, 210.0f, 1.25f, phases.data(), 120);
        });
    }

    void overtoneSinger (Builder& b)
    {
        // an "er" drone with a narrow whistle resonance that picks out harmonics 5..14 one after another
        const float freqs[5] = { 520.0f, 1450.0f, 2500.0f, 3500.0f, 4400.0f };
        const float bws[5] = { 90.0f, 120.0f, 160.0f, 220.0f, 280.0f };
        b.eachSpectrum ([&] (float p, Additive& a)
        {
            const float target = 5.0f + 9.0f * p;
            vowelSpectrum (a, freqs, bws, 130.0f, 1.0f);
            for (int h = 2; h <= 30; ++h)
            {
                const float boost = 1.0f + 9.0f * bump ((float) h, target, 0.42f);
                a.coef[(size_t) h] *= boost;
            }
        });
    }

    //==============================================================================
    // Spectral

    void spectralTilt (Builder& b)
    {
        // dark -> very bright spectral slope, with chirp-like (Schroeder) phases for a low crest factor
        b.eachSpectrum ([] (float p, Additive& a)
        {
            const float slope = lerpf (2.6f, 0.3f, p);
            for (int h = 1; h < maxH; ++h)
            {
                const float phase = -pi * (float) h * (float) (h - 1) / (float) maxH;
                a.set (h, std::pow ((float) h, -slope), phase);
            }
        });
    }

    void combFlange (Builder& b)
    {
        // a saw plus a delayed copy of itself; the delay shrinks, so the comb teeth spread upward
        b.eachFrame ([] (float p)
        {
            const double delay = 0.5 * std::pow (1.0 / 16.0, (double) p);
            return [delay] (double t) { return saw (t) + 0.85f * saw (t + delay); };
        });
    }

    void oddToEven (Builder& b)
    {
        b.eachSpectrum ([] (float p, Additive& a)
        {
            const float odd = std::cos (0.5f * pi * p), even = std::sin (0.5f * pi * p);
            a.set (1, 1.0f, 0.0f);
            for (int h = 2; h < maxH; ++h)
                a.set (h, ((h & 1) != 0 ? odd : even) * std::pow ((float) h, -0.9f), 0.0f);
        });
    }

    void sineToStatic (Builder& b)
    {
        // a sine that dissolves into a dense, rough spectrum
        Rng rng (0xD155u);
        std::vector<float> amps ((size_t) maxH + 1), phases ((size_t) maxH + 1);
        for (int h = 0; h <= maxH; ++h)
        {
            amps[(size_t) h] = 0.2f + 0.8f * rng.next01();
            phases[(size_t) h] = rng.next01() * 2.0f * pi;
        }

        b.eachSpectrum ([&] (float p, Additive& a)
        {
            const float limit = 2.0f + 700.0f * p * p;
            const float level = 2.2f * std::pow (p, 0.7f);
            a.set (1, 1.0f, 0.0f);
            for (int h = 2; h < maxH && (float) h < limit + 1.0f; ++h)
            {
                const float edge = smooth01 ((limit - (float) h) / (0.25f * limit + 1.0f));
                a.set (h, level * edge * amps[(size_t) h] / std::sqrt ((float) h), phases[(size_t) h]);
            }
        });
    }

    void prismScatter (Builder& b)
    {
        // sparse partials whose levels glide between random constellations (cyclic: the end meets the start)
        Rng rng (0x9215Bu);
        constexpr int numPartials = 20, numKeys = 5;
        int harmonics[numPartials];
        float phases[numPartials];
        float levels[numKeys][numPartials];

        for (int i = 0; i < numPartials; ++i)
        {
            bool unique = false;
            while (! unique)
            {
                const float r = rng.next01();
                harmonics[i] = 2 + (int) (r * r * 54.0f);
                unique = true;
                for (int j = 0; j < i; ++j)
                    unique = unique && harmonics[j] != harmonics[i];
            }
            phases[i] = rng.next01() * 2.0f * pi;
        }

        for (int k = 0; k < numKeys - 1; ++k)
            for (int i = 0; i < numPartials; ++i)
                levels[k][i] = rng.chance (0.35f) ? 0.0f : (0.25f + 0.75f * rng.next01()) / std::pow ((float) harmonics[i], 0.7f);
        for (int i = 0; i < numPartials; ++i)
            levels[numKeys - 1][i] = levels[0][i];

        b.eachSpectrum ([&] (float p, Additive& a)
        {
            const auto s = segment (p, numKeys);
            const float w = smooth01 (s.t);
            a.set (1, 0.7f, 0.0f);
            for (int i = 0; i < numPartials; ++i)
                a.coef[(size_t) harmonics[i]] += std::polar (lerpf (levels[s.index][i], levels[s.index + 1][i], w),
                                                             phases[i]);
        });
    }

    //==============================================================================
    // FM

    void fmBloom (Builder& b)
    {
        b.eachFrame ([] (float p)
        {
            const double index = 7.0 * std::pow ((double) p, 1.5);
            return [index] (double t) { return (float) std::sin (twoPi * t + index * std::sin (twoPi * t)); };
        });
    }

    void fmRatioWalk (Builder& b)
    {
        // carrier 1 with the modulator walking through ratios 1..8
        b.eachFrame ([] (float p)
        {
            const auto s = segment (p, 8, 0.2f);
            const double index = 1.8 + 0.8 * std::sin (pi * p);
            return [s, index] (double t)
            {
                auto fm = [t, index] (int ratio) { return (float) std::sin (twoPi * t + index * std::sin (twoPi * ratio * t)); };
                return lerpf (fm (s.index + 1), fm (s.index + 2), s.t);
            };
        });
    }

    void feedbackFm (Builder& b)
    {
        // a self-modulating sine: sine -> saw-like -> edgy. Iterated until periodic.
        for (int f = 0; f < b.numFrames; ++f)
        {
            const double beta = 1.4 * b.pos (f);
            float* x = b.frame (f);
            double y1 = 0.0, y2 = 0.0;
            for (int pass = 0; pass < 4; ++pass)
                for (int i = 0; i < wtSize; ++i)
                {
                    const double y = std::sin (twoPi * i / wtSize + beta * 0.5 * (y1 + y2));
                    y2 = y1;
                    y1 = y;
                    if (pass == 3)
                        x[i] = (float) y;
                }
            closeLoop (x, (float) std::sin (beta * 0.5 * (y1 + y2)));
        }
    }

    void fmBell (Builder& b)
    {
        // carrier on harmonic 2, modulator on 7, plus a hum an octave below: bell-like half-integer partials
        b.eachFrame ([] (float p)
        {
            const double index = 4.0 * p;
            return [index] (double t)
            {
                return (float) (std::sin (twoPi * 2.0 * t + index * std::sin (twoPi * 7.0 * t)) + 0.45 * std::sin (twoPi * t));
            };
        });
    }

    //==============================================================================
    // Bass

    void subWeight (Builder& b)
    {
        // a sine pushed into soft saturation, with a little even harmonic for weight
        b.eachFrame ([] (float p)
        {
            const float drive = expMap (0.4f, 10.0f, p);
            const float even = 0.3f * p;
            return [drive, even] (double t) { return std::tanh (drive * (sine (t) + even * sine (2.0 * t + 0.16))); };
        });
    }

    void growlTalker (Builder& b)
    {
        // saw through a talking two-formant filter (nominal 55 Hz), then driven
        const Vowel keys[] = { { 300, 760, 2400 }, { 500, 900, 2400 }, { 760, 1180, 2500 },
                               { 560, 1750, 2500 }, { 300, 2150, 2700 } };
        for (int f = 0; f < b.numFrames; ++f)
        {
            const float p = b.pos (f);
            const auto s = segment (p, 5);
            const auto v = mixVowels (keys[s.index], keys[s.index + 1], smooth01 (s.t));
            const float freqs[5] = { v.f1, v.f2, v.f3, 3300.0f, 4200.0f };
            const float bws[5] = { 110.0f, 140.0f, 220.0f, 300.0f, 350.0f };

            b.add.clear();
            for (int h = 1; h < 400; ++h)
            {
                const auto r = formantResponse (55.0 * h, freqs, bws, 5);
                const auto c = cdouble (sawHarmonic (h)) * r * (h == 1 ? 1.6 : 1.0);
                b.add.coef[(size_t) h] = cfloat ((float) c.real(), (float) c.imag());
            }
            float* x = b.frame (f);
            b.add.render (x);
            finishFrame (x);
            for (int i = 0; i < wtSize; ++i)
                x[i] = std::tanh (2.2f * x[i]);
        }
    }

    void reeseCycle (Builder& b)
    {
        // detuned saws baked across the table: one full beat cycle, so it loops end -> start
        b.eachFrame ([] (float p)
        {
            return [p] (double t) { return saw (t) + saw (t + p) + 0.5f * saw (t - 2.0 * p + 0.25); };
        });
    }

    void fmGrowl (Builder& b)
    {
        // three-operator stack (1 <- 2 <- 1) with both indices moving
        b.eachFrame ([] (float p)
        {
            const double i1 = 0.3 + 3.7 * std::pow ((double) p, 0.8);
            const double s2 = std::sin (1.5 * pi * p);
            const double i2 = 3.0 * s2 * s2;
            return [i1, i2] (double t)
            {
                const double mod = std::sin (twoPi * 2.0 * t + i2 * std::sin (twoPi * t));
                return (float) (std::sin (twoPi * t + i1 * mod) + 0.35 * std::sin (twoPi * t));
            };
        });
    }

    //==============================================================================
    // Keys & Pads

    void tineKeys (Builder& b)
    {
        // electric-piano-like: a tine (fundamental + a little octave) with bell partials that grow,
        // through an offset magnetic-pickup curve that adds bark: mellow -> struck hard
        b.eachFrame ([] (float p)
        {
            const float bell = 0.55f * std::pow (p, 1.4f);
            const float g = 0.3f + 2.9f * p;
            return [bell, g] (double t)
            {
                const float y = sine (t) + 0.1f * sine (2.0 * t + 0.25)
                              + bell * (0.35f * sine (7.0 * t) + 0.18f * sine (14.0 * t + 0.3));
                constexpr float offset = 0.45f;
                const float v = g * (y + offset), v0 = g * offset;
                return v / std::sqrt (v * v + 1.0f) - v0 / std::sqrt (v0 * v0 + 1.0f);
            };
        });
    }

    void drawbarOrgan (Builder& b)
    {
        // registrations on 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1' (harmonics 1 2 3 4 5 6 8)
        constexpr int bars = 7, numKeys = 7;
        const int harm[bars] = { 1, 2, 3, 4, 5, 6, 8 };
        const int regs[numKeys][bars] = { { 8, 0, 0, 0, 0, 0, 0 }, { 8, 4, 0, 0, 0, 0, 0 }, { 8, 8, 4, 0, 0, 0, 0 },
                                          { 8, 8, 8, 4, 0, 0, 0 }, { 8, 6, 8, 6, 4, 0, 4 }, { 8, 8, 8, 8, 6, 6, 6 },
                                          { 8, 8, 8, 8, 8, 8, 8 } };
        auto level = [] (int d) { return d <= 0 ? 0.0f : std::pow (10.0f, (float) (d - 8) * 3.0f / 20.0f); };

        Rng rng (0x0E6Au);
        float phases[bars];
        for (auto& ph : phases)
            ph = rng.next01() * 2.0f * pi;

        for (int f = 0; f < b.numFrames; ++f)
        {
            const float p = b.pos (f);
            const auto s = segment (p, numKeys, 0.2f);
            b.add.clear();
            for (int k = 0; k < bars; ++k)
                b.add.set (harm[k], lerpf (level (regs[s.index][k]), level (regs[s.index + 1][k]), s.t), phases[k]);

            float* x = b.frame (f);
            b.add.render (x);
            finishFrame (x);
            const float drive = lerpf (0.3f, 1.6f, p);   // a little preamp warmth as it fills up
            for (int i = 0; i < wtSize; ++i)
                x[i] = std::tanh (drive * x[i]);
        }
    }

    void nebulaPad (Builder& b)
    {
        // a soft fixed spectrum whose partial phases and levels wander (cyclic, so it loops)
        constexpr int numKeys = 5, top = 160;
        Rng rng (0x4EB01Au);
        std::vector<float> phase ((size_t) (numKeys * (top + 1))), gainDb ((size_t) (numKeys * (top + 1)));
        for (int k = 0; k < numKeys - 1; ++k)
            for (int h = 1; h <= top; ++h)
            {
                phase[(size_t) (k * (top + 1) + h)] = rng.next01() * 2.0f * pi;
                gainDb[(size_t) (k * (top + 1) + h)] = rng.nextBipolar() * 5.0f;
            }
        for (int h = 1; h <= top; ++h)
        {
            phase[(size_t) ((numKeys - 1) * (top + 1) + h)] = phase[(size_t) h];
            gainDb[(size_t) ((numKeys - 1) * (top + 1) + h)] = gainDb[(size_t) h];
        }

        b.eachSpectrum ([&] (float p, Additive& a)
        {
            const auto s = segment (p, numKeys);
            const float w = smooth01 (s.t);
            const size_t k0 = (size_t) (s.index * (top + 1)), k1 = (size_t) ((s.index + 1) * (top + 1));
            for (int h = 1; h <= top; ++h)
            {
                const float hf = (float) h;
                float env = std::pow (hf, -1.1f) * (1.0f + 1.4f * bump (std::log2 (hf), 2.1f, 0.35f))
                            * (1.0f + 0.9f * bump (std::log2 (hf), 3.5f, 0.3f)) / (1.0f + std::pow (hf / 60.0f, 3.0f));
                if (h == 1)
                    env *= 0.8f;

                float d = phase[k1 + (size_t) h] - phase[k0 + (size_t) h];
                d -= 2.0f * pi * std::round (d / (2.0f * pi));
                const float ph = phase[k0 + (size_t) h] + d * w;
                const float db = lerpf (gainDb[k0 + (size_t) h], gainDb[k1 + (size_t) h], w);
                a.set (h, env * std::pow (10.0f, db / 20.0f), ph);
            }
        });
    }

    void bowedStrings (Builder& b)
    {
        // saw with violin-ish body resonances (nominal 196 Hz) under an opening bow brightness
        b.eachSpectrum ([] (float p, Additive& a)
        {
            const float cutoff = expMap (1.5f, 60.0f, p);
            for (int h = 1; h < 600; ++h)
            {
                const float f = 196.0f * (float) h;
                const float lf = std::log2 (f);
                const float body = 1.0f + 1.2f * bump (lf, std::log2 (290.0f), 0.18f)
                                   + 0.8f * bump (lf, std::log2 (520.0f), 0.2f)
                                   + 1.0f * bump (lf, std::log2 (2900.0f), 0.45f);
                const float w = (float) h / cutoff;
                const float lp = 1.0f / (1.0f + w * w * w * w);
                a.coef[(size_t) h] = sawHarmonic (h) * (body * lp);
            }
        });
    }

    //==============================================================================
    // Weird

    void mangleOrbit (Builder& b)
    {
        // nested phase modulation whose depths orbit around (loops end -> start)
        b.eachFrame ([] (float p)
        {
            const double a = 2.0 + 1.5 * std::sin (twoPi * p);
            const double c = 1.5 + 1.5 * std::sin (twoPi * 2.0 * p + 1.0);
            const double d = 2.0 * (1.0 - std::cos (twoPi * p));
            const double spin = p;
            return [a, c, d, spin] (double t)
            {
                const double inner = std::sin (twoPi * (5.0 * t + spin));
                const double mid = std::sin (twoPi * 3.0 * t + c * inner);
                return (float) (std::sin (twoPi * t + a * mid) + 0.4 * std::sin (twoPi * 2.0 * t + d * std::sin (twoPi * t)));
            };
        });
    }

    int bytePattern (int which, int k)
    {
        switch (which)
        {
            case 0:  return k;
            case 1:  return k ^ (k >> 2);
            case 2:  return (k * 3) & (k >> 1 | 0x55);
            case 3:  return (k | (k >> 3)) * 2;
            case 4:  return k ^ ((k * 5) >> 3);
            case 5:  return (k * (k >> 4)) ^ 0x80;
            default: return (k >> 1) ^ (k << 2) ^ (k >> 5);
        }
    }

    void byteLogic (Builder& b)
    {
        // 8-bit integer logic on a 256-step cycle, blended from pattern to pattern
        b.eachFrame ([] (float p)
        {
            const auto s = segment (p, 7, 0.2f);
            return [s] (double t)
            {
                const int k = jlimit (0, 255, (int) (t * 256.0));
                const float a = (float) (bytePattern (s.index, k) & 255) / 127.5f - 1.0f;
                const float c = (float) (bytePattern (s.index + 1, k) & 255) / 127.5f - 1.0f;
                return lerpf (a, c, s.t);
            };
        });
    }

    void primeLattice (Builder& b)
    {
        // only prime-numbered harmonics (plus the fundamental), a spectral band sweeping up through them
        std::vector<int> primes;
        for (int n = 2; n < 700; ++n)
        {
            bool isPrime = true;
            for (int d = 2; d * d <= n && isPrime; ++d)
                isPrime = (n % d) != 0;
            if (isPrime)
                primes.push_back (n);
        }

        Rng rng (0x9817u);
        std::vector<float> phases (primes.size());
        for (auto& ph : phases)
            ph = rng.next01() * 2.0f * pi;

        b.eachSpectrum ([&] (float p, Additive& a)
        {
            const float centre = lerpf (1.0f, 8.0f, p);   // log2 harmonic
            a.set (1, 0.5f, 0.0f);
            for (size_t i = 0; i < primes.size(); ++i)
            {
                const float lh = std::log2 ((float) primes[i]);
                a.set (primes[i], bump (lh, centre, 0.8f) / std::sqrt ((float) primes[i]) * 2.0f, phases[i]);
            }
        });
    }

    //==============================================================================
    using GenFn = void (*) (Builder&);

    struct TableDef
    {
        const char* name;
        const char* category;
        int frames;
        GenFn generate;
        bool normaliseFrames;
    };

    const TableDef tableDefs[] = {
        { "Origin Shapes",     "Basic",       65,  basicShapes,    true },
        { "Pulse Width",      "Basic",       64,  pulseWidth,     true },
        { "Harmonic Ladder",  "Basic",       64,  harmonicLadder, true },
        { "Hollow Fade",      "Basic",       64,  hollowFade,     true },
        { "Saw Swarm",        "Analog",      256, sawSwarm,       true },
        { "Sync Sweep",       "Analog",      256, syncSweep,      true },
        { "Resonant Sweep",   "Analog",      128, resonantSweep,  true },
        { "West Fold",        "Analog",      64,  westFold,       true },
        { "Vintage Pulse",    "Analog",      64,  vintagePulse,   true },
        { "Phase Res",        "Digital",     128, phaseRes,       true },
        { "Bit Crush",        "Digital",     64,  bitCrush,       true },
        { "Chip Pulses",      "Digital",     64,  chipPulses,     true },
        { "Ring Sweep",       "Digital",     64,  ringSweep,      true },
        { "Vowel Glide",      "Vocal",       64,  vowelGlide,     true },
        { "Choir Air",        "Vocal",       64,  choirAir,       true },
        { "Overtone Singer",  "Vocal",       64,  overtoneSinger, true },
        { "Spectral Tilt",    "Spectral",    64,  spectralTilt,   true },
        { "Comb Flange",      "Spectral",    64,  combFlange,     true },
        { "Odd to Even",      "Spectral",    64,  oddToEven,      true },
        { "Sine to Static",   "Spectral",    64,  sineToStatic,   true },
        { "Prism Scatter",    "Spectral",    64,  prismScatter,   true },
        { "FM Bloom",         "FM",          64,  fmBloom,        true },
        { "FM Ratio Walk",    "FM",          64,  fmRatioWalk,    true },
        { "Feedback FM",      "FM",          64,  feedbackFm,     true },
        { "FM Bell",          "FM",          64,  fmBell,         true },
        { "Sub Weight",       "Bass",        64,  subWeight,      true },
        { "Growl Talker",     "Bass",        64,  growlTalker,    true },
        { "Reese Cycle",      "Bass",        256, reeseCycle,     true },
        { "FM Growl",         "Bass",        64,  fmGrowl,        true },
        { "Tine Keys",        "Keys & Pads", 64,  tineKeys,       true },
        { "Drawbar Organ",    "Keys & Pads", 64,  drawbarOrgan,   true },
        { "Nebula Pad",       "Keys & Pads", 64,  nebulaPad,      true },
        { "Bowed Strings",    "Keys & Pads", 64,  bowedStrings,   true },
        { "Mangle Orbit",     "Weird",       64,  mangleOrbit,    true },
        { "Byte Logic",       "Weird",       64,  byteLogic,      true },
        { "Prime Lattice",    "Weird",       64,  primeLattice,   true },
    };

    constexpr int numTables = (int) (sizeof (tableDefs) / sizeof (tableDefs[0]));

    const std::vector<FactoryTableInfo>& tableInfos()
    {
        static const std::vector<FactoryTableInfo> infos = []
        {
            std::vector<FactoryTableInfo> v;
            for (const auto& d : tableDefs)
                v.push_back ({ d.name, d.category });
            return v;
        }();
        return infos;
    }

    //==============================================================================
    // Noise sources: 48 kHz stereo. Loops are 3 s with the tail crossfaded into the head.

    constexpr float noiseRate = 48000.0f;
    constexpr int loopLength = 144000;
    constexpr int loopFade = 4800;
    constexpr int loopRender = loopLength + loopFade;

    /** Roughly gaussian white noise (sum of three uniforms). */
    inline float gauss (Rng& rng) { return (rng.nextBipolar() + rng.nextBipolar() + rng.nextBipolar()) * 0.577f; }

    /** Paul Kellet's refined pink noise filter. */
    struct PinkFilter
    {
        float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;

        float process (float white)
        {
            b0 = 0.99886f * b0 + white * 0.0555179f;
            b1 = 0.99332f * b1 + white * 0.0750759f;
            b2 = 0.96900f * b2 + white * 0.1538520f;
            b3 = 0.86650f * b3 + white * 0.3104856f;
            b4 = 0.55000f * b4 + white * 0.5329522f;
            b5 = -0.7616f * b5 - white * 0.0168980f;
            const float out = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
            b6 = white * 0.115926f;
            return out * 0.11f;
        }
    };

    /** Smooth random control signal: cosine-interpolated random points every `interval` samples. */
    struct SlowRandom
    {
        SlowRandom (uint32_t seed, int intervalSamples) : rng (seed), interval (intervalSamples)
        {
            a = rng.next01();
            b = rng.next01();
        }

        float next()
        {
            if (++count >= interval)
            {
                count = 0;
                a = b;
                b = rng.next01();
            }
            const float t = (float) count / (float) interval;
            return lerpf (a, b, 0.5f - 0.5f * std::cos (pi * t));
        }

        Rng rng;
        int interval, count = 0;
        float a = 0.0f, b = 0.0f;
    };

    void crossfadeLoop (std::vector<float>& x)
    {
        for (int i = 0; i < loopFade; ++i)
        {
            const float w = 0.5f * pi * (float) i / (float) loopFade;
            x[(size_t) i] = x[(size_t) i] * std::sin (w) + x[(size_t) (loopLength + i)] * std::cos (w);
        }
        x.resize ((size_t) loopLength);
    }

    /** Scales a loop to an RMS target (default -12 dBFS), then soft-limits the rare peaks above
        0.6 so nothing passes 0.95. */
    void normaliseLoop (NoiseSource& n, float rmsTarget = 0.25f)
    {
        double sum = 0.0;
        for (auto* ch : { &n.left, &n.right })
            for (auto v : *ch)
                sum += (double) v * v;

        const auto rms = (float) std::sqrt (sum / (double) (n.left.size() + n.right.size()));
        if (rms < 1.0e-9f)
            return;

        constexpr float knee = 0.6f, ceiling = 0.95f;
        const float gain = rmsTarget / rms;
        for (auto* ch : { &n.left, &n.right })
            for (auto& v : *ch)
            {
                v *= gain;
                const float a = std::abs (v);
                if (a > knee)
                    v = std::copysign (knee + (ceiling - knee) * std::tanh ((a - knee) / (ceiling - knee)), v);
            }
    }

    void normalisePeak (NoiseSource& n, float target = 0.9f)
    {
        float peak = 0.0f;
        for (auto* ch : { &n.left, &n.right })
            for (auto v : *ch)
                peak = jmax (peak, std::abs (v));
        if (peak > 1.0e-9f)
            for (auto* ch : { &n.left, &n.right })
                FloatVectorOperations::multiply (ch->data(), target / peak, (int) ch->size());
    }

    /** Renders a loop: fn (channel, rng, buffer) fills loopRender samples per channel. */
    template <typename Fn>
    std::shared_ptr<NoiseSource> makeLoop (const char* name, uint32_t seed, Fn&& fn)
    {
        auto n = std::make_shared<NoiseSource>();
        n->name = name;
        n->loops = true;
        n->sampleRate = noiseRate;
        int channel = 0;
        for (auto* ch : { &n->left, &n->right })
        {
            ch->assign ((size_t) loopRender, 0.0f);
            Rng rng (seed + 0x1000193u * (uint32_t) (channel + 1));
            fn (channel, rng, *ch);
            crossfadeLoop (*ch);
            ++channel;
        }
        return n;
    }

    std::shared_ptr<NoiseSource> makeOneShot (const char* name, float seconds)
    {
        auto n = std::make_shared<NoiseSource>();
        n->name = name;
        n->loops = false;
        n->sampleRate = noiseRate;
        n->left.assign ((size_t) (seconds * noiseRate), 0.0f);
        n->right.assign ((size_t) (seconds * noiseRate), 0.0f);
        return n;
    }

    std::shared_ptr<NoiseSource> whiteNoise()
    {
        auto n = makeLoop ("White Noise", 0x1001u, [] (int, Rng& rng, std::vector<float>& x)
        {
            for (auto& v : x)
                v = gauss (rng);
        });
        normaliseLoop (*n);
        return n;
    }

    std::shared_ptr<NoiseSource> pinkNoise()
    {
        auto n = makeLoop ("Pink Noise", 0x1002u, [] (int, Rng& rng, std::vector<float>& x)
        {
            PinkFilter pink;
            for (auto& v : x)
                v = pink.process (gauss (rng));
        });
        normaliseLoop (*n);
        return n;
    }

    std::shared_ptr<NoiseSource> brownNoise()
    {
        auto n = makeLoop ("Brown Noise", 0x1003u, [] (int, Rng& rng, std::vector<float>& x)
        {
            float y = 0.0f;
            for (int i = -4800; i < (int) x.size(); ++i)      // warm up so the start isn't a fade-in
            {
                y = 0.9985f * y + 0.05f * gauss (rng);
                if (i >= 0)
                    x[(size_t) i] = y;
            }
        });
        normaliseLoop (*n);
        return n;
    }

    std::shared_ptr<NoiseSource> blueNoise()
    {
        // differentiated pink: rises 3 dB per octave
        auto n = makeLoop ("Blue Noise", 0x1004u, [] (int, Rng& rng, std::vector<float>& x)
        {
            PinkFilter pink;
            float prev = 0.0f;
            for (auto& v : x)
            {
                const float s = pink.process (gauss (rng));
                v = s - prev;
                prev = s;
            }
        });
        normaliseLoop (*n);
        return n;
    }

    std::shared_ptr<NoiseSource> vinylCrackle()
    {
        auto n = makeLoop ("Vinyl Crackle", 0x1005u, [] (int channel, Rng& rng, std::vector<float>& x)
        {
            // crackle timing is shared by both channels (same seed); level and texture differ per side
            Rng events (0x7A11u);
            PinkFilter pink;
            aa::dsp::OnePole hissLp, rumbleLp;
            hissLp.setCutoff (7000.0f, noiseRate);
            rumbleLp.setCutoff (35.0f, noiseRate);
            float pop = 0.0f, popDecay = 0.0f, prevNoise = 0.0f;
            float pan = 0.5f;

            for (auto& v : x)
            {
                if (events.chance (22.0f / noiseRate))
                {
                    const float u = events.next01();
                    pop = 0.08f + 0.92f * u * u * u * u;
                    popDecay = std::exp (-1.0f / (noiseRate * (0.0002f + 0.0012f * events.next01())));
                    pan = events.next01();
                }

                const float side = channel == 0 ? std::sqrt (1.0f - pan) : std::sqrt (pan);
                const float noise = gauss (rng);
                const float click = pop * side * (noise - prevNoise);   // bright, short burst
                prevNoise = noise;
                pop *= popDecay;

                const float hiss = hissLp.lowpass (pink.process (gauss (rng))) * 0.05f;
                const float rumble = rumbleLp.lowpass (gauss (rng)) * 0.6f;
                v = click + hiss + rumble;
            }
        });
        normaliseLoop (*n, 0.1f);
        return n;
    }

    std::shared_ptr<NoiseSource> digitalGrit()
    {
        auto n = makeLoop ("Digital Grit", 0x1006u, [] (int channel, Rng& rng, std::vector<float>& x)
        {
            SlowRandom holdControl (0x6217u + (uint32_t) channel, 2400);
            float held = 0.0f;
            int countdown = 0;
            for (auto& v : x)
            {
                if (--countdown <= 0)
                {
                    const float c = holdControl.next();
                    countdown = 1 + rng.nextInt (1 + (int) (c * c * 60.0f));
                    held = std::round (rng.nextBipolar() * 7.0f) / 7.0f;   // ~4 bit
                }
                else
                {
                    holdControl.next();
                }
                v = held;
            }
        });
        normaliseLoop (*n);
        return n;
    }

    std::shared_ptr<NoiseSource> metalShimmer()
    {
        // a cluster of inharmonic, ringing resonators struck by sparse impulses and a faint noise bed
        auto n = makeLoop ("Metal Shimmer", 0x1007u, [] (int channel, Rng& rng, std::vector<float>& x)
        {
            constexpr int numRes = 16;
            float c1[numRes], c2[numRes], gain[numRes], y1[numRes] = {}, y2[numRes] = {};
            Rng shape (0x3E7A1u);
            for (int k = 0; k < numRes; ++k)
            {
                const float f = 310.0f * std::pow ((float) (k + 1), 1.41f) * (1.0f + 0.04f * shape.nextBipolar())
                                * (channel == 0 ? 1.0f : 1.003f);
                const float decay = 0.5f + 1.6f * shape.next01();                  // seconds
                const float r = std::exp (-1.0f / (decay * noiseRate));
                c1[k] = 2.0f * r * std::cos (2.0f * pi * jmin (f, 16000.0f) / noiseRate);
                c2[k] = -r * r;
                gain[k] = (1.0f - r) * (0.6f + 0.8f * shape.next01());
            }

            for (int i = -2 * (int) noiseRate; i < (int) x.size(); ++i)     // 2 s pre-roll: start ringing
            {
                float excite = 0.02f * gauss (rng);
                if (rng.chance (14.0f / noiseRate))
                    excite += (rng.nextBipolar() > 0 ? 1.0f : -1.0f) * (0.5f + 2.0f * rng.next01()) * 40.0f;

                float sum = 0.0f;
                for (int k = 0; k < numRes; ++k)
                {
                    const float y = c1[k] * y1[k] + c2[k] * y2[k] + excite * gain[k];
                    y2[k] = y1[k];
                    y1[k] = y;
                    sum += y;
                }
                if (i >= 0)
                    x[(size_t) i] = sum;
            }
        });
        normaliseLoop (*n);
        return n;
    }

    std::shared_ptr<NoiseSource> solarWind()
    {
        auto n = makeLoop ("Solar Wind", 0x1008u, [] (int channel, Rng& rng, std::vector<float>& x)
        {
            SlowRandom sweep (0x51D0u + (uint32_t) channel * 7u, 19200), gust (0x6057u + (uint32_t) channel * 3u, 28800);
            aa::dsp::Svf band;
            aa::dsp::OnePole rumble;
            rumble.setCutoff (90.0f, noiseRate);
            PinkFilter pink;

            for (size_t i = 0; i < x.size(); ++i)
            {
                const float s = sweep.next(), g = gust.next();
                if ((i & 15) == 0)
                    band.setCutoffRes (expMap (220.0f, 3200.0f, s), 0.45f, noiseRate);
                const float white = gauss (rng);
                x[i] = band.bandpass (white) * (0.25f + 0.9f * g * g) + rumble.lowpass (pink.process (white)) * 0.3f;
            }
        });
        normaliseLoop (*n);
        return n;
    }

    std::shared_ptr<NoiseSource> breath()
    {
        auto n = makeLoop ("Breath", 0x1009u, [] (int channel, Rng& rng, std::vector<float>& x)
        {
            aa::dsp::Svf low, high;
            low.setCutoffQ (1400.0f, 0.8f, noiseRate);
            high.setCutoffQ (3300.0f, 1.3f, noiseRate);
            aa::dsp::OnePole hp;
            hp.setCutoff (300.0f, noiseRate);
            SlowRandom level (0xB2EAu + (uint32_t) channel, 14400);

            for (auto& v : x)
            {
                const float w = hp.highpass (gauss (rng));
                v = (low.bandpass (w) + 0.6f * high.bandpass (w)) * (0.75f + 0.25f * level.next());
            }
        });
        normaliseLoop (*n);
        return n;
    }

    std::shared_ptr<NoiseSource> rain()
    {
        auto n = makeLoop ("Rain", 0x100Au, [] (int channel, Rng& rng, std::vector<float>& x)
        {
            // droplets: short decaying resonances (same drops on both sides, panned) over a soft hiss bed
            struct Drop { float y1, y2, c1, c2; };
            std::vector<Drop> drops;
            drops.reserve (128);
            Rng events (0xDA1Bu);
            aa::dsp::OnePole hissHp, bedLp;
            hissHp.setCutoff (1800.0f, noiseRate);
            bedLp.setCutoff (900.0f, noiseRate);
            PinkFilter pink;

            for (size_t i = 0; i < x.size(); ++i)
            {
                if (events.chance (380.0f / noiseRate))
                {
                    const float u = events.next01(), pan = events.next01();
                    const float w = 2.0f * pi * expMap (1400.0f, 7000.0f, events.next01()) / noiseRate;
                    const float r = std::exp (-1.0f / (noiseRate * (0.002f + 0.01f * events.next01())));
                    const float amp = (0.04f + 0.6f * u * u * u) * (channel == 0 ? std::sqrt (1.0f - pan) : std::sqrt (pan));
                    if (drops.size() < 128)
                        drops.push_back ({ amp * std::sin (w), 0.0f, 2.0f * r * std::cos (w), -r * r });
                }

                float sum = 0.0f;
                for (auto& d : drops)
                {
                    const float y = d.c1 * d.y1 + d.c2 * d.y2;
                    d.y2 = d.y1;
                    d.y1 = y;
                    sum += y;
                }

                if ((i & 63) == 0)
                    drops.erase (std::remove_if (drops.begin(), drops.end(),
                                                 [] (const Drop& d) { return std::abs (d.y1) + std::abs (d.y2) < 1.0e-5f; }),
                                 drops.end());

                const float white = gauss (rng);
                x[i] = sum + hissHp.highpass (white) * 0.05f + bedLp.lowpass (pink.process (white)) * 0.08f;
            }
        });
        normaliseLoop (*n);
        return n;
    }

    std::shared_ptr<NoiseSource> tapeHum()
    {
        auto n = makeLoop ("Tape Hiss & Hum", 0x100Bu, [] (int, Rng& rng, std::vector<float>& x)
        {
            aa::dsp::OnePole hp, lp;
            hp.setCutoff (700.0f, noiseRate);
            lp.setCutoff (12000.0f, noiseRate);
            for (auto& v : x)
                v = lp.lowpass (hp.highpass (gauss (rng)));
        });

        // 50 Hz mains hum: exactly 150 cycles per loop, so it's added after the crossfade
        for (auto* ch : { &n->left, &n->right })
            for (int i = 0; i < loopLength; ++i)
            {
                const double ph = twoPi * 50.0 * i / noiseRate;
                (*ch)[(size_t) i] += (float) (0.35 * std::sin (ph) + 0.18 * std::sin (2.0 * ph + 0.4)
                                              + 0.12 * std::sin (3.0 * ph + 1.1) + 0.05 * std::sin (5.0 * ph + 2.0));
            }
        normaliseLoop (*n);
        return n;
    }

    std::shared_ptr<NoiseSource> clickShot()
    {
        auto n = makeOneShot ("Click", 0.03f);
        Rng rng (0x100Cu);
        for (int c = 0; c < 2; ++c)
        {
            auto& x = c == 0 ? n->left : n->right;
            for (size_t i = 0; i < x.size(); ++i)
            {
                const float t = (float) i / noiseRate;
                const float burst = gauss (rng) * std::exp (-t / 0.0006f) * 0.6f;
                const float ping = std::sin (2.0f * pi * 2900.0f * t) * std::exp (-t / 0.002f) * 0.45f;
                const float body = std::sin (2.0f * pi * 600.0f * t) * std::exp (-t / 0.004f) * 0.35f;
                x[i] = burst + ping + body;
            }
            x[0] += 0.9f;
            x[1] -= 0.5f;
        }
        normalisePeak (*n);
        return n;
    }

    std::shared_ptr<NoiseSource> hitShot()
    {
        auto n = makeOneShot ("Hit", 0.4f);
        Rng rng (0x100Du);
        double bodyPhase = 0.0;
        std::vector<float> body (n->left.size());
        for (size_t i = 0; i < body.size(); ++i)
        {
            const float t = (float) i / noiseRate;
            const float f = 140.0f + 200.0f * std::exp (-t / 0.03f);
            bodyPhase += f / noiseRate;
            body[i] = (float) std::sin (twoPi * bodyPhase) * std::exp (-t / 0.11f);
        }

        for (int c = 0; c < 2; ++c)
        {
            auto& x = c == 0 ? n->left : n->right;
            aa::dsp::Svf band;
            band.setCutoffQ (2400.0f, 0.8f, noiseRate);
            for (size_t i = 0; i < x.size(); ++i)
            {
                const float t = (float) i / noiseRate;
                const float attack = jmin (1.0f, t / 0.0008f);
                const float noise = band.bandpass (gauss (rng)) * std::exp (-t / 0.035f) * 1.4f;
                x[i] = std::tanh (1.3f * (attack * (noise + 0.9f * body[i])));
            }
        }
        normalisePeak (*n);
        return n;
    }

    std::shared_ptr<NoiseSource> zapShot()
    {
        auto n = makeOneShot ("Zap", 0.3f);
        for (int c = 0; c < 2; ++c)
        {
            auto& x = c == 0 ? n->left : n->right;
            const double detune = c == 0 ? 1.0 : 1.012;
            double phase = 0.0;
            for (size_t i = 0; i < x.size(); ++i)
            {
                const double t = (double) i / noiseRate;
                const double f = (60.0 + 3600.0 * std::exp (-t / 0.035)) * detune;
                phase += f / noiseRate;
                const double env = std::min (1.0, t / 0.001) * std::exp (-t / 0.09);
                x[i] = (float) (std::tanh (2.0 * std::sin (twoPi * phase)) * env);
            }
        }
        normalisePeak (*n);
        return n;
    }

    using NoiseFn = std::shared_ptr<NoiseSource> (*)();

    struct NoiseDef
    {
        const char* name;
        NoiseFn generate;
    };

    const NoiseDef noiseDefs[] = {
        { "White Noise",     whiteNoise },
        { "Pink Noise",      pinkNoise },
        { "Brown Noise",     brownNoise },
        { "Blue Noise",      blueNoise },
        { "Vinyl Crackle",   vinylCrackle },
        { "Digital Grit",    digitalGrit },
        { "Metal Shimmer",   metalShimmer },
        { "Solar Wind",      solarWind },
        { "Breath",          breath },
        { "Rain",            rain },
        { "Tape Hiss & Hum", tapeHum },
        { "Click",           clickShot },
        { "Hit",             hitShot },
        { "Zap",             zapShot },
    };

    constexpr int numNoises = (int) (sizeof (noiseDefs) / sizeof (noiseDefs[0]));
} // namespace

//==============================================================================
int getNumFactoryTables() { return numTables; }

const FactoryTableInfo& getFactoryTableInfo (int index)
{
    return tableInfos()[(size_t) jlimit (0, numTables - 1, index)];
}

StringArray getFactoryTableNames()
{
    StringArray names;
    for (const auto& d : tableDefs)
        names.add (d.name);
    return names;
}

WavetablePtr createFactoryTable (int index)
{
    const auto& def = tableDefs[jlimit (0, numTables - 1, index)];
    Builder builder (def.frames);
    def.generate (builder);

    if (def.normaliseFrames)
        for (int f = 0; f < builder.numFrames; ++f)
            finishFrame (builder.frame (f));

    return Wavetable::fromFrames (def.name, builder.frames, builder.numFrames);
}

int getNumNoiseTypes() { return numNoises; }

StringArray getNoiseNames()
{
    StringArray names;
    for (const auto& d : noiseDefs)
        names.add (d.name);
    return names;
}

std::shared_ptr<const NoiseSource> createNoise (int index)
{
    return noiseDefs[jlimit (0, numNoises - 1, index)].generate();
}
} // namespace ers
