#pragma once

#include "CritterDsp.h"

namespace critter
{
/** Decoding audio files into SampleData and packing them into / out of the plugin state.
    Runs on the message thread or a background thread, never on the audio thread. */
struct SampleCodec
{
    static std::unique_ptr<SampleData> fromFile (juce::AudioFormatManager& formats, const juce::File& file,
                                                 juce::String& error);

    static std::unique_ptr<SampleData> fromEmbedded (juce::AudioFormatManager& formats, const juce::MemoryBlock& data,
                                                     const juce::String& format, const juce::String& path,
                                                     const juce::String& name);

    static bool canLoad (juce::AudioFormatManager& formats, const juce::String& path);
};
} // namespace critter
