#include "GremlinWidgets.h"

using namespace juce;

namespace gremlin
{
//==============================================================================
Colour fxColour (Fx fx)
{
    switch (fx)
    {
        case Fx::stutter:   return pal::green;
        case Fx::reverse:   return pal::cyan;
        case Fx::tapeStop:  return pal::pink;
        case Fx::halfSpeed: return pal::purple;
        case Fx::crush:     return pal::yellow;
        case Fx::gate:      return pal::orange;
        case Fx::scatter:   return pal::lavender;
        case Fx::none:      break;
    }
    return Colour (0xff4a3a66);
}

void drawFxIcon (Graphics& g, Rectangle<float> area, Fx fx, Colour colour, float sw)
{
    const float s = jmin (area.getWidth(), area.getHeight());
    auto r = area.withSizeKeepingCentre (s, s);
    auto P = [&r] (float x, float y) { return Point<float> (r.getX() + x * r.getWidth(), r.getY() + y * r.getHeight()); };
    const PathStrokeType stroke (sw, PathStrokeType::curved, PathStrokeType::rounded);

    g.setColour (colour);
    switch (fx)
    {
        case Fx::stutter:
        {
            const float alphas[] = { 1.0f, 0.62f, 0.32f };
            for (int i = 0; i < 3; ++i)
            {
                g.setColour (colour.withMultipliedAlpha (alphas[i]));
                auto bar = Rectangle<float> (P (0.12f + 0.28f * (float) i, 0.16f), P (0.30f + 0.28f * (float) i, 0.84f));
                g.fillRoundedRectangle (bar, s * 0.05f);
            }
            break;
        }
        case Fx::reverse:
        {
            Path p;
            p.addTriangle (P (0.08f, 0.5f), P (0.48f, 0.18f), P (0.48f, 0.82f));
            p.addTriangle (P (0.50f, 0.5f), P (0.90f, 0.18f), P (0.90f, 0.82f));
            g.fillPath (p);
            break;
        }
        case Fx::tapeStop:
        {
            Path p;
            p.startNewSubPath (P (0.08f, 0.22f));
            p.cubicTo (P (0.45f, 0.20f), P (0.62f, 0.45f), P (0.86f, 0.80f));
            g.strokePath (p, stroke);
            Path head;
            head.addTriangle (P (0.94f, 0.92f), P (0.66f, 0.80f), P (0.88f, 0.60f));
            g.fillPath (head);
            break;
        }
        case Fx::halfSpeed:
        {
            // a snail: spiral shell on a foot
            Path shell;
            const auto c = P (0.56f, 0.50f);
            const int steps = 40;
            for (int i = 0; i <= steps; ++i)
            {
                const float t = (float) i / (float) steps;
                const float a = t * MathConstants<float>::twoPi * 2.1f;
                const float rad = s * (0.04f + 0.27f * t);
                const Point<float> p (c.x + std::cos (a) * rad, c.y + std::sin (a) * rad);
                if (i == 0)
                    shell.startNewSubPath (p);
                else
                    shell.lineTo (p);
            }
            g.strokePath (shell, stroke);
            Path foot;
            foot.startNewSubPath (P (0.06f, 0.84f));
            foot.lineTo (P (0.92f, 0.84f));
            foot.startNewSubPath (P (0.16f, 0.84f));
            foot.lineTo (P (0.10f, 0.52f));
            g.strokePath (foot, stroke);
            g.fillEllipse (Rectangle<float> (s * 0.12f, s * 0.12f).withCentre (P (0.10f, 0.48f)));
            break;
        }
        case Fx::crush:
        {
            const float levels[] = { 0.62f, 0.30f, 0.18f, 0.34f, 0.66f, 0.82f, 0.62f };
            Path p;
            p.startNewSubPath (P (0.06f, levels[0]));
            for (int i = 0; i < 7; ++i)
            {
                const float x0 = 0.06f + 0.126f * (float) i;
                if (i > 0)
                    p.lineTo (P (x0, levels[i]));
                p.lineTo (P (x0 + 0.126f, levels[i]));
            }
            g.strokePath (p, PathStrokeType (sw, PathStrokeType::mitered, PathStrokeType::square));
            break;
        }
        case Fx::gate:
        {
            Path p;
            p.startNewSubPath (P (0.04f, 0.76f));
            p.lineTo (P (0.12f, 0.76f));
            p.lineTo (P (0.12f, 0.24f));
            p.lineTo (P (0.38f, 0.24f));
            p.lineTo (P (0.38f, 0.76f));
            p.lineTo (P (0.58f, 0.76f));
            p.lineTo (P (0.58f, 0.24f));
            p.lineTo (P (0.84f, 0.24f));
            p.lineTo (P (0.84f, 0.76f));
            p.lineTo (P (0.96f, 0.76f));
            g.strokePath (p, PathStrokeType (sw, PathStrokeType::mitered, PathStrokeType::square));
            break;
        }
        case Fx::scatter:
        {
            const float sq[][3] = { { 0.24f, 0.28f, 0.26f }, { 0.70f, 0.24f, 0.18f }, { 0.62f, 0.68f, 0.30f }, { 0.20f, 0.76f, 0.16f } };
            for (int i = 0; i < 4; ++i)
            {
                const auto c = P (sq[i][0], sq[i][1]);
                const float sz = sq[i][2] * s;
                Path p;
                p.addRectangle (Rectangle<float> (sz, sz).withCentre (c));
                p.applyTransform (AffineTransform::rotation (0.35f * (float) (i - 1), c.x, c.y));
                g.setColour (colour.withMultipliedAlpha (i == 2 ? 1.0f : 0.75f));
                g.fillPath (p);
            }
            break;
        }
        case Fx::none:
            g.drawEllipse (r.reduced (s * 0.3f), sw);
            break;
    }
}

void drawChromaticText (Graphics& g, const String& text, const Font& font, Rectangle<float> area, Justification just,
                        Colour main, float split)
{
    g.setFont (font);
    if (split > 0.01f)
    {
        g.setColour (pal::pink.withAlpha (0.75f * main.getFloatAlpha()));
        g.drawText (text, area.translated (-split, 0.0f), just, false);
        g.setColour (pal::cyan.withAlpha (0.75f * main.getFloatAlpha()));
        g.drawText (text, area.translated (split, 0.0f), just, false);
    }
    g.setColour (main);
    g.drawText (text, area, just, false);
}

void drawScanlines (Graphics& g, Rectangle<float> area, float spacing, float alpha)
{
    g.setColour (Colours::black.withAlpha (alpha));
    for (float y = area.getY(); y < area.getBottom(); y += spacing)
        g.fillRect (area.getX(), y, area.getWidth(), spacing * 0.45f);
}

//==============================================================================
void GremlinLookAndFeel::drawKnob (Graphics& g, Rectangle<float> bounds, float proportion, bool bipolar, bool hovered,
                                   bool dragging, Colour accent, aa::Knob& knob)
{
    const float size = jmin (bounds.getWidth(), bounds.getHeight());
    auto square = bounds.withSizeKeepingCentre (size, size);
    const auto centre = square.getCentre();
    const float r = size * 0.5f - 2.0f;
    const float startA = -MathConstants<float>::pi * 0.75f;
    const float endA = MathConstants<float>::pi * 0.75f;
    const float valueA = startA + proportion * (endA - startA);
    const bool hero = (bool) knob.getProperties()["hero"];
    const bool lit = hovered || dragging;

    if (hero)
    {
        // segmented LED ring
        const int segs = 33;
        for (int i = 0; i < segs; ++i)
        {
            const float t = (float) i / (float) (segs - 1);
            const float a = startA + t * (endA - startA);
            const Point<float> dir (std::sin (a), -std::cos (a));
            const auto p1 = centre + dir * (r * 0.80f);
            const auto p2 = centre + dir * (r * 0.97f);
            const bool on = t <= proportion + 0.001f;
            const auto c = t < 0.5f ? pal::green.interpolatedWith (pal::yellow, t * 2.0f)
                                    : pal::yellow.interpolatedWith (pal::pink, (t - 0.5f) * 2.0f);
            if (on)
            {
                g.setColour (c.withAlpha (0.22f));
                g.drawLine ({ p1, p2 }, r * 0.13f);
                g.setColour (c);
                g.drawLine ({ p1, p2 }, r * 0.055f);
            }
            else
            {
                g.setColour (Colours::white.withAlpha (0.07f));
                g.drawLine ({ p1, p2 }, r * 0.05f);
            }
        }

        const float bodyR = r * 0.66f;
        auto body = Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre);
        drawSoftShadow (g, body.translated (0.0f, bodyR * 0.12f), Colours::black.withAlpha (0.6f), 5);
        g.setGradientFill (ColourGradient (Colour (0xff35274f), body.getX() + bodyR * 0.5f, body.getY(),
                                           Colour (0xff120b1d), body.getRight() - bodyR * 0.3f, body.getBottom(), true));
        g.fillEllipse (body);
        g.setColour (Colours::white.withAlpha (lit ? 0.2f : 0.12f));
        g.drawEllipse (body.reduced (0.6f), 1.2f);
        g.setColour (Colours::black.withAlpha (0.35f));
        g.drawEllipse (body.reduced (bodyR * 0.16f), 1.0f);

        // pointer notch
        const Point<float> dir (std::sin (valueA), -std::cos (valueA));
        const auto tipCol = proportion < 0.5f ? pal::green.interpolatedWith (pal::yellow, proportion * 2.0f)
                                              : pal::yellow.interpolatedWith (pal::pink, (proportion - 0.5f) * 2.0f);
        g.setColour (tipCol.withAlpha (0.35f));
        g.drawLine ({ centre + dir * (bodyR * 0.62f), centre + dir * (bodyR * 0.92f) }, bodyR * 0.2f);
        g.setColour (Colours::white);
        g.drawLine ({ centre + dir * (bodyR * 0.64f), centre + dir * (bodyR * 0.9f) }, bodyR * 0.08f);

        // big readout in the middle
        const auto txt = String (roundToInt (proportion * 100.0f)) + "%";
        drawChromaticText (g, txt, aa::Fonts::accent (bodyR * 0.78f), body.translated (0.0f, -bodyR * 0.04f),
                           Justification::centred, pal::text, lit ? 1.6f : 1.0f);
        return;
    }

    const float arcR = r * 0.86f;
    const float arcW = jmax (2.0f, r * 0.12f);
    {
        Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startA, endA, true);
        g.setColour (Colours::black.withAlpha (0.45f));
        g.strokePath (track, PathStrokeType (arcW + 2.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (Colours::white.withAlpha (0.06f));
        g.strokePath (track, PathStrokeType (arcW, PathStrokeType::curved, PathStrokeType::rounded));
    }

    const float fromA = bipolar ? 0.0f : startA;
    drawGlowArc (g, centre, arcR, jmin (fromA, valueA), jmax (fromA, valueA), arcW, lit ? accent.brighter (0.3f) : accent, true);

    const float bodyR = r * 0.6f;
    auto body = Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre);
    drawSoftShadow (g, body.translated (0.0f, bodyR * 0.14f), Colours::black.withAlpha (0.55f), 4);
    g.setGradientFill (ColourGradient (Colour (lit ? 0xff3d2d5a : 0xff31244a), body.getX() + bodyR * 0.5f, body.getY(),
                                       Colour (0xff110a1b), body.getRight() - bodyR * 0.3f, body.getBottom(), true));
    g.fillEllipse (body);
    g.setColour (Colours::white.withAlpha (0.13f));
    g.drawEllipse (body.reduced (0.5f), 1.0f);

    const Point<float> dir (std::sin (valueA), -std::cos (valueA));
    g.setColour (pal::text.withAlpha (0.92f));
    g.drawLine ({ centre + dir * (bodyR * 0.25f), centre + dir * (bodyR * 0.8f) }, jmax (1.8f, bodyR * 0.13f));

    const auto tip = centre + dir * arcR;
    g.setColour (accent.withAlpha (0.4f));
    g.fillEllipse (Rectangle<float> (arcW * 2.4f, arcW * 2.4f).withCentre (tip));
    g.setColour (Colours::white.withAlpha (0.95f));
    g.fillEllipse (Rectangle<float> (arcW * 0.9f, arcW * 0.9f).withCentre (tip));
}

void GremlinLookAndFeel::drawKnobCaption (Graphics& g, Rectangle<float> area, const String& text, bool showingValue,
                                          Colour accent, aa::Knob& knob)
{
    const bool hero = (bool) knob.getProperties()["hero"];
    if (showingValue && ! hero)
    {
        g.setColour (accent.brighter (0.2f));
        g.setFont (aa::Fonts::accent (jlimit (14.0f, 22.0f, area.getHeight() * 1.25f)));
        g.drawFittedText (text, area.toNearestInt().expanded (0, 3), Justification::centred, 1, 0.8f);
        return;
    }

    g.setColour (showingValue ? accent : pal::textDim);
    g.setFont (aa::Fonts::uiBold (jlimit (9.0f, 12.0f, area.getHeight() * 0.72f)).withExtraKerningFactor (0.14f));
    g.drawFittedText (knob.getLabel().toUpperCase(), area.toNearestInt(), Justification::centred, 1, 0.8f);
}

Rectangle<float> GremlinLookAndFeel::drawPanel (Graphics& g, Rectangle<float> bounds, const String& title, Colour tint)
{
    const float radius = 12.0f;
    g.setGradientFill (ColourGradient (Colour (0xff1e1531), bounds.getX(), bounds.getY(),
                                       Colour (0xff140d21), bounds.getX(), bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, radius);

    g.setColour (pal::purple.withAlpha (0.26f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
    g.setColour (Colours::white.withAlpha (0.05f));
    g.drawHorizontalLine ((int) bounds.getY() + 1, bounds.getX() + radius, bounds.getRight() - radius);

    auto content = bounds.reduced (12.0f, 9.0f);
    if (title.isNotEmpty())
    {
        auto titleArea = content.removeFromTop (14.0f);
        const auto bullet = tint.isTransparent() ? pal::green : tint;
        auto sq = Rectangle<float> (6.0f, 6.0f).withCentre ({ titleArea.getX() + 3.0f, titleArea.getCentreY() });
        g.setColour (bullet.withAlpha (0.3f));
        g.fillRect (sq.expanded (2.5f));
        g.setColour (bullet);
        g.fillRect (sq);

        g.setFont (aa::Fonts::uiBold (10.5f).withExtraKerningFactor (0.2f));
        g.setColour (pal::textDim);
        g.drawText (title.toUpperCase(), titleArea.withTrimmedLeft (13.0f), Justification::centredLeft, false);
        content.removeFromTop (4.0f);
    }
    return content;
}

//==============================================================================
FxFader::FxFader (AudioProcessorValueTreeState& state, const String& paramID, Fx f, const String& l)
    : fx (f), label (l)
{
    setSliderStyle (Slider::LinearVertical);
    setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    setSliderSnapsToMousePosition (false);
    setVelocityBasedMode (false);
    setScrollWheelEnabled (true);
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (state, paramID, *this);
    if (auto* p = state.getParameter (paramID))
        setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

FxFader::~FxFader() = default;

Rectangle<float> FxFader::tubeArea() const
{
    auto b = getLocalBounds().toFloat();
    return Rectangle<float> (b.getCentreX() - 8.0f, b.getY() + 27.0f, 16.0f, b.getHeight() - 27.0f - 19.0f);
}

void FxFader::tick (float dt)
{
    if (flashLevel > 0.0f)
    {
        flashLevel = jmax (0.0f, flashLevel - dt * 2.6f);
        repaint();
    }
}

void FxFader::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const auto c = fxColour (fx);
    const float prop = (float) valueToProportionOfLength (getValue());
    const float f = flashLevel;
    const bool lit = hovered || dragging;

    // icon
    auto icon = Rectangle<float> (22.0f, 22.0f).withCentre ({ b.getCentreX(), b.getY() + 12.0f });
    if (f > 0.0f || live)
    {
        const float a = jmax (f, live ? 0.7f : 0.0f);
        g.setGradientFill (ColourGradient (c.withAlpha (0.3f * a), icon.getCentreX(), icon.getCentreY(), c.withAlpha (0.0f),
                                           icon.getCentreX() + 13.0f, icon.getCentreY(), true));
        g.fillEllipse (icon.expanded (2.0f));
    }
    drawFxIcon (g, icon.reduced (1.0f), fx, c.withAlpha (prop > 0.001f ? 0.55f + 0.45f * jmax (f, lit ? 1.0f : 0.0f) : 0.25f), 2.0f);

    // tube
    auto t = tubeArea();
    g.setColour (Colour (0xff0b0713));
    g.fillRoundedRectangle (t, t.getWidth() * 0.5f);
    g.setColour (c.withAlpha (0.18f + 0.25f * f));
    g.drawRoundedRectangle (t.reduced (0.5f), t.getWidth() * 0.5f, 1.0f);

    // ticks
    g.setColour (Colours::white.withAlpha (0.13f));
    for (int i = 1; i < 4; ++i)
    {
        const float y = t.getBottom() - 4.0f - (t.getHeight() - 8.0f) * (float) i / 4.0f;
        g.fillRect (t.getX() - 6.0f, y, 3.0f, 1.0f);
        g.fillRect (t.getRight() + 3.0f, y, 3.0f, 1.0f);
    }

    auto inner = t.reduced (3.5f);
    const float level = inner.getHeight() * prop;
    if (level > 0.5f)
    {
        auto fill = inner.withTop (inner.getBottom() - level);
        g.setColour (c.withAlpha (0.16f + 0.3f * f));
        g.fillRoundedRectangle (fill.expanded (4.0f + 3.0f * f), inner.getWidth() * 0.5f + 4.0f);
        g.setGradientFill (ColourGradient (c.darker (0.55f), 0.0f, inner.getBottom(), c.brighter (0.15f + 0.5f * f), 0.0f,
                                           fill.getY(), false));
        g.fillRoundedRectangle (fill, inner.getWidth() * 0.5f);
        g.setColour (Colours::white.withAlpha (0.35f + 0.4f * f));
        g.fillRoundedRectangle (fill.withWidth (2.0f).translated (2.0f, 0.0f).withTrimmedTop (3.0f).withTrimmedBottom (3.0f), 1.0f);
    }

    // thumb
    const float ty = inner.getBottom() - level;
    auto thumb = Rectangle<float> (t.getWidth() + 14.0f, 7.0f).withCentre ({ t.getCentreX(), ty });
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (thumb.translated (0.0f, 1.5f), 3.5f);
    g.setColour (lit ? Colours::white : pal::text.withAlpha (0.9f));
    g.fillRoundedRectangle (thumb, 3.5f);
    g.setColour (c);
    g.fillRoundedRectangle (thumb.reduced (5.0f, 2.2f), 1.5f);

    // label / value
    auto labelArea = b.removeFromBottom (16.0f);
    if (lit)
    {
        g.setColour (c.brighter (0.2f));
        g.setFont (aa::Fonts::accent (19.0f));
        g.drawText (getTextFromValue (getValue()), labelArea.expanded (0.0f, 2.0f), Justification::centred, false);
    }
    else
    {
        g.setColour (live || f > 0.3f ? c : pal::textDim);
        g.setFont (aa::Fonts::uiBold (9.5f).withExtraKerningFactor (0.08f));
        g.drawFittedText (label.toUpperCase(), labelArea.toNearestInt(), Justification::centred, 1, 0.7f);
    }
}

//==============================================================================
ForcePad::ForcePad (AudioProcessorValueTreeState& state, const String& paramID, Fx f, const String& l)
    : fx (f), label (l)
{
    if (auto* p = state.getParameter (paramID))
    {
        attachment = std::make_unique<ParameterAttachment> (*p, [this] (float v)
        {
            on = v > 0.5f;
            if (! on)
                latched = false;
            repaint();
        }, state.undoManager);
        attachment->sendInitialUpdate();
    }
    setRepaintsOnMouseActivity (false);
}

void ForcePad::mouseDown (const MouseEvent& e)
{
    if (attachment == nullptr)
        return;

    if (e.mods.isShiftDown() || e.mods.isPopupMenu() || e.mods.isCommandDown())
    {
        latched = ! on;
        momentary = false;
        attachment->setValueAsCompleteGesture (latched ? 1.0f : 0.0f);
    }
    else
    {
        momentary = true;
        latched = false;
        attachment->beginGesture();
        attachment->setValueAsPartOfGesture (1.0f);
    }
    press = 1.0f;
    repaint();
}

void ForcePad::mouseUp (const MouseEvent&)
{
    if (attachment != nullptr && momentary)
    {
        attachment->setValueAsPartOfGesture (0.0f);
        attachment->endGesture();
    }
    momentary = false;
    repaint();
}

void ForcePad::tick (float dt, bool engineDoingIt)
{
    const float targetPress = momentary ? 1.0f : 0.0f;
    const float targetGlow = on ? 1.0f : 0.0f;
    const float oldPress = press, oldGlow = glow;
    press += (targetPress - press) * jmin (1.0f, dt * 22.0f);
    glow += (targetGlow - glow) * jmin (1.0f, dt * 14.0f);
    phase += dt;
    const bool wasActive = active;
    active = engineDoingIt;
    if (std::abs (press - oldPress) > 0.002f || std::abs (glow - oldGlow) > 0.002f || active || wasActive != active || on)
        repaint();
}

void ForcePad::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (3.0f);
    const auto c = fxColour (fx);
    const bool hover = isMouseOver (true);
    const float depth = 7.0f;
    const float sink = press * 4.0f;
    const float radius = 10.0f;

    // glow halo
    if (glow > 0.01f)
    {
        const float pulse = 0.85f + 0.15f * std::sin (phase * 9.0f);
        g.setColour (c.withAlpha (0.16f * glow * pulse));
        g.fillRoundedRectangle (b.expanded (3.0f), radius + 3.0f);
        g.setColour (c.withAlpha (0.10f * glow * pulse));
        g.fillRoundedRectangle (b.expanded (6.0f), radius + 6.0f);
    }

    // skirt
    auto skirt = b.withTrimmedTop (depth);
    g.setColour (Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (skirt.translated (0.0f, 2.0f), radius);
    g.setColour (aa::mix (Colour (0xff0e0918), c.darker (0.9f), glow));
    g.fillRoundedRectangle (skirt, radius);

    // top face
    auto face = b.withHeight (b.getHeight() - depth).translated (0.0f, sink);
    const auto topCol = aa::mix (Colour (hover ? 0xff35284d : 0xff2b2040), c.brighter (0.15f), glow);
    const auto botCol = aa::mix (Colour (0xff1b1329), c.darker (0.35f), glow);
    g.setGradientFill (ColourGradient (topCol, 0.0f, face.getY(), botCol, 0.0f, face.getBottom(), false));
    g.fillRoundedRectangle (face, radius);
    g.setColour (Colours::white.withAlpha (0.10f + 0.25f * glow));
    g.drawRoundedRectangle (face.reduced (0.5f), radius, 1.0f);

    if (active && ! on)
    {
        const float pulse = 0.5f + 0.5f * std::sin (phase * 14.0f);
        g.setColour (c.withAlpha (0.35f + 0.4f * pulse));
        g.drawRoundedRectangle (face.reduced (1.5f), radius - 1.0f, 2.0f);
    }

    // icon + label
    auto content = face.reduced (8.0f, 6.0f);
    const auto ink = aa::mix (c, pal::ink, glow);
    auto iconArea = content.removeFromTop (content.getHeight() * 0.58f);
    drawFxIcon (g, iconArea.withSizeKeepingCentre (26.0f, 26.0f), fx, ink, 2.4f);

    g.setColour (aa::mix (pal::text.withAlpha (0.85f), pal::ink, glow));
    g.setFont (aa::Fonts::uiBold (10.5f).withExtraKerningFactor (0.12f));
    g.drawFittedText (label.toUpperCase(), content.toNearestInt(), Justification::centred, 1, 0.7f);

    if (latched && on)
    {
        auto pin = Rectangle<float> (7.0f, 7.0f).withCentre ({ face.getRight() - 11.0f, face.getY() + 11.0f });
        g.setColour (pal::ink);
        g.fillEllipse (pin);
    }
}

//==============================================================================
SeedBox::SeedBox (AudioProcessorValueTreeState& state, const String& paramID)
{
    param = state.getParameter (paramID);
    if (param != nullptr)
    {
        attachment = std::make_unique<ParameterAttachment> (*param, [this] (float v)
        {
            value = roundToInt (v);
            repaint();
        }, state.undoManager);
        attachment->sendInitialUpdate();
    }
    setTooltip ("Seed: drag the number to pick a different pattern, click the die to re-roll. "
                "With Lock on, the same seed always gives the same glitch pattern.");
}

Rectangle<float> SeedBox::dieArea() const
{
    auto b = getLocalBounds().toFloat();
    return b.removeFromRight (b.getHeight()).reduced (5.0f);
}

void SeedBox::setSeed (int v)
{
    if (attachment != nullptr)
        attachment->setValueAsCompleteGesture ((float) jlimit (1, 999, v));
}

void SeedBox::mouseDown (const MouseEvent& e)
{
    if (dieArea().contains (e.position))
    {
        setSeed (1 + random.nextInt (999));
        roll = 1.0f;
        return;
    }
    if (attachment == nullptr)
        return;
    draggingNumber = true;
    dragStartValue = value;
    attachment->beginGesture();
}

void SeedBox::mouseDrag (const MouseEvent& e)
{
    if (! draggingNumber || attachment == nullptr)
        return;
    const int delta = -e.getDistanceFromDragStartY() / 4;
    attachment->setValueAsPartOfGesture ((float) jlimit (1, 999, dragStartValue + delta));
}

void SeedBox::mouseUp (const MouseEvent&)
{
    if (draggingNumber && attachment != nullptr)
        attachment->endGesture();
    draggingNumber = false;
}

void SeedBox::mouseDoubleClick (const MouseEvent& e)
{
    if (param != nullptr && ! dieArea().contains (e.position))
        setSeed (roundToInt (param->convertFrom0to1 (param->getDefaultValue())));
}

void SeedBox::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& wheel)
{
    const float d = std::abs (wheel.deltaY) > std::abs (wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;
    if (d != 0.0f)
        setSeed (value + (d > 0.0f ? 1 : -1));
}

void SeedBox::tick (float dt)
{
    if (roll > 0.0f)
    {
        roll = jmax (0.0f, roll - dt * 2.2f);
        repaint();
    }
}

void SeedBox::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const bool hover = isMouseOver (true);
    const float alpha = dimmed ? 0.55f : 1.0f;

    g.setColour (Colour (0xff0b0713));
    g.fillRoundedRectangle (b, 8.0f);
    g.setColour (pal::purple.withAlpha ((hover ? 0.6f : 0.32f) * alpha));
    g.drawRoundedRectangle (b.reduced (0.5f), 8.0f, 1.0f);

    auto die = dieArea();
    auto text = b.withRight (die.getX() - 2.0f).reduced (8.0f, 2.0f);
    g.setColour (pal::textDim.withMultipliedAlpha (alpha));
    g.setFont (aa::Fonts::uiBold (9.0f).withExtraKerningFactor (0.2f));
    g.drawText ("SEED", text.removeFromTop (14.0f).translated (0.0f, 3.0f), Justification::centredLeft, false);
    drawChromaticText (g, String::formatted ("%03d", value), aa::Fonts::accent (jmin (34.0f, text.getHeight() * 1.25f)),
                       text.translated (0.0f, -1.0f), Justification::centredLeft, pal::green.withAlpha (alpha), 1.0f);
    drawScanlines (g, b.reduced (2.0f), 3.0f, 0.18f);

    // the die
    const float angle = roll * roll * MathConstants<float>::twoPi * 1.5f;
    const auto dc = die.getCentre();
    Graphics::ScopedSaveState save (g);
    g.addTransform (AffineTransform::rotation (angle, dc.x, dc.y));
    auto body = die.reduced (2.0f);
    g.setColour (pal::pink.withAlpha (hover ? 0.35f : 0.18f));
    g.fillRoundedRectangle (body.expanded (2.5f), 7.0f);
    g.setGradientFill (ColourGradient (pal::pink.brighter (0.2f), body.getX(), body.getY(), pal::pink.darker (0.4f),
                                       body.getRight(), body.getBottom(), false));
    g.fillRoundedRectangle (body, 6.0f);
    g.setColour (Colours::white.withAlpha (0.35f));
    g.drawRoundedRectangle (body.reduced (0.5f), 6.0f, 1.0f);

    const int pips = (value % 6) + 1;
    static const float layouts[6][6][2] = {
        { { 0.5f, 0.5f } },
        { { 0.28f, 0.28f }, { 0.72f, 0.72f } },
        { { 0.26f, 0.26f }, { 0.5f, 0.5f }, { 0.74f, 0.74f } },
        { { 0.28f, 0.28f }, { 0.72f, 0.28f }, { 0.28f, 0.72f }, { 0.72f, 0.72f } },
        { { 0.26f, 0.26f }, { 0.74f, 0.26f }, { 0.5f, 0.5f }, { 0.26f, 0.74f }, { 0.74f, 0.74f } },
        { { 0.28f, 0.24f }, { 0.72f, 0.24f }, { 0.28f, 0.5f }, { 0.72f, 0.5f }, { 0.28f, 0.76f }, { 0.72f, 0.76f } },
    };
    const float pip = body.getWidth() * 0.17f;
    g.setColour (pal::ink);
    for (int i = 0; i < pips; ++i)
        g.fillEllipse (Rectangle<float> (pip, pip).withCentre ({ body.getX() + body.getWidth() * layouts[pips - 1][i][0],
                                                                 body.getY() + body.getHeight() * layouts[pips - 1][i][1] }));
}

//==============================================================================
Timeline::Timeline (GremlinProcessor& p) : processor (p)
{
    setTooltip ("The gremlin's diary: every grid step lights up in the colour of the trick it played. "
                "With Lock on, the steps ahead of the playhead show what's coming.");
    setInterceptsMouseClicks (true, false);
}

void Timeline::resetSlots()
{
    for (auto& s : slots)
        s = Slot {};
}

void Timeline::addEvent (const StepEvent& e)
{
    if (gridBeats <= 0.0 || numSlots <= 0)
        return;

    const double page = pageLenPpq;
    double pos = std::fmod (e.ppq, page);
    if (pos < 0.0)
        pos += page;
    const int idx = jlimit (0, numSlots - 1, (int) std::floor (pos / gridBeats + 1.0e-6));
    const int span = jmax (1, roundToInt (e.lengthPpq / gridBeats));

    for (int k = 0; k < span; ++k)
    {
        auto& s = slots[(size_t) ((idx + k) % numSlots)];
        s.fx = e.fx;
        s.forced = e.forced;
        s.continuation = k > 0;
        s.slices = e.slices;
        s.ppq = e.ppq + (double) k * gridBeats;
    }

    if (e.fx != 0)
        flash = 1.0f;
}

void Timeline::tick (double dt)
{
    auto& eng = processor.engine;
    const double ppqPerBar = eng.uiPpqPerBar.load();
    const int grid = processor.gridIndex();
    const int lockBars = processor.lockBars();
    locked = lockBars > 0;

    if (grid != lastGrid || lockBars != lastLockBars || std::abs (ppqPerBar - lastPpqPerBar) > 1.0e-6)
    {
        lastGrid = grid;
        lastLockBars = lockBars;
        lastPpqPerBar = ppqPerBar;
        gridBeats = gridBeatsFor (grid);
        pageLenPpq = (double) (locked ? lockBars : 2) * ppqPerBar;
        numSlots = jlimit (1, (int) slots.size(), roundToInt (pageLenPpq / gridBeats));
        pageLenPpq = numSlots * gridBeats;
        resetSlots();
        backdrop = {};
    }

    // follow the host clock smoothly between audio blocks
    const double host = eng.uiPpq.load();
    const double bpm = eng.uiBpm.load();
    ppq += dt * bpm / 60.0;
    const double err = host - ppq;
    if (std::abs (err) > 0.5)
        ppq = host;
    else
        ppq += err * jmin (1.0, dt * 6.0);
    lastHostPpq = host;

    // locked: predict the rest of the page
    if (locked)
    {
        const auto dice = processor.diceSettings (ppqPerBar);
        const double pageStart = std::floor (ppq / pageLenPpq) * pageLenPpq;
        int remaining = 0;
        for (int k = 0; k < numSlots; ++k)
        {
            if (remaining > 0)
            {
                --remaining;
                predictedCont[(size_t) k] = true;
                predicted[(size_t) k] = predicted[(size_t) (k - 1)];
                continue;
            }
            predictedCont[(size_t) k] = false;
            predicted[(size_t) k] = lockedDecision (dice, processor.seedValue(), lockBars, grid,
                                                    pageStart + k * gridBeats, ppqPerBar);
            remaining = predicted[(size_t) k].span - 1;
        }
    }

    flash = jmax (0.0f, flash - (float) dt * 3.0f);
    repaint();
}

void Timeline::rebuildBackdrop (float scale)
{
    scale = jlimit (1.0f, 4.0f, scale);
    backdrop = Image (Image::ARGB, jmax (1, roundToInt ((float) getWidth() * scale)), jmax (1, roundToInt ((float) getHeight() * scale)), true);
    Graphics g (backdrop);
    g.addTransform (AffineTransform::scale (scale));

    auto b = getLocalBounds().toFloat();
    g.setColour (Colour (0xff0b0713));
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (pal::purple.withAlpha (0.3f));
    g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);

    strip = b.reduced (10.0f, 0.0f).withTrimmedTop (17.0f).withTrimmedBottom (7.0f);
    const float slotW = strip.getWidth() / (float) numSlots;
    const double ppqPerBar = processor.engine.uiPpqPerBar.load();
    const int stepsPerBar = jmax (1, roundToInt (ppqPerBar / gridBeats));
    const int stepsPerBeat = jmax (1, roundToInt (1.0 / gridBeats));

    for (int k = 0; k <= numSlots; ++k)
    {
        const float x = strip.getX() + slotW * (float) k;
        const bool bar = k % stepsPerBar == 0;
        const bool beat = k % stepsPerBeat == 0;
        g.setColour (Colours::white.withAlpha (bar ? 0.22f : (beat ? 0.09f : 0.035f)));
        g.fillRect (x - 0.5f, bar ? b.getY() + 5.0f : strip.getY(), 1.0f, bar ? strip.getBottom() - b.getY() - 5.0f : strip.getHeight());

        if (bar && k < numSlots)
        {
            g.setFont (aa::Fonts::accent (15.0f));
            g.setColour (pal::textDim);
            g.drawText ("BAR " + String (k / stepsPerBar + 1), Rectangle<float> (x + 5.0f, b.getY() + 2.0f, 80.0f, 15.0f),
                        Justification::centredLeft, false);
        }
    }

    g.setFont (aa::Fonts::accent (15.0f));
    g.setColour (pal::textDim.withAlpha (0.5f));
    g.drawText (locked ? "LOCKED LOOP" : "FREE ROAM", Rectangle<float> (b.getRight() - 150.0f, b.getY() + 2.0f, 140.0f, 15.0f),
                Justification::centredRight, false);
}

void Timeline::paint (Graphics& g)
{
    const float physScale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (backdrop.isNull() || std::abs ((float) backdrop.getWidth() - (float) getWidth() * jlimit (1.0f, 4.0f, physScale)) > 2.0f)
        rebuildBackdrop (physScale);

    auto b = getLocalBounds().toFloat();
    g.drawImage (backdrop, b);

    const float slotW = strip.getWidth() / (float) numSlots;
    double phase = std::fmod (ppq, pageLenPpq) / pageLenPpq;
    if (phase < 0.0)
        phase += 1.0;
    const int cur = jlimit (0, numSlots - 1, (int) std::floor (phase * numSlots));
    const float headX = strip.getX() + (float) phase * strip.getWidth();
    const float gap = jmin (2.0f, slotW * 0.12f);

    auto blockRect = [&] (int k, bool cont, bool nextCont)
    {
        auto r = Rectangle<float> (strip.getX() + slotW * (float) k, strip.getY(), slotW, strip.getHeight()).reduced (gap, 2.0f);
        if (cont)
            r = r.withLeft (r.getX() - gap * 2.0f - 0.5f);
        if (nextCont)
            r = r.withRight (r.getRight() + gap * 2.0f + 0.5f);
        return r;
    };

    auto drawBlock = [&] (Rectangle<float> r, Fx fx, float alpha, bool hollow, bool forced, bool showIcon)
    {
        const auto c = fxColour (fx);
        const float rad = jmin (4.0f, r.getWidth() * 0.3f);
        if (fx == Fx::none)
        {
            g.setColour (Colours::white.withAlpha (0.2f * alpha));
            g.fillRoundedRectangle (r.withTop (r.getBottom() - 3.0f), 1.5f);
            return;
        }
        if (hollow)
        {
            g.setColour (c.withAlpha (0.10f * alpha));
            g.fillRoundedRectangle (r, rad);
            g.setColour (c.withAlpha (0.55f * alpha));
            g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
        }
        else
        {
            g.setColour (c.withAlpha (0.22f * alpha));
            g.fillRoundedRectangle (r.expanded (1.5f), rad + 1.5f);
            g.setGradientFill (ColourGradient (c.withAlpha (alpha), 0.0f, r.getY(), c.darker (0.5f).withAlpha (alpha), 0.0f,
                                               r.getBottom(), false));
            g.fillRoundedRectangle (r, rad);
            if (forced)
            {
                g.setColour (pal::ink.withAlpha (0.35f * alpha));
                for (float x = r.getX() - r.getHeight(); x < r.getRight(); x += 6.0f)
                {
                    Graphics::ScopedSaveState s (g);
                    g.reduceClipRegion (r.toNearestInt());
                    g.drawLine (x, r.getBottom(), x + r.getHeight(), r.getY(), 2.0f);
                }
            }
        }
        if (showIcon && r.getWidth() >= 16.0f)
            drawFxIcon (g, r.withSizeKeepingCentre (jmin (16.0f, r.getWidth() - 4.0f), 16.0f), fx,
                        hollow ? c.withAlpha (0.7f * alpha) : pal::ink.withAlpha (0.75f * alpha), 1.6f);
    };

    for (int k = 0; k < numSlots; ++k)
    {
        const auto& s = slots[(size_t) k];
        const bool ahead = k > cur;
        const bool fresh = s.fx >= 0 && s.ppq > ppq - pageLenPpq + gridBeats * 0.5;
        const bool nextCont = k + 1 < numSlots && slots[(size_t) k + 1].continuation && slots[(size_t) k + 1].fx == s.fx;

        if (! ahead && fresh)
        {
            const float age = (float) jlimit (0.0, 1.0, (ppq - s.ppq) / pageLenPpq);
            const float a = 1.0f - 0.45f * age;
            drawBlock (blockRect (k, s.continuation, nextCont), (Fx) s.fx, a, false, s.forced, ! s.continuation);
        }
        else if (locked)
        {
            const auto& d = predicted[(size_t) k];
            const bool pNext = k + 1 < numSlots && predictedCont[(size_t) k + 1];
            drawBlock (blockRect (k, predictedCont[(size_t) k], pNext), d.fx, ahead ? 0.85f : 0.4f, true, false,
                       ! predictedCont[(size_t) k]);
        }
        else if (s.fx >= 0)
        {
            drawBlock (blockRect (k, s.continuation, nextCont), (Fx) s.fx, 0.22f, false, s.forced, false);
        }
        else
        {
            auto r = blockRect (k, false, false);
            g.setColour (Colours::white.withAlpha (0.05f));
            g.fillRoundedRectangle (r.withTop (r.getBottom() - 3.0f), 1.5f);
        }
    }

    // phosphor trail + scanline playhead
    {
        Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (strip.expanded (0.0f, 2.0f).toNearestInt());
        const float trail = 70.0f;
        g.setGradientFill (ColourGradient (pal::green.withAlpha (0.0f), headX - trail, 0.0f,
                                           pal::green.withAlpha (0.16f + 0.1f * flash), headX, 0.0f, false));
        g.fillRect (Rectangle<float> (headX - trail, strip.getY(), trail, strip.getHeight()));
    }
    g.setColour (pal::green.withAlpha (0.25f + 0.2f * flash));
    g.fillRect (Rectangle<float> (headX - 3.0f, strip.getY() - 3.0f, 6.0f, strip.getHeight() + 6.0f));
    g.setColour (Colours::white.withAlpha (0.95f));
    g.fillRect (Rectangle<float> (headX - 1.0f, strip.getY() - 3.0f, 2.0f, strip.getHeight() + 6.0f));

    drawScanlines (g, b.reduced (2.0f), 3.0f, 0.16f);
}
} // namespace gremlin
