#pragma once

#include <aakit/AdventureKit.h>

namespace orrery
{
namespace palette
{
    const juce::Colour navyTop { 0xff0d1630 };
    const juce::Colour navyBottom { 0xff1b1440 };
    const juce::Colour brass { 0xffd9b26f };
    const juce::Colour brassLight { 0xfff6dfa6 };
    const juce::Colour brassDark { 0xff7a5a2a };
    const juce::Colour brassDeep { 0xff3d2c14 };
    const juce::Colour parchment { 0xfff1e6c8 };
    const juce::Colour enamel { 0xff121c3d };
    const juce::Colour ink { 0xff070b1a };
} // namespace palette

aa::Theme makeTheme();

/** Fills text as a brass-engraved title: dark drop shadow, metallic gradient, fine highlight. */
void drawEngravedText (juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Point<float> baseline,
                       float kerning = 0.0f);

/** A small four-pointed sparkle (used for stars, ornaments and the bar jewel). */
void drawSparkle (juce::Graphics& g, juce::Point<float> c, float radius, juce::Colour colour);

//==============================================================================
/** Knob that can show a short value (a number, a note name) on its enamel cap instead of a pointer. */
class OrreryKnob : public aa::Knob
{
public:
    using aa::Knob::Knob;
    std::function<juce::String()> centreText;
};

/** Toggle drawn as a little brass plaque with a jewel lamp. */
class JewelToggle : public aa::Toggle
{
public:
    JewelToggle (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, const juce::String& label)
        : aa::Toggle (state, paramID, label), text (label) {}

    void setAccent (juce::Colour c) { jewel = c; aa::Toggle::setAccent (c); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::String text;
    juce::Colour jewel { palette::brass };
};

/** Choice stepper restyled as an engraved brass dial window. */
class BrassStepper : public aa::ChoiceBox
{
public:
    BrassStepper (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, const juce::String& label);
    void paint (juce::Graphics&) override;

private:
    juce::AudioParameterChoice* param = nullptr;
    juce::String caption;
};

//==============================================================================
class OrreryLookAndFeel : public aa::LookAndFeel
{
public:
    explicit OrreryLookAndFeel (const aa::Theme& t) : aa::LookAndFeel (t) {}

    void drawKnob (juce::Graphics&, juce::Rectangle<float> bounds, float proportion, bool bipolar, bool hovered,
                   bool dragging, juce::Colour accent, aa::Knob&) override;
    void drawKnobCaption (juce::Graphics&, juce::Rectangle<float> area, const juce::String& text, bool showingValue,
                          juce::Colour accent, aa::Knob&) override;
    juce::Rectangle<float> drawPanel (juce::Graphics&, juce::Rectangle<float> bounds, const juce::String& title,
                                      juce::Colour tint = {}) override;

    /** Engraved panel title with a fading brass rule that ends at ruleEndX. */
    static void drawPanelTitle (juce::Graphics&, juce::Rectangle<float> titleArea, const juce::String& title, float ruleEndX);
};
} // namespace orrery
