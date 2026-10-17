#pragma once

#include <aakit/PluginBase.h>
#include <aakit/dsp/Core.h>
#include <array>
#include <atomic>
#include <vector>

namespace ers::fx
{
/** The ten effects in the rack. The user can reorder them (drag in the FX page). */
enum class Kind { hyper, distortion, flanger, phaser, chorus, delay, compressor, reverb, eq, filter, count };
constexpr int numKinds = (int) Kind::count;

juce::String kindName (Kind);         // "Hyper", "Distortion", ...
juce::String kindPrefix (Kind);       // parameter id prefix: "hy", "di", "fl", "ph", "ch", "dl", "co", "rv", "eq", "ff"

/** Describes one FX parameter. The processor creates the actual AudioProcessorParameters from these,
    in this order, and passes their current (denormalised, possibly modulated) values to process(). */
struct ParamSpec
{
    juce::String id;                 // e.g. "dlFeedback" (prefix + Name); every effect has "<prefix>On"
    juce::String name;               // host-facing name, e.g. "Delay Feedback"
    juce::String label;              // short knob caption, e.g. "FEEDBACK"
    Kind owner;
    enum class Type { floating, choice, toggle } type = Type::floating;
    float min = 0.0f, max = 1.0f, def = 0.0f;
    aa::params::Unit unit = aa::params::Unit::none;
    float skewCentre = aa::params::noSkew;
    float step = 0.0f;
    juce::StringArray choices;       // Type::choice: the options (value = index)
    bool modulatable = true;         // false for choices/toggles and anything that must not move per sample
};

const std::vector<ParamSpec>& paramSpecs();
int specIndex (const juce::String& id);              // -1 if unknown
std::vector<int> specsFor (Kind);                    // indices of a kind's params (On toggle first)

using Order = std::array<Kind, numKinds>;
Order defaultOrder();

/** Host tempo info for synced effects. */
struct TempoInfo
{
    double bpm = 120.0;
    double ppq = 0.0;
    bool playing = false;
};

class FxRack
{
public:
    FxRack();
    ~FxRack();

    void prepare (double sampleRate, int maxBlockSize);   // allocates
    void reset();                                          // clears all delay lines / reverb tails

    /** Audio-thread safe (just copies the array). */
    void setOrder (const Order& order) noexcept;

    /** values: one real-valued entry per paramSpecs() entry (choices/toggles as their index / 0-1).
        Processes in place. Effects whose On toggle is off are skipped (but fade out cleanly over a
        few ms when switched off, and their state is reset when switched back on). */
    void process (float* left, float* right, int numSamples, const float* values, const TempoInfo& tempo) noexcept;

    double getTailSeconds() const noexcept { return 12.0; }

    // ---- metering for the UI (written on the audio thread) ---------------------
    std::atomic<float> compressorGainReductionDb { 0.0f };   // single band / max of bands, positive dB
    std::atomic<float> distortionDrive { 0.0f };              // 0..1 activity, for a little animation

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace ers::fx
