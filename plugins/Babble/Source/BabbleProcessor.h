#pragma once

#include <aakit/AdventureKit.h>
#include "BabbleEngine.h"

class BabbleProcessor : public aa::PluginBase
{
public:
    BabbleProcessor();
    ~BabbleProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override { engine.reset(); }
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override { return isInstrumentLayout (layouts); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    babble::Engine engine; // the editor reads its UI feed (atomics + event queue)

private:
    babble::Settings readSettings() const;

    std::atomic<float>* vowel = nullptr;
    std::atomic<float>* babbleAmount = nullptr;
    std::atomic<float>* babbleRate = nullptr;
    std::atomic<float>* shift = nullptr;
    std::atomic<float>* breath = nullptr;
    std::atomic<float>* vibDepth = nullptr;
    std::atomic<float>* reverb = nullptr;
    std::atomic<float>* volume = nullptr;
    std::atomic<float>* voice = nullptr;
    std::atomic<float>* brightness = nullptr;
    std::atomic<float>* vibRate = nullptr;
    std::atomic<float>* vibDelay = nullptr;
    std::atomic<float>* glide = nullptr;
    std::atomic<float>* babbleSync = nullptr;
    std::atomic<float>* babbleDiv = nullptr;
    std::atomic<float>* choir = nullptr;
    std::atomic<float>* detune = nullptr;
    std::atomic<float>* attack = nullptr;
    std::atomic<float>* release = nullptr;
    std::atomic<float>* chorus = nullptr;

    aa::dsp::Transport transport;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BabbleProcessor)
};
