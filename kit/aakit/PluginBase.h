#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <vector>
#include <limits>

namespace aa
{
class PluginBase;

/** A preset that ships with the plugin. Values are given in real parameter units
    (Hz, ms, %...). Any parameter not listed is reset to its default. */
struct FactoryPreset
{
    juce::String name;
    std::vector<std::pair<juce::String, float>> values;
    std::function<void (PluginBase&)> applyExtra; // optional: non-parameter state (patterns, etc.)
};

/** Common processor base: owns the parameter tree, handles state, presets and boilerplate. */
class PluginBase : public juce::AudioProcessor
{
public:
    PluginBase (const BusesProperties& buses, juce::AudioProcessorValueTreeState::ParameterLayout layout);
    ~PluginBase() override;

    juce::AudioProcessorValueTreeState apvts;

    //==============================================================================
    // Presets (call from the message thread)
    void setFactoryPresets (std::vector<FactoryPreset> presets);
    const std::vector<FactoryPreset>& getFactoryPresets() const noexcept { return factoryPresets; }
    void loadFactoryPreset (int index);
    bool loadPresetFile (const juce::File& file);
    bool savePresetFile (const juce::File& file);
    juce::File getUserPresetFolder() const;
    juce::Array<juce::File> getUserPresetFiles() const;
    juce::String getCurrentPresetName() const { return currentPresetName; }
    int getCurrentFactoryPresetIndex() const noexcept { return currentFactoryIndex; }
    juce::ChangeBroadcaster presetChanged; // fires on the message thread when the preset name changes

    //==============================================================================
    // Plugin-specific non-parameter state (sequencer patterns, loaded samples...)
    virtual void saveExtraState (juce::ValueTree&) {}
    virtual void loadExtraState (const juce::ValueTree&) {}
    /** Called before a factory preset is applied so extra state can go back to defaults. */
    virtual void resetExtraState() {}

    //==============================================================================
    juce::RangedAudioParameter* param (const juce::String& id) const;
    std::atomic<float>* raw (const juce::String& id) const;

    float uiScale = 1.0f; // remembered editor zoom (message thread only)

    //==============================================================================
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return tailSeconds; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return currentPresetName; }
    void changeProgramName (int, const juce::String&) override {}

    bool hasEditor() const override { return true; }

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Bus layout helper for effects: mono->mono or stereo->stereo. */
    static bool isMonoOrStereoEffectLayout (const BusesLayout& layouts);
    /** Bus layout helper for instruments: stereo (or mono) out only. */
    static bool isInstrumentLayout (const BusesLayout& layouts);

protected:
    double tailSeconds = 0.0;

private:
    std::unique_ptr<juce::XmlElement> createStateXml();
    void restoreFromXml (const juce::XmlElement& xml, bool includeUiState);
    void setPresetName (const juce::String& name, int factoryIndex);

    std::vector<FactoryPreset> factoryPresets;
    juce::String currentPresetName { "Default" };
    int currentFactoryIndex = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginBase)
};

//==============================================================================
/** Parameter-construction helpers so plugins can declare their layouts compactly. */
namespace params
{
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

    enum class Unit { none, percent, hz, ms, seconds, db, semitones, cents, ratio, beats };

    /** Pass as skewCentre for a linear range (any value outside [min, max] also means "no skew"). */
    constexpr float noSkew = std::numeric_limits<float>::lowest();

    juce::String formatValue (float value, Unit unit);
    float parseValue (const juce::String& text, Unit unit);

    /** Linear-range float with a unit-aware string conversion. */
    std::unique_ptr<juce::AudioParameterFloat> floatParam (const juce::String& id, const juce::String& name,
                                                           float min, float max, float def, Unit unit = Unit::none,
                                                           float skewCentre = noSkew, float step = 0.0f);

    /** 0..100 % parameter. */
    std::unique_ptr<juce::AudioParameterFloat> percent (const juce::String& id, const juce::String& name, float def);

    std::unique_ptr<juce::AudioParameterChoice> choice (const juce::String& id, const juce::String& name,
                                                        const juce::StringArray& choices, int def);

    std::unique_ptr<juce::AudioParameterBool> toggle (const juce::String& id, const juce::String& name, bool def);

    std::unique_ptr<juce::AudioParameterInt> integer (const juce::String& id, const juce::String& name,
                                                      int min, int max, int def, const juce::String& suffix = {});
} // namespace params
} // namespace aa
