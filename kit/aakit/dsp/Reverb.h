#pragma once

#include "Core.h"

namespace aa::dsp
{
/** A lush 8-channel feedback-delay-network reverb.

    Signal flow: pre-delay -> 4 Hadamard diffusion stages -> 8-line FDN with Householder mixing,
    per-line damping and slow modulation -> decorrelated stereo out. Output is wet only. */
class FdnReverb
{
public:
    static constexpr int numChannels = 8;

    struct Params
    {
        float size = 0.6f;          // 0..1 room size
        float decaySeconds = 3.0f;  // RT60
        float damping = 0.4f;       // 0..1 high-frequency absorption
        float predelayMs = 15.0f;
        float modDepth = 0.35f;     // 0..1
        float modRate = 0.4f;       // Hz
        float width = 1.0f;         // 0..1
        float lowCutHz = 90.0f;
        bool freeze = false;
    };

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        const int maxPre = (int) (sr * 0.5f) + 8;
        preL.prepare (maxPre);
        preR.prepare (maxPre);

        // Diffusion stages: lengths roughly double each stage (total ~ 70 ms).
        Rng rng (0xC0FFEEu);
        float stageMs = 5.0f;
        for (int s = 0; s < numDiffusers; ++s)
        {
            for (int c = 0; c < numChannels; ++c)
            {
                const float lo = stageMs * (float) c / numChannels;
                const float hi = stageMs * (float) (c + 1) / numChannels;
                const float ms = lo + (hi - lo) * rng.next01();
                diffDelaySamples[s][c] = std::max (1, (int) (ms * 0.001f * sr));
                diffusers[s][c].prepare (diffDelaySamples[s][c] + 2);
                flip[s][c] = rng.chance (0.5f);
            }
            stageMs *= 2.0f;
        }

        // Feedback lines: up to ~300 ms at max size + modulation headroom.
        for (int c = 0; c < numChannels; ++c)
        {
            lines[c].prepare ((int) (sr * 0.35f) + 64);
            lfoPhase[c] = (float) c / numChannels;
        }

        lowCutL.reset(); lowCutR.reset();
        setParams (params);
        reset();
    }

    void reset()
    {
        preL.clear(); preR.clear();
        for (auto& stage : diffusers)
            for (auto& d : stage)
                d.clear();
        for (auto& l : lines)
            l.clear();
        for (auto& f : dampState)
            f = 0.0f;
        lowCutL.reset(); lowCutR.reset();
    }

    void setParams (const Params& p)
    {
        params = p;
        const float sizeMul = 0.25f + 0.75f * clamp01 (p.size);
        // Exponentially spread base lengths between 45 ms and 160 ms (scaled by size).
        for (int c = 0; c < numChannels; ++c)
        {
            const float t = (float) c / (numChannels - 1);
            const float ms = 45.0f * std::pow (160.0f / 45.0f, t) * sizeMul;
            lineLength[c] = ms * 0.001f * sr;

            const float rt = std::max (0.05f, p.decaySeconds);
            lineGain[c] = p.freeze ? 1.0f : std::pow (10.0f, -3.0f * (lineLength[c] / sr) / rt);
        }

        // Damping one-pole coefficient (cutoff from ~18 kHz down to ~1.2 kHz).
        const float dampHz = p.freeze ? 20000.0f : 18000.0f * std::pow (1200.0f / 18000.0f, clamp01 (p.damping));
        dampCoeff = 1.0f - std::exp (-twoPi * std::min (dampHz, sr * 0.45f) / sr);

        modSamples = p.modDepth * 0.0025f * sr; // up to ~2.5 ms wobble
        preDelaySamples = std::max (1.0f, p.predelayMs * 0.001f * sr);
        lowCutL.setCutoff (p.lowCutHz, sr);
        lowCutR.setCutoff (p.lowCutHz, sr);
    }

    /** Process one stereo sample; writes wet output. */
    inline void processSample (float inL, float inR, float& outL, float& outR)
    {
        const float inputGain = params.freeze ? 0.0f : 1.0f;
        preL.push (inL * inputGain);
        preR.push (inR * inputGain);
        const float l = preL.read (preDelaySamples);
        const float r = preR.read (preDelaySamples);

        float x[numChannels];
        for (int c = 0; c < numChannels; ++c)
            x[c] = (c & 1) ? r : l;

        // Diffusion
        for (int s = 0; s < numDiffusers; ++s)
        {
            for (int c = 0; c < numChannels; ++c)
            {
                diffusers[s][c].push (x[c]);
                const float d = diffusers[s][c].readInt (diffDelaySamples[s][c]);
                x[c] = flip[s][c] ? -d : d;
            }
            hadamard8 (x);
        }

        // Feedback network
        float y[numChannels];
        const float lfoInc = params.modRate / sr;
        for (int c = 0; c < numChannels; ++c)
        {
            lfoPhase[c] += lfoInc * (1.0f + 0.13f * (float) c);
            if (lfoPhase[c] >= 1.0f) lfoPhase[c] -= 1.0f;
            const float mod = modSamples * (0.5f + 0.5f * fastSin (lfoPhase[c]));
            float v = lines[c].read (lineLength[c] + mod + 2.0f);
            dampState[c] += dampCoeff * (v - dampState[c]);
            dampState[c] = undenormalise (dampState[c]);
            y[c] = dampState[c] * lineGain[c];
        }

        // Householder reflection: y - 2/N * sum(y)
        float sum = 0.0f;
        for (int c = 0; c < numChannels; ++c)
            sum += y[c];
        sum *= 2.0f / numChannels;

        for (int c = 0; c < numChannels; ++c)
            lines[c].push (undenormalise (y[c] - sum + x[c] * 0.5f));

        // Stereo out
        float wl = 0.0f, wr = 0.0f;
        for (int c = 0; c < numChannels; ++c)
        {
            wl += y[c] * outSignL[c];
            wr += y[c] * outSignR[c];
        }
        wl *= 0.35f;
        wr *= 0.35f;

        const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * params.width;
        outL = lowCutL.highpass (mid + side);
        outR = lowCutR.highpass (mid - side);
    }

    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            processSample (inL[i], inR[i], outL[i], outR[i]);
    }

private:
    static constexpr int numDiffusers = 4;

    static inline float fastSin (float phase01)
    {
        // parabolic sine approximation, phase in [0,1)
        const float x = phase01 * 2.0f - 1.0f;           // -1..1
        return 4.0f * x * (1.0f - std::abs (x)) * -1.0f; // ~ -sin(pi*x)
    }

    static inline void hadamard8 (float* x)
    {
        for (int h = 1; h < numChannels; h <<= 1)
            for (int i = 0; i < numChannels; i += h << 1)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = x[j], b = x[j + h];
                    x[j] = a + b;
                    x[j + h] = a - b;
                }
        constexpr float norm = 0.35355339f; // 1/sqrt(8)
        for (int i = 0; i < numChannels; ++i)
            x[i] *= norm;
    }

    float sr = 44100.0f;
    Params params;
    DelayLine preL, preR;
    DelayLine diffusers[numDiffusers][numChannels];
    int diffDelaySamples[numDiffusers][numChannels] {};
    bool flip[numDiffusers][numChannels] {};
    DelayLine lines[numChannels];
    float lineLength[numChannels] {}, lineGain[numChannels] {}, dampState[numChannels] {}, lfoPhase[numChannels] {};
    float dampCoeff = 0.5f, modSamples = 0.0f, preDelaySamples = 1.0f;
    OnePole lowCutL, lowCutR;

    static constexpr float outSignL[numChannels] = { 1, -1, 1, 1, -1, 1, -1, -1 };
    static constexpr float outSignR[numChannels] = { 1, 1, -1, 1, 1, -1, -1, 1 };
};

//==============================================================================
/** Stereo chorus / ensemble (two modulated taps per side). */
class StereoChorus
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        dl.prepare ((int) (sr * 0.06f));
        dr.prepare ((int) (sr * 0.06f));
    }
    void reset() { dl.clear(); dr.clear(); }

    /** depth 0..1, rate Hz, mix 0..1 */
    void process (float* left, float* right, int n, float rate, float depth, float mix)
    {
        const float baseMs = 12.0f, depthMs = 2.0f + 6.0f * depth;
        for (int i = 0; i < n; ++i)
        {
            phase += rate / sr;
            if (phase >= 1.0f) phase -= 1.0f;
            const float s1 = std::sin (twoPi * phase);
            const float s2 = std::sin (twoPi * (phase + 0.33f));
            const float s3 = std::sin (twoPi * (phase + 0.66f));

            const float inL = left[i], inR = right[i];
            dl.push (inL);
            dr.push (inR);
            const float msToS = sr * 0.001f;
            const float wl = 0.5f * (dl.read ((baseMs + depthMs * s1) * msToS) + dr.read ((baseMs + depthMs * s3) * msToS));
            const float wr = 0.5f * (dr.read ((baseMs + depthMs * s2) * msToS) + dl.read ((baseMs + depthMs * -s1) * msToS));
            left[i] = inL + (wl - inL) * mix * 0.7f;
            right[i] = inR + (wr - inR) * mix * 0.7f;
        }
    }

private:
    float sr = 44100.0f, phase = 0.0f;
    DelayLine dl, dr;
};
} // namespace aa::dsp
