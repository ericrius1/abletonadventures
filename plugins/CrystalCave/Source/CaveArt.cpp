#include "CaveArt.h"

using namespace juce;

namespace cave
{
namespace
{
    Colour shadeOf (Colour base, float shade)
    {
        if (shade <= 1.0f)
            return base.withMultipliedBrightness (shade);
        return base.interpolatedWith (colours::frost, jlimit (0.0f, 1.0f, shade - 1.0f));
    }
} // namespace

Point<float> Crystal::tip() const
{
    return base + Point<float> (std::sin (angle), -std::cos (angle)) * height
           + Point<float> (std::cos (angle), std::sin (angle)) * (width * tipSkew);
}

Point<float> Crystal::centre() const
{
    return base + Point<float> (std::sin (angle), -std::cos (angle)) * (height * 0.55f);
}

Colour Crystal::colour() const
{
    return colours::ice.interpolatedWith (colours::violet, jlimit (0.0f, 1.0f, hue));
}

void drawCrystal (Graphics& g, const Crystal& c, float brightness, float glow, float alpha)
{
    const float w = c.width, h = c.height;
    const float t = jmin (h * 0.42f, w * 1.15f); // length of the pointed tip
    const auto tf = AffineTransform::rotation (c.angle).translated (c.base);
    auto P = [&tf] (float x, float y) { return Point<float> (x, y).transformedBy (tf); };

    const auto bl = P (-w * 0.5f, 0.0f), bcl = P (-w * 0.14f, w * 0.12f), bcr = P (w * 0.22f, w * 0.12f), br = P (w * 0.5f, 0.0f);
    const auto sl = P (-w * 0.5f, -h + t), scl = P (-w * 0.14f, -h + t * 0.72f);
    const auto scr = P (w * 0.22f, -h + t * 0.8f), sr = P (w * 0.5f, -h + t * 1.06f);
    const auto tp = P (w * c.tipSkew, -h);

    const Colour base = c.colour();
    const Colour dark = colours::deep.interpolatedWith (base, 0.18f);

    if (glow > 0.0f)
    {
        const auto mid = c.centre();
        const float rad = h * 0.75f + w;
        g.setGradientFill (ColourGradient (base.withAlpha (0.30f * glow * alpha), mid, base.withAlpha (0.0f),
                                           mid.translated (rad, 0.0f), true));
        g.fillEllipse (Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (mid));
    }

    auto quad = [] (Point<float> a, Point<float> b, Point<float> c2, Point<float> d)
    {
        Path p;
        p.startNewSubPath (a);
        p.lineTo (b);
        p.lineTo (c2);
        p.lineTo (d);
        p.closeSubPath();
        return p;
    };
    auto tri = [] (Point<float> a, Point<float> b, Point<float> c2)
    {
        Path p;
        p.addTriangle (a, b, c2);
        return p;
    };

    const float b = brightness * (1.0f + 0.35f * glow);
    auto face = [&] (const Path& p, float shade, Point<float> from, Point<float> to)
    {
        g.setGradientFill (ColourGradient (dark.withAlpha (0.92f * alpha), from,
                                           shadeOf (base, shade * b).withAlpha (alpha), to, false));
        g.fillPath (p);
    };

    const auto baseMid = (bl + br) * 0.5f;
    face (quad (bl, bcl, scl, sl), 0.62f, baseMid, (sl + scl) * 0.5f);
    face (quad (bcl, bcr, scr, scl), 0.9f, baseMid, (scl + scr) * 0.5f);
    face (quad (bcr, br, sr, scr), 0.42f, baseMid, (scr + sr) * 0.5f);

    g.setColour (shadeOf (base, 1.12f * b).withAlpha (alpha));
    g.fillPath (tri (sl, scl, tp));
    g.setColour (shadeOf (base, 1.28f * b).withAlpha (alpha));
    g.fillPath (tri (scl, scr, tp));
    g.setColour (shadeOf (base, 0.68f * b).withAlpha (alpha));
    g.fillPath (tri (scr, sr, tp));

    // a faint internal refraction streak across the front face
    {
        const auto a = bcl + (scl - bcl) * 0.35f, z = scr + (bcr - scr) * 0.3f;
        g.setColour (colours::frost.withAlpha ((0.10f + 0.12f * glow) * alpha));
        g.drawLine ({ a, z }, jmax (0.6f, w * 0.05f));
    }

    // edge highlights
    const float edgeA = jlimit (0.0f, 1.0f, (0.32f + 0.45f * glow) * brightness) * alpha;
    g.setColour (colours::frost.withAlpha (edgeA));
    g.drawLine ({ scl, tp }, 1.0f);
    g.drawLine ({ bcl, scl }, 0.8f);
    g.setColour (colours::frost.withAlpha (edgeA * 0.55f));
    g.drawLine ({ sl, tp }, 0.8f);
    g.drawLine ({ scl, scr }, 0.7f);

    Path outline;
    outline.startNewSubPath (bl);
    outline.lineTo (sl);
    outline.lineTo (tp);
    outline.lineTo (sr);
    outline.lineTo (br);
    g.setColour (base.brighter (0.4f).withAlpha ((0.28f + 0.3f * glow) * alpha));
    g.strokePath (outline, PathStrokeType (0.8f, PathStrokeType::mitered));
}

void drawGlint (Graphics& g, Point<float> centre, float size, Colour colour, float rotation)
{
    if (size <= 0.2f)
        return;

    g.setGradientFill (ColourGradient (colour.withMultipliedAlpha (0.45f), centre, colour.withAlpha (0.0f),
                                       centre.translated (size * 1.1f, 0.0f), true));
    g.fillEllipse (Rectangle<float> (size * 2.2f, size * 2.2f).withCentre (centre));

    Path star;
    const float w = size * 0.16f;
    star.startNewSubPath (0.0f, -size);
    star.lineTo (w, -w);
    star.lineTo (size, 0.0f);
    star.lineTo (w, w);
    star.lineTo (0.0f, size);
    star.lineTo (-w, w);
    star.lineTo (-size, 0.0f);
    star.lineTo (-w, -w);
    star.closeSubPath();
    g.setColour (colour.withAlpha (1.0f).interpolatedWith (Colours::white, 0.6f).withAlpha (colour.getFloatAlpha()));
    g.fillPath (star, AffineTransform::rotation (rotation).translated (centre));
}

Path snowflakePath (Point<float> centre, float radius)
{
    Path p;
    for (int arm = 0; arm < 6; ++arm)
    {
        const float a = MathConstants<float>::twoPi * (float) arm / 6.0f;
        const Point<float> dir (std::sin (a), -std::cos (a));
        const Point<float> side (dir.y * -1.0f, dir.x);
        p.startNewSubPath (centre);
        p.lineTo (centre + dir * radius);
        for (float f : { 0.5f, 0.78f })
        {
            const auto root = centre + dir * (radius * f);
            const float len = radius * (f < 0.6f ? 0.32f : 0.22f);
            p.startNewSubPath (root);
            p.lineTo (root + (dir * 0.7f + side * 0.7f) * len);
            p.startNewSubPath (root);
            p.lineTo (root + (dir * 0.7f - side * 0.7f) * len);
        }
    }
    return p;
}

void drawGemIcon (Graphics& g, Point<float> centre, float radius)
{
    constexpr int sides = 8;
    Point<float> outer[sides], inner[sides];
    for (int k = 0; k < sides; ++k)
    {
        const float a = MathConstants<float>::twoPi * ((float) k + 0.5f) / (float) sides;
        const Point<float> d (std::sin (a), -std::cos (a));
        outer[k] = centre + d * radius;
        inner[k] = centre + d * (radius * 0.5f);
    }

    g.setGradientFill (ColourGradient (colours::ice.withAlpha (0.35f), centre, colours::ice.withAlpha (0.0f),
                                       centre.translated (radius * 2.0f, 0.0f), true));
    g.fillEllipse (Rectangle<float> (radius * 4.0f, radius * 4.0f).withCentre (centre));

    const Point<float> light (-0.55f, -0.83f);
    for (int k = 0; k < sides; ++k)
    {
        const int k2 = (k + 1) % sides;
        Path facet;
        facet.startNewSubPath (outer[k]);
        facet.lineTo (outer[k2]);
        facet.lineTo (inner[k2]);
        facet.lineTo (inner[k]);
        facet.closeSubPath();
        const float am = MathConstants<float>::twoPi * ((float) k + 1.0f) / (float) sides;
        const float lit = jlimit (0.0f, 1.0f, 0.5f + 0.5f * (std::sin (am) * light.x - std::cos (am) * light.y));
        g.setColour (colours::deep.interpolatedWith (k % 2 == 0 ? colours::ice : colours::violet, 0.3f + 0.65f * lit));
        g.fillPath (facet);
    }

    Path table;
    table.startNewSubPath (inner[0]);
    for (int k = 1; k < sides; ++k)
        table.lineTo (inner[k]);
    table.closeSubPath();
    g.setGradientFill (ColourGradient (colours::frost, centre.translated (-radius * 0.4f, -radius * 0.4f),
                                       colours::ice.interpolatedWith (colours::violet, 0.5f),
                                       centre.translated (radius * 0.4f, radius * 0.4f), false));
    g.fillPath (table);

    g.setColour (colours::frost.withAlpha (0.5f));
    g.strokePath (table, PathStrokeType (0.7f));
    for (int k = 0; k < sides; ++k)
        g.drawLine ({ outer[k], inner[k] }, 0.6f);
}
} // namespace cave
