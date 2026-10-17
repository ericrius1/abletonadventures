#pragma once

#include "Params.h"

namespace ers
{
//==============================================================================
/** A drawable LFO shape: breakpoints (x 0..1 ascending, y 0..1) joined by curved segments.
    The first point is always at x = 0 and the last at x = 1 (they should share the same y for a
    seamless loop, but they don't have to - envelopes made with "Envelope" mode often don't). */
struct LfoShape
{
    struct Point
    {
        float x = 0.0f, y = 0.0f;
        float curve = 0.0f;   // bend of the segment *to the next point*: -1 .. 1 (0 = straight)
    };

    std::vector<Point> points;

    enum class Preset { sine, triangle, sawDown, sawUp, square, pulse, steps, expDecay, bounce, stairs, random, count };
    static juce::StringArray presetNames();
    static LfoShape preset (Preset p, int seed = 1);

    /** Value at x (0..1). */
    float evaluate (float x) const noexcept;

    /** Render to a lookup table of lfoTableSize + 1 samples. */
    void render (float* table) const;

    juce::ValueTree toValueTree (const juce::Identifier& type) const;
    static LfoShape fromValueTree (const juce::ValueTree& tree);

    /** Keeps x sorted, pins the end points, clamps everything. */
    void sanitise();

    bool operator== (const LfoShape& other) const noexcept;
    bool operator!= (const LfoShape& other) const noexcept { return ! (*this == other); }

    static float curveSegment (float t, float curve) noexcept;
};

//==============================================================================
/** One modulation routing. All fields are atomics so the message thread can edit a slot while the
    audio thread reads it (a half-updated slot for one block is harmless). */
struct ModSlot
{
    std::atomic<int> source { (int) ModSource::none };
    std::atomic<int> dest { -1 };                // index into modDests()
    std::atomic<float> amount { 0.0f };           // -1 .. 1, in normalised parameter units
    std::atomic<int> aux { (int) ModSource::none };  // optional: amount is scaled by this source (0..1)
    std::atomic<bool> bipolar { false };          // map a unipolar source to -1..1 (and vice versa)

    bool isActive() const noexcept
    {
        return source.load (std::memory_order_relaxed) != (int) ModSource::none
            && dest.load (std::memory_order_relaxed) >= 0
            && amount.load (std::memory_order_relaxed) != 0.0f;
    }

    void clear() noexcept
    {
        source = (int) ModSource::none;
        dest = -1;
        amount = 0.0f;
        aux = (int) ModSource::none;
        bipolar = false;
    }
};

/** Plain copy of a slot, used by the engine once per block and for presets. */
struct ModRoute
{
    ModSource source = ModSource::none;
    int dest = -1;
    float amount = 0.0f;
    ModSource aux = ModSource::none;
    bool bipolar = false;
};

class ModMatrix
{
public:
    std::array<ModSlot, maxModSlots> slots;

    /** Adds a routing (or updates the amount of an existing source->dest pair). Returns the slot index or -1 if full. */
    int add (ModSource source, int dest, float amount);
    void remove (int slotIndex);
    void clear();
    int find (ModSource source, int dest) const;
    /** Every slot targeting dest. */
    std::vector<int> slotsForDest (int dest) const;

    int snapshot (std::array<ModRoute, maxModSlots>& out) const noexcept;  // returns number of active routes

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& tree);

    /** Bumped on every structural change (message thread) so UIs can refresh. */
    std::atomic<int> version { 0 };
};
} // namespace ers
