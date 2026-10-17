#pragma once

#include <aakit/AdventureKit.h>
#include <aakit/dsp/Transport.h>
#include "Engine.h"
#include "Fx.h"

class SynthProcessor : public aa::PluginBase, private juce::Timer
{
public:
    SynthProcessor();
    ~SynthProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override { return isInstrumentLayout (layouts); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;

    // ---- wavetables (message thread) ---------------------------------------------------
    ers::WavetablePtr getOscTable (int osc) const;
    bool loadCustomTable (int osc, const juce::File& file, juce::String& error);
    bool loadCustomTable (int osc, ers::WavetablePtr table);
    void selectFactoryTable (int osc, int index);
    int getCustomTableIndex() const noexcept { return ers::getNumFactoryTables(); }
    juce::String getOscTableName (int osc) const;
    std::atomic<int> tableVersion { 0 };   // bumped whenever an oscillator's table changes

    // ---- modulation (message thread) ---------------------------------------------------
    ers::ModMatrix matrix;
    const ers::LfoShape& getLfoShape (int lfo) const { return lfoShapes[(size_t) lfo]; }
    void setLfoShape (int lfo, const ers::LfoShape& shape);
    std::atomic<int> lfoShapeVersion { 0 };
    juce::String getMacroName (int m) const { return macroNames[m]; }
    void setMacroName (int m, const juce::String& name) { macroNames.set (m, name); }

    // ---- FX order (message thread) -----------------------------------------------------
    ers::fx::Order getFxOrder() const;
    void setFxOrder (const ers::fx::Order& order);
    ers::fx::FxRack& getFxRack() noexcept { return rack; }

    // ---- parameters ------------------------------------------------------------------
    juce::RangedAudioParameter* vpParam (int index) const noexcept { return vpParams[(size_t) index]; }

    // ---- UI feed ---------------------------------------------------------------------
    ers::Engine engine;
    juce::MidiKeyboardState keyboardState;
    static constexpr int scopeSize = 2048;
    std::array<std::atomic<float>, scopeSize> scope {};
    std::atomic<int> scopeWrite { 0 };
    std::atomic<float> outputLevelL { 0.0f }, outputLevelR { 0.0f };

    // aa::PluginBase
    void saveExtraState (juce::ValueTree&) override;
    void loadExtraState (const juce::ValueTree&) override;
    void resetExtraState() override;
    void stateLoaded() override;

private:
    void timerCallback() override;
    void syncTables();
    void publishLfoTable (int lfo);
    void retire (std::shared_ptr<const void> object);
    ers::WavetablePtr factoryTable (int index);
    void processFx (float* l, float* r, int n);

    std::array<juce::RangedAudioParameter*, ers::vp::count> vpParams {};
    std::array<std::atomic<float>*, ers::vp::count> vpRaw {};
    std::vector<juce::NormalisableRange<float>> vpRanges;
    std::vector<juce::RangedAudioParameter*> fxParams;
    std::vector<std::atomic<float>*> fxRaw;

    std::array<float, ers::vp::count> blockValues {}, blockNorm {};
    std::vector<float> fxValues, fxBase, fxNorm;
    std::array<ers::ModRoute, ers::maxModSlots> routeSnapshot {};

    // wavetable + noise ownership (message thread) and the raw pointers the audio thread reads
    std::array<ers::WavetablePtr, ers::numOscs> oscTables, customTables;
    std::array<std::atomic<ers::Wavetable*>, ers::numOscs> oscLive {};
    std::array<int, ers::numOscs> loadedTableIndex { -1, -1, -1 };
    std::map<int, ers::WavetablePtr> factoryCache;
    std::shared_ptr<const ers::NoiseSource> noise;
    std::atomic<const ers::NoiseSource*> noiseLive { nullptr };
    int loadedNoiseIndex = -1;

    struct LfoTable { std::array<float, ers::lfoTableSize + 1> data {}; };
    std::array<ers::LfoShape, ers::numLfos> lfoShapes;
    std::array<std::shared_ptr<LfoTable>, ers::numLfos> lfoTables;
    std::array<std::atomic<LfoTable*>, ers::numLfos> lfoLive {};

    std::vector<std::pair<std::shared_ptr<const void>, double>> graveyard;
    juce::StringArray macroNames;
    std::array<std::atomic<int>, ers::fx::numKinds> fxOrder {};

    ers::fx::FxRack rack;
    aa::dsp::Transport transport;
    aa::dsp::Smoother volume;
    aa::dsp::PeakMeter meterL, meterR;
    float sr = 44100.0f;
    bool firstBlock = true;
    float dcCoeff = 0.999f, dcInL = 0.0f, dcInR = 0.0f, dcOutL = 0.0f, dcOutR = 0.0f;

public:
    int lastPage = 0;   // editor page, remembered while the plugin is open

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthProcessor)
};
