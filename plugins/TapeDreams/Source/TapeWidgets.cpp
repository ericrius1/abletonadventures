#include "TapeWidgets.h"

using namespace juce;

namespace tapeui
{
namespace
{
    constexpr float pi = MathConstants<float>::pi;

    Point<float> polar (Point<float> c, float radius, float angle)
    {
        return { c.x + radius * std::sin (angle), c.y - radius * std::cos (angle) };
    }
} // namespace

Image makeLayer (int width, int height, float scale)
{
    return Image (Image::ARGB, jmax (1, roundToInt ((float) width * scale)), jmax (1, roundToInt ((float) height * scale)), true);
}

void drawTextCentred (Graphics& g, const String& text, const Font& font, Point<float> centre, float rotation, Colour colour)
{
    GlyphArrangement ga;
    ga.addLineOfText (font, text, 0.0f, 0.0f);
    Path p;
    ga.createPath (p);
    const auto b = p.getBounds();
    p.applyTransform (AffineTransform::translation (-b.getCentreX(), -b.getCentreY())
                          .rotated (rotation)
                          .translated (centre.x, centre.y));
    g.setColour (colour);
    g.fillPath (p);
}

void drawScrew (Graphics& g, Point<float> c, float r, float angle)
{
    auto disc = Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c);
    g.setColour (Colours::black.withAlpha (0.28f));
    g.fillEllipse (disc.translated (0.0f, r * 0.18f).expanded (r * 0.12f));
    g.setGradientFill (ColourGradient (Colour (0xfff1ece2), c.x - r * 0.6f, c.y - r * 0.7f,
                                       Colour (0xff857b6e), c.x + r * 0.8f, c.y + r * 0.9f, true));
    g.fillEllipse (disc);
    g.setColour (Colours::black.withAlpha (0.35f));
    g.drawEllipse (disc, jmax (0.5f, r * 0.12f));

    for (float extra : { 0.0f, pi * 0.5f })
    {
        Path slot;
        slot.addRoundedRectangle (-r * 0.72f, -r * 0.13f, r * 1.44f, r * 0.26f, r * 0.1f);
        slot.applyTransform (AffineTransform::rotation (angle + extra).translated (c.x, c.y));
        g.setColour (Colour (0xff3b332b).withAlpha (0.85f));
        g.fillPath (slot);
    }
}

//==============================================================================
void TapeLookAndFeel::drawKnob (Graphics& g, Rectangle<float> bounds, float proportion, bool, bool hovered,
                                bool dragging, Colour accent, aa::Knob& knob)
{
    const bool onDark = (bool) knob.getProperties().getWithDefault ("onDark", false);
    const float size = jmin (bounds.getWidth(), bounds.getHeight());
    const auto c = bounds.getCentre();
    const float r = size * 0.5f - 1.0f;
    const float startA = -pi * 0.75f, endA = pi * 0.75f;
    const float valueA = startA + proportion * (endA - startA);

    // Tick marks: lit up to the value in the knob's colour.
    constexpr int ticks = 21;
    for (int i = 0; i < ticks; ++i)
    {
        const float t = (float) i / (float) (ticks - 1);
        const float a = startA + t * (endA - startA);
        const bool major = i % 5 == 0;
        const float lit = jlimit (0.0f, 1.0f, (proportion - t) * (float) (ticks - 1) + 1.0f);
        const float r1 = r * (major ? 0.83f : 0.87f), r2 = r * 0.98f;
        const auto unlit = onDark ? palette::cream.withAlpha (0.22f) : palette::brown.withAlpha (0.22f);
        const auto col = unlit.interpolatedWith (accent, lit);
        g.setColour (col);
        g.drawLine (Line<float> (polar (c, r1, a), polar (c, r2, a)), major ? jmax (1.6f, r * 0.055f) : jmax (1.1f, r * 0.04f));
    }

    // Skirt: brushed chrome with knurled edge.
    const float skirtR = r * 0.76f;
    auto skirt = Rectangle<float> (skirtR * 2.0f, skirtR * 2.0f).withCentre (c);
    for (int i = 3; i > 0; --i)
    {
        g.setColour (palette::brownDark.withAlpha (0.09f));
        g.fillEllipse (skirt.translated (0.0f, skirtR * 0.07f * (float) i).expanded (skirtR * 0.03f * (float) i));
    }
    g.setGradientFill (ColourGradient (Colour (0xfffbf7ef), c.x - skirtR * 0.55f, c.y - skirtR,
                                       Colour (0xff9b8f7f), c.x + skirtR * 0.45f, c.y + skirtR, false));
    g.fillEllipse (skirt);

    {
        Path knurl;
        constexpr int ridges = 44;
        for (int i = 0; i < ridges; ++i)
        {
            const float a = (float) i / (float) ridges * MathConstants<float>::twoPi + valueA;
            knurl.startNewSubPath (polar (c, skirtR * 0.86f, a));
            knurl.lineTo (polar (c, skirtR * 0.99f, a));
        }
        g.setColour (Colours::black.withAlpha (0.16f));
        g.strokePath (knurl, PathStrokeType (jmax (0.7f, r * 0.022f)));
    }
    g.setColour (palette::brownDark.withAlpha (0.45f));
    g.drawEllipse (skirt, jmax (0.8f, r * 0.025f));

    // Skirt notch in the accent colour.
    g.setColour (accent.darker (0.1f));
    g.drawLine (Line<float> (polar (c, skirtR * 0.7f, valueA), polar (c, skirtR * 0.98f, valueA)), jmax (2.0f, r * 0.075f));

    // Cap: glossy bakelite.
    const float capR = r * 0.55f;
    auto cap = Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c);
    g.setColour (Colours::black.withAlpha (0.3f));
    g.fillEllipse (cap.translated (0.0f, capR * 0.09f).expanded (capR * 0.05f));
    g.setGradientFill (ColourGradient (Colour (0xff7a5640), c.x - capR * 0.45f, c.y - capR * 0.6f,
                                       Colour (0xff1f140d), c.x + capR * 0.6f, c.y + capR * 0.9f, true));
    g.fillEllipse (cap);
    g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.0f), c.x, c.y - capR * 0.2f,
                                       Colours::black.withAlpha (0.25f), c.x, c.y + capR, false));
    g.drawEllipse (cap.reduced (capR * 0.16f), jmax (0.8f, capR * 0.05f));

    // Specular highlight.
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (hovered || dragging ? 0.42f : 0.3f), c.x - capR * 0.3f, c.y - capR * 0.75f,
                                       Colours::white.withAlpha (0.0f), c.x - capR * 0.1f, c.y - capR * 0.05f, false));
    g.fillEllipse (Rectangle<float> (capR * 1.3f, capR * 0.8f).withCentre ({ c.x - capR * 0.12f, c.y - capR * 0.45f }));

    // Pointer.
    g.setColour (palette::creamLight);
    g.drawLine (Line<float> (polar (c, capR * 0.28f, valueA), polar (c, capR * 0.86f, valueA)), jmax (1.8f, capR * 0.13f));

    if (hovered || dragging)
    {
        g.setColour (accent.withAlpha (dragging ? 0.5f : 0.3f));
        g.drawEllipse (skirt.expanded (r * 0.03f), jmax (1.0f, r * 0.03f));
    }
}

void TapeLookAndFeel::drawKnobCaption (Graphics& g, Rectangle<float> area, const String& text, bool showingValue,
                                       Colour accent, aa::Knob& knob)
{
    const bool onDark = (bool) knob.getProperties().getWithDefault ("onDark", false);
    const float h = jlimit (9.5f, 12.5f, area.getHeight() * 0.78f);
    g.setFont (aa::Fonts::uiBold (h).withExtraKerningFactor (showingValue ? 0.02f : 0.1f));
    if (onDark)
        g.setColour (showingValue ? accent.brighter (0.3f) : palette::cream.withAlpha (0.8f));
    else
        g.setColour (showingValue ? accent.darker (0.45f) : palette::brown.withAlpha (0.82f));
    g.drawFittedText (showingValue ? text : text.toUpperCase(), area.toNearestInt(), Justification::centred, 1, 0.8f);
}

//==============================================================================
VuMeter::VuMeter (const String& l) : label (l)
{
    setInterceptsMouseClicks (false, false);
}

void VuMeter::setLevel (float meanSquare)
{
    const float db = 10.0f * std::log10 (jmax (1.0e-10f, meanSquare));
    const float vu = db + 14.0f;
    target = jlimit (0.0f, 1.3f, std::pow (10.0f, vu / 20.0f) / 1.4125f);
}

void VuMeter::tick (double dt)
{
    // Second-order needle: ~300 ms to settle with a hint of overshoot, like a real VU movement.
    const float w = 15.0f, zeta = 0.74f;
    const float t = (float) dt;
    const int steps = jmax (1, (int) std::ceil (t / 0.004f));
    const float h = t / (float) steps;
    for (int i = 0; i < steps; ++i)
    {
        const float acc = w * w * (target - pos) - 2.0f * zeta * w * vel;
        vel += acc * h;
        pos += vel * h;
        if (pos > 1.07f) { pos = 1.07f; vel = -vel * 0.35f; }  // the peg
        if (pos < -0.015f) { pos = -0.015f; vel = -vel * 0.3f; }
    }
    if (std::abs (pos - drawnPos) > 0.0005f)
        repaint();
}

VuMeter::Geometry VuMeter::geometry() const
{
    auto b = getLocalBounds().toFloat();
    const float bezel = jmax (6.0f, b.getHeight() * 0.075f);
    auto face = b.reduced (bezel);
    const float h = face.getHeight();
    return { face, { face.getCentreX(), face.getBottom() + h * 0.45f }, h * 1.12f, 0.56f };
}

Point<float> VuMeter::pointOnArc (const Geometry& geo, float p, float radius) const
{
    const float a = -geo.halfAngle + 2.0f * geo.halfAngle * p;
    return polar (geo.pivot, radius, a);
}

void VuMeter::rebuild (float scale)
{
    layerScale = scale;
    const auto geo = geometry();
    const auto b = getLocalBounds().toFloat();
    const auto& f = geo.face;
    const float h = f.getHeight();

    face = makeLayer (getWidth(), getHeight(), scale);
    {
        Graphics g (face);
        g.addTransform (AffineTransform::scale (scale));

        // Housing
        g.setGradientFill (ColourGradient (Colour (0xff5b4232), 0.0f, b.getY(), palette::brownDark, 0.0f, b.getBottom(), false));
        g.fillRoundedRectangle (b, 9.0f);
        g.setColour (Colours::white.withAlpha (0.12f));
        g.drawRoundedRectangle (b.reduced (0.75f), 9.0f, 1.0f);
        g.setColour (Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (f.expanded (1.5f), 5.5f, 2.0f);

        // Face: warm backlit paper
        g.setGradientFill (ColourGradient (Colour (0xfffff6dc), f.getCentreX(), f.getBottom() - h * 0.1f,
                                           Colour (0xffe9cf92), f.getX(), f.getY(), true));
        g.fillRoundedRectangle (f, 5.0f);

        Graphics::ScopedSaveState s (g);
        Path clip;
        clip.addRoundedRectangle (f, 5.0f);
        g.reduceClipRegion (clip);

        const float R = geo.radius;
        // Red zone band
        {
            Path red;
            const float a0 = -geo.halfAngle + 2.0f * geo.halfAngle * 0.708f, a1 = geo.halfAngle;
            red.addCentredArc (geo.pivot.x, geo.pivot.y, R + 2.5f, R + 2.5f, 0.0f, a0, a1, true);
            g.setColour (palette::red.withAlpha (0.9f));
            g.strokePath (red, PathStrokeType (h * 0.05f, PathStrokeType::curved, PathStrokeType::butt));
        }
        // Main arc
        {
            Path arc;
            arc.addCentredArc (geo.pivot.x, geo.pivot.y, R, R, 0.0f, -geo.halfAngle * 0.96f, geo.halfAngle, true);
            g.setColour (palette::ink.withAlpha (0.85f));
            g.strokePath (arc, PathStrokeType (1.1f));
        }

        // Ticks + numerals
        const float marks[] = { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f };
        const auto numeralFont = aa::Fonts::uiBold (jmax (7.5f, h * 0.095f));
        for (float m : marks)
        {
            const float p = std::pow (10.0f, m / 20.0f) / 1.4125f;
            const bool hot = m >= 0.0f;
            g.setColour (hot && m > 0.0f ? palette::red : palette::ink);
            g.drawLine (Line<float> (pointOnArc (geo, p, R), pointOnArc (geo, p, R + h * 0.09f)), m == 0.0f ? 1.8f : 1.2f);
            const String txt = m > 0.0f ? "+" + String ((int) m) : String ((int) std::abs (m));
            drawTextCentred (g, txt, numeralFont, pointOnArc (geo, p, R + h * 0.17f), 0.0f,
                             hot && m > 0.0f ? palette::red : palette::ink.withAlpha (0.9f));
        }
        // minor ticks
        for (float m = -19.0f; m < 3.0f; m += 1.0f)
        {
            if (m > -10.0f && m < -7.0f) continue;
            const float p = std::pow (10.0f, m / 20.0f) / 1.4125f;
            g.setColour (m >= 0.0f ? palette::red : palette::ink.withAlpha (0.6f));
            if (m < -10.0f && (int) m % 2 != 0) continue;
            g.drawLine (Line<float> (pointOnArc (geo, p, R), pointOnArc (geo, p, R + h * 0.045f)), 0.8f);
        }

        // percentage sub-scale under the arc
        for (int i = 0; i <= 4; ++i)
        {
            const float p = (float) i / 4.0f * 0.708f;
            g.setColour (palette::ink.withAlpha (0.45f));
            g.drawLine (Line<float> (pointOnArc (geo, p, R - h * 0.02f), pointOnArc (geo, p, R - h * 0.07f)), 0.8f);
        }

        drawTextCentred (g, "VU", aa::Fonts::uiBold (h * 0.2f), { f.getCentreX(), f.getY() + h * 0.6f }, 0.0f, palette::ink);

        g.setColour (palette::brown.withAlpha (0.75f));
        g.setFont (aa::Fonts::uiBold (jmax (7.0f, h * 0.085f)).withExtraKerningFactor (0.18f));
        g.drawText (label, Rectangle<float> (f.getX() + 6.0f, f.getBottom() - h * 0.2f, f.getWidth() * 0.3f, h * 0.14f),
                    Justification::centredLeft, false);
        g.drawText ("dB", Rectangle<float> (f.getRight() - 6.0f - f.getWidth() * 0.3f, f.getBottom() - h * 0.2f, f.getWidth() * 0.3f, h * 0.14f),
                    Justification::centredRight, false);
    }

    glass = makeLayer (getWidth(), getHeight(), scale);
    {
        Graphics g (glass);
        g.addTransform (AffineTransform::scale (scale));
        Path clip;
        clip.addRoundedRectangle (f, 5.0f);
        g.reduceClipRegion (clip);

        // pivot cover
        const float coverR = h * 0.24f;
        auto cover = Rectangle<float> (coverR * 2.6f, coverR * 2.0f).withCentre ({ f.getCentreX(), f.getBottom() + coverR * 0.35f });
        g.setGradientFill (ColourGradient (Colour (0xff5a4334), cover.getCentreX(), cover.getY(),
                                           palette::brownDark, cover.getCentreX(), cover.getBottom(), false));
        g.fillEllipse (cover);
        g.setColour (Colours::white.withAlpha (0.18f));
        g.drawEllipse (cover.reduced (0.5f), 1.0f);

        // inner shadow along the top of the window
        g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.28f), 0.0f, f.getY(), Colours::transparentBlack, 0.0f, f.getY() + h * 0.14f, false));
        g.fillRect (f.withHeight (h * 0.14f));

        // glass sheen
        Path sheen;
        sheen.startNewSubPath (f.getX(), f.getY());
        sheen.lineTo (f.getX() + f.getWidth() * 0.55f, f.getY());
        sheen.lineTo (f.getX() + f.getWidth() * 0.25f, f.getBottom());
        sheen.lineTo (f.getX(), f.getBottom());
        sheen.closeSubPath();
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.22f), f.getX(), f.getY(),
                                           Colours::white.withAlpha (0.0f), f.getX() + f.getWidth() * 0.4f, f.getBottom(), false));
        g.fillPath (sheen);
    }
}

void VuMeter::paint (Graphics& g)
{
    const float physScale = jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (face.isNull() || std::abs (physScale - layerScale) > 0.01f)
        rebuild (physScale);

    const auto b = getLocalBounds().toFloat();
    g.drawImage (face, b);

    const auto geo = geometry();
    {
        Graphics::ScopedSaveState s (g);
        Path clip;
        clip.addRoundedRectangle (geo.face, 5.0f);
        g.reduceClipRegion (clip);

        const auto tip = pointOnArc (geo, pos, geo.radius + geo.face.getHeight() * 0.1f);
        const auto base = pointOnArc (geo, pos, geo.face.getHeight() * 0.1f);
        g.setColour (Colours::black.withAlpha (0.13f));
        g.drawLine (Line<float> (base.translated (2.5f, 3.0f), tip.translated (2.5f, 3.0f)), 1.6f);
        g.setColour (palette::ink);
        g.drawLine (Line<float> (base, tip), 1.5f);
    }
    g.drawImage (glass, b);
    drawnPos = pos;
}

//==============================================================================
Lamp::Lamp (const String& l, Colour c) : label (l), colour (c)
{
    setInterceptsMouseClicks (false, false);
}

void Lamp::setLevel (float newLevel)
{
    newLevel = jlimit (0.0f, 1.0f, newLevel);
    if (std::abs (newLevel - level) > 0.004f)
    {
        level = newLevel;
        repaint();
    }
}

void Lamp::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    auto labelArea = b.removeFromBottom (13.0f);
    const float d = jmin (b.getWidth(), b.getHeight()) * 0.52f;
    const auto c = b.getCentre();
    auto lens = Rectangle<float> (d, d).withCentre (c);

    // halo
    if (level > 0.01f)
        for (int i = 4; i > 0; --i)
        {
            g.setColour (colour.withAlpha (0.07f * level));
            g.fillEllipse (lens.expanded (d * 0.16f * (float) i));
        }

    // chrome bezel
    auto bezel = lens.expanded (d * 0.18f);
    g.setGradientFill (ColourGradient (Colour (0xffeee8dc), bezel.getX(), bezel.getY(), Colour (0xff6e6458), bezel.getRight(), bezel.getBottom(), false));
    g.fillEllipse (bezel);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.drawEllipse (bezel, 0.8f);

    const auto off = colour.withSaturation (colour.getSaturation() * 0.6f).withBrightness (0.28f);
    const auto lit = colour.brighter (0.25f);
    const auto body = off.interpolatedWith (lit, level);
    g.setGradientFill (ColourGradient (body.brighter (0.5f + 0.5f * level), lens.getX() + d * 0.35f, lens.getY() + d * 0.3f,
                                       body.darker (0.5f), lens.getRight(), lens.getBottom(), true));
    g.fillEllipse (lens);
    g.setColour (Colours::white.withAlpha (0.55f));
    g.fillEllipse (Rectangle<float> (d * 0.3f, d * 0.2f).withCentre ({ lens.getX() + d * 0.36f, lens.getY() + d * 0.28f }));

    g.setColour (palette::cream.withAlpha (0.55f + 0.45f * level));
    g.setFont (aa::Fonts::uiBold (9.5f).withExtraKerningFactor (0.15f));
    g.drawText (label, labelArea, Justification::centred, false);
}

//==============================================================================
TransportKey::TransportKey (AudioProcessorValueTreeState& state, const String& paramID)
{
    attachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (state, paramID, *this);
    setClickingTogglesState (true);
}

TransportKey::~TransportKey() = default;

void TransportKey::paintButton (Graphics& g, bool highlighted, bool down)
{
    const bool on = getToggleState();
    auto b = getLocalBounds().toFloat().reduced (2.0f, 1.0f);
    const float travel = 6.0f;
    const float press = on ? 4.0f : (down ? 2.0f : 0.0f);

    // slot shadow
    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (b.translated (0.0f, 1.5f), 7.0f);

    // key side
    auto side = b.withTrimmedTop (press);
    g.setGradientFill (ColourGradient (Colour (0xffb59f7c), 0.0f, side.getY(), Colour (0xff7d6a52), 0.0f, side.getBottom(), false));
    g.fillRoundedRectangle (side, 7.0f);

    // key top
    auto top = b.withTrimmedBottom (travel).translated (0.0f, press);
    g.setGradientFill (ColourGradient (on ? Colour (0xffeedcb8) : Colour (0xfffbf3e2), 0.0f, top.getY(),
                                       on ? Colour (0xffd6bf95) : Colour (0xffe2cfaa), 0.0f, top.getBottom(), false));
    g.fillRoundedRectangle (top, 6.0f);
    g.setColour (Colours::white.withAlpha (highlighted ? 0.8f : 0.55f));
    g.drawLine (top.getX() + 6.0f, top.getY() + 1.2f, top.getRight() - 6.0f, top.getY() + 1.2f, 1.0f);
    g.setColour (palette::brownDark.withAlpha (0.55f));
    g.drawRoundedRectangle (top.reduced (0.5f), 6.0f, 1.0f);

    // stop glyph + label
    auto content = top.reduced (10.0f, 4.0f);
    auto glyph = content.removeFromLeft (16.0f).withSizeKeepingCentre (12.0f, 12.0f);
    g.setColour (on ? palette::orange : palette::brown);
    g.fillRoundedRectangle (glyph, 2.0f);

    auto led = content.removeFromRight (10.0f).withSizeKeepingCentre (8.0f, 8.0f);
    if (on)
    {
        g.setColour (palette::orange.withAlpha (0.3f));
        g.fillEllipse (led.expanded (4.0f));
    }
    g.setColour (on ? palette::orange.brighter (0.3f) : palette::brown.withAlpha (0.35f));
    g.fillEllipse (led);
    g.setColour (Colours::black.withAlpha (0.35f));
    g.drawEllipse (led, 0.7f);

    g.setColour (palette::brown);
    g.setFont (aa::Fonts::uiBold (12.0f).withExtraKerningFactor (0.1f));
    g.drawText (on ? "STOPPED" : "TAPE STOP", content.withTrimmedLeft (4.0f), Justification::centred, false);
}

//==============================================================================
CassetteView::CassetteView()
{
    setTooltip ("Press and hold the cassette to brake the tape (momentary Tape Stop)");
    setMouseCursor (MouseCursor::PointingHandCursor);
    for (auto& m : motes)
    {
        m.p = { 90.0f + rng.nextFloat() * 220.0f, 76.0f + rng.nextFloat() * 66.0f };
        m.vx = (rng.nextFloat() - 0.5f) * 4.0f;
        m.vy = (rng.nextFloat() - 0.5f) * 3.0f;
        m.size = 0.8f + rng.nextFloat() * 1.6f;
        m.phase = rng.nextFloat() * 6.28f;
    }
}

void CassetteView::setTrackName (const String& name)
{
    if (name != trackName)
    {
        if (trackName.isNotEmpty())
            clunkVel = 120.0f; // clunk!
        trackName = name;
        over = {};
        repaint();
    }
}

void CassetteView::setCharacter (float age, float wear, float hiss, float dropout)
{
    ageAmt = age;
    wearAmt = wear;
    hissAmt = hiss;
    dropAmt = jlimit (0.0f, 1.0f, dropout);
}

void CassetteView::mouseDown (const MouseEvent&)
{
    if (onHold)
        onHold (true);
}

void CassetteView::mouseUp (const MouseEvent&)
{
    if (onHold)
        onHold (false);
}

AffineTransform CassetteView::designTransform() const
{
    const auto b = getLocalBounds().toFloat();
    const float margin = b.getWidth() * 0.02f;
    const float w = b.getWidth() - margin * 2.0f;
    const float s = w / designW;
    return AffineTransform::scale (s).translated (margin, margin * 0.7f);
}

static void drawHeart (Graphics& g, Point<float> c, float size, float angle, Colour colour)
{
    Path p;
    p.startNewSubPath (0.0f, 0.35f);
    p.cubicTo (-0.1f, 0.2f, -0.55f, 0.0f, -0.5f, -0.3f);
    p.cubicTo (-0.45f, -0.6f, -0.05f, -0.6f, 0.0f, -0.25f);
    p.cubicTo (0.05f, -0.6f, 0.45f, -0.6f, 0.5f, -0.3f);
    p.cubicTo (0.55f, 0.0f, 0.1f, 0.2f, 0.0f, 0.35f);
    p.closeSubPath();
    p.applyTransform (AffineTransform::scale (size).rotated (angle).translated (c.x, c.y));
    g.setColour (colour.withAlpha (0.25f));
    g.fillPath (p);
    g.setColour (colour);
    g.strokePath (p, PathStrokeType (1.5f, PathStrokeType::curved, PathStrokeType::rounded));
}

static void drawSparkle (Graphics& g, Point<float> c, float size, Colour colour)
{
    Path p;
    p.startNewSubPath (c.x, c.y - size);
    p.quadraticTo (c.x, c.y, c.x + size, c.y);
    p.quadraticTo (c.x, c.y, c.x, c.y + size);
    p.quadraticTo (c.x, c.y, c.x - size, c.y);
    p.quadraticTo (c.x, c.y, c.x, c.y - size);
    p.closeSubPath();
    g.setColour (colour);
    g.fillPath (p);
}

float CassetteView::packRadius (bool left) const
{
    const float rMin = 24.0f, rMax = 63.0f;
    const float p = left ? 1.0f - progress : progress;
    return std::sqrt (rMin * rMin + p * (rMax * rMax - rMin * rMin));
}

void CassetteView::tick (double dt, float speed, float motor)
{
    clock += dt;
    const float t = (float) dt;

    // Exaggerate the wobble so it reads visually; motor brings everything to a halt.
    const float target = jlimit (0.0f, 2.5f, motor * (1.0f + (speed - motor) * 16.0f));
    visualSpeed += (target - visualSpeed) * jmin (1.0f, t * 12.0f);

    const float tapeV = 70.0f * visualSpeed; // design units per second at the pack surface
    angleL -= tapeV / packRadius (true) * t;
    angleR -= tapeV / packRadius (false) * t;
    angleL = std::fmod (angleL, MathConstants<float>::twoPi);
    angleR = std::fmod (angleR, MathConstants<float>::twoPi);

    // The tape slowly moves from one pack to the other, auto-reversing at the end of a side.
    progress += direction * visualSpeed * t / 240.0f;
    if (progress > 1.0f) { progress = 1.0f; direction = -1.0f; }
    if (progress < 0.0f) { progress = 0.0f; direction = 1.0f; }

    // damped spring for the insert "clunk"
    {
        const int steps = jmax (1, (int) std::ceil (t / 0.004f));
        const float h = t / (float) steps;
        for (int i = 0; i < steps; ++i)
        {
            clunkVel += (-900.0f * clunk - 18.0f * clunkVel) * h;
            clunk += clunkVel * h;
        }
    }
    if (std::abs (clunk) < 0.01f && std::abs (clunkVel) < 0.05f)
        clunk = clunkVel = 0.0f;

    for (auto& m : motes)
    {
        m.phase += t * (0.6f + m.size * 0.3f);
        m.vx += (rng.nextFloat() - 0.5f) * 6.0f * t;
        m.vy += (rng.nextFloat() - 0.5f) * 6.0f * t - 0.4f * t;
        m.vx = jlimit (-5.0f, 5.0f, m.vx);
        m.vy = jlimit (-4.0f, 4.0f, m.vy);
        m.p += Point<float> (m.vx, m.vy) * t;
        if (m.p.x < 86.0f) m.p.x = 314.0f;
        if (m.p.x > 314.0f) m.p.x = 86.0f;
        if (m.p.y < 72.0f) m.p.y = 146.0f;
        if (m.p.y > 146.0f) m.p.y = 72.0f;
    }

    repaint();
}

void CassetteView::drawInterior (Graphics& g)
{
    const Rectangle<float> window (86.0f, 72.0f, 228.0f, 74.0f);
    g.setGradientFill (ColourGradient (Colour (0xff2f2620), window.getCentreX(), window.getY(),
                                       Colour (0xff120d0a), window.getCentreX(), window.getBottom(), false));
    g.fillRoundedRectangle (window, 12.0f);

    // the far wall of the shell, seen through the window
    g.setColour (palette::tealDark.withAlpha (0.35f));
    g.fillRoundedRectangle (window.reduced (6.0f, 10.0f).withTrimmedTop (40.0f), 6.0f);
    g.setColour (Colours::white.withAlpha (0.05f));
    for (float x = window.getX() + 10.0f; x < window.getRight(); x += 9.0f)
        g.drawVerticalLine ((int) x, window.getY() + 50.0f, window.getBottom() - 6.0f);
}

void CassetteView::drawReel (Graphics& g, Point<float> c, float packR, float angle, bool)
{
    // Tape pack: satin brown with wound rings.
    auto pack = Rectangle<float> (packR * 2.0f, packR * 2.0f).withCentre (c);
    g.setGradientFill (ColourGradient (Colour (0xff6e4a31), c.x - packR * 0.3f, c.y - packR * 0.35f,
                                       Colour (0xff2a170d), c.x + packR, c.y + packR, true));
    g.fillEllipse (pack);
    g.setColour (Colours::black.withAlpha (0.18f));
    for (float rr = 26.0f; rr < packR; rr += 3.2f)
        g.drawEllipse (Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (c), 0.5f);

    // a sheen wedge that turns with the pack, so the spin reads even when the hub is hidden
    {
        Path wedge;
        wedge.addPieSegment (pack, angle, angle + 0.5f, 26.0f / packR);
        g.setColour (Colours::white.withAlpha (0.06f));
        g.fillPath (wedge);
        Path wedge2;
        wedge2.addPieSegment (pack, angle + pi, angle + pi + 0.3f, 26.0f / packR);
        g.fillPath (wedge2);
    }
    g.setColour (Colour (0xff8a6142).withAlpha (0.55f));
    g.drawEllipse (pack.reduced (0.6f), 1.2f);

    // Hub: white plastic ring with six drive teeth.
    const float hubR = 20.0f, holeR = 12.0f;
    auto hub = Rectangle<float> (hubR * 2.0f, hubR * 2.0f).withCentre (c);
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillEllipse (hub.expanded (2.0f).translated (0.0f, 1.0f));
    g.setGradientFill (ColourGradient (Colour (0xfffbf7ee), c.x - hubR * 0.5f, c.y - hubR * 0.6f,
                                       Colour (0xffc6bba8), c.x + hubR * 0.6f, c.y + hubR, true));
    g.fillEllipse (hub);
    g.setColour (Colour (0xff9e927f));
    g.drawEllipse (hub.reduced (0.5f), 1.0f);
    g.drawEllipse (hub.reduced (4.0f), 0.6f);

    auto hole = Rectangle<float> (holeR * 2.0f, holeR * 2.0f).withCentre (c);
    g.setColour (Colour (0xff1a120d));
    g.fillEllipse (hole);

    Path teeth;
    for (int i = 0; i < 6; ++i)
    {
        Path tooth;
        tooth.addRoundedRectangle (-1.9f, -holeR - 0.5f, 3.8f, 5.2f, 1.0f);
        tooth.applyTransform (AffineTransform::rotation (angle + (float) i * pi / 3.0f));
        teeth.addPath (tooth);
    }
    teeth.applyTransform (AffineTransform::translation (c.x, c.y));
    g.setColour (Colour (0xffeee6d6));
    g.fillPath (teeth);

    // little moulding dots on the hub face make the rotation easy to see
    for (int i = 0; i < 3; ++i)
    {
        const auto p = polar (c, hubR - 3.6f, angle + 0.5f + (float) i * pi * 2.0f / 3.0f);
        g.setColour (Colour (0xffa89b86));
        g.fillEllipse (Rectangle<float> (2.6f, 2.6f).withCentre (p));
    }
}

void CassetteView::drawShell (Graphics& g)
{
    const Rectangle<float> shell (0.0f, 0.0f, designW, designH);
    const Rectangle<float> label (20.0f, 14.0f, 360.0f, 170.0f);
    const Rectangle<float> window (86.0f, 72.0f, 228.0f, 74.0f);

    Path shellPath;
    shellPath.addRoundedRectangle (shell, 16.0f);

    // Soft shadow onto the deck
    DropShadow (Colours::black.withAlpha (0.6f), 16, { 0, 7 }).drawForPath (g, shellPath);

    // Shell plastic
    g.setGradientFill (ColourGradient (Colour (0xff3a9394), 0.0f, 0.0f, Colour (0xff1d5758), 0.0f, designH, false));
    g.fillPath (shellPath);
    {
        // moulded speckle texture
        Random r (99);
        for (int i = 0; i < 420; ++i)
        {
            g.setColour ((i % 2 == 0 ? Colours::white : Colours::black).withAlpha (0.035f));
            g.fillEllipse (r.nextFloat() * designW, r.nextFloat() * designH, 1.4f, 1.4f);
        }
    }
    // rim bevel
    g.setColour (Colours::white.withAlpha (0.22f));
    g.drawRoundedRectangle (shell.reduced (1.5f), 15.0f, 1.2f);
    g.setColour (Colours::black.withAlpha (0.45f));
    g.drawRoundedRectangle (shell.reduced (0.5f), 16.0f, 1.0f);
    g.setColour (Colours::black.withAlpha (0.18f));
    g.drawRoundedRectangle (shell.reduced (7.0f), 11.0f, 1.0f);

    // write-protect tabs
    for (float x : { 44.0f, 334.0f })
    {
        g.setColour (Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (x, 1.5f, 22.0f, 6.0f, 2.0f);
    }

    // Bottom head area (trapezoid) with capstan holes
    {
        Path trap;
        trap.startNewSubPath (88.0f, 194.0f);
        trap.lineTo (312.0f, 194.0f);
        trap.lineTo (334.0f, designH);
        trap.lineTo (66.0f, designH);
        trap.closeSubPath();
        g.setGradientFill (ColourGradient (Colour (0xff25696a), 0.0f, 194.0f, Colour (0xff164546), 0.0f, designH, false));
        g.fillPath (trap);
        g.setColour (Colours::white.withAlpha (0.16f));
        g.strokePath (trap, PathStrokeType (1.0f));
        g.setColour (Colours::black.withAlpha (0.25f));
        g.drawLine (90.0f, 195.5f, 310.0f, 195.5f, 1.0f);

        for (float x : { 136.0f, 264.0f })
        {
            auto hole = Rectangle<float> (16.0f, 16.0f).withCentre ({ x, 228.0f });
            g.setColour (Colour (0xff0d0907));
            g.fillEllipse (hole);
            g.setColour (Colours::white.withAlpha (0.15f));
            g.drawEllipse (hole.expanded (1.0f), 1.0f);
        }
        for (float x : { 100.0f, 300.0f })
        {
            g.setColour (Colour (0xff0d0907));
            g.fillRoundedRectangle (Rectangle<float> (8.0f, 9.0f).withCentre ({ x, 236.0f }), 1.5f);
        }
        // head opening with the tape visible
        g.setColour (Colour (0xff0d0907));
        g.fillRoundedRectangle (Rectangle<float> (40.0f, 12.0f).withCentre ({ 200.0f, 250.0f }), 2.0f);
        g.setColour (Colour (0xff5a3a24));
        g.fillRect (Rectangle<float> (40.0f, 2.2f).withCentre ({ 200.0f, 254.0f }));
    }

    // Label
    {
        Path labelPath;
        labelPath.addRoundedRectangle (label, 8.0f);
        g.setColour (Colours::black.withAlpha (0.25f));
        g.fillRoundedRectangle (label.translated (0.0f, 1.5f), 8.0f);
        g.setGradientFill (ColourGradient (palette::paper, 0.0f, label.getY(), Colour (0xffecdcb8), 0.0f, label.getBottom(), false));
        g.fillPath (labelPath);

        Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (labelPath);

        // paper fibres
        Random r (5);
        for (int i = 0; i < 260; ++i)
        {
            g.setColour (palette::brown.withAlpha (0.035f));
            const float x = r.nextFloat() * designW, y = label.getY() + r.nextFloat() * label.getHeight();
            g.drawLine (x, y, x + 2.0f + r.nextFloat() * 4.0f, y + (r.nextFloat() - 0.5f) * 1.5f, 0.5f);
        }

        // 70s stripes along the bottom of the label
        const Colour stripes[] = { palette::orange, palette::mustard, palette::teal };
        float y = label.getBottom() - 19.0f;
        for (auto c : stripes)
        {
            g.setColour (c);
            g.fillRect (label.getX(), y, label.getWidth(), 5.4f);
            y += 6.0f;
        }

        // writing lines
        g.setColour (palette::teal.withAlpha (0.25f));
        g.drawHorizontalLine (64, label.getX() + 14.0f, label.getRight() - 14.0f);
        g.drawHorizontalLine (163, label.getX() + 14.0f, label.getRight() - 14.0f);

        // handwritten title, with doodles
        drawTextCentred (g, "Tape Dreams", aa::Fonts::accent (31.0f), { 200.0f, 41.0f }, -0.025f, palette::ink);
        drawHeart (g, { 286.0f, 29.0f }, 14.0f, 0.3f, palette::orange);
        drawSparkle (g, { 116.0f, 31.0f }, 6.0f, palette::mustard);
        drawSparkle (g, { 124.0f, 48.0f }, 3.5f, palette::teal);
        // handwritten track list: the preset name
        const String track = trackName.isEmpty() ? String ("Side A") : trackName;
        drawTextCentred (g, "~ " + track + " ~", aa::Fonts::accent (14.5f), { 200.0f, 155.0f }, 0.012f, palette::tealDark);

        // side letter + tape type
        auto sideBox = Rectangle<float> (36.0f, 34.0f).withCentre ({ 54.0f, 109.0f });
        g.setColour (palette::orange);
        g.fillRoundedRectangle (sideBox, 6.0f);
        drawTextCentred (g, "A", aa::Fonts::uiBold (24.0f), sideBox.getCentre().translated (0.0f, 1.0f), 0.0f, palette::creamLight);

        drawTextCentred (g, "C-60", aa::Fonts::uiBold (13.0f), { 347.0f, 102.0f }, 0.0f, palette::brown);
        drawTextCentred (g, "TYPE I", aa::Fonts::uiBold (7.5f).withExtraKerningFactor (0.15f), { 347.0f, 118.0f }, 0.0f,
                         palette::brown.withAlpha (0.7f));

        // window frame printed on the label
        g.setColour (palette::brown.withAlpha (0.8f));
        g.drawRoundedRectangle (window.expanded (3.0f), 14.0f, 1.6f);
    }

    // Screws
    drawScrew (g, { 11.0f, 11.0f }, 5.2f, 0.4f);
    drawScrew (g, { designW - 11.0f, 11.0f }, 5.2f, 1.1f);
    drawScrew (g, { 11.0f, designH - 11.0f }, 5.2f, 0.9f);
    drawScrew (g, { designW - 11.0f, designH - 11.0f }, 5.2f, 0.2f);
    drawScrew (g, { 200.0f, 212.0f }, 5.2f, 0.7f);
}

void CassetteView::rebuild (float scale)
{
    layerScale = scale;
    const auto xf = designTransform();
    const Rectangle<float> window (86.0f, 72.0f, 228.0f, 74.0f);
    Path windowPath;
    windowPath.addRoundedRectangle (window, 12.0f);

    under = makeLayer (getWidth(), getHeight(), scale);
    {
        Graphics g (under);
        g.addTransform (AffineTransform::scale (scale));
        g.addTransform (xf);
        drawInterior (g);
    }

    over = makeLayer (getWidth(), getHeight(), scale);
    {
        Graphics g (over);
        g.addTransform (AffineTransform::scale (scale));
        g.addTransform (xf);
        {
            Graphics::ScopedSaveState s (g);
            Path outside;
            outside.addRectangle (-40.0f, -40.0f, designW + 80.0f, designH + 80.0f);
            outside.addPath (windowPath);
            outside.setUsingNonZeroWinding (false);
            g.reduceClipRegion (outside);
            drawShell (g);
        }

        // Glass over the window: printed scale and reflections
        Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (windowPath);
        g.setColour (Colours::white.withAlpha (0.4f));
        for (int i = 0; i <= 10; ++i)
        {
            const float x = 150.0f + (float) i * 10.0f;
            g.drawLine (x, window.getBottom() - (i % 5 == 0 ? 9.0f : 6.0f), x, window.getBottom() - 3.0f, 0.8f);
        }
        drawTextCentred (g, "100", aa::Fonts::ui (6.0f), { 150.0f, window.getBottom() - 13.0f }, 0.0f, Colours::white.withAlpha (0.4f));
        drawTextCentred (g, "50", aa::Fonts::ui (6.0f), { 200.0f, window.getBottom() - 13.0f }, 0.0f, Colours::white.withAlpha (0.4f));
        drawTextCentred (g, "0", aa::Fonts::ui (6.0f), { 250.0f, window.getBottom() - 13.0f }, 0.0f, Colours::white.withAlpha (0.4f));

        Path glint;
        glint.startNewSubPath (window.getX() + 40.0f, window.getY());
        glint.lineTo (window.getX() + 90.0f, window.getY());
        glint.lineTo (window.getX() + 50.0f, window.getBottom());
        glint.lineTo (window.getX(), window.getBottom());
        glint.closeSubPath();
        g.setColour (Colours::white.withAlpha (0.07f));
        g.fillPath (glint);
        Path glint2;
        glint2.startNewSubPath (window.getX() + 104.0f, window.getY());
        glint2.lineTo (window.getX() + 116.0f, window.getY());
        glint2.lineTo (window.getX() + 76.0f, window.getBottom());
        glint2.lineTo (window.getX() + 64.0f, window.getBottom());
        glint2.closeSubPath();
        g.setColour (Colours::white.withAlpha (0.06f));
        g.fillPath (glint2);

        g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.45f), 0.0f, window.getY(), Colours::transparentBlack,
                                           0.0f, window.getY() + 12.0f, false));
        g.fillRect (window.withHeight (12.0f));
        g.setColour (Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (window.reduced (0.5f), 12.0f, 1.5f);
    }
}

void CassetteView::paint (Graphics& g)
{
    const float physScale = jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (under.isNull() || over.isNull() || std::abs (physScale - layerScale) > 0.01f)
        rebuild (physScale);

    if (clunk != 0.0f)
        g.addTransform (AffineTransform::translation (0.0f, jlimit (-4.0f, 6.0f, clunk)));

    const auto b = getLocalBounds().toFloat();
    g.drawImage (under, b);

    {
        Graphics::ScopedSaveState s (g);
        g.addTransform (designTransform());
        Path windowPath;
        windowPath.addRoundedRectangle (Rectangle<float> (86.0f, 72.0f, 228.0f, 74.0f), 12.0f);
        g.reduceClipRegion (windowPath);

        drawReel (g, { 116.0f, 109.0f }, packRadius (true), angleL, true);
        drawReel (g, { 284.0f, 109.0f }, packRadius (false), angleR, false);

        // dust motes drifting in the light
        for (auto& m : motes)
        {
            const float a = 0.18f + 0.22f * (0.5f + 0.5f * std::sin (m.phase));
            g.setColour (palette::creamLight.withAlpha (a));
            g.fillEllipse (Rectangle<float> (m.size, m.size).withCentre (m.p));
        }

        // hiss: a fizz of grain in the window
        const int grains = (int) (hissAmt * 70.0f);
        for (int i = 0; i < grains; ++i)
        {
            g.setColour (Colours::white.withAlpha (0.08f + 0.14f * rng.nextFloat()));
            const float sz = 0.6f + rng.nextFloat() * 0.8f;
            g.fillRect (86.0f + rng.nextFloat() * 228.0f, 72.0f + rng.nextFloat() * 74.0f, sz, sz);
        }

        // dropouts: the light flickers
        if (dropAmt > 0.01f)
        {
            g.setColour (Colours::black.withAlpha (jmin (0.5f, dropAmt * 0.6f)));
            g.fillRect (Rectangle<float> (86.0f, 72.0f, 228.0f, 74.0f));
        }
    }

    g.drawImage (over, b);

    if (ageAmt > 0.01f || wearAmt > 0.01f)
    {
        Graphics::ScopedSaveState s (g);
        g.addTransform (designTransform());
        Path shellPath;
        shellPath.addRoundedRectangle (Rectangle<float> (0.0f, 0.0f, designW, designH), 16.0f);
        g.reduceClipRegion (shellPath);

        // sun-faded, yellowed plastic and paper
        g.setColour (Colour (0xffd8b46e).withAlpha (ageAmt * 0.24f));
        g.fillPath (shellPath);

        // scuffs and scratches
        Random r (31);
        const int scuffs = (int) (wearAmt * 46.0f);
        for (int i = 0; i < scuffs; ++i)
        {
            const float x = r.nextFloat() * designW, y = r.nextFloat() * designH;
            const float len = 4.0f + r.nextFloat() * 18.0f, ang = -0.4f + r.nextFloat() * 0.8f;
            g.setColour ((i % 3 == 0 ? palette::brownDark : Colours::white).withAlpha (0.1f + 0.14f * r.nextFloat()));
            g.drawLine (x, y, x + len * std::cos (ang), y + len * std::sin (ang), 0.6f + r.nextFloat() * 0.6f);
        }
    }

    if (hover)
    {
        Graphics::ScopedSaveState s (g);
        g.addTransform (designTransform());
        g.setColour (palette::creamLight.withAlpha (0.18f));
        g.drawRoundedRectangle (Rectangle<float> (0.0f, 0.0f, designW, designH).reduced (1.0f), 16.0f, 2.0f);
    }
}
} // namespace tapeui
