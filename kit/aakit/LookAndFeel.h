#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include "Fonts.h"

namespace aa
{
class Knob;

/** Shared look & feel. Plugins can subclass it and override drawKnob() etc. for a bespoke style. */
class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    explicit LookAndFeel (const Theme& t = {});

    void setTheme (const Theme& t);
    const Theme& theme() const noexcept { return themeData; }

    /** Draws the dial part of an aa::Knob into the given square. */
    virtual void drawKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float proportion,
                           bool bipolar, bool hovered, bool dragging, juce::Colour accent, Knob& knob);

    /** Draws the caption below a knob (label normally, value while interacting). */
    virtual void drawKnobCaption (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text,
                                  bool showingValue, juce::Colour accent, Knob& knob);

    /** Draws a rounded translucent section panel with an optional title. Returns the content area. */
    virtual juce::Rectangle<float> drawPanel (juce::Graphics& g, juce::Rectangle<float> bounds,
                                              const juce::String& title, juce::Colour tint = {});

    // juce::LookAndFeel overrides
    juce::Font getPopupMenuFont() override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon,
                            const juce::Colour* textColour) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override;
    int getPopupMenuBorderSize() override { return 4; }
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;
    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                           float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;

    /** Draws a soft drop-shadow ellipse (cheap: a few translucent rings, no blur). */
    static void drawSoftShadow (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, int rings = 6);

    /** Draws an arc stroke with a glow halo. */
    static void drawGlowArc (juce::Graphics& g, juce::Point<float> centre, float radius, float fromAngle, float toAngle,
                             float thickness, juce::Colour colour, bool glow);

private:
    Fonts::Holder fontHolder;
    Theme themeData;
};

/** Finds the aa::LookAndFeel for a component (falls back to a static default theme). */
LookAndFeel* findLookAndFeel (juce::Component& c);
const Theme& themeFor (juce::Component& c);
} // namespace aa
