#include "CrystalLookAndFeel.h"
#include "CaveArt.h"

using namespace juce;

CrystalLookAndFeel::CrystalLookAndFeel (const aa::Theme& t) : aa::LookAndFeel (t) {}

void CrystalLookAndFeel::drawKnob (Graphics& g, Rectangle<float> bounds, float proportion, bool bipolar, bool hovered,
                                   bool dragging, Colour accent, aa::Knob&)
{
    const float size = jmin (bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const float r = size * 0.5f - 1.0f;
    const float pi = MathConstants<float>::pi;
    const float startA = -pi * 0.75f, endA = pi * 0.75f;
    const float valueA = startA + proportion * (endA - startA);
    const bool active = hovered || dragging;

    // ---- ring ------------------------------------------------------------------
    const float arcR = r * 0.88f;
    const float arcW = jmax (1.6f, r * 0.062f);
    {
        Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startA, endA, true);
        g.setColour (Colours::white.withAlpha (0.075f));
        g.strokePath (track, PathStrokeType (arcW, PathStrokeType::curved, PathStrokeType::rounded));

        // tiny frost ticks just outside the ring
        g.setColour (cave::colours::ice.withAlpha (0.22f));
        for (int i = 0; i <= 10; ++i)
        {
            const float a = startA + (endA - startA) * (float) i / 10.0f;
            const Point<float> d (std::sin (a), -std::cos (a));
            const float dotR = (i % 5 == 0) ? 1.1f : 0.7f;
            g.fillEllipse (Rectangle<float> (dotR * 2.0f, dotR * 2.0f).withCentre (centre + d * (arcR + arcW * 1.6f)));
        }
    }

    const float fromA = bipolar ? 0.0f : startA;
    const auto arcColour = aa::mix (accent, cave::colours::violet,
                                    bipolar ? std::abs (proportion - 0.5f) * 2.0f : proportion * 0.75f);
    aa::LookAndFeel::drawGlowArc (g, centre, arcR, jmin (fromA, valueA), jmax (fromA, valueA), arcW,
                                  active ? arcColour.brighter (0.25f) : arcColour, true);

    // ---- gem ---------------------------------------------------------------------
    const float gemR = r * 0.63f;
    drawSoftShadow (g, Rectangle<float> (gemR * 2.0f, gemR * 2.0f).withCentre (centre.translated (0.0f, gemR * 0.2f)),
                    Colours::black.withAlpha (0.6f), 5);

    g.setGradientFill (ColourGradient (accent.withAlpha (0.08f + 0.16f * proportion + (active ? 0.1f : 0.0f)), centre,
                                       accent.withAlpha (0.0f), centre.translated (gemR * 1.5f, 0.0f), true));
    g.fillEllipse (Rectangle<float> (gemR * 3.0f, gemR * 3.0f).withCentre (centre));

    constexpr int sides = 8;
    Point<float> outer[sides], inner[sides];
    const float rot = valueA + pi / (float) sides;
    for (int k = 0; k < sides; ++k)
    {
        const float a = rot + MathConstants<float>::twoPi * (float) k / (float) sides;
        const Point<float> d (std::sin (a), -std::cos (a));
        outer[k] = centre + d * gemR;
        inner[k] = centre + d * (gemR * 0.56f);
    }

    const Point<float> light (-0.55f, -0.83f);
    const Colour deep (0xff16296a);
    const Colour gemTint = aa::mix (accent, cave::colours::violet, 0.2f);
    for (int k = 0; k < sides; ++k)
    {
        const int k2 = (k + 1) % sides;
        Path facet;
        facet.startNewSubPath (outer[k]);
        facet.lineTo (outer[k2]);
        facet.lineTo (inner[k2]);
        facet.lineTo (inner[k]);
        facet.closeSubPath();

        const float am = rot + MathConstants<float>::twoPi * ((float) k + 0.5f) / (float) sides;
        const float lit = jlimit (0.0f, 1.0f, 0.5f + 0.5f * (std::sin (am) * light.x - std::cos (am) * light.y));
        const Colour baseC = aa::mix (deep, gemTint, 0.28f + 0.62f * lit);
        const Colour hiC = aa::mix (baseC, Colours::white, 0.35f * lit * lit + (active ? 0.12f : 0.0f));
        g.setGradientFill (ColourGradient (baseC.darker (0.2f), (outer[k] + outer[k2]) * 0.5f,
                                           hiC, (inner[k] + inner[k2]) * 0.5f, false));
        g.fillPath (facet);
    }

    Path table;
    table.startNewSubPath (inner[0]);
    for (int k = 1; k < sides; ++k)
        table.lineTo (inner[k]);
    table.closeSubPath();
    const float tr = gemR * 0.56f;
    g.setGradientFill (ColourGradient (aa::mix (gemTint, Colours::white, active ? 0.75f : 0.62f), centre + light * tr,
                                       aa::mix (deep, gemTint, 0.7f), centre - light * tr, false));
    g.fillPath (table);

    // facet edges
    g.setColour (Colours::white.withAlpha (0.2f));
    for (int k = 0; k < sides; ++k)
        g.drawLine ({ outer[k], inner[k] }, 0.7f);
    g.setColour (Colours::white.withAlpha (0.38f));
    g.strokePath (table, PathStrokeType (0.8f));

    Path rim;
    rim.startNewSubPath (outer[0]);
    for (int k = 1; k < sides; ++k)
        rim.lineTo (outer[k]);
    rim.closeSubPath();
    g.setColour (aa::mix (accent, Colours::white, 0.5f).withAlpha (0.45f));
    g.strokePath (rim, PathStrokeType (1.0f));

    // pointer etched into the table + a glint on the ring
    const Point<float> dir (std::sin (valueA), -std::cos (valueA));
    g.setColour (cave::colours::night.withAlpha (0.55f));
    g.drawLine ({ centre + dir * (tr * 0.15f), centre + dir * (tr * 0.86f) }, jmax (2.4f, gemR * 0.13f));
    g.setColour (Colours::white);
    g.drawLine ({ centre + dir * (tr * 0.2f), centre + dir * (tr * 0.82f) }, jmax (1.4f, gemR * 0.07f));

    cave::drawGlint (g, centre + dir * arcR, arcW * (active ? 3.0f : 2.4f), arcColour, 0.0f);
}

void CrystalLookAndFeel::drawKnobCaption (Graphics& g, Rectangle<float> area, const String& text, bool showingValue,
                                          Colour accent, aa::Knob&)
{
    const float h = jlimit (9.0f, 13.0f, area.getHeight() * 0.72f);
    if (showingValue)
    {
        g.setFont (aa::Fonts::uiBold (h));
        g.setColour (aa::mix (accent, Colours::white, 0.35f));
        g.drawFittedText (text, area.toNearestInt(), Justification::centred, 1, 0.8f);
        return;
    }

    g.setFont (aa::Fonts::ui (h * 0.92f).withExtraKerningFactor (0.12f));
    g.setColour (theme().textDim);
    g.drawFittedText (text.toUpperCase(), area.toNearestInt(), Justification::centred, 1, 0.8f);
}

Rectangle<float> CrystalLookAndFeel::drawPanel (Graphics& g, Rectangle<float> b, const String& title, Colour)
{
    const float radius = theme().cornerRadius;

    // frosted glass body
    g.setGradientFill (ColourGradient (cave::colours::ice.withAlpha (0.085f), b.getX(), b.getY(),
                                       cave::colours::violet.withAlpha (0.05f), b.getRight(), b.getBottom(), false));
    g.fillRoundedRectangle (b, radius);
    g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.0f), b.getX(), b.getY(),
                                       Colours::black.withAlpha (0.18f), b.getX(), b.getBottom(), false));
    g.fillRoundedRectangle (b, radius);

    // top sheen
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.07f), b.getX(), b.getY(),
                                       Colours::transparentWhite, b.getX(), b.getY() + 30.0f, false));
    g.fillRoundedRectangle (b.withHeight (jmin (30.0f, b.getHeight())), radius);

    // icy outline, brighter along the top-left
    g.setGradientFill (ColourGradient (cave::colours::ice.withAlpha (0.42f), b.getX(), b.getY(),
                                       cave::colours::violet.withAlpha (0.12f), b.getRight(), b.getBottom(), false));
    g.drawRoundedRectangle (b.reduced (0.5f), radius, 1.0f);

    auto content = b.reduced (12.0f, 10.0f);
    if (title.isNotEmpty())
    {
        auto titleArea = content.removeFromTop (18.0f);

        // small diamond
        Path diamond;
        const auto c = Point<float> (titleArea.getX() + 4.0f, titleArea.getCentreY());
        diamond.addQuadrilateral (c.x, c.y - 4.5f, c.x + 3.5f, c.y, c.x, c.y + 4.5f, c.x - 3.5f, c.y);
        g.setColour (cave::colours::ice);
        g.fillPath (diamond);
        g.setColour (cave::colours::ice.withAlpha (0.25f));
        g.fillEllipse (Rectangle<float> (12.0f, 12.0f).withCentre (c));

        const auto font = aa::Fonts::display (15.0f).withExtraKerningFactor (0.2f);
        g.setFont (font);
        g.setColour (cave::colours::text.withAlpha (0.82f));
        g.drawText (title.toUpperCase(), titleArea.withTrimmedLeft (14.0f), Justification::centredLeft, false);

        // hairline that fades out to the right of the title
        const float tw = aa::Fonts::textWidth (font, title.toUpperCase());
        const float lineX = titleArea.getX() + 14.0f + tw + 10.0f;
        if (lineX < titleArea.getRight() - 10.0f)
        {
            g.setGradientFill (ColourGradient (cave::colours::ice.withAlpha (0.3f), lineX, 0.0f,
                                               cave::colours::ice.withAlpha (0.0f), titleArea.getRight(), 0.0f, false));
            g.fillRect (Rectangle<float> (lineX, titleArea.getCentreY(), titleArea.getRight() - lineX, 1.0f));
        }
        content.removeFromTop (4.0f);
    }
    return content;
}
