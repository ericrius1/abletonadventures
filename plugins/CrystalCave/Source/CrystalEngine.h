#pragma once

// Crystal Cave's reverb engine. Header-only, allocation-free once prepared.
//
//   in -> pre-delay -> (+ shimmer return) -> band limit -> 4-stage Hadamard diffusion
//      -> 16-line modulated FDN (Hadamard feedback, per-line shelving damping) -> width -> low/high cut -> wet out
//
//   Shimmer (classic topology): the FDN output is band-limited, pitch shifted by a 4-grain Hann shifter,
//   brightened ("Sparkle"), DC-blocked, level-guarded and fed back into the network input. The return gain is
//   normalised by the network's energy gain so the loop gain stays below 1 at every decay time.

#include <aakit/dsp/Core.h>

namespace crystal
{
namespace dsp = aa::dsp;

//==============================================================================
/** Clean granular pitch shifter: four Hann-windowed taps sweep through a delay line at 90 degree offsets.
    Hann^1 and Hann^2 both sum to a constant at 75 % overlap, so the level is steady for tonal and noisy
    material alike. Each grain can be offset by a random jitter (chosen while its window is silent). */
class GrainShifter
{
public:
    static constexpr int numTaps = 4;

    void prepare (float sampleRate, float windowSeconds, uint32_t seed)
    {
        sr = sampleRate;
        window = windowSeconds * sr;
        maxJitter = 0.018f * sr;
        line.prepare ((int) (window + maxJitter) + minDelay + 16);
        rng.seed (seed);
        reset();
    }

    void reset()
    {
        line.clear();
        phase = 0.0f;
        for (int k = 0; k < numTaps; ++k)
        {
            lastP[k] = 0.25f * (float) k;
            jitter[k] = 0.0f;
        }
    }

    /** Pitch ratio (2 = octave up). Changing it never clicks: only the sweep speed changes. */
    void setRatio (float ratio) { inc = (1.0f - ratio) / window; }

    float process (float x, float jitterAmount)
    {
        line.push (x);
        phase += inc;
        phase -= std::floor (phase);

        float out = 0.0f;
        for (int k = 0; k < numTaps; ++k)
        {
            float p = phase + 0.25f * (float) k;
            if (p >= 1.0f)
                p -= 1.0f;

            if (std::abs (p - lastP[k]) > 0.5f) // wrapped: a new grain starts while its window is at zero
                jitter[k] = rng.next01() * jitterAmount * maxJitter;

            lastP[k] = p;
            out += hann (p) * line.read ((float) minDelay + p * window + jitter[k]);
        }
        return out * 0.7f;
    }

private:
    static float hann (float p)
    {
        // sin^2 (pi p) using Bhaskara's sine approximation (error < 0.2 %)
        const float q = p * (1.0f - p);
        const float s = 16.0f * q / (5.0f - 4.0f * q);
        return s * s;
    }

    static constexpr int minDelay = 4;
    dsp::DelayLine line;
    dsp::Rng rng;
    float sr = 44100.0f, window = 4000.0f, maxJitter = 0.0f, phase = 0.0f, inc = 0.0f;
    float lastP[numTaps] {}, jitter[numTaps] {};
};

//==============================================================================
class CrystalReverb
{
public:
    static constexpr int numLines = 16;
    static constexpr int numStages = 4;
    static constexpr int controlBlock = 32;

    struct Params
    {
        float predelayMs = 18.0f;
        float size = 0.65f;          // 0..1
        float decaySeconds = 4.5f;   // RT60
        float damping = 0.35f;       // 0..1
        float modulation = 0.4f;     // 0..1
        float lowCutHz = 140.0f;
        float highCutHz = 11000.0f;
        float width = 1.0f;          // 0..1
        float shimmer = 0.3f;        // 0..1
        float shimmerRatio = 2.0f;   // pitch ratio of the shimmer shifter
        float sparkle = 0.4f;        // 0..1
        bool freeze = false;
    };

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;

        const int maxPre = (int) (sr * 0.26f) + 8;
        preL.prepare (maxPre);
        preR.prepare (maxPre);

        dsp::Rng rng (0x5EEDC0DEu);

        // Diffusion: each stage spreads its 16 delays across a slot that roughly doubles per stage.
        const float stageMs[numStages] = { 3.0f, 6.0f, 11.0f, 19.0f };
        for (int s = 0; s < numStages; ++s)
            for (int c = 0; c < numLines; ++c)
            {
                const float lo = stageMs[s] * (float) c / numLines;
                const float hi = stageMs[s] * (float) (c + 1) / numLines;
                const float ms = lo + (hi - lo) * rng.next01();
                diffDelay[s][c] = std::max (1, (int) (ms * 0.001f * sr));
                diffusers[s][c].prepare (diffDelay[s][c] + 2);
                flip[s][c] = rng.chance (0.5f);
            }

        // FDN line lengths: exponential spread over a 2.9:1 ratio, with a little jitter so nothing lines up.
        float meanBase = 0.0f;
        for (int c = 0; c < numLines; ++c)
        {
            baseLen[c] = std::pow (2.9f, (float) c / (float) (numLines - 1)) * (1.0f + 0.045f * rng.nextBipolar());
            meanBase += baseLen[c] / numLines;
            lines[c].prepare ((int) (sr * (maxUnitSeconds * 2.9f * 1.05f + 0.006f)) + 64);

            const float golden = std::fmod ((float) c * 0.618034f, 1.0f);
            lfoRate[c] = 0.07f + 0.8f * golden;
            const float ph = dsp::twoPi * std::fmod ((float) c * 0.377f, 1.0f);
            lfoS[c] = std::sin (ph);
            lfoC[c] = std::cos (ph);
        }
        meanBaseLen = meanBase;

        shifterL.prepare (sr, 0.085f, 0x1234567u);
        shifterR.prepare (sr, 0.085f, 0x7654321u);

        reset();
        snapToTargets();
    }

    void reset()
    {
        preL.clear();
        preR.clear();
        for (auto& stage : diffusers)
            for (auto& d : stage)
                d.clear();
        for (auto& l : lines)
            l.clear();
        for (int c = 0; c < numLines; ++c)
            dampLp[c] = 0.0f;

        inHpZL = inHpZR = inLpZL = inLpZR = 0.0f;
        outHpL.reset(); outHpR.reset(); outLpL.reset(); outLpR.reset();
        shHpL.reset(); shHpR.reset(); shLpL.reset(); shLpR.reset();
        shifterL.reset();
        shifterR.reset();
        shelfL = shelfR = dcL = dcR = 0.0f;
        retL = retR = 0.0f;
        agcEnv = 0.0f;
        agcGain = 1.0f;
        limEnv = 0.0f;
        shimmerSum = 0.0f;
        shimmerCount = 0;
    }

    /** Set the targets (any thread that owns the engine, normally the audio thread once per block). */
    void setParams (const Params& p) { target = p; }

    /** Jump every smoothed value to its target (after prepare / when the host restarts playback). */
    void snapToTargets()
    {
        firstUpdate = true;
        updateControl();
        firstUpdate = false;
        controlCountdown = controlBlock;
    }

    /** Wet-only stereo processing. In and out may alias. */
    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples)
    {
        int i = 0;
        while (i < numSamples)
        {
            if (controlCountdown <= 0)
            {
                updateControl();
                controlCountdown = controlBlock;
            }
            const int n = std::min (numSamples - i, controlCountdown);
            processChunk (inL + i, inR + i, outL + i, outR + i, n);
            controlCountdown -= n;
            i += n;
        }
    }

    /** RMS of the shimmer return since the last call (for the UI). */
    float takeShimmerLevel()
    {
        const float v = shimmerCount > 0 ? std::sqrt (shimmerSum / (float) shimmerCount) : 0.0f;
        shimmerSum = 0.0f;
        shimmerCount = 0;
        return v;
    }

    float getFreezeAmount() const noexcept { return freezeAmt; }

    /** Estimated power gain of the network (noise in -> wet out) for a decay time and size. Used to keep the
        shimmer loop gain constant (and below 1) across decay settings. The per-line geometric series is scaled
        by a factor fitted to measurements as a function of the number of loop passes in one RT60 (it falls
        slowly with the pass count because interpolation and the band limits shave a little off every pass).
        The fit errs on the high side so the real loop gain stays at or below the nominal one. */
    float networkEnergy (float rtSeconds, float unitSeconds) const
    {
        rtSeconds = std::max (0.05f, rtSeconds);
        float e = 0.0f;
        for (int c = 0; c < numLines; ++c)
        {
            const float g = std::exp (-6.9078f * baseLen[c] * unitSeconds / rtSeconds);
            const float g2 = g * g;
            e += g2 / std::max (1.0e-4f, 1.0f - g2);
        }
        const float passes = std::max (1.0f, rtSeconds / (meanBaseLen * unitSeconds));
        const float scale = std::max (0.05f, 0.172f - 0.0148f * std::log (passes)) * energyScale;
        return scale * e / (float) numLines;
    }

    // Calibration constants (see tools in the plugin notes): output scaling and energy model.
    static constexpr float maxUnitSeconds = 0.070f;
    float outScale = 0.25f;
    float injectGain = 0.5f;
    float energyScale = 1.1f;

private:
    static float unitSecondsFor (float size)
    {
        return 0.008f + (maxUnitSeconds - 0.008f) * std::pow (dsp::clamp01 (size), 1.5f);
    }

    static float onePoleA (float hz, float sampleRate)
    {
        hz = std::min (hz, sampleRate * 0.45f);
        return 1.0f - std::exp (-dsp::twoPi * hz / sampleRate);
    }

    void updateControl()
    {
        const float blockSec = (float) controlBlock / sr;
        auto coeff = [&] (float seconds) { return firstUpdate ? 1.0f : 1.0f - std::exp (-blockSec / seconds); };
        const float kFast = coeff (0.03f), kMed = coeff (0.09f), kSlow = coeff (0.35f);

        sm.size += kSlow * (target.size - sm.size);
        sm.logRt += kMed * (std::log (std::max (0.1f, target.decaySeconds)) - sm.logRt);
        sm.damping += kMed * (target.damping - sm.damping);
        sm.mod += kMed * (target.modulation - sm.mod);
        sm.logLow += kFast * (std::log (std::max (10.0f, target.lowCutHz)) - sm.logLow);
        sm.logHigh += kFast * (std::log (std::max (200.0f, target.highCutHz)) - sm.logHigh);
        sm.width += kFast * (target.width - sm.width);
        sm.shimmer += kMed * (target.shimmer - sm.shimmer);
        sm.sparkle += kMed * (target.sparkle - sm.sparkle);
        sm.predelay += coeff (0.12f) * (std::max (1.0f, target.predelayMs * 0.001f * sr) - sm.predelay);

        const float freezeTarget = target.freeze ? 1.0f : 0.0f;
        if (firstUpdate)
            freezeAmt = freezeTarget;
        else
        {
            const float step = blockSec / 0.15f;
            freezeAmt += juce::jlimit (-step, step, freezeTarget - freezeAmt);
        }
        // Equal-ish crossfade curve: the loop closes smoothly while the input fades out.
        const float fz = freezeAmt * freezeAmt * (3.0f - 2.0f * freezeAmt);

        const float invN = 1.0f / (float) controlBlock;
        auto ramp = [&] (float& value, float& step, float newTarget)
        {
            if (firstUpdate)
            {
                value = newTarget;
                step = 0.0f;
            }
            else
                step = (newTarget - value) * invN;
        };

        // ---- network --------------------------------------------------------------
        const float unit = unitSecondsFor (sm.size);
        const float rt = std::exp (sm.logRt);
        const float rtHf = std::max (0.08f, rt * (1.0f - 0.9f * std::pow (dsp::clamp01 (sm.damping), 0.8f)));

        for (int c = 0; c < numLines; ++c)
        {
            const float loopSec = baseLen[c] * unit;
            const float gDecay = std::exp (-6.9078f * loopSec / rt);
            const float gHigh = std::exp (-6.9078f * loopSec / rtHf);
            const float gT = gDecay + (1.0f - gDecay) * fz;
            const float hRatio = gHigh / gDecay;
            const float hT = hRatio + (1.0f - hRatio) * fz;
            ramp (len[c], lenStep[c], loopSec * sr);
            ramp (gain[c], gainStep[c], gT);
            ramp (hfGain[c], hfStep[c], hT);
        }

        dampA = onePoleA (6500.0f * std::pow (1500.0f / 6500.0f, dsp::clamp01 (sm.damping)), sr);

        const float depthSec = 0.00004f + 0.0017f * std::pow (dsp::clamp01 (sm.mod), 1.4f);
        ramp (modDepth, modDepthStep, depthSec * sr * (1.0f - 0.5f * fz));

        const float rateMul = 0.55f + 0.9f * sm.mod;
        for (int c = 0; c < numLines; ++c)
        {
            const float w = dsp::twoPi * lfoRate[c] * rateMul / sr;
            rotC[c] = std::cos (w);
            rotS[c] = std::sin (w);
            // renormalise the quadrature oscillator
            const float m = lfoS[c] * lfoS[c] + lfoC[c] * lfoC[c];
            const float k = 1.5f - 0.5f * m;
            lfoS[c] *= k;
            lfoC[c] *= k;
        }

        ramp (inGain, inGainStep, 1.0f - fz);
        ramp (predelay, predelayStep, sm.predelay);

        // ---- filters ----------------------------------------------------------------
        const float lowHz = std::exp (sm.logLow), highHz = std::exp (sm.logHigh);
        inHpA = onePoleA (lowHz * 0.7f, sr);
        inLpA = onePoleA (highHz * 1.3f, sr);
        outHpL.setCutoffQ (lowHz, 0.7071f, sr);
        outHpR.setCutoffQ (lowHz, 0.7071f, sr);
        outLpL.setCutoffQ (highHz, 0.7071f, sr);
        outLpR.setCutoffQ (highHz, 0.7071f, sr);
        ramp (width, widthStep, sm.width);

        // ---- shimmer ------------------------------------------------------------------
        const float ratio = juce::jlimit (0.25f, 4.0f, target.shimmerRatio);
        shifterL.setRatio (ratio);
        shifterR.setRatio (ratio);

        const float shLow = ratio >= 1.0f ? 260.0f : 130.0f;
        const float antiAlias = 0.42f * sr / std::max (1.0f, ratio);
        const float shHigh = std::min ({ 9500.0f, antiAlias, highHz * 1.15f });
        shHpL.setCutoffQ (shLow, 0.7071f, sr);
        shHpR.setCutoffQ (shLow, 0.7071f, sr);
        shLpL.setCutoffQ (shHigh, 0.7071f, sr);
        shLpR.setCutoffQ (shHigh, 0.7071f, sr);

        sparkleBoost = 1.0f * sm.sparkle;
        jitterAmt = sm.sparkle;
        shelfA = onePoleA (3200.0f, sr);
        dcA = onePoleA (8.0f, sr);

        const float loopGain = 0.95f * std::pow (dsp::clamp01 (sm.shimmer), 0.8f);
        const float energy = networkEnergy (rt, unit);
        const float shTarget = loopGain / std::sqrt (energy) / (1.0f + 0.45f * sm.sparkle) * (1.0f - fz);
        ramp (shGain, shGainStep, shTarget);

        // Long decays build up a lot of energy under sustained input: trim the wet a little as decay grows
        // (unity at the 4.5 s default, about -3 dB at 30 s).
        ramp (wetComp, wetCompStep, std::sqrt (1.225f / (1.0f + rt / 20.0f)));
        limRelease = 1.0f - std::exp (-1.0f / (0.2f * sr));

        agcAttack = onePoleA (1.0f / 0.004f, sr);
        agcRelease = 1.0f - std::exp (-1.0f / (0.25f * sr));
    }

    static inline void hadamard16 (float* x)
    {
        for (int h = 1; h < numLines; h <<= 1)
            for (int i = 0; i < numLines; i += h << 1)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = x[j], b = x[j + h];
                    x[j] = a + b;
                    x[j + h] = a - b;
                }
        for (int i = 0; i < numLines; ++i)
            x[i] *= 0.25f; // 1 / sqrt (16)
    }

    /** Transparent below |x| = 2, then saturates smoothly: a last-resort guard inside the loop. */
    static inline float lineGuard (float x)
    {
        const float a = std::abs (x);
        if (a <= 2.0f)
            return x;
        const float over = a - 2.0f;
        return std::copysign (2.0f + over / (1.0f + over), x);
    }

    void processChunk (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        float shimmerAcc = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < numLines; ++c)
            {
                len[c] += lenStep[c];
                gain[c] += gainStep[c];
                hfGain[c] += hfStep[c];
            }
            modDepth += modDepthStep;
            inGain += inGainStep;
            predelay += predelayStep;
            width += widthStep;
            shGain += shGainStep;

            // ---- input ----------------------------------------------------------------
            preL.push (inL[i]);
            preR.push (inR[i]);
            float l = preL.read (predelay) * inGain + retL;
            float r = preR.read (predelay) * inGain + retR;

            inHpZL += inHpA * (l - inHpZL);
            inHpZR += inHpA * (r - inHpZR);
            l -= inHpZL;
            r -= inHpZR;
            inLpZL += inLpA * (l - inLpZL);
            inLpZR += inLpA * (r - inLpZR);
            l = inLpZL;
            r = inLpZR;

            // ---- diffusion --------------------------------------------------------------
            float x[numLines];
            for (int c = 0; c < numLines; ++c)
                x[c] = (c & 1) ? r : l;

            for (int s = 0; s < numStages; ++s)
            {
                for (int c = 0; c < numLines; ++c)
                {
                    diffusers[s][c].push (x[c]);
                    const float d = diffusers[s][c].readInt (diffDelay[s][c]);
                    x[c] = flip[s][c] ? -d : d;
                }
                hadamard16 (x);
            }

            // ---- feedback network -----------------------------------------------------------
            float y[numLines];
            for (int c = 0; c < numLines; ++c)
            {
                const float s0 = lfoS[c], c0 = lfoC[c];
                lfoS[c] = s0 * rotC[c] + c0 * rotS[c];
                lfoC[c] = c0 * rotC[c] - s0 * rotS[c];

                const float v = lines[c].read (len[c] + modDepth * (1.0f + s0) + 2.0f);
                dampLp[c] += dampA * (v - dampLp[c]);
                y[c] = gain[c] * (dampLp[c] + hfGain[c] * (v - dampLp[c]));
            }

            float wl = 0.0f, wr = 0.0f;
            for (int c = 0; c < numLines; ++c)
            {
                wl += y[c] * outSignL[c];
                wr += y[c] * outSignR[c];
            }
            wl *= outScale;
            wr *= outScale;

            hadamard16 (y);
            for (int c = 0; c < numLines; ++c)
                lines[c].push (lineGuard (y[c] + x[c] * injectGain));

            // ---- shimmer loop ---------------------------------------------------------------
            {
                const float a = shLpL.lowpass (shHpL.highpass (wl));
                const float b = shLpR.lowpass (shHpR.highpass (wr));
                // cross the channels so each generation swaps sides: the sparkle drifts around the stereo field
                float pa = shifterL.process (b, jitterAmt);
                float pb = shifterR.process (a, jitterAmt);

                shelfL += shelfA * (pa - shelfL);
                shelfR += shelfA * (pb - shelfR);
                pa += sparkleBoost * (pa - shelfL);
                pb += sparkleBoost * (pb - shelfR);

                dcL += dcA * (pa - dcL);
                dcR += dcA * (pb - dcR);
                pa = (pa - dcL) * shGain;
                pb = (pb - dcR) * shGain;

                const float peak = std::max (std::abs (pa), std::abs (pb));
                agcEnv += (peak > agcEnv ? agcAttack : agcRelease) * (peak - agcEnv);
                const float agcTarget = agcEnv > agcThreshold ? agcThreshold / agcEnv : 1.0f;
                agcGain += (agcTarget < agcGain ? 0.02f : 0.0005f) * (agcTarget - agcGain);

                retL = dsp::undenormalise (dsp::softClip (pa * agcGain));
                retR = dsp::undenormalise (dsp::softClip (pb * agcGain));
                shimmerAcc += retL * retL + retR * retR;
            }

            // ---- output ---------------------------------------------------------------------
            wetComp += wetCompStep;
            const float mid = 0.5f * (wl + wr) * wetComp, side = 0.5f * (wl - wr) * width * wetComp;
            const float ol = outLpL.lowpass (outHpL.highpass (mid + side));
            const float orr = outLpR.lowpass (outHpR.highpass (mid - side));

            // Safety limiter on the wet signal (instant attack, smooth release): huge builds stay below 0 dBFS.
            const float pk = std::max (std::abs (ol), std::abs (orr));
            limEnv = pk > limEnv ? pk : limEnv + limRelease * (pk - limEnv);
            const float lg = limEnv > limThreshold ? limThreshold / limEnv : 1.0f;
            outL[i] = ol * lg;
            outR[i] = orr * lg;
        }
        limEnv = dsp::undenormalise (limEnv);

        for (int c = 0; c < numLines; ++c)
            dampLp[c] = dsp::undenormalise (dampLp[c]);
        inHpZL = dsp::undenormalise (inHpZL); inHpZR = dsp::undenormalise (inHpZR);
        inLpZL = dsp::undenormalise (inLpZL); inLpZR = dsp::undenormalise (inLpZR);
        shelfL = dsp::undenormalise (shelfL); shelfR = dsp::undenormalise (shelfR);
        dcL = dsp::undenormalise (dcL); dcR = dsp::undenormalise (dcR);

        shimmerSum += shimmerAcc * 0.5f;
        shimmerCount += n;
    }

    //==============================================================================
    struct Smoothed
    {
        float size = 0.65f, logRt = 1.5f, damping = 0.35f, mod = 0.4f, logLow = 4.9f, logHigh = 9.3f;
        float width = 1.0f, shimmer = 0.3f, sparkle = 0.4f, predelay = 800.0f;
    };

    float sr = 44100.0f;
    Params target;
    Smoothed sm;
    bool firstUpdate = false;
    int controlCountdown = 0;

    dsp::DelayLine preL, preR;
    dsp::DelayLine diffusers[numStages][numLines];
    int diffDelay[numStages][numLines] {};
    bool flip[numStages][numLines] {};

    dsp::DelayLine lines[numLines];
    float baseLen[numLines] {}, meanBaseLen = 1.7f;
    float len[numLines] {}, lenStep[numLines] {};
    float gain[numLines] {}, gainStep[numLines] {};
    float hfGain[numLines] {}, hfStep[numLines] {};
    float dampLp[numLines] {};
    float lfoRate[numLines] {}, lfoS[numLines] {}, lfoC[numLines] {}, rotC[numLines] {}, rotS[numLines] {};
    float dampA = 0.5f, modDepth = 0.0f, modDepthStep = 0.0f;
    float freezeAmt = 0.0f, inGain = 1.0f, inGainStep = 0.0f, predelay = 1.0f, predelayStep = 0.0f;
    float width = 1.0f, widthStep = 0.0f;

    float inHpA = 0.0f, inLpA = 1.0f, inHpZL = 0.0f, inHpZR = 0.0f, inLpZL = 0.0f, inLpZR = 0.0f;
    dsp::Svf outHpL, outHpR, outLpL, outLpR;

    GrainShifter shifterL, shifterR;
    dsp::Svf shHpL, shHpR, shLpL, shLpR;
    float shelfA = 0.3f, shelfL = 0.0f, shelfR = 0.0f, sparkleBoost = 0.0f, jitterAmt = 0.0f;
    float dcA = 0.0012f, dcL = 0.0f, dcR = 0.0f;
    float shGain = 0.0f, shGainStep = 0.0f;
    float agcEnv = 0.0f, agcGain = 1.0f, agcAttack = 0.1f, agcRelease = 0.0001f;
    static constexpr float agcThreshold = 0.45f;
    float retL = 0.0f, retR = 0.0f;
    float wetComp = 1.0f, wetCompStep = 0.0f, limEnv = 0.0f, limRelease = 0.0001f;
    static constexpr float limThreshold = 0.89f;
    float shimmerSum = 0.0f;
    int shimmerCount = 0;

    // Two orthogonal rows of the 16x16 Hadamard matrix -> decorrelated left/right outputs.
    static constexpr float outSignL[numLines] = { 1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, -1, -1, 1, -1, 1 };
    static constexpr float outSignR[numLines] = { 1, 1, -1, -1, 1, 1, -1, -1, -1, -1, 1, 1, -1, -1, 1, 1 };
};
} // namespace crystal
