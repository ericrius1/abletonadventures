#include "OrreryLookAndFeel.h"

using namespace juce;

namespace orrery
{
aa::Theme makeTheme()
{
    aa::Theme t;
    t.background = palette::navyTop;
    t.backgroundAlt = palette::navyBottom;
    t.panel = Colour (0xb30b1230);
    t.panelOutline = palette::brass.withAlpha (0.45f);
    t.text = palette::parchment;
    t.textDim = palette::parchment.withAlpha (0.62f);
    t.accent = palette::brass;
    t.accent2 = palette::brassLight;
    t.knobBody = Colour (0xff2a2440);
    t.knobTrack = Colour (0x8c03060f);
    t.shadow = Colour (0xaa000000);
    t.popupBackground = Colour (0xf50e1633);
    t.pill = Colour (0x660a0f24);
    t.cornerRadius = 10.0f;
    t.glow = true;
    return t;
}

void drawEngravedText (Graphics& g, const String& text, const Font& font, Point<float> baseline, float kerning)
{
    GlyphArrangement ga;
    ga.addLineOfText (font.withExtraKerningFactor (kerning), text, baseline.x, baseline.y);
    Path p;
    ga.createPath (p);
    const auto b = p.getBounds();

    g.setColour (palette::ink.withAlpha (0.75f));
    g.fillPath (p, AffineTransform::translation (0.0f, 2.0f));
    g.setColour (palette::brassDeep);
    g.strokePath (p, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded), AffineTransform::translation (0.0f, 1.0f));

    ColourGradient grad (palette::brassLight, 0.0f, b.getY(), palette::brassDark, 0.0f, b.getBottom(), false);
    grad.addColour (0.45, palette::brass);
    grad.addColour (0.62, Colour (0xffb98d4a));
    grad.addColour (0.8, Colour (0xffe8c98a));
    g.setGradientFill (grad);
    g.fillPath (p);

    g.setColour (Colours::white.withAlpha (0.18f));
    g.strokePath (p, PathStrokeType (0.6f));
}

void drawSparkle (Graphics& g, Point<float> c, float radius, Colour colour)
{
    Path p;
    const float w = radius * 0.22f;
    p.startNewSubPath (c.x, c.y - radius);
    p.quadraticTo (c.x + w, c.y - w, c.x + radius, c.y);
    p.quadraticTo (c.x + w, c.y + w, c.x, c.y + radius);
    p.quadraticTo (c.x - w, c.y + w, c.x - radius, c.y);
    p.quadraticTo (c.x - w, c.y - w, c.x, c.y - radius);
    p.closeSubPath();
    g.setColour (colour);
    g.fillPath (p);
}

//==============================================================================
void OrreryLookAndFeel::drawKnob (Graphics& g, Rectangle<float> bounds, float proportion, bool bipolar, bool hovered,
                                  bool dragging, Colour accent, aa::Knob& knob)
{
    const float size = jmin (bounds.getWidth(), bounds.getHeight());
    const auto c = bounds.getCentre();
    const float r = size * 0.5f - 1.0f;
    const float startA = -MathConstants<float>::pi * 0.75f;
    const float endA = MathConstants<float>::pi * 0.75f;
    const float valueA = startA + proportion * (endA - startA);
    const bool lively = hovered || dragging;
    const bool enabled = knob.isEnabled();

    // Engraved scale ticks
    for (int i = 0; i <= 10; ++i)
    {
        const float a = startA + (float) i / 10.0f * (endA - startA);
        const bool major = i % 5 == 0;
        const float r0 = r * (major ? 0.9f : 0.94f), r1 = r * 1.0f;
        const Point<float> d (std::sin (a), -std::cos (a));
        g.setColour (palette::brass.withAlpha (major ? 0.55f : 0.3f));
        g.drawLine ({ c + d * r0, c + d * r1 }, major ? 1.2f : 0.9f);
    }

    // Groove + value arc
    const float arcR = r * 0.8f;
    const float arcW = jmax (2.2f, r * 0.085f);
    {
        Path groove;
        groove.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startA, endA, true);
        g.setColour (palette::ink.withAlpha (0.75f));
        g.strokePath (groove, PathStrokeType (arcW + 1.6f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (palette::brass.withAlpha (0.12f));
        g.strokePath (groove, PathStrokeType (0.8f), AffineTransform::translation (0.0f, arcW * 0.5f + 0.8f));
    }
    const float fromA = bipolar ? 0.0f : startA;
    const auto arcColour = lively ? accent.brighter (0.25f) : accent;
    if (enabled)
        aa::LookAndFeel::drawGlowArc (g, c, arcR, jmin (fromA, valueA), jmax (fromA, valueA), arcW, arcColour, true);

    // Brass body
    const float bodyR = r * 0.62f;
    auto body = Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c);
    aa::LookAndFeel::drawSoftShadow (g, body.translated (0.0f, bodyR * 0.18f), Colours::black.withAlpha (0.55f), 5);

    ColourGradient brassGrad (lively ? Colour (0xfffff0c4) : palette::brassLight, body.getX() + bodyR * 0.5f, body.getY() + bodyR * 0.35f,
                              palette::brassDeep, body.getRight(), body.getBottom(), true);
    brassGrad.addColour (0.35, palette::brass);
    brassGrad.addColour (0.75, palette::brassDark);
    g.setGradientFill (brassGrad);
    g.fillEllipse (body);

    // knurled rim
    g.setColour (palette::brassDeep.withAlpha (0.5f));
    const int knurls = 36;
    for (int i = 0; i < knurls; ++i)
    {
        const float a = (float) i / (float) knurls * MathConstants<float>::twoPi;
        const Point<float> d (std::sin (a), -std::cos (a));
        g.drawLine ({ c + d * (bodyR * 0.86f), c + d * (bodyR * 0.99f) }, 0.7f);
    }
    g.setColour (palette::brassLight.withAlpha (0.55f));
    g.drawEllipse (body.reduced (0.5f), 0.8f);

    // Enamel cap
    const float capR = bodyR * 0.7f;
    auto cap = Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c);
    g.setGradientFill (ColourGradient (Colour (0xff24346a), cap.getX() + capR * 0.6f, cap.getY() + capR * 0.4f,
                                       palette::ink, cap.getRight(), cap.getBottom(), true));
    g.fillEllipse (cap);
    g.setColour (palette::brassDeep.withAlpha (0.9f));
    g.drawEllipse (cap, 1.0f);
    g.setColour (Colours::white.withAlpha (0.1f));
    g.drawEllipse (cap.reduced (1.2f), 0.6f);

    const Point<float> dir (std::sin (valueA), -std::cos (valueA));
    String centre;
    if (auto* ok = dynamic_cast<OrreryKnob*> (&knob))
        if (ok->centreText)
            centre = ok->centreText();

    if (centre.isNotEmpty())
    {
        auto font = aa::Fonts::uiBold (jmin (capR * 0.95f, 15.0f));
        g.setFont (font);
        g.setColour (palette::parchment.withAlpha (enabled ? 1.0f : 0.5f));
        g.drawFittedText (centre, cap.reduced (capR * 0.12f, 0.0f).translated (0.0f, 0.5f).toNearestInt(), Justification::centred, 1, 0.6f);

        // jewel pointer on the brass rim
        const auto tip = c + dir * (bodyR * 0.84f);
        g.setColour (accent.withAlpha (0.35f));
        g.fillEllipse (Rectangle<float> (7.0f, 7.0f).withCentre (tip));
        g.setColour (accent.brighter (0.5f));
        g.fillEllipse (Rectangle<float> (3.6f, 3.6f).withCentre (tip));
    }
    else
    {
        const auto p1 = c + dir * (capR * 0.2f);
        const auto p2 = c + dir * (bodyR * 0.9f);
        g.setColour (palette::ink.withAlpha (0.8f));
        g.drawLine ({ p1, p2 }, jmax (2.6f, bodyR * 0.16f));
        g.setColour (palette::parchment);
        g.drawLine ({ p1, p2 }, jmax (1.5f, bodyR * 0.09f));
        g.fillEllipse (Rectangle<float> (3.0f, 3.0f).withCentre (c));
    }

    // glowing tip on the value arc
    if (enabled)
    {
        const auto tip = c + dir * arcR;
        g.setColour (arcColour.withAlpha (0.35f));
        g.fillEllipse (Rectangle<float> (arcW * 2.6f, arcW * 2.6f).withCentre (tip));
        g.setColour (Colours::white.withAlpha (0.9f));
        g.fillEllipse (Rectangle<float> (arcW * 0.95f, arcW * 0.95f).withCentre (tip));
    }
}

void OrreryLookAndFeel::drawKnobCaption (Graphics& g, Rectangle<float> area, const String& text, bool showingValue,
                                         Colour accent, aa::Knob&)
{
    const float h = jlimit (9.0f, 12.0f, area.getHeight() * 0.72f);
    if (showingValue)
    {
        g.setFont (aa::Fonts::uiBold (h));
        g.setColour (accent.brighter (0.35f));
        g.drawFittedText (text, area.toNearestInt(), Justification::centred, 1, 0.7f);
    }
    else
    {
        g.setFont (aa::Fonts::uiBold (h * 0.92f).withExtraKerningFactor (0.08f));
        g.setColour (palette::parchment.withAlpha (0.66f));
        g.drawFittedText (text.toUpperCase(), area.toNearestInt(), Justification::centred, 1, 0.7f);
    }
}

Rectangle<float> OrreryLookAndFeel::drawPanel (Graphics& g, Rectangle<float> bounds, const String& title, Colour)
{
    const float radius = 9.0f;

    // shadow
    for (int i = 4; i > 0; --i)
    {
        g.setColour (Colours::black.withAlpha (0.07f));
        g.fillRoundedRectangle (bounds.translated (0.0f, 2.0f).expanded ((float) i * 1.5f), radius + (float) i * 1.5f);
    }

    g.setGradientFill (ColourGradient (Colour (0xd8121b3e), 0.0f, bounds.getY(), Colour (0xe00a0f26), 0.0f, bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, radius);

    // brass double border
    g.setGradientFill (ColourGradient (palette::brassLight.withAlpha (0.85f), bounds.getX(), bounds.getY(),
                                       palette::brassDark.withAlpha (0.85f), bounds.getRight(), bounds.getBottom(), false));
    g.drawRoundedRectangle (bounds.reduced (0.6f), radius, 1.3f);
    g.setColour (palette::brass.withAlpha (0.22f));
    g.drawRoundedRectangle (bounds.reduced (3.5f), radius - 3.0f, 0.7f);

    // corner rivets
    for (auto p : { bounds.getTopLeft() + Point<float> (7.5f, 7.5f), bounds.getTopRight() + Point<float> (-7.5f, 7.5f),
                    bounds.getBottomLeft() + Point<float> (7.5f, -7.5f), bounds.getBottomRight() + Point<float> (-7.5f, -7.5f) })
    {
        g.setGradientFill (ColourGradient (palette::brassLight, p.x - 1.0f, p.y - 1.0f, palette::brassDark, p.x + 1.5f, p.y + 1.5f, true));
        g.fillEllipse (Rectangle<float> (3.4f, 3.4f).withCentre (p));
    }

    auto content = bounds.reduced (12.0f, 8.0f);
    if (title.isNotEmpty())
    {
        auto titleArea = content.removeFromTop (18.0f);
        drawPanelTitle (g, titleArea, title, titleArea.getRight() - 10.0f);
        content.removeFromTop (2.0f);
    }
    return content;
}

void OrreryLookAndFeel::drawPanelTitle (Graphics& g, Rectangle<float> titleArea, const String& title, float ruleEndX)
{
    titleArea.removeFromLeft (4.0f);
    auto font = aa::Fonts::display (12.5f).withExtraKerningFactor (0.12f);
    g.setFont (font);
    g.setColour (palette::brassLight);
    g.drawText (title.toUpperCase(), titleArea, Justification::centredLeft, false);

    const float tw = aa::Fonts::textWidth (font, title.toUpperCase());
    const float lineY = titleArea.getCentreY() + 0.5f;
    const float x0 = titleArea.getX() + tw + 10.0f, x1 = ruleEndX;
    if (x1 > x0 + 10.0f)
    {
        g.setGradientFill (ColourGradient (palette::brass.withAlpha (0.55f), x0, lineY, palette::brass.withAlpha (0.05f), x1, lineY, false));
        g.fillRect (Rectangle<float> (x0, lineY - 0.5f, x1 - x0, 1.0f));
    }
}

//==============================================================================
void JewelToggle::paintButton (Graphics& g, bool highlighted, bool down)
{
    const bool on = getToggleState();
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const float radius = jmin (6.0f, b.getHeight() * 0.5f);

    g.setColour (palette::ink.withAlpha (on ? 0.45f : 0.6f));
    g.fillRoundedRectangle (b, radius);
    g.setColour (on ? jewel.withAlpha (0.16f) : Colours::transparentBlack);
    g.fillRoundedRectangle (b, radius);
    g.setColour (palette::brass.withAlpha (highlighted ? 0.75f : 0.45f));
    g.drawRoundedRectangle (b.reduced (0.5f), radius, down ? 1.4f : 1.0f);

    // jewel lamp
    const float d = jmin (b.getHeight() - 8.0f, 10.0f);
    auto lamp = Rectangle<float> (d, d).withCentre ({ b.getX() + 6.0f + d * 0.5f, b.getCentreY() });
    if (on)
    {
        g.setColour (jewel.withAlpha (0.28f));
        g.fillEllipse (lamp.expanded (3.5f));
        g.setGradientFill (ColourGradient (Colours::white, lamp.getX() + d * 0.3f, lamp.getY() + d * 0.25f, jewel, lamp.getRight(),
                                           lamp.getBottom(), true));
    }
    else
    {
        g.setGradientFill (ColourGradient (Colour (0xff3a4266), lamp.getX() + d * 0.3f, lamp.getY() + d * 0.25f, palette::ink,
                                           lamp.getRight(), lamp.getBottom(), true));
    }
    g.fillEllipse (lamp);
    g.setColour (palette::brass.withAlpha (0.8f));
    g.drawEllipse (lamp, 0.9f);

    auto textArea = b.withTrimmedLeft (d + 12.0f).withTrimmedRight (4.0f);
    g.setColour (on ? palette::parchment : palette::parchment.withAlpha (0.55f));
    g.setFont (aa::Fonts::uiBold (jlimit (9.0f, 11.5f, b.getHeight() * 0.48f)).withExtraKerningFactor (0.06f));
    g.drawFittedText (text.toUpperCase(), textArea.toNearestInt(), Justification::centredLeft, 1, 0.75f);
}

//==============================================================================
BrassStepper::BrassStepper (AudioProcessorValueTreeState& state, const String& paramID, const String& label)
    : aa::ChoiceBox (state, paramID, label, aa::ChoiceBox::Style::stepper), caption (label)
{
    param = dynamic_cast<AudioParameterChoice*> (state.getParameter (paramID));
}

void BrassStepper::paint (Graphics& g)
{
    if (param == nullptr)
        return;

    auto b = getLocalBounds().toFloat();
    if (caption.isNotEmpty())
    {
        auto labelArea = b.removeFromTop (jlimit (12.0f, 16.0f, b.getHeight() * 0.38f));
        g.setColour (palette::parchment.withAlpha (0.66f));
        g.setFont (aa::Fonts::uiBold (9.5f).withExtraKerningFactor (0.08f));
        g.drawText (caption.toUpperCase(), labelArea, Justification::centred);
    }

    auto box = b.reduced (1.0f);
    const bool hover = isMouseOver (true);
    const float radius = jmin (7.0f, box.getHeight() * 0.5f);

    g.setGradientFill (ColourGradient (palette::ink.withAlpha (0.85f), 0.0f, box.getY(), Colour (0xcc141d40), 0.0f, box.getBottom(), false));
    g.fillRoundedRectangle (box, radius);
    g.setColour (palette::brass.withAlpha (hover ? 0.8f : 0.5f));
    g.drawRoundedRectangle (box.reduced (0.5f), radius, 1.0f);

    auto inner = box.reduced (5.0f, 0.0f);
    auto left = inner.removeFromLeft (9.0f);
    auto right = inner.removeFromRight (9.0f);
    g.setColour (hover ? palette::brassLight : palette::brass.withAlpha (0.7f));
    for (auto [r, pointRight] : { std::pair { left, false }, std::pair { right, true } })
    {
        auto a = r.withSizeKeepingCentre (4.0f, 7.0f);
        Path p;
        p.startNewSubPath (pointRight ? a.getX() : a.getRight(), a.getY());
        p.lineTo (pointRight ? a.getRight() : a.getX(), a.getCentreY());
        p.lineTo (pointRight ? a.getX() : a.getRight(), a.getBottom());
        p.closeSubPath();
        g.fillPath (p);
    }

    g.setColour (palette::parchment);
    g.setFont (aa::Fonts::uiBold (jlimit (9.5f, 12.5f, box.getHeight() * 0.5f)));
    g.drawFittedText (param->getCurrentChoiceName(), inner.toNearestInt(), Justification::centred, 1, 0.6f);
}
} // namespace orrery
