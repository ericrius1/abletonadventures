#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "LookAndFeel.h"
#include "PluginBase.h"

namespace aa
{
//==============================================================================
/** Rotary knob with caption. Shows its label, or the live value while hovered/dragged.
    Double-click resets to the default, mouse-wheel nudges, drag up/down or left/right. */
class Knob : public juce::Slider
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, const juce::String& label);
    ~Knob() override;

    void setAccent (juce::Colour c)       { accent = c; repaint(); }
    juce::Colour getAccent() const        { return accent.isTransparent() ? themeFor (*const_cast<Knob*> (this)).accent : accent; }
    void setBipolar (bool b)              { bipolar = b; repaint(); }
    void setCaptionVisible (bool b)       { captionVisible = b; repaint(); }
    const juce::String& getLabel() const  { return label; }
    juce::String getParamID() const       { return paramID; }

    /** Normalised 0..1 position of the knob. */
    float getProportion() const;

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void valueChanged() override { repaint(); }
    void startedDragging() override { dragging = true; repaint(); }
    void stoppedDragging() override { dragging = false; repaint(); }

    juce::Rectangle<float> getDialBounds() const;

private:
    juce::String paramID, label;
    juce::Colour accent;
    bool bipolar = false, hovered = false, dragging = false, captionVisible = true;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

//==============================================================================
/** Toggle attached to a bool parameter. Draws as a pill switch with a label,
    or as a lit "pad" button when style == Style::pad. */
class Toggle : public juce::ToggleButton
{
public:
    enum class Style { switchWithLabel, pad };

    Toggle (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, const juce::String& label,
            Style style = Style::switchWithLabel);
    ~Toggle() override;

    void setAccent (juce::Colour c) { accent = c; repaint(); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::String label;
    Style style;
    juce::Colour accent;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

//==============================================================================
/** Selector for a choice parameter. "Stepper" shows < value > with a popup menu on click;
    "Segmented" shows every option side by side. */
class ChoiceBox : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class Style { stepper, segmented };

    ChoiceBox (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, const juce::String& label,
               Style style = Style::stepper);
    ~ChoiceBox() override;

    void setAccent (juce::Colour c) { accent = c; repaint(); }
    void setShortNames (const juce::StringArray& names) { shortNames = names; repaint(); }
    int getIndex() const noexcept { return index; }

    /** Optional custom painter for segments (e.g. waveform icons). */
    std::function<void (juce::Graphics&, juce::Rectangle<float>, int index, bool selected)> segmentPainter;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    void setIndex (int newIndex);
    juce::Rectangle<float> getBoxArea() const;
    juce::String nameFor (int i) const;

    juce::AudioParameterChoice* choiceParam = nullptr;
    juce::String label;
    Style style;
    juce::Colour accent;
    juce::StringArray shortNames;
    int index = 0;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

//==============================================================================
/** Preset browser pill: "<  Preset Name  >". Click for a menu of factory + user presets. */
class PresetSelector : public juce::Component, public juce::SettableTooltipClient, private juce::ChangeListener
{
public:
    explicit PresetSelector (PluginBase& processor);
    ~PresetSelector() override;

    void setAccent (juce::Colour c) { accent = c; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void step (int delta);
    void showMenu();
    void saveUserPreset();

    PluginBase& processor;
    juce::Colour accent;
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
/** A one-line text label drawn with the kit fonts (not a juce::Label - no editing). */
class Caption : public juce::Component
{
public:
    Caption (const juce::String& t = {}, float height = 12.0f, bool bold = false)
        : text (t), fontHeight (height), isBold (bold) { setInterceptsMouseClicks (false, false); }

    void setText (const juce::String& t) { text = t; repaint(); }
    void setColour (juce::Colour c) { colour = c; repaint(); }
    void setJustification (juce::Justification j) { just = j; repaint(); }
    void paint (juce::Graphics& g) override;

private:
    juce::String text;
    float fontHeight;
    bool isBold;
    juce::Colour colour;
    juce::Justification just { juce::Justification::centred };
};

//==============================================================================
/** Lays out a row of components evenly inside an area. */
void layoutRow (juce::Rectangle<int> area, std::initializer_list<juce::Component*> comps, int gap = 6);
void layoutGrid (juce::Rectangle<int> area, const std::vector<juce::Component*>& comps, int columns, int gapX = 6, int gapY = 6);
} // namespace aa
