#include "PluginBase.h"

namespace aa
{
using namespace juce;

namespace
{
    const Identifier stateRootId { "AdventureState" };
    const Identifier extraId { "EXTRA" };
}

PluginBase::PluginBase (const BusesProperties& buses, AudioProcessorValueTreeState::ParameterLayout layout)
    : AudioProcessor (buses),
      apvts (*this, nullptr, "PARAMS", std::move (layout))
{
}

PluginBase::~PluginBase() = default;

//==============================================================================
const String PluginBase::getName() const { return JucePlugin_Name; }

bool PluginBase::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool PluginBase::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

RangedAudioParameter* PluginBase::param (const String& id) const { return apvts.getParameter (id); }

std::atomic<float>* PluginBase::raw (const String& id) const
{
    auto* p = apvts.getRawParameterValue (id);
    jassert (p != nullptr); // unknown parameter id
    return p;
}

bool PluginBase::isMonoOrStereoEffectLayout (const BusesLayout& layouts)
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

bool PluginBase::isInstrumentLayout (const BusesLayout& layouts)
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == AudioChannelSet::mono() || out == AudioChannelSet::stereo();
}

//==============================================================================
std::unique_ptr<XmlElement> PluginBase::createStateXml()
{
    auto root = std::make_unique<XmlElement> (stateRootId);
    root->setAttribute ("version", JucePlugin_VersionString);
    root->setAttribute ("preset", currentPresetName);
    root->setAttribute ("presetIndex", currentFactoryIndex);
    root->setAttribute ("uiScale", (double) uiScale);

    if (auto params = apvts.copyState().createXml())
        root->addChildElement (params.release());

    ValueTree extra (extraId);
    saveExtraState (extra);
    if (auto extraXml = extra.createXml())
        root->addChildElement (extraXml.release());

    return root;
}

void PluginBase::restoreFromXml (const XmlElement& xml, bool includeUiState)
{
    if (! xml.hasTagName (stateRootId))
        return;

    if (auto* params = xml.getChildByName (apvts.state.getType()))
    {
        // Merge onto defaults so that parameters added in future versions keep sane values.
        auto newState = ValueTree::fromXml (*params);
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            {
                bool found = false;
                for (auto child : newState)
                    if (child.getProperty ("id").toString() == rp->getParameterID())
                        found = true;

                if (! found)
                {
                    ValueTree child ("PARAM");
                    child.setProperty ("id", rp->getParameterID(), nullptr);
                    child.setProperty ("value", rp->convertFrom0to1 (rp->getDefaultValue()), nullptr);
                    newState.appendChild (child, nullptr);
                }
            }

        apvts.replaceState (newState);
    }

    if (auto* extra = xml.getChildByName (extraId))
        loadExtraState (ValueTree::fromXml (*extra));
    else
        resetExtraState();

    if (includeUiState)
        uiScale = (float) jlimit (0.5, 3.0, xml.getDoubleAttribute ("uiScale", 1.0));

    setPresetName (xml.getStringAttribute ("preset", "Default"), xml.getIntAttribute ("presetIndex", -1));
}

void PluginBase::getStateInformation (MemoryBlock& destData)
{
    if (auto xml = createStateXml())
        copyXmlToBinary (*xml, destData);
}

void PluginBase::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        restoreFromXml (*xml, true);
}

//==============================================================================
void PluginBase::setPresetName (const String& name, int factoryIndex)
{
    currentPresetName = name;
    currentFactoryIndex = factoryIndex;

    if (MessageManager::getInstanceWithoutCreating() != nullptr)
        presetChanged.sendChangeMessage();
}

void PluginBase::setFactoryPresets (std::vector<FactoryPreset> presets)
{
    factoryPresets = std::move (presets);
    if (! factoryPresets.empty())
        currentPresetName = factoryPresets.front().name;
}

void PluginBase::loadFactoryPreset (int index)
{
    if (! isPositiveAndBelow (index, (int) factoryPresets.size()))
        return;

    const auto& preset = factoryPresets[(size_t) index];

    for (auto* p : getParameters())
    {
        auto* rp = dynamic_cast<RangedAudioParameter*> (p);
        if (rp == nullptr)
            continue;

        float target = rp->getDefaultValue();
        for (const auto& [id, value] : preset.values)
            if (id == rp->getParameterID())
                target = rp->convertTo0to1 (value);

        rp->beginChangeGesture();
        rp->setValueNotifyingHost (target);
        rp->endChangeGesture();
    }

    resetExtraState();
    if (preset.applyExtra)
        preset.applyExtra (*this);

    setPresetName (preset.name, index);
}

File PluginBase::getUserPresetFolder() const
{
    return File::getSpecialLocation (File::userDocumentsDirectory)
        .getChildFile ("Adventure Audio")
        .getChildFile (String (JucePlugin_Name) + " Presets");
}

Array<File> PluginBase::getUserPresetFiles() const
{
    auto folder = getUserPresetFolder();
    if (! folder.isDirectory())
        return {};

    auto files = folder.findChildFiles (File::findFiles, false, "*.aapreset");
    files.sort();
    return files;
}

bool PluginBase::loadPresetFile (const File& file)
{
    auto xml = parseXML (file);
    if (xml == nullptr || ! xml->hasTagName (stateRootId))
        return false;

    restoreFromXml (*xml, false);
    setPresetName (file.getFileNameWithoutExtension(), -1);
    return true;
}

bool PluginBase::savePresetFile (const File& file)
{
    setPresetName (file.getFileNameWithoutExtension(), -1);
    auto xml = createStateXml();
    if (xml == nullptr)
        return false;

    xml->removeAttribute ("uiScale");
    file.getParentDirectory().createDirectory();
    return xml->writeTo (file);
}

//==============================================================================
namespace params
{
    String formatValue (float v, Unit unit)
    {
        switch (unit)
        {
            case Unit::percent:   return String (roundToInt (v)) + "%";
            case Unit::hz:        return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                                                      : String (v, v < 10.0f ? 2 : (v < 100.0f ? 1 : 0)) + " Hz";
            case Unit::ms:        return v >= 1000.0f ? String (v / 1000.0f, 2) + " s"
                                                      : String (v, v < 10.0f ? 1 : 0) + " ms";
            case Unit::seconds:   return v < 1.0f ? String (roundToInt (v * 1000.0f)) + " ms" : String (v, 2) + " s";
            case Unit::db:        return (v <= -59.9f ? String ("-inf") : String (v, 1)) + " dB";
            case Unit::semitones: return (v > 0.0f ? "+" : "") + String (v, std::abs (v - std::round (v)) < 0.01f ? 0 : 1) + " st";
            case Unit::cents:     return (v > 0.0f ? "+" : "") + String (roundToInt (v)) + " ct";
            case Unit::ratio:     return String (v, 2) + "x";
            case Unit::beats:     return String (v, 2) + " beats";
            case Unit::none:      break;
        }
        return String (v, 2);
    }

    float parseValue (const String& text, Unit unit)
    {
        // Accepts what formatValue() prints ("1.2 kHz", "350 ms", "2.5 s", "-inf dB") as well as bare numbers.
        const auto t = text.trim().toLowerCase();
        if (t.startsWith ("-inf"))
            return -60.0f;
        float v = t.retainCharacters ("0123456789.-+").getFloatValue();
        switch (unit)
        {
            case Unit::hz:      if (t.contains ("khz") || t.endsWith ("k")) v *= 1000.0f; break;
            case Unit::ms:      if (t.endsWith ("s") && ! t.endsWith ("ms")) v *= 1000.0f; break;
            case Unit::seconds: if (t.endsWith ("ms")) v *= 0.001f; break;
            default: break;
        }
        return v;
    }

    std::unique_ptr<AudioParameterFloat> floatParam (const String& id, const String& name, float min, float max,
                                                     float def, Unit unit, float skewCentre, float step)
    {
        NormalisableRange<float> range (min, max, step);
        if (skewCentre > min && skewCentre < max)
            range.setSkewForCentre (skewCentre);

        return std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, range, def,
            AudioParameterFloatAttributes()
                .withStringFromValueFunction ([unit] (float v, int) { return formatValue (v, unit); })
                .withValueFromStringFunction ([unit] (const String& s) { return parseValue (s, unit); }));
    }

    std::unique_ptr<AudioParameterFloat> percent (const String& id, const String& name, float def)
    {
        return floatParam (id, name, 0.0f, 100.0f, def, Unit::percent);
    }

    std::unique_ptr<AudioParameterChoice> choice (const String& id, const String& name, const StringArray& choices, int def)
    {
        return std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, choices, def);
    }

    std::unique_ptr<AudioParameterBool> toggle (const String& id, const String& name, bool def)
    {
        return std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def);
    }

    std::unique_ptr<AudioParameterInt> integer (const String& id, const String& name, int min, int max, int def,
                                                const String& suffix)
    {
        return std::make_unique<AudioParameterInt> (
            ParameterID { id, 1 }, name, min, max, def,
            AudioParameterIntAttributes().withStringFromValueFunction ([suffix] (int v, int) { return String (v) + suffix; }));
    }
} // namespace params
} // namespace aa
