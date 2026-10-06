#pragma once

#include <aakit/AdventureKit.h>
#include "GremlinEngine.h"

class GremlinProcessor : public aa::PluginBase
{
public:
    GremlinProcessor();
    ~GremlinProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override { engine.reset(); }
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override { return isMonoOrStereoEffectLayout (layouts); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /** Parameter IDs of the per-effect odds, in gremlin::Fx order (stutter first). */
    static const juce::StringArray& weightIDs();
    /** Parameter IDs of the force pads (stutter, reverse, tape stop, half speed). */
    static const juce::StringArray& forceIDs();

    /** Current dice settings (callable from any thread; used by the UI to preview locked patterns). */
    gremlin::DiceSettings diceSettings (double ppqPerBar) const;
    int gridIndex() const       { return juce::jlimit (0, 3, (int) grid->load()); }
    int lockBars() const        { return gremlin::lockBarsFor ((int) lock->load()); }
    uint32_t seedValue() const  { return (uint32_t) juce::jmax (1, (int) seed->load()); }
    float chaosAmount() const   { return chaos->load() / 100.0f; }

    gremlin::Engine engine;

private:
    std::atomic<float>* chaos = nullptr;
    std::atomic<float>* grid = nullptr;
    std::atomic<float>* mix = nullptr;
    std::array<std::atomic<float>*, 4> force {};
    std::atomic<float>* ratchet = nullptr;
    std::array<std::atomic<float>*, gremlin::numDiceFx> weights {};
    std::atomic<float>* slice = nullptr;
    std::atomic<float>* crush = nullptr;
    std::atomic<float>* smooth = nullptr;
    std::atomic<float>* seed = nullptr;
    std::atomic<float>* lock = nullptr;

    aa::dsp::Transport transport;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GremlinProcessor)
};
