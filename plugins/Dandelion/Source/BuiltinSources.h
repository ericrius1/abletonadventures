#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <aakit/dsp/Core.h>
#include <thread>

namespace dandelion
{
/** A source sound for the grain engine. Immutable once created, so the audio thread can read it freely. */
struct SampleData
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 48000.0;
    int rootNote = 60;
    juce::String name;
};

/** Table sine, phase in cycles (any value). Plenty accurate for generating source material. */
inline float tableSin (double phaseCycles)
{
    static const auto table = []
    {
        std::array<float, 4097> t {};
        for (size_t i = 0; i < t.size(); ++i)
            t[i] = (float) std::sin (juce::MathConstants<double>::twoPi * (double) i / 4096.0);
        return t;
    }();
    const double p = (phaseCycles - std::floor (phaseCycles)) * 4096.0;
    const int i = (int) p;
    const float f = (float) (p - (double) i);
    return table[(size_t) i] + (table[(size_t) i + 1] - table[(size_t) i]) * f;
}

/** Four procedurally generated sources, so Dandelion sounds lovely before you feed it anything.
    Shared between plugin instances (they're a few MB) and generated on a background thread so
    plugin scanning and instantiation stay instant. */
struct BuiltinBank
{
    static constexpr int count = 4;
    static constexpr double rate = 48000.0;
    std::array<SampleData, count> sources;
    std::atomic<bool> ready { false };

    BuiltinBank()
    {
        worker = std::thread ([this]
        {
            makeGlassChoir (sources[0]);
            if (cancelled()) return;
            makeMusicBox (sources[1]);
            if (cancelled()) return;
            makeWindChimes (sources[2]);
            if (cancelled()) return;
            makeVelvetStrings (sources[3]);
            ready.store (! cancelled());
        });
    }

    ~BuiltinBank()
    {
        cancel.store (true);
        if (worker.joinable())
            worker.join();
    }

    static const juce::StringArray& names()
    {
        static const juce::StringArray n { "Glass Choir", "Music Box", "Wind Chimes", "Velvet Strings" };
        return n;
    }

private:
    std::thread worker;
    std::atomic<bool> cancel { false };
    bool cancelled() const { return cancel.load(); }

    static void prepare (SampleData& s, const juce::String& name, double seconds)
    {
        s.name = name;
        s.sampleRate = rate;
        s.rootNote = 60;
        s.buffer.setSize (2, (int) (seconds * rate));
        s.buffer.clear();
    }

    static void normalise (SampleData& s)
    {
        const float peak = s.buffer.getMagnitude (0, s.buffer.getNumSamples());
        if (peak > 0.0f)
            s.buffer.applyGain (0.9f / peak);
        // gentle fades so loops never click
        const int fade = (int) (0.02 * rate);
        s.buffer.applyGainRamp (0, fade, 0.0f, 1.0f);
        s.buffer.applyGainRamp (s.buffer.getNumSamples() - fade, fade, 1.0f, 0.0f);
    }

    static float hz (float note) { return aa::dsp::midiToHz (note); }

    // Breathy choir chord: detuned sines with formant-ish partial weights and air.
    static void makeGlassChoir (SampleData& s)
    {
        prepare (s, "Glass Choir", 6.0);
        aa::dsp::Rng rng (7);
        const float chord[] = { 48.0f, 55.0f, 60.0f, 64.0f, 67.0f, 71.0f };
        auto* l = s.buffer.getWritePointer (0);
        auto* r = s.buffer.getWritePointer (1);
        const int n = s.buffer.getNumSamples();
        aa::dsp::OnePole air;
        air.setCutoff (3500.0f, (float) rate);
        for (int v = 0; v < 6; ++v)
        {
            for (int d = 0; d < 3; ++d)
            {
                const float detune = (float) (d - 1) * 0.07f + rng.nextBipolar() * 0.02f;
                const float f0 = hz (chord[v] + detune);
                const float pan = 0.5f + 0.4f * rng.nextBipolar();
                const float vibRate = 4.5f + rng.next01() * 1.5f;
                double phase[6] {};
                for (int i = 0; i < n; ++i)
                {
                    const double t = (double) i / rate;
                    const float vib = 1.0f + 0.004f * tableSin (vibRate * t + (float) v * 0.159);
                    const float swell = 0.6f + 0.4f * tableSin (0.17f * t + (float) d * 0.159);
                    float sample = 0.0f;
                    for (int h = 0; h < 6; ++h)
                    {
                        const float partial = (float) (h + 1);
                        const float weight = h == 0 ? 1.0f : (h == 2 || h == 3 ? 0.35f : 0.15f) / partial;
                        phase[h] += (double) (f0 * partial * vib / (float) rate);
                        sample += weight * tableSin (phase[h]);
                    }
                    sample *= swell * 0.05f;
                    l[i] += sample * (1.0f - pan);
                    r[i] += sample * pan;
                }
            }
        }
        for (int i = 0; i < n; ++i)
        {
            const float breath = air.lowpass (rng.nextBipolar()) * 0.05f;
            l[i] += breath;
            r[i] += breath;
        }
        normalise (s);
    }

    // FM music-box notes on a pentatonic arpeggio.
    static void makeMusicBox (SampleData& s)
    {
        prepare (s, "Music Box", 6.0);
        const float notes[] = { 72, 76, 79, 84, 81, 79, 76, 74, 72, 79, 84, 88, 86, 84, 79, 76 };
        auto* l = s.buffer.getWritePointer (0);
        auto* r = s.buffer.getWritePointer (1);
        const int n = s.buffer.getNumSamples();
        const int step = (int) (0.36 * rate);
        for (int k = 0; k < 16; ++k)
        {
            const int start = k * step;
            const float f = hz (notes[k]);
            const float pan = 0.5f + 0.35f * std::sin ((float) k * 1.7f);
            const float envMul = std::exp (-2.6f / (float) rate), modMul = std::exp (-9.0f / (float) rate);
            float env = 1.0f, modEnv = 1.0f;
            for (int i = start; i < n && env > 0.0005f; ++i)
            {
                const double t = (double) (i - start) / rate;
                const float mod = tableSin (f * 3.5f * t) * 2.2f * modEnv;
                const float v = tableSin (f * t + mod * 0.159f) * env * 0.4f
                                + tableSin (f * 2.0f * t) * env * env * 0.12f;
                l[i] += v * (1.0f - pan);
                r[i] += v * pan;
                env *= envMul;
                modEnv *= modMul;
            }
        }
        normalise (s);
    }

    // Inharmonic tubular chimes struck at random.
    static void makeWindChimes (SampleData& s)
    {
        prepare (s, "Wind Chimes", 6.0);
        aa::dsp::Rng rng (99);
        const float tubes[] = { 72, 74, 77, 79, 81, 84, 86 };
        const float ratios[] = { 1.0f, 2.756f, 5.404f, 8.933f };
        auto* l = s.buffer.getWritePointer (0);
        auto* r = s.buffer.getWritePointer (1);
        const int n = s.buffer.getNumSamples();
        for (int k = 0; k < 22; ++k)
        {
            const int start = (int) (rng.next01() * (float) (n - rate * 0.5));
            const float f = hz (tubes[rng.nextInt (7)]);
            const float pan = rng.next01();
            const float strike = 0.4f + 0.6f * rng.next01();
            float decay[4], mul[4];
            for (int p = 0; p < 4; ++p)
            {
                decay[p] = strike * 0.3f / (float) (p + 1);
                mul[p] = std::exp (-(1.2f + 2.4f * (float) p) / (float) rate);
            }
            const int end = std::min (n, start + (int) (5.0 * rate));
            for (int i = start; i < end; ++i)
            {
                const double t = (double) (i - start) / rate;
                float v = 0.0f;
                for (int p = 0; p < 4; ++p)
                {
                    v += tableSin (f * ratios[p] * t) * decay[p];
                    decay[p] *= mul[p];
                }
                l[i] += v * (1.0f - pan);
                r[i] += v * pan;
            }
        }
        normalise (s);
    }

    // Warm detuned saw ensemble, low-passed.
    static void makeVelvetStrings (SampleData& s)
    {
        prepare (s, "Velvet Strings", 6.0);
        aa::dsp::Rng rng (3);
        const float chord[] = { 48.0f, 52.0f, 55.0f, 60.0f, 64.0f };
        auto* l = s.buffer.getWritePointer (0);
        auto* r = s.buffer.getWritePointer (1);
        const int n = s.buffer.getNumSamples();
        for (int v = 0; v < 5; ++v)
        {
            for (int d = 0; d < 4; ++d)
            {
                aa::dsp::BlepOsc osc;
                osc.phase = rng.next01();
                aa::dsp::Svf lp;
                lp.setCutoffRes (1800.0f + 400.0f * (float) d, 0.1f, (float) rate);
                const float f = hz (chord[v] + rng.nextBipolar() * 0.12f);
                const float pan = rng.next01();
                const float vibRate = 5.0f + rng.next01();
                for (int i = 0; i < n; ++i)
                {
                    const double t = (double) i / rate;
                    const float dt = f * (1.0f + 0.003f * tableSin (vibRate * t)) / (float) rate;
                    const float x = lp.lowpass (osc.saw (dt)) * 0.05f;
                    osc.advance (dt);
                    l[i] += x * (1.0f - pan);
                    r[i] += x * pan;
                }
            }
        }
        normalise (s);
    }
};
} // namespace dandelion
