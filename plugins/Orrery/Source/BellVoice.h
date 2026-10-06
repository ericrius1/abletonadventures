#pragma once

#include <aakit/AdventureKit.h>

/** The celestial bell: a small modal synthesiser. Each voice is a bank of damped complex
    resonators (one per vibration mode) that are "struck" on note-on. The Material control morphs
    the mode ratios/decays between a glass bar, a wooden marimba bar and a bronze bell. */
namespace orrery
{
struct BellSettings
{
    float material = 0.25f;    // 0 glass .. 0.5 wood .. 1 metal
    float brightness = 0.55f;  // mallet hardness, 0..1
    float decaySeconds = 2.5f; // ring time of the fundamental (T60)
};

class BellVoice
{
public:
    static constexpr int maxModes = 8;

    void prepare (float sampleRate)
    {
        sr = sampleRate;
        kill();
    }

    void kill()
    {
        active = false;
        released = false;
        fading = false;
        numModes = 0;
        note = -1;
        source = -1;
    }

    bool isActive() const noexcept { return active; }
    float getLevel() const noexcept { return active ? env * fade : 0.0f; }

    void start (int midiNote, float velocity01, float pan, const BellSettings& s, juce::uint32 newId, int newSource,
                aa::dsp::Rng& rng)
    {
        using namespace aa::dsp;

        note = midiNote;
        source = newSource;
        id = newId;
        active = true;
        released = false;
        fading = false;
        fade = 1.0f;
        fadeStep = 0.0f;
        numModes = 0;

        const float detuneCents = rng.nextBipolar() * 2.5f;
        const float f0 = midiToHz ((float) midiNote + detuneCents * 0.01f);
        const Table m = tableFor (s.material);

        const float v = clamp01 (velocity01);
        const float hardness = clamp01 (s.brightness * (0.62f + 0.38f * v) + 0.1f * (v - 0.5f));
        const float tilt = lerp (-1.9f, 0.12f, hardness);
        const float keyDecay = juce::jlimit (0.35f, 2.0f, std::pow (523.25f / f0, 0.4f));
        const float baseT60 = juce::jmax (0.05f, s.decaySeconds * m.decayScale * keyDecay);

        float ampSq = 0.0f;
        slowest = 0.0f;
        slowestReleased = 0.0f;

        auto addMode = [&] (float ratio, float amp, float t60)
        {
            const float f = f0 * ratio;
            if (numModes >= maxModes || f < 20.0f || f > sr * 0.45f || amp < 1.0e-4f)
                return;
            const float w = twoPi * f / sr;
            const float r = std::exp (-6.9078f / juce::jmax (1.0f, t60 * sr));
            const float relT60 = juce::jmin (t60, juce::jmax (0.28f, 0.45f * t60));
            const float rRel = std::exp (-6.9078f / juce::jmax (1.0f, relT60 * sr));
            const int k = numModes++;
            cosW[k] = std::cos (w);
            sinW[k] = std::sin (w);
            rRelease[k] = rRel;
            cr[k] = r * cosW[k];
            ci[k] = r * sinW[k];
            re[k] = amp;
            im[k] = 0.0f;
            ampSq += amp * amp;
            slowest = juce::jmax (slowest, r);
            slowestReleased = juce::jmax (slowestReleased, rRel);
        };

        for (int k = 0; k < 6; ++k)
        {
            const float ratio = m.ratio[k];
            const float amp = m.amp[k] * (ratio > 1.0f ? std::pow (ratio, tilt) : 1.0f);
            addMode (ratio, amp, baseT60 * m.decay[k]);
        }
        addMode (1.0f + m.doubletDetune, m.doubletAmp, baseT60 * 0.85f);                      // beating doublet
        addMode (m.tingRatio, 0.22f * hardness * hardness, 0.035f + 0.05f * hardness);          // strike "ting"

        // Loudness: velocity curve, gentle high-note taming, normalised mode energy.
        const float velGain = 0.1f + 0.9f * std::pow (v, 1.5f);
        const float keyGain = juce::jlimit (0.45f, 1.0f, std::pow (440.0f / f0, 0.22f)) * juce::jlimit (0.5f, 1.0f, f0 / 90.0f);
        const float norm = 0.2f * velGain * keyGain / std::sqrt (juce::jmax (1.0e-6f, ampSq));
        for (int k = 0; k < numModes; ++k)
            re[k] *= norm;

        // Mallet: harder = faster attack.
        const float attackMs = m.attackMs * lerp (2.4f, 0.45f, hardness);
        attackCoeff = 1.0f - std::exp (-1.0f / juce::jmax (1.0f, attackMs * 0.001f * sr));
        attack = 0.0f;

        // Woody "thock" of the mallet hitting the bar.
        noiseAmp = m.noise * 0.55f * norm * (0.4f + 0.6f * hardness);
        noiseEnvCoeff = std::exp (-6.9078f / juce::jmax (1.0f, 0.018f * sr));
        noiseEnv = noiseAmp > 0.0f ? 1.0f : 0.0f;
        noiseFilter.reset();
        noiseFilter.setCutoffQ (juce::jmin (f0 * 2.3f, sr * 0.4f), 1.3f, sr);
        noiseRng.seed (rng.nextU32());

        const float p = juce::jlimit (-1.0f, 1.0f, pan);
        const float angle = (p + 1.0f) * pi * 0.25f;
        gainL = std::cos (angle) * 1.41421356f;
        gainR = std::sin (angle) * 1.41421356f;

        env = 1.0f;
    }

    /** Note-off: the bar keeps ringing but with a shorter, damped decay. */
    void release()
    {
        if (! active || released)
            return;
        released = true;
        for (int k = 0; k < numModes; ++k)
        {
            cr[k] = rRelease[k] * cosW[k];
            ci[k] = rRelease[k] * sinW[k];
        }
        slowest = slowestReleased;
    }

    /** Quick click-free fade (voice stealing, all-sound-off). */
    void fadeOut (float seconds)
    {
        if (! active)
            return;
        fading = true;
        fadeStep = juce::jmax (fadeStep, 1.0f / juce::jmax (1.0f, seconds * sr));
    }

    void render (float* left, float* right, int numSamples)
    {
        if (! active)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            float s = 0.0f;
            for (int k = 0; k < numModes; ++k)
            {
                const float nr = re[k] * cr[k] - im[k] * ci[k];
                const float ni = re[k] * ci[k] + im[k] * cr[k];
                re[k] = nr;
                im[k] = ni;
                s += ni;
            }

            if (noiseEnv > 0.0f)
            {
                s += noiseFilter.bandpass (noiseRng.nextBipolar()) * noiseAmp * noiseEnv;
                noiseEnv *= noiseEnvCoeff;
                if (noiseEnv < 1.0e-4f)
                    noiseEnv = 0.0f;
            }

            attack += (1.0f - attack) * attackCoeff;
            s *= attack * fade;
            left[i] += s * gainL;
            right[i] += s * gainR;

            env *= slowest;
            if (fading)
            {
                fade -= fadeStep;
                if (fade <= 0.0f)
                {
                    kill();
                    return;
                }
            }
        }

        if (env < 2.0e-4f)
            kill();
    }

    int note = -1;
    int source = -1;          // orbit index, or -1 for notes played from the keyboard
    juce::uint32 id = 0;
    bool released = false, fading = false;

private:
    struct Table
    {
        float ratio[6], amp[6], decay[6];
        float decayScale, attackMs, doubletDetune, doubletAmp, tingRatio, noise;
    };

    static Table lerpTable (const Table& a, const Table& b, float t)
    {
        Table r;
        auto l = [t] (float x, float y) { return x + (y - x) * t; };
        for (int k = 0; k < 6; ++k)
        {
            r.ratio[k] = l (a.ratio[k], b.ratio[k]);
            r.amp[k] = l (a.amp[k], b.amp[k]);
            r.decay[k] = l (a.decay[k], b.decay[k]);
        }
        r.decayScale = l (a.decayScale, b.decayScale);
        r.attackMs = l (a.attackMs, b.attackMs);
        r.doubletDetune = l (a.doubletDetune, b.doubletDetune);
        r.doubletAmp = l (a.doubletAmp, b.doubletAmp);
        r.tingRatio = l (a.tingRatio, b.tingRatio);
        r.noise = l (a.noise, b.noise);
        return r;
    }

    static Table tableFor (float material)
    {
        // Glass bar: sweet, nearly harmonic with a free-bar mode for sparkle.
        static const Table glass { { 1.0f, 2.0f, 2.76f, 4.07f, 5.40f, 6.98f },
                                   { 1.0f, 0.40f, 0.24f, 0.13f, 0.08f, 0.05f },
                                   { 1.0f, 0.78f, 0.55f, 0.42f, 0.33f, 0.25f },
                                   1.0f, 1.1f, 0.0009f, 0.36f, 9.2f, 0.0f };
        // Marimba bar: tuned 1:4:10 overtones that die quickly, short body.
        static const Table wood { { 1.0f, 3.99f, 9.6f, 2.0f, 6.3f, 13.1f },
                                  { 1.0f, 0.62f, 0.34f, 0.06f, 0.07f, 0.05f },
                                  { 1.0f, 0.30f, 0.12f, 0.5f, 0.15f, 0.06f },
                                  0.3f, 0.55f, 0.0015f, 0.1f, 6.6f, 1.0f };
        // Bronze bell: slightly stretched partials, a low hum and long shimmering decay.
        static const Table metal { { 1.0f, 2.0f, 2.92f, 4.16f, 5.43f, 0.5f },
                                   { 1.0f, 0.58f, 0.38f, 0.3f, 0.2f, 0.22f },
                                   { 1.0f, 0.86f, 0.72f, 0.6f, 0.5f, 1.2f },
                                   1.45f, 0.35f, 0.0028f, 0.45f, 11.4f, 0.0f };

        material = aa::dsp::clamp01 (material);
        if (material < 0.5f)
            return lerpTable (glass, wood, material * 2.0f);
        return lerpTable (wood, metal, (material - 0.5f) * 2.0f);
    }

    float sr = 44100.0f;
    bool active = false;
    int numModes = 0;
    float re[maxModes] {}, im[maxModes] {}, cr[maxModes] {}, ci[maxModes] {};
    float cosW[maxModes] {}, sinW[maxModes] {}, rRelease[maxModes] {};
    float slowest = 0.0f, slowestReleased = 0.0f, env = 0.0f;
    float attack = 0.0f, attackCoeff = 1.0f;
    float fade = 1.0f, fadeStep = 0.0f;
    float gainL = 1.0f, gainR = 1.0f;
    float noiseAmp = 0.0f, noiseEnv = 0.0f, noiseEnvCoeff = 0.0f;
    aa::dsp::Svf noiseFilter;
    aa::dsp::Rng noiseRng;
};
} // namespace orrery
