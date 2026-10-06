#pragma once

#include <aakit/AdventureKit.h>

/** The eight drum voices of Critter Kit. Header-only, allocation-free, audio-thread safe. */
namespace critter
{
constexpr int numVoices = 8;
constexpr int numSteps = 16;
constexpr int firstNote = 36;   // C1: bottom-left pad of an Ableton Drum Rack / Push grid
constexpr int maxChunk = 256;   // internal render chunk (any host block size is split into these)
constexpr double maxSampleSeconds = 10.0;

enum Kind { kick = 0, snare, clap, closedHat, openHat, tom, cowbell, zap };

inline const char* roleName (int v)
{
    static const char* names[] = { "Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Tom", "Cowbell", "Zap" };
    return names[juce::jlimit (0, numVoices - 1, v)];
}

inline const char* critterName (int v)
{
    static const char* names[] = { "Bumble", "Snappy", "Clappo", "Tiki", "Tsss", "Boomer", "Clink", "Zappy" };
    return names[juce::jlimit (0, numVoices - 1, v)];
}

//==============================================================================
/** Audio a critter has eaten. Immutable once it has been handed to the audio thread. */
struct SampleData
{
    juce::AudioBuffer<float> audio;     // 1 or 2 channels at sourceRate, at most maxSampleSeconds long
    double sourceRate = 44100.0;
    juce::String filePath, displayName;
    juce::MemoryBlock embedded;         // compressed copy that travels inside the plugin state
    juce::String embeddedFormat;        // "flac" or "raw16"
    std::array<float, 40> overview {};  // peak envelope for the little badge in the UI

    int numFrames() const noexcept { return audio.getNumSamples(); }
};

/** One critter's settings for the current block, already converted to DSP units. */
struct VoiceSettings
{
    float tune = 0.0f;        // semitones (voice tune + kit pitch)
    float decay = 0.3f;       // seconds
    float decayNorm = 0.5f;   // knob position 0..1 (sample playback uses it to detect "full length")
    float tone = 0.5f;        // 0..1
    float snap = 0.5f;        // 0..1
    float level = 1.0f;       // linear gain
    float pan = 0.0f;         // -1..1
};

//==============================================================================
inline float t60Coeff (float seconds, float sr) { return std::exp (-6.9078f / std::max (1.0f, seconds * sr)); }
inline float tauCoeff (float seconds, float sr) { return std::exp (-1.0f / std::max (1.0f, seconds * sr)); }
inline float sin01 (float phase) { return std::sin (aa::dsp::twoPi * phase); }
inline float wrap01 (float p) { return p >= 1.0f ? p - std::floor (p) : p; }

inline float blepSquare (float phase, float dt)
{
    float v = phase < 0.5f ? 1.0f : -1.0f;
    v += aa::dsp::BlepOsc::polyBlep (phase, dt);
    float t2 = phase - 0.5f;
    if (t2 < 0.0f)
        t2 += 1.0f;
    v -= aa::dsp::BlepOsc::polyBlep (t2, dt);
    return v;
}

/** Triangle that starts at zero (phase 0) and rises, so voices start without a click. */
inline float tri01 (float phase)
{
    float p = phase + 0.25f;
    if (p >= 1.0f)
        p -= 1.0f;
    return 1.0f - 4.0f * std::abs (p - 0.5f);
}

/** Velocity (0..1) to gain: soft at the bottom, full at the top. */
inline float velocityGain (float v)
{
    v = aa::dsp::clamp01 (v);
    return 0.12f + 0.88f * v * std::sqrt (v);
}

//==============================================================================
class Voice
{
public:
    void prepare (float sampleRate, int kindIndex)
    {
        sr = sampleRate;
        kind = kindIndex;
        rng.seed (0x51ED270Bu + (uint32_t) kindIndex * 7919u);
        levelSm.reset (sr, 0.02, 1.0f);
        panSm.reset (sr, 0.03, 0.0f);
        tailCoeff = tauCoeff (0.0015f, sr);
        smoothCoeff = 1.0f - tauCoeff (0.01f, sr);
        reset();
    }

    void reset()
    {
        for (auto& s : slots)
            s = Slot {};
        tailL = tailR = 0.0f;
        current = 0;
        first = true;
    }

    bool isActive() const noexcept { return slots[0].active || slots[1].active || tailL != 0.0f || tailR != 0.0f; }

    /** Block-rate update: level/pan targets plus live decay/tone changes for ringing hits. */
    void update (const VoiceSettings& s)
    {
        if (first)
        {
            levelSm.snap (s.level);
            panSm.snap (s.pan);
            first = false;
        }
        levelSm.setTarget (s.level);
        panSm.setTarget (s.pan);

        const bool changed = std::abs (s.decay - live.decay) > 1.0e-4f || std::abs (s.tone - live.tone) > 1.0e-4f
                             || std::abs (s.decayNorm - live.decayNorm) > 1.0e-4f;
        live = s;
        if (changed)
            for (auto& slot : slots)
                if (slot.active)
                {
                    slot.decay = s.decay;
                    slot.decayNorm = s.decayNorm;
                    slot.tone = s.tone;
                    configure (slot, false);
                }
    }

    void trigger (float velocity, float detuneSemis, const SampleData* sample)
    {
        // Retrigger: the ringing hit gets a short fade while the new one starts in the other slot.
        auto& old = slots[(size_t) current];
        if (old.active && old.fadeStep <= 0.0f)
            old.fadeStep = 1.0f / (0.004f * sr);

        current ^= 1;
        auto& s = slots[(size_t) current];
        if (s.active)
            killToTail (s);

        s = Slot {};
        s.active = true;
        s.vel = velocityGain (velocity);
        s.velRaw = aa::dsp::clamp01 (velocity);
        s.ratio = aa::dsp::semitonesToRatio (live.tune + detuneSemis);
        s.decay = live.decay;
        s.decayNorm = live.decayNorm;
        s.tone = live.tone;
        s.snap = live.snap;
        s.sample = sample;
        s.isSample = sample != nullptr && sample->numFrames() > 4;
        configure (s, true);
    }

    /** Fast fade of everything that's ringing (hat choke, all-sound-off). */
    void choke (float seconds)
    {
        for (auto& s : slots)
            if (s.active)
            {
                const float step = 1.0f / std::max (1.0f, seconds * sr);
                s.fadeStep = std::max (s.fadeStep, step);
            }
    }

    /** Adds this voice into outL/outR. `sample` is the sample owned by the critter for this block. */
    void render (float* outL, float* outR, int n, const SampleData* sample)
    {
        jassert (n <= maxChunk);

        // per-sample level/pan gains
        if (levelSm.isSmoothing() || panSm.isSmoothing())
        {
            for (int i = 0; i < n; ++i)
            {
                const float lv = levelSm.next();
                const float pv = panSm.next();
                const float a = (pv + 1.0f) * aa::dsp::pi * 0.25f;
                gainL[i] = lv * std::cos (a) * 1.41421356f;
                gainR[i] = lv * std::sin (a) * 1.41421356f;
            }
        }
        else
        {
            const float lv = levelSm.current, pv = panSm.current;
            const float a = (pv + 1.0f) * aa::dsp::pi * 0.25f;
            const float gl = lv * std::cos (a) * 1.41421356f, gr = lv * std::sin (a) * 1.41421356f;
            for (int i = 0; i < n; ++i)
            {
                gainL[i] = gl;
                gainR[i] = gr;
            }
        }

        for (auto& s : slots)
        {
            if (! s.active)
                continue;

            if (s.isSample)
            {
                if (s.sample != sample || sample == nullptr)
                {
                    killToTail (s); // the critter was fed something else (or went back to synth)
                    continue;
                }
                renderSample (s, *sample, tmpL, tmpR, n);
                mix (s, tmpL, tmpR, outL, outR, n);
            }
            else
            {
                renderSynth (s, tmpL, n);
                mix (s, tmpL, tmpL, outL, outR, n);
            }
        }

        if (tailL != 0.0f || tailR != 0.0f)
        {
            for (int i = 0; i < n; ++i)
            {
                outL[i] += tailL;
                outR[i] += tailR;
                tailL *= tailCoeff;
                tailR *= tailCoeff;
            }
            if (std::abs (tailL) < 1.0e-6f && std::abs (tailR) < 1.0e-6f)
                tailL = tailR = 0.0f;
        }
    }

private:
    struct Slot
    {
        bool active = false, isSample = false, useEnv = true;
        int age = 0, hold = 0, counter = 0, burst = 0, spacing = 1;
        float vel = 1.0f, velRaw = 1.0f, fade = 1.0f, fadeStep = 0.0f;
        float lastL = 0.0f, lastR = 0.0f;
        float ratio = 1.0f, decay = 0.3f, decayNorm = 0.5f, tone = 0.5f, snap = 0.5f;

        float ph[6] {};
        float dt[6] {};
        float amp = 0.0f, ampC = 0.0f;
        float e2 = 0.0f, e2C = 0.0f, e3 = 0.0f, e3C = 0.0f, e4 = 0.0f, e4C = 0.0f;
        float c[10] {};
        float lp = 0.0f;
        aa::dsp::Svf f1, f2, f3;

        const SampleData* sample = nullptr; // compared against, never dereferenced across blocks
        double pos = 0.0, inc = 1.0;
        float fadeIn = 1.0f, fadeInStep = 1.0f;
        int filterMode = 0;
    };

    //==========================================================================
    void killToTail (Slot& s)
    {
        tailL += s.lastL;
        tailR += s.lastR;
        s.active = false;
    }

    void mix (Slot& s, const float* srcL, const float* srcR, float* outL, float* outR, int n)
    {
        const float v = s.vel;
        float fade = s.fade;
        const float step = s.fadeStep;
        float l = 0.0f, r = 0.0f;
        int i = 0;
        for (; i < n; ++i)
        {
            if (step > 0.0f)
            {
                fade -= step;
                if (fade <= 0.0f)
                {
                    fade = 0.0f;
                    s.active = false;
                    l = r = 0.0f;
                    break;
                }
            }
            const float g = v * fade;
            l = srcL[i] * g * gainL[i];
            r = srcR[i] * g * gainR[i];
            outL[i] += l;
            outR[i] += r;
        }
        s.fade = fade;
        s.lastL = l;
        s.lastR = r;
    }

    //==========================================================================
    /** Sets up the coefficients for a hit. fresh = new trigger (reset state), otherwise a live
        update of decay/tone while the hit rings. */
    void configure (Slot& s, bool fresh)
    {
        const float tone = s.tone, snap = s.snap, decay = s.decay, ratio = s.ratio;

        if (s.isSample)
        {
            s.useEnv = s.decayNorm < 0.985f;
            s.ampC = t60Coeff (decay * 1.6f, sr);

            // Tone: below the middle closes a low-pass, above it opens a high-pass, centre = untouched.
            if (tone < 0.45f)
            {
                s.filterMode = 1;
                const float t = (0.45f - tone) / 0.45f;
                s.f1.setCutoffQ (20000.0f * std::pow (0.01f, t), 0.75f, sr);
            }
            else if (tone > 0.55f)
            {
                s.filterMode = 2;
                const float t = (tone - 0.55f) / 0.45f;
                s.f1.setCutoffQ (20.0f * std::pow (100.0f, t), 0.75f, sr);
            }
            else
            {
                s.filterMode = 0;
                s.f1.setCutoffQ (20000.0f, 0.75f, sr);
            }
            s.f2.g = s.f1.g; s.f2.k = s.f1.k; s.f2.a1 = s.f1.a1; s.f2.a2 = s.f1.a2; s.f2.a3 = s.f1.a3;

            if (fresh && s.sample != nullptr)
            {
                const auto& smp = *s.sample;
                s.inc = smp.sourceRate / (double) sr * (double) ratio;
                s.pos = 0.0;
                s.fadeIn = 1.0f;
                s.fadeInStep = 1.0f;
                if (snap < 0.45f)
                {
                    // Snap below the middle skips into the sample (trim silence, chop into a hit).
                    const double t = (0.45 - snap) / 0.45;
                    const double maxOffset = std::min (0.3 * smp.numFrames(), 0.5 * smp.sourceRate);
                    s.pos = t * t * maxOffset;
                    s.fadeIn = 0.0f;
                    s.fadeInStep = 1.0f / (0.0015f * sr);
                }
                s.c[1] = snap > 0.55f ? (snap - 0.55f) / 0.45f * 2.2f : 0.0f; // transient boost
                s.e4 = 1.0f;
                s.e4C = tauCoeff (0.012f, sr);
                s.amp = 1.0f;
            }
            return;
        }

        switch (kind)
        {
            case kick:
            {
                s.dt[0] = 47.0f * ratio / sr;
                s.ampC = t60Coeff (decay, sr);
                s.c[8] = 1.0f + tone * tone * 5.0f;                  // drive into the sine (target)
                s.c[9] = 1.0f / aa::dsp::softClip (s.c[8]);
                if (fresh)
                {
                    s.c[2] = s.c[8];
                    s.c[3] = s.c[9];
                }
                s.c[4] = 1.0f - std::exp (-aa::dsp::twoPi * std::min (0.45f * sr, 1500.0f + 7000.0f * tone) / sr);
                if (fresh)
                {
                    s.amp = 1.0f;
                    s.e2 = 1.0f;
                    s.e2C = tauCoeff (0.005f + 0.03f * std::pow (1.0f - snap, 1.5f), sr);      // pitch sweep
                    s.c[1] = (1.5f + 7.0f * snap) * (0.85f + 0.15f * s.velRaw);                // sweep depth
                    s.e3 = 1.0f;
                    s.e3C = tauCoeff (0.0011f, sr);                                            // beater click
                    s.c[5] = (0.06f + 0.4f * snap) * (0.6f + 0.4f * s.velRaw);
                    s.hold = (int) (0.006f * sr);
                }
                break;
            }
            case snare:
            {
                s.dt[0] = 185.0f * ratio / sr;
                s.dt[1] = s.dt[0] * 1.72f;
                s.ampC = t60Coeff (juce::jlimit (0.04f, 1.6f, decay * 0.5f), sr);  // drum body
                s.e3C = t60Coeff (decay, sr);                                       // snares
                s.f1.setCutoffQ (600.0f + 2000.0f * tone, 0.6f, sr);
                s.f2.setCutoffQ (3500.0f + 7500.0f * tone, 0.6f, sr);
                s.c[4] = 1.25f - 0.5f * snap;
                s.c[5] = 0.4f + 0.9f * snap;
                if (fresh)
                {
                    s.amp = 1.0f;
                    s.e2 = 1.0f;
                    s.e2C = tauCoeff (0.01f, sr);
                    s.c[2] = 0.2f + 0.5f * snap;
                    s.e3 = 1.0f;
                    s.e4 = 1.0f;
                    s.e4C = tauCoeff (0.004f, sr);
                    s.c[6] = 1.3f * snap * (0.5f + 0.5f * s.velRaw);
                }
                break;
            }
            case clap:
            {
                const float fc = 1150.0f * ratio * std::pow (2.0f, (tone - 0.5f) * 1.6f);
                s.f1.setCutoffQ (fc, 1.4f, sr);
                s.f2.setCutoffQ (380.0f, 0.7f, sr);
                s.ampC = t60Coeff (decay, sr);
                if (fresh)
                {
                    s.spacing = std::max (1, (int) ((0.004f + 0.010f * (1.0f - snap)) * sr));
                    s.e2 = 1.0f;
                    s.e2C = tauCoeff (0.0026f + 0.002f * (1.0f - snap), sr);
                    s.counter = 0;
                    s.burst = 0;
                    s.amp = 0.0f;
                }
                break;
            }
            case closedHat:
            case openHat:
            {
                static const float metal[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
                for (int k = 0; k < 6; ++k)
                    s.dt[k] = std::min (0.45f, metal[k] * 1.12f * ratio / sr);
                const float fc = std::min (0.4f * sr, 7200.0f * std::pow (2.0f, (tone - 0.5f) * 1.4f) * std::sqrt (ratio));
                s.f1.setCutoffQ (fc, 1.1f, sr);
                s.f2.setCutoffQ (fc * 0.72f, 0.7f, sr);
                s.ampC = t60Coeff (decay, sr);
                s.c[0] = 0.12f + 0.45f * snap;  // noise in the metal
                if (fresh)
                {
                    static const float startPhase[6] = { 0.0f, 0.37f, 0.71f, 0.13f, 0.52f, 0.89f };
                    for (int k = 0; k < 6; ++k)
                        s.ph[k] = startPhase[k];
                    s.amp = 1.0f;
                    s.e4 = 1.0f;
                    s.e4C = tauCoeff (0.0025f, sr);
                    s.c[1] = 1.6f * snap * (0.5f + 0.5f * s.velRaw);
                }
                break;
            }
            case tom:
            {
                s.dt[0] = 98.0f * ratio / sr;
                s.ampC = t60Coeff (decay, sr);
                s.c[7] = tone * 0.55f;                               // targets, glided per sample
                s.c[8] = 1.0f + tone * 2.5f;
                s.c[9] = 1.0f / aa::dsp::softClip (s.c[8]);
                if (fresh)
                {
                    s.c[2] = s.c[7];
                    s.c[3] = s.c[8];
                    s.c[4] = s.c[9];
                    s.amp = 1.0f;
                    s.e2 = 1.0f;
                    s.e2C = tauCoeff (0.03f + 0.06f * (1.0f - snap), sr);
                    s.c[1] = 0.25f + 1.4f * snap;
                    s.e3 = 1.0f;
                    s.e3C = tauCoeff (0.003f, sr);
                    s.c[5] = 0.06f + 0.3f * snap;
                    s.c[6] = 1.0f - std::exp (-aa::dsp::twoPi * 2500.0f / sr);
                }
                break;
            }
            case cowbell:
            {
                s.dt[0] = std::min (0.45f, 540.0f * ratio / sr);
                s.dt[1] = std::min (0.45f, 800.0f * ratio / sr);
                s.dt[2] = std::min (0.45f, 2450.0f * ratio * std::pow (2.0f, (tone - 0.5f) * 0.6f) / sr);
                const float fc = std::min (0.4f * sr, 1650.0f * std::pow (ratio, 0.7f) * std::pow (2.0f, (tone - 0.5f) * 1.6f));
                s.f1.setCutoffQ (fc, 1.6f, sr);
                s.ampC = t60Coeff (decay, sr);
                s.e2C = t60Coeff (std::min (0.06f, decay * 0.5f), sr);
                s.e3C = t60Coeff (juce::jlimit (0.02f, 0.4f, decay * 0.35f), sr);
                s.c[3] = std::pow (snap, 1.6f); // cowbell -> clave
                if (fresh)
                {
                    s.amp = s.e2 = s.e3 = 1.0f;
                }
                break;
            }
            case zap:
            default:
            {
                s.dt[0] = 150.0f * ratio / sr;
                s.ampC = t60Coeff (decay, sr);
                s.e2C = tauCoeff (0.012f + decay * 0.22f, sr);
                s.c[2] = tone * tone * 4.0f;
                if (fresh)
                {
                    s.c[1] = 3.0f + 26.0f * snap;
                    s.amp = s.e2 = 1.0f;
                }
                break;
            }
        }
    }

    //==========================================================================
    void renderSynth (Slot& s, float* out, int n)
    {
        switch (kind)
        {
            case kick:
            {
                const float base = s.dt[0], depth = s.c[1], clickLp = s.c[4], clickAmt = s.c[5];
                float drive = s.c[2], norm = s.c[3];
                const float driveTarget = s.c[8], normTarget = s.c[9], glide = smoothCoeff;
                for (int i = 0; i < n; ++i)
                {
                    drive += (driveTarget - drive) * glide;
                    norm += (normTarget - norm) * glide;
                    s.ph[0] = wrap01 (s.ph[0] + std::min (0.45f, base * (1.0f + depth * s.e2)));
                    s.e2 *= s.e2C;
                    const float body = aa::dsp::softClip (sin01 (s.ph[0]) * drive) * norm;
                    if (s.age < s.hold)
                        ++s.age;
                    else
                        s.amp *= s.ampC;
                    const float noise = rng.nextBipolar();
                    s.lp += clickLp * (noise - s.lp);
                    out[i] = body * s.amp * 0.9f + s.lp * s.e3 * clickAmt;
                    s.e3 *= s.e3C;
                }
                s.c[2] = drive;
                s.c[3] = norm;
                if (s.amp < 1.0e-4f && s.age >= s.hold)
                    s.active = false;
                break;
            }
            case snare:
            {
                const float sweep = s.c[2], bodyGain = s.c[4], noiseGain = s.c[5], boost = s.c[6];
                for (int i = 0; i < n; ++i)
                {
                    const float pm = 1.0f + sweep * s.e2;
                    s.e2 *= s.e2C;
                    s.ph[0] = wrap01 (s.ph[0] + s.dt[0] * pm);
                    s.ph[1] = wrap01 (s.ph[1] + s.dt[1] * pm);
                    const float body = (0.62f * sin01 (s.ph[0]) + 0.38f * sin01 (s.ph[1])) * s.amp * bodyGain;
                    s.amp *= s.ampC;
                    const float nz = s.f2.lowpass (s.f1.highpass (rng.nextBipolar()));
                    const float snares = nz * s.e3 * noiseGain * (1.0f + boost * s.e4);
                    s.e3 *= s.e3C;
                    s.e4 *= s.e4C;
                    out[i] = aa::dsp::softClip ((body + snares * 1.1f) * 1.7f) * 0.95f;
                }
                if (s.amp < 1.0e-4f && s.e3 < 1.0e-4f)
                    s.active = false;
                break;
            }
            case clap:
            {
                constexpr int numBursts = 4;
                for (int i = 0; i < n; ++i)
                {
                    float e;
                    if (s.burst < numBursts - 1)
                    {
                        e = s.e2;
                        s.e2 *= s.e2C;
                        if (++s.counter >= s.spacing)
                        {
                            s.counter = 0;
                            ++s.burst;
                            const float g = 0.8f + 0.2f * rng.next01();
                            if (s.burst == numBursts - 1)
                                s.amp = g;
                            else
                                s.e2 = g;
                        }
                    }
                    else
                    {
                        e = s.amp;
                        s.amp *= s.ampC;
                    }
                    const float y = s.f2.highpass (s.f1.bandpass (rng.nextBipolar()));
                    out[i] = aa::dsp::softClip (y * e * 4.2f) * 0.6f;
                }
                if (s.burst >= numBursts - 1 && s.amp < 1.0e-4f)
                    s.active = false;
                break;
            }
            case closedHat:
            case openHat:
            {
                const float noiseMix = s.c[0], boost = s.c[1];
                for (int i = 0; i < n; ++i)
                {
                    float m = 0.0f;
                    for (int k = 0; k < 6; ++k)
                    {
                        m += blepSquare (s.ph[k], s.dt[k]);
                        s.ph[k] = wrap01 (s.ph[k] + s.dt[k]);
                    }
                    const float x = m * (1.0f / 6.0f) * (1.0f - noiseMix) + rng.nextBipolar() * noiseMix;
                    const float y = s.f2.highpass (s.f1.bandpass (x));
                    out[i] = y * s.amp * (1.0f + boost * s.e4) * 1.9f;
                    s.amp *= s.ampC;
                    s.e4 *= s.e4C;
                }
                if (s.amp < 1.0e-4f)
                    s.active = false;
                break;
            }
            case tom:
            {
                const float sweep = s.c[1], clickAmt = s.c[5], lpc = s.c[6];
                float triMix = s.c[2], drive = s.c[3], norm = s.c[4];
                const float glide = smoothCoeff;
                for (int i = 0; i < n; ++i)
                {
                    triMix += (s.c[7] - triMix) * glide;
                    drive += (s.c[8] - drive) * glide;
                    norm += (s.c[9] - norm) * glide;
                    s.ph[0] = wrap01 (s.ph[0] + std::min (0.45f, s.dt[0] * (1.0f + sweep * s.e2)));
                    s.e2 *= s.e2C;
                    const float osc = sin01 (s.ph[0]) * (1.0f - triMix) + tri01 (s.ph[0]) * triMix;
                    const float shaped = aa::dsp::softClip (osc * drive) * norm;
                    s.lp += lpc * (rng.nextBipolar() - s.lp);
                    out[i] = (shaped * s.amp + s.lp * s.e3 * clickAmt) * 0.78f;
                    s.amp *= s.ampC;
                    s.e3 *= s.e3C;
                }
                s.c[2] = triMix;
                s.c[3] = drive;
                s.c[4] = norm;
                if (s.amp < 1.0e-4f)
                    s.active = false;
                break;
            }
            case cowbell:
            {
                const float claveMix = s.c[3];
                for (int i = 0; i < n; ++i)
                {
                    const float sq = 0.5f * (blepSquare (s.ph[0], s.dt[0]) + blepSquare (s.ph[1], s.dt[1]));
                    s.ph[0] = wrap01 (s.ph[0] + s.dt[0]);
                    s.ph[1] = wrap01 (s.ph[1] + s.dt[1]);
                    const float bell = s.f1.bandpass (sq) * (0.62f * s.e2 + 0.38f * s.amp);
                    const float clave = sin01 (s.ph[2]) * s.e3;
                    s.ph[2] = wrap01 (s.ph[2] + s.dt[2]);
                    out[i] = bell * (1.0f - claveMix) * 1.25f + clave * claveMix * 0.85f;
                    s.amp *= s.ampC;
                    s.e2 *= s.e2C;
                    s.e3 *= s.e3C;
                }
                if (s.amp < 1.0e-4f && s.e3 < 1.0e-4f)
                    s.active = false;
                break;
            }
            case zap:
            default:
            {
                const float base = s.dt[0], depth = s.c[1], index = s.c[2];
                for (int i = 0; i < n; ++i)
                {
                    const float env = s.e2;
                    s.e2 *= s.e2C;
                    const float dt = std::min (0.3f, base * (1.0f + depth * env));
                    s.ph[0] = wrap01 (s.ph[0] + dt);
                    s.ph[1] = wrap01 (s.ph[1] + dt * 1.5f);
                    const float mod = index * (0.25f + 0.75f * env) * sin01 (s.ph[1]);
                    out[i] = std::sin (aa::dsp::twoPi * s.ph[0] + mod) * s.amp * 0.55f;
                    s.amp *= s.ampC;
                }
                if (s.amp < 1.0e-4f)
                    s.active = false;
                break;
            }
        }
    }

    //==========================================================================
    void renderSample (Slot& s, const SampleData& smp, float* outL, float* outR, int n)
    {
        const int len = smp.numFrames();
        const float* dl = smp.audio.getReadPointer (0);
        const float* dr = smp.audio.getNumChannels() > 1 ? smp.audio.getReadPointer (1) : dl;
        const double fadeLen = std::max (1.0, 0.003 * smp.sourceRate);
        const float boost = s.c[1];

        auto read = [len] (const float* d, int i, float f)
        {
            const float xm1 = d[std::max (0, i - 1)];
            const float x0 = d[i];
            const float x1 = d[std::min (len - 1, i + 1)];
            const float x2 = d[std::min (len - 1, i + 2)];
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * f + c2) * f + c1) * f + x0;
        };

        int i = 0;
        for (; i < n; ++i)
        {
            if (s.pos >= (double) (len - 1))
            {
                s.active = false;
                break;
            }
            const int idx = (int) s.pos;
            const float fr = (float) (s.pos - (double) idx);
            float l = read (dl, idx, fr);
            float r = dr == dl ? l : read (dr, idx, fr);

            float g = (1.0f + boost * s.e4) * s.fadeIn;
            s.e4 *= s.e4C;
            if (s.fadeIn < 1.0f)
                s.fadeIn = std::min (1.0f, s.fadeIn + s.fadeInStep);
            if (s.useEnv)
            {
                g *= s.amp;
                s.amp *= s.ampC;
            }
            const double remaining = (double) (len - 1) - s.pos;
            if (remaining < fadeLen)
                g *= (float) (remaining / fadeLen);

            const auto ol = s.f1.process (l);
            const auto orr = s.f2.process (r);
            if (s.filterMode != 0)
            {
                l = s.filterMode == 1 ? ol.lp : ol.hp;
                r = s.filterMode == 1 ? orr.lp : orr.hp;
            }

            outL[i] = l * g;
            outR[i] = r * g;
            s.pos += s.inc;
        }
        for (; i < n; ++i)
            outL[i] = outR[i] = 0.0f;

        if (s.useEnv && s.amp < 1.0e-4f)
            s.active = false;
    }

    //==========================================================================
    int kind = 0;
    float sr = 44100.0f;
    std::array<Slot, 2> slots;
    int current = 0;
    bool first = true;
    VoiceSettings live;
    aa::dsp::Smoother levelSm, panSm;
    aa::dsp::Rng rng;
    float tailL = 0.0f, tailR = 0.0f, tailCoeff = 0.99f, smoothCoeff = 0.002f;
    float tmpL[maxChunk] {}, tmpR[maxChunk] {}, gainL[maxChunk] {}, gainR[maxChunk] {};
};
} // namespace critter
