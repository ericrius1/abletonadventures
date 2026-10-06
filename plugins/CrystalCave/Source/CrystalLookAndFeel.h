#pragma once

#include <aakit/AdventureKit.h>

/** Crystal Cave's look: faceted gem knobs that turn (and catch the light) with their value,
    frosted-glass panels and airy display-font titles. */
class CrystalLookAndFeel : public aa::LookAndFeel
{
public:
    explicit CrystalLookAndFeel (const aa::Theme& t);

    void drawKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float proportion, bool bipolar, bool hovered,
                   bool dragging, juce::Colour accent, aa::Knob& knob) override;

    void drawKnobCaption (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text, bool showingValue,
                          juce::Colour accent, aa::Knob& knob) override;

    juce::Rectangle<float> drawPanel (juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& title,
                                      juce::Colour tint) override;
};
