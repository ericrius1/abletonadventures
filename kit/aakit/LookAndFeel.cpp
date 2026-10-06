#include "LookAndFeel.h"
#include "Widgets.h"

namespace aa
{
using namespace juce;

LookAndFeel::LookAndFeel (const Theme& t)
{
    setTheme (t);
}

void LookAndFeel::setTheme (const Theme& t)
{
    themeData = t;

    setColour (PopupMenu::backgroundColourId, t.popupBackground);
    setColour (PopupMenu::textColourId, t.text);
    setColour (PopupMenu::highlightedBackgroundColourId, t.accent.withAlpha (0.25f));
    setColour (PopupMenu::highlightedTextColourId, t.text);
    setColour (TooltipWindow::backgroundColourId, t.popupBackground);
    setColour (TooltipWindow::textColourId, t.text);
    setColour (TooltipWindow::outlineColourId, t.accent.withAlpha (0.4f));
    setColour (Label::textColourId, t.text);
    setColour (Slider::textBoxTextColourId, t.text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, t.popupBackground);
    setColour (Slider::textBoxHighlightColourId, t.accent.withAlpha (0.4f));
    setColour (TextEditor::backgroundColourId, t.popupBackground);
    setColour (TextEditor::textColourId, t.text);
    setColour (TextEditor::highlightColourId, t.accent.withAlpha (0.4f));
    setColour (TextEditor::outlineColourId, t.accent.withAlpha (0.4f));
    setColour (TextEditor::focusedOutlineColourId, t.accent);
    setColour (CaretComponent::caretColourId, t.accent);
    setColour (ResizableWindow::backgroundColourId, t.background);
    setColour (AlertWindow::backgroundColourId, t.popupBackground);
    setColour (AlertWindow::textColourId, t.text);
    setColour (AlertWindow::outlineColourId, t.accent.withAlpha (0.4f));
    setColour (TextButton::buttonColourId, t.knobBody);
    setColour (TextButton::textColourOffId, t.text);
    setColour (TextButton::textColourOnId, t.text);
    setColour (ComboBox::backgroundColourId, t.knobBody);
    setColour (ComboBox::textColourId, t.text);
    setColour (ComboBox::outlineColourId, t.panelOutline);
    setColour (ComboBox::arrowColourId, t.accent);
}

//==============================================================================
void LookAndFeel::drawSoftShadow (Graphics& g, Rectangle<float> area, Colour colour, int rings)
{
    const float baseAlpha = colour.getFloatAlpha() / (float) rings;
    for (int i = rings; i > 0; --i)
    {
        const float grow = (float) i * area.getWidth() * 0.018f;
        g.setColour (colour.withAlpha (baseAlpha * 0.9f));
        g.fillEllipse (area.expanded (grow));
    }
}

void LookAndFeel::drawGlowArc (Graphics& g, Point<float> centre, float radius, float fromAngle, float toAngle,
                               float thickness, Colour colour, bool glow)
{
    if (std::abs (toAngle - fromAngle) < 0.001f)
        return;

    Path arc;
    arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, fromAngle, toAngle, true);

    if (glow)
    {
        g.setColour (colour.withMultipliedAlpha (0.10f));
        g.strokePath (arc, PathStrokeType (thickness * 3.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (colour.withMultipliedAlpha (0.18f));
        g.strokePath (arc, PathStrokeType (thickness * 1.9f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    g.setColour (colour);
    g.strokePath (arc, PathStrokeType (thickness, PathStrokeType::curved, PathStrokeType::rounded));
}

void LookAndFeel::drawKnob (Graphics& g, Rectangle<float> bounds, float proportion, bool bipolar,
                            bool hovered, bool dragging, Colour accent, Knob&)
{
    const auto& t = themeData;
    const float size = jmin (bounds.getWidth(), bounds.getHeight());
    auto square = bounds.withSizeKeepingCentre (size, size);
    const auto centre = square.getCentre();
    const float r = size * 0.5f - 2.0f;

    const float startA = -MathConstants<float>::pi * 0.75f;
    const float endA = MathConstants<float>::pi * 0.75f;
    const float valueA = startA + proportion * (endA - startA);

    // Track + value arc
    const float arcR = r * 0.86f;
    const float arcW = jmax (2.0f, r * 0.13f);

    {
        Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startA, endA, true);
        g.setColour (t.knobTrack);
        g.strokePath (track, PathStrokeType (arcW, PathStrokeType::curved, PathStrokeType::rounded));
    }

    const float fromA = bipolar ? 0.0f : startA;
    const auto arcColour = accent.interpolatedWith (t.accent2, bipolar ? std::abs (proportion - 0.5f) : proportion * 0.6f);
    drawGlowArc (g, centre, arcR, jmin (fromA, valueA), jmax (fromA, valueA), arcW,
                 hovered || dragging ? arcColour.brighter (0.25f) : arcColour, t.glow);

    // Body
    const float bodyR = r * 0.64f;
    auto body = Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre);
    drawSoftShadow (g, body.translated (0.0f, bodyR * 0.16f), t.shadow.withMultipliedAlpha (0.7f), 5);

    ColourGradient bodyGrad (t.knobBody.brighter (hovered ? 0.55f : 0.4f), body.getX() + bodyR * 0.4f, body.getY(),
                             t.knobBody.darker (0.5f), body.getRight() - bodyR * 0.3f, body.getBottom(), true);
    g.setGradientFill (bodyGrad);
    g.fillEllipse (body);

    g.setColour (Colours::white.withAlpha (0.14f));
    g.drawEllipse (body.reduced (0.5f), 1.0f);

    // Inner dimple ring
    g.setColour (Colours::black.withAlpha (0.18f));
    g.drawEllipse (body.reduced (bodyR * 0.22f), 1.0f);

    // Pointer
    const float px = std::sin (valueA), py = -std::cos (valueA);
    const auto p1 = centre + Point<float> (px, py) * (bodyR * 0.28f);
    const auto p2 = centre + Point<float> (px, py) * (bodyR * 0.82f);
    g.setColour (t.text.withAlpha (0.92f));
    g.drawLine ({ p1, p2 }, jmax (2.0f, bodyR * 0.14f));

    // Glowing tip on the arc
    const auto tip = centre + Point<float> (px, py) * arcR;
    g.setColour (arcColour.withAlpha (0.35f));
    g.fillEllipse (Rectangle<float> (arcW * 2.4f, arcW * 2.4f).withCentre (tip));
    g.setColour (Colours::white.withAlpha (0.9f));
    g.fillEllipse (Rectangle<float> (arcW * 0.9f, arcW * 0.9f).withCentre (tip));
}

void LookAndFeel::drawKnobCaption (Graphics& g, Rectangle<float> area, const String& text, bool showingValue,
                                   Colour accent, Knob&)
{
    const auto& t = themeData;
    const float h = jlimit (9.0f, 15.0f, area.getHeight() * 0.85f);
    g.setFont (showingValue ? Fonts::uiBold (h) : Fonts::ui (h));
    g.setColour (showingValue ? accent.brighter (0.3f) : t.textDim);
    g.drawFittedText (showingValue ? text : text.toUpperCase(), area.toNearestInt(), Justification::centred, 1, 0.8f);
}

Rectangle<float> LookAndFeel::drawPanel (Graphics& g, Rectangle<float> bounds, const String& title, Colour tint)
{
    const auto& t = themeData;
    const auto fill = tint.isTransparent() ? t.panel : tint;

    g.setGradientFill (ColourGradient (fill.withMultipliedAlpha (1.15f), bounds.getX(), bounds.getY(),
                                       fill.withMultipliedAlpha (0.75f), bounds.getX(), bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, t.cornerRadius);

    g.setColour (t.panelOutline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), t.cornerRadius, 1.0f);

    // Top sheen
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.06f), bounds.getX(), bounds.getY(),
                                       Colours::transparentWhite, bounds.getX(), bounds.getY() + 24.0f, false));
    g.fillRoundedRectangle (bounds.withHeight (jmin (24.0f, bounds.getHeight())), t.cornerRadius);

    auto content = bounds.reduced (10.0f, 8.0f);
    if (title.isNotEmpty())
    {
        auto titleArea = content.removeFromTop (16.0f);
        g.setFont (Fonts::uiBold (11.5f).withExtraKerningFactor (0.12f));
        g.setColour (t.textDim);
        g.drawText (title.toUpperCase(), titleArea, Justification::centredLeft, false);

        // little accent dot
        g.setColour (t.accent);
        const float tw = Fonts::textWidth (Fonts::uiBold (11.5f).withExtraKerningFactor (0.12f), title.toUpperCase());
        g.fillEllipse (titleArea.getX() + tw + 6.0f, titleArea.getCentreY() - 2.5f, 5.0f, 5.0f);
        content.removeFromTop (2.0f);
    }
    return content;
}

//==============================================================================
Font LookAndFeel::getPopupMenuFont()               { return Fonts::ui (15.0f); }
Font LookAndFeel::getComboBoxFont (ComboBox&)      { return Fonts::ui (14.0f); }
Font LookAndFeel::getLabelFont (Label& l)          { return Fonts::ui (jmax (10.0f, l.getFont().getHeight())); }

void LookAndFeel::drawPopupMenuBackground (Graphics& g, int width, int height)
{
    const auto& t = themeData;
    auto r = Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    g.fillAll (t.popupBackground);
    g.setColour (t.accent.withAlpha (0.35f));
    g.drawRect (r, 1.0f);
}

void LookAndFeel::drawPopupMenuItem (Graphics& g, const Rectangle<int>& area, bool isSeparator, bool isActive,
                                     bool isHighlighted, bool isTicked, bool hasSubMenu, const String& text,
                                     const String& shortcutKeyText, const Drawable*, const Colour*)
{
    const auto& t = themeData;

    if (isSeparator)
    {
        auto r = area.toFloat().reduced (8.0f, 0.0f);
        g.setColour (t.textDim.withAlpha (0.25f));
        g.fillRect (r.withSizeKeepingCentre (r.getWidth(), 1.0f));
        return;
    }

    auto r = area.toFloat().reduced (3.0f, 1.0f);

    if (isHighlighted && isActive)
    {
        g.setColour (t.accent.withAlpha (0.22f));
        g.fillRoundedRectangle (r, 5.0f);
    }

    auto textArea = r.reduced (10.0f, 0.0f);
    auto tickArea = textArea.removeFromLeft (14.0f);

    if (isTicked)
    {
        g.setColour (t.accent);
        g.fillEllipse (tickArea.withSizeKeepingCentre (7.0f, 7.0f));
    }

    g.setColour (isActive ? (isHighlighted ? t.text : t.text.withAlpha (0.88f)) : t.textDim.withAlpha (0.5f));
    g.setFont (getPopupMenuFont());
    g.drawFittedText (text, textArea.toNearestInt(), Justification::centredLeft, 1);

    if (hasSubMenu)
    {
        auto arrow = textArea.removeFromRight (10.0f).withSizeKeepingCentre (6.0f, 10.0f);
        Path p;
        p.startNewSubPath (arrow.getX(), arrow.getY());
        p.lineTo (arrow.getRight(), arrow.getCentreY());
        p.lineTo (arrow.getX(), arrow.getBottom());
        g.strokePath (p, PathStrokeType (1.5f));
    }
    else if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (t.textDim);
        g.setFont (Fonts::ui (12.0f));
        g.drawText (shortcutKeyText, textArea, Justification::centredRight);
    }
}

void LookAndFeel::getIdealPopupMenuItemSize (const String& text, bool isSeparator, int standardMenuItemHeight,
                                             int& idealWidth, int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth = 50;
        idealHeight = 9;
        return;
    }

    idealHeight = standardMenuItemHeight > 0 ? standardMenuItemHeight : 26;
    idealWidth = (int) Fonts::textWidth (getPopupMenuFont(), text) + 50;
}

void LookAndFeel::drawTooltip (Graphics& g, const String& text, int width, int height)
{
    const auto& t = themeData;
    auto r = Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    g.setColour (t.popupBackground);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (t.accent.withAlpha (0.4f));
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (t.text);
    g.setFont (Fonts::ui (13.0f));
    g.drawFittedText (text, r.reduced (8.0f, 4.0f).toNearestInt(), Justification::centredLeft, 4);
}

Rectangle<int> LookAndFeel::getTooltipBounds (const String& tipText, Point<int> screenPos, Rectangle<int> parentArea)
{
    const int w = jmin (320, (int) Fonts::textWidth (Fonts::ui (13.0f), tipText) + 20);
    const int lines = jmax (1, (int) std::ceil (Fonts::textWidth (Fonts::ui (13.0f), tipText) / 300.0f));
    const int h = 10 + lines * 17;
    return Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                           screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin (parentArea);
}

void LookAndFeel::drawCornerResizer (Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
{
    const auto c = themeData.textDim.withAlpha (isMouseOver || isMouseDragging ? 0.8f : 0.35f);
    g.setColour (c);
    for (int i = 1; i <= 3; ++i)
    {
        const float d = (float) i * (float) jmin (w, h) / 4.0f;
        g.drawLine ((float) w - d, (float) h - 2.0f, (float) w - 2.0f, (float) h - d, 1.3f);
    }
}

void LookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int width, int height, float sliderPos,
                                    float minSliderPos, float maxSliderPos, Slider::SliderStyle style, Slider& slider)
{
    if (style != Slider::LinearVertical && style != Slider::LinearHorizontal && style != Slider::LinearBar)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const auto& t = themeData;
    const auto accent = slider.findColour (Slider::trackColourId).isOpaque() ? slider.findColour (Slider::trackColourId)
                                                                             : t.accent;
    auto r = Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const bool vertical = style == Slider::LinearVertical;
    const float thickness = jmax (4.0f, (vertical ? r.getWidth() : r.getHeight()) * 0.28f);

    auto track = vertical ? r.withSizeKeepingCentre (thickness, r.getHeight())
                          : r.withSizeKeepingCentre (r.getWidth(), thickness);
    g.setColour (t.knobTrack);
    g.fillRoundedRectangle (track, thickness * 0.5f);

    Rectangle<float> filled = vertical ? track.withTop (sliderPos) : track.withRight (sliderPos);
    g.setColour (accent.withAlpha (0.25f));
    g.fillRoundedRectangle (filled.expanded (2.0f), thickness * 0.5f + 2.0f);
    g.setGradientFill (vertical ? ColourGradient (accent, 0.0f, track.getBottom(), t.accent2, 0.0f, track.getY(), false)
                                : ColourGradient (accent, track.getX(), 0.0f, t.accent2, track.getRight(), 0.0f, false));
    g.fillRoundedRectangle (filled, thickness * 0.5f);

    const float thumb = thickness * 2.1f;
    auto thumbPos = vertical ? Point<float> (track.getCentreX(), sliderPos) : Point<float> (sliderPos, track.getCentreY());
    g.setColour (t.shadow);
    g.fillEllipse (Rectangle<float> (thumb, thumb).withCentre (thumbPos.translated (0.0f, 1.5f)));
    g.setColour (t.text);
    g.fillEllipse (Rectangle<float> (thumb, thumb).withCentre (thumbPos));
    g.setColour (accent);
    g.fillEllipse (Rectangle<float> (thumb * 0.45f, thumb * 0.45f).withCentre (thumbPos));
}

//==============================================================================
LookAndFeel* findLookAndFeel (Component& c)
{
    return dynamic_cast<LookAndFeel*> (&c.getLookAndFeel());
}

const Theme& themeFor (Component& c)
{
    if (auto* l = findLookAndFeel (c))
        return l->theme();

    static const Theme fallback;
    return fallback;
}
} // namespace aa
