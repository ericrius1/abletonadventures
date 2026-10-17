#pragma once

#include <aakit/dsp/Core.h>
#include <vector>

namespace ers
{
enum class FilterType
{
    lp12, lp24, ladder, diode, hp12, hp24, bp12, bp24, notch, peak,
    combPlus, combMinus, flanger, phaser, formant, ring, downsample, wasp,
    count
};

juce::StringArray filterTypeNames();

/** One stereo multimode filter, used twice per synth voice (and once in the FX rack).

    All types are stable at any setting and self-oscillation is tamed with soft saturation.
    setParams is called once per control block (32 samples); coefficients are interpolated across
    the block so sweeps never zipper. process() runs in place. */
class VoiceFilter
{
public:
    VoiceFilter();
    ~VoiceFilter();
    VoiceFilter (VoiceFilter&&) noexcept;
    VoiceFilter& operator= (VoiceFilter&&) noexcept;

    void prepare (float sampleRate);   // allocates the comb / flanger delay lines (up to 50 ms)
    void reset();

    /** cutoffHz: 20..20000 (already including key tracking and modulation).
        reso, drive, var, mix: 0..1. var is type specific:
          lp/hp/bp/notch/peak/ladder/diode : "fat" - extra pre-saturation and low-end weight
          comb + / comb - / flanger          : damping in the feedback path
          phaser                             : spread of the notches
          formant                            : vowel (A-E-I-O-U morph; cutoff shifts the formants)
          ring                               : ring-mod waveform sine -> square
          downsample                         : bit depth reduction
          wasp                               : mode morph LP -> BP -> HP */
    void setParams (FilterType type, float cutoffHz, float reso, float drive, float var, float mix);

    void process (float* left, float* right, int numSamples);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace ers
