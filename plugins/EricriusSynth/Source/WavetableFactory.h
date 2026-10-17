#pragma once

#include "Wavetable.h"

namespace ers
{
/** The built-in wavetable library. Every table is generated procedurally from code in this
    plugin (additive / FM / phase-distortion / formant maths), deterministic, and original. */
struct FactoryTableInfo
{
    juce::String name;
    juce::String category;   // "Basic", "Analog", "Digital", "Vocal", "Spectral", "FM", "Bass", "Keys & Pads", "Weird"
};

int getNumFactoryTables();
const FactoryTableInfo& getFactoryTableInfo (int index);
juce::StringArray getFactoryTableNames();

/** Builds the table (allocates, a few ms). Message thread or background thread only.
    Index 0 is "Origin Shapes" (sine -> triangle -> saw -> square -> narrow pulse). */
WavetablePtr createFactoryTable (int index);

//==============================================================================
/** Noise oscillator sources, also generated procedurally. Stored at 48 kHz, stereo. */
struct NoiseSource
{
    juce::String name;
    std::vector<float> left, right;
    bool loops = true;           // false: one-shot (plays once per note, e.g. a click or a hit)
    float sampleRate = 48000.0f;
};

int getNumNoiseTypes();
juce::StringArray getNoiseNames();
std::shared_ptr<const NoiseSource> createNoise (int index);
} // namespace ers
