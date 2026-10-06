#include "CritterArt.h"

using namespace juce;

namespace critter
{
namespace
{
    constexpr float pi = MathConstants<float>::pi;

    struct Shape
    {
        float w, h;      // body size in units of the drawing area
        float eyeY;      // eye height as a fraction of h
        float eyeDx;     // eye spacing as a fraction of w
        float mouthY;    // mouth height as a fraction of h
        float mouthW;
    };

    Shape shapeFor (int kind)
    {
        switch (kind)
        {
            case 0:  return { 0.80f, 0.64f, 0.66f, 0.21f, 0.43f, 0.14f }; // Bumble
            case 1:  return { 0.78f, 0.54f, 0.00f, 0.20f, 0.36f, 0.14f }; // Snappy (eyes on stalks)
            case 2:  return { 0.64f, 0.71f, 0.60f, 0.22f, 0.34f, 0.13f }; // Clappo
            case 3:  return { 0.62f, 0.60f, 0.62f, 0.23f, 0.36f, 0.12f }; // Tiki
            case 4:  return { 0.56f, 0.74f, 0.64f, 0.22f, 0.36f, 0.10f }; // Tsss
            case 5:  return { 0.84f, 0.68f, 0.62f, 0.21f, 0.27f, 0.12f }; // Boomer
            case 6:  return { 0.74f, 0.62f, 0.64f, 0.22f, 0.24f, 0.10f }; // Clink
            default: return { 0.64f, 0.66f, 0.60f, 0.00f, 0.29f, 0.11f }; // Zappy (one big eye)
        }
    }

    /** Gumdrop body: round top, widest a little below the middle, flat-ish feet. Unit coords, y up = negative. */
    Path makeBlob (float w, float h)
    {
        const float hw = w * 0.5f;
        const float midY = -h * 0.42f;
        Path p;
        p.startNewSubPath (0.0f, -h);
        p.cubicTo (hw * 0.62f, -h, hw, -h + (h + midY) * 0.42f, hw, midY);
        p.cubicTo (hw, midY * 0.3f, hw * 0.86f, 0.0f, 0.0f, 0.0f);
        p.cubicTo (-hw * 0.86f, 0.0f, -hw, midY * 0.3f, -hw, midY);
        p.cubicTo (-hw, -h + (h + midY) * 0.42f, -hw * 0.62f, -h, 0.0f, -h);
        p.closeSubPath();
        return p;
    }

    /** Jellyfish body with a scalloped hem (Zappy). */
    Path makeJelly (float w, float h)
    {
        const float hw = w * 0.5f;
        const float sideY = -h * 0.45f;
        Path p;
        p.startNewSubPath (-hw, sideY);
        p.cubicTo (-hw, -h * 0.98f, -hw * 0.45f, -h, 0.0f, -h);
        p.cubicTo (hw * 0.45f, -h, hw, -h * 0.98f, hw, sideY);
        p.cubicTo (hw, -h * 0.2f, hw * 0.98f, -0.06f, hw * 0.96f, -0.02f);
        const int bumps = 4;
        for (int i = 0; i < bumps; ++i)
        {
            const float x0 = hw * 0.96f - (float) i * (w * 0.96f) / (float) bumps;
            const float x1 = x0 - (w * 0.96f) / (float) bumps;
            p.quadraticTo ((x0 + x1) * 0.5f, 0.05f, x1, -0.02f);
        }
        p.cubicTo (-hw * 0.98f, -0.06f, -hw, -h * 0.2f, -hw, sideY);
        p.closeSubPath();
        return p;
    }

    Path ellipseAt (float cx, float cy, float w, float h, float rotation = 0.0f)
    {
        Path e;
        e.addEllipse (-w * 0.5f, -h * 0.5f, w, h);
        e.applyTransform (AffineTransform::rotation (rotation).translated (cx, cy));
        return e;
    }

    struct Painter
    {
        Graphics& g;
        AffineTransform xf;
        float size, sx, sy, lw;
        Colour base;

        Path t (Path p) const { p.applyTransform (xf); return p; }
        Point<float> pt (float x, float y) const { return Point<float> (x, y).transformedBy (xf); }

        void fillOutlined (const Path& unitPath, Colour fill, float outline = 1.0f) const
        {
            const auto p = t (unitPath);
            g.setColour (fill);
            g.fillPath (p);
            if (outline > 0.0f)
            {
                g.setColour (colours::ink);
                g.strokePath (p, PathStrokeType (lw * outline, PathStrokeType::curved, PathStrokeType::rounded));
            }
        }

        void stroke (const Path& unitPath, Colour c, float widthMul) const
        {
            g.setColour (c);
            g.strokePath (t (unitPath), PathStrokeType (lw * widthMul, PathStrokeType::curved, PathStrokeType::rounded));
        }

        void eye (float ux, float uy, float rxUnit, float ryUnit, const Pose& pose, float baselineBlink = 0.0f) const
        {
            const auto c = pt (ux, uy);
            const float rx = rxUnit * size * sx, ry = ryUnit * size * sy;
            const auto& ink = colours::ink;

            if (pose.excited > 0.5f)
            {
                Path happy;
                happy.startNewSubPath (c.x - rx * 0.9f, c.y + ry * 0.25f);
                happy.quadraticTo (c.x, c.y - ry * 0.9f, c.x + rx * 0.9f, c.y + ry * 0.25f);
                g.setColour (ink);
                g.strokePath (happy, PathStrokeType (lw * 1.15f, PathStrokeType::curved, PathStrokeType::rounded));
                return;
            }

            const float open = 1.0f - jlimit (0.0f, 1.0f, jmax (pose.blink, baselineBlink));
            if (open < 0.16f)
            {
                Path lid;
                lid.startNewSubPath (c.x - rx * 0.9f, c.y);
                lid.quadraticTo (c.x, c.y + ry * 0.45f, c.x + rx * 0.9f, c.y);
                g.setColour (ink);
                g.strokePath (lid, PathStrokeType (lw * 1.05f, PathStrokeType::curved, PathStrokeType::rounded));
                return;
            }

            // Sclera, squashed from the top when the lid comes down.
            const float h = ry * 2.0f * open;
            auto sclera = Rectangle<float> (rx * 2.0f, h).withCentre ({ c.x, c.y + ry * (1.0f - open) });
            g.setColour (Colours::white);
            g.fillEllipse (sclera);

            {
                // Only a half-closed eye needs clipping; open pupils are kept inside the sclera.
                const bool clipNeeded = open < 0.99f;
                Graphics::ScopedSaveState ss (g);
                if (clipNeeded)
                {
                    Path clip;
                    clip.addEllipse (sclera);
                    g.reduceClipRegion (clip);
                }
                const float pr = rx * 0.6f;
                auto lk = pose.look;
                if (lk.getDistanceFromOrigin() > 1.0f)
                    lk /= lk.getDistanceFromOrigin();
                const auto pc = c.translated (lk.x * rx * 0.36f, lk.y * ry * 0.3f + ry * (1.0f - open) * 0.5f);
                g.setColour (ink);
                g.fillEllipse (Rectangle<float> (pr * 2.0f, pr * 2.1f).withCentre (pc));
                g.setColour (Colours::white);
                g.fillEllipse (Rectangle<float> (pr * 0.7f, pr * 0.7f).withCentre (pc.translated (-pr * 0.32f, -pr * 0.38f)));

                if (clipNeeded)
                {
                    // eyelid in body colour
                    g.setColour (base.darker (0.05f));
                    g.fillRect (sclera.withHeight (jmax (0.0f, ry * 0.2f * (1.0f - open))));
                }
            }
            g.setColour (ink);
            g.drawEllipse (sclera, lw * 0.8f);
        }

        void mouth (float ux, float uy, float widthUnit, float open) const
        {
            if (open < 0.08f)
            {
                Path smile;
                smile.startNewSubPath (ux - widthUnit * 0.5f, uy - 0.005f);
                smile.quadraticTo (ux, uy + 0.055f, ux + widthUnit * 0.5f, uy - 0.005f);
                stroke (smile, colours::ink, 1.0f);
                return;
            }

            open = jmin (1.3f, open);
            const float w = widthUnit * (0.75f + 0.4f * open);
            const float h = 0.03f + 0.115f * open;
            Path m;
            m.addRoundedRectangle (ux - w * 0.5f, uy - h * 0.3f, w, h, jmin (w, h) * 0.48f);
            const auto mp = t (m);
            g.setColour (Colour (0xff5a2346));
            g.fillPath (mp);
            {
                // tongue (sized to sit inside the mouth, so no clipping needed)
                const auto mb = mp.getBounds();
                g.setColour (Colour (0xffff7d9c));
                g.fillEllipse (Rectangle<float> (mb.getWidth() * 0.62f, mb.getHeight() * 0.42f)
                                   .withCentre ({ mb.getCentreX(), mb.getBottom() - mb.getHeight() * 0.27f }));
            }
            g.setColour (colours::ink);
            g.strokePath (mp, PathStrokeType (lw, PathStrokeType::curved, PathStrokeType::rounded));
        }

        void cheeks (float ux, float uy) const
        {
            g.setColour (colours::blush.withAlpha (0.38f));
            g.fillPath (t (ellipseAt (-ux, uy, 0.10f, 0.055f)));
            g.fillPath (t (ellipseAt (ux, uy, 0.10f, 0.055f)));
        }
    };
} // namespace

Path sparklePath (Point<float> c, float r, float angle)
{
    Path p;
    const float inner = r * 0.28f;
    for (int i = 0; i < 8; ++i)
    {
        const float a = angle + (float) i * pi * 0.25f;
        const float rr = (i % 2 == 0) ? r : inner;
        const auto q = c + Point<float> (std::sin (a), -std::cos (a)) * rr;
        if (i == 0)
            p.startNewSubPath (q);
        else
            p.lineTo (q);
    }
    p.closeSubPath();
    return p;
}

void drawCritter (Graphics& g, int kind, Rectangle<float> area, const Pose& pose)
{
    const float size = jmin (area.getWidth(), area.getHeight());
    if (size < 4.0f)
        return;

    const auto shape = shapeFor (kind);
    const float W = shape.w, H = shape.h;
    const float sq = jlimit (-0.6f, 0.8f, pose.squash);
    const float sx = 1.0f + 0.2f * sq, sy = 1.0f - 0.24f * sq;
    const float groundY = area.getBottom() - size * 0.05f;
    const float cx = area.getCentreX();
    const Colour base = colours::critter (kind);
    const Colour ink = colours::ink;
    const float time = pose.time;
    const float act = jlimit (0.0f, 1.0f, pose.action);

    Painter P { g, AffineTransform::scale (size * sx, size * sy).translated (cx, groundY - pose.bob), size, sx, sy,
                jmax (1.1f, size * 0.025f), base };

    // ---- ground shadow -------------------------------------------------------
    {
        const float lift = jlimit (0.0f, 0.5f, pose.bob / size * 2.5f);
        const float sw = W * size * (1.0f + 0.16f * sq) * (1.0f - lift);
        g.setColour (ink.withAlpha (0.13f * (1.0f - lift * 0.6f)));
        g.fillEllipse (Rectangle<float> (sw, size * 0.07f).withCentre ({ cx, groundY + size * 0.005f }));
    }

    // ---- behind the body ------------------------------------------------------
    switch (kind)
    {
        case 0: // Bumble: wings + antennae
        {
            const float flap = std::sin (time * 48.0f) * 0.35f * act;
            for (int s = -1; s <= 1; s += 2)
            {
                const float fs = (float) s;
                const auto wing = ellipseAt (fs * 0.31f, -H * 0.84f, 0.3f, 0.16f, fs * (-0.75f - flap));
                P.fillOutlined (wing, Colour (0xffe6f6ff).withAlpha (0.92f), 0.8f);
                Path vein;
                vein.startNewSubPath (fs * 0.22f, -H * 0.76f);
                vein.lineTo (fs * 0.4f, -H * 0.94f);
                vein.applyTransform (AffineTransform::rotation (-fs * flap * 0.5f, fs * 0.22f, -H * 0.76f));
                P.stroke (vein, Colour (0xff8fc6ee), 0.7f);
            }
            for (int s = -1; s <= 1; s += 2)
            {
                const float wob = std::sin (time * 3.0f + (float) s) * 0.01f + act * 0.02f * (float) s;
                Path a;
                a.startNewSubPath ((float) s * 0.06f, -H + 0.03f);
                a.quadraticTo ((float) s * 0.07f, -H - 0.09f, (float) s * 0.13f + wob, -H - 0.12f);
                P.stroke (a, ink, 1.0f);
                g.setColour (ink);
                g.fillPath (P.t (ellipseAt ((float) s * 0.13f + wob, -H - 0.12f, 0.055f, 0.055f)));
            }
            break;
        }
        case 1: // Snappy: eye stalks + arms
        {
            for (int s = -1; s <= 1; s += 2)
            {
                Path stalk;
                stalk.startNewSubPath ((float) s * 0.08f, -H * 0.85f);
                stalk.quadraticTo ((float) s * 0.1f, -H - 0.02f, (float) s * 0.12f, -H - 0.06f);
                P.stroke (stalk, ink, 2.6f);
                P.stroke (stalk, base, 1.2f);

                const float lift = act * 0.08f;
                Path arm;
                arm.startNewSubPath ((float) s * W * 0.38f, -H * 0.42f);
                arm.quadraticTo ((float) s * (W * 0.56f), -H * 0.42f, (float) s * (W * 0.5f + 0.09f), -H * 0.58f - lift);
                P.stroke (arm, ink, 3.2f);
                P.stroke (arm, base.darker (0.08f), 1.4f);
            }
            break;
        }
        case 3: // Tiki: tuft + wings
        {
            const float tuftColourWob = std::sin (time * 2.0f) * 0.04f;
            P.fillOutlined (ellipseAt (-0.045f, -H - 0.03f, 0.04f, 0.1f, -0.5f + tuftColourWob), base.darker (0.25f), 0.8f);
            P.fillOutlined (ellipseAt (0.045f, -H - 0.03f, 0.04f, 0.1f, 0.5f + tuftColourWob), base.darker (0.25f), 0.8f);
            P.fillOutlined (ellipseAt (0.0f, -H - 0.05f, 0.045f, 0.12f, tuftColourWob), base.darker (0.15f), 0.8f);
            const float flap = std::sin (time * 34.0f) * 0.6f * act;
            for (int s = -1; s <= 1; s += 2)
                P.fillOutlined (ellipseAt ((float) s * W * 0.47f, -H * 0.42f, 0.1f, 0.17f, (float) s * (0.35f + flap)),
                                base.brighter (0.15f), 0.9f);
            break;
        }
        case 4: // Tsss: curly tail
        {
            const float wob = std::sin (time * 2.6f) * 0.015f + act * 0.02f;
            Path tail;
            tail.startNewSubPath (W * 0.2f, -0.04f);
            tail.cubicTo (W * 0.5f, 0.0f, W * 0.5f + 0.16f, -0.01f, W * 0.5f + 0.17f, -0.1f - wob);
            tail.cubicTo (W * 0.5f + 0.18f, -0.17f - wob, W * 0.5f + 0.09f, -0.18f - wob, W * 0.5f + 0.09f, -0.12f - wob);
            P.stroke (tail, ink, 4.6f);
            P.stroke (tail, base, 2.6f);
            break;
        }
        case 5: // Boomer: ears
        {
            for (int s = -1; s <= 1; s += 2)
            {
                const float wig = act * 0.02f * (float) s;
                P.fillOutlined (ellipseAt ((float) s * W * 0.34f + wig, -H * 0.9f, 0.2f, 0.19f), base, 1.0f);
                P.fillOutlined (ellipseAt ((float) s * W * 0.34f + wig, -H * 0.9f, 0.1f, 0.095f), Colour (0xffffb7c5), 0.0f);
            }
            break;
        }
        case 6: // Clink: horns + ears
        {
            for (int s = -1; s <= 1; s += 2)
            {
                const float fs = (float) s;
                Path horn;
                horn.startNewSubPath (fs * 0.1f, -H * 0.93f);
                horn.quadraticTo (fs * 0.17f, -H - 0.11f, fs * 0.24f, -H - 0.07f);
                horn.quadraticTo (fs * 0.2f, -H - 0.03f, fs * 0.2f, -H * 0.88f);
                horn.closeSubPath();
                P.fillOutlined (horn, Colour (0xfffff2d2), 0.9f);

                const float flick = act * 0.25f * fs;
                P.fillOutlined (ellipseAt (fs * W * 0.5f, -H * 0.76f, 0.15f, 0.075f, fs * 0.35f + flick), base.darker (0.05f), 0.9f);
                P.fillOutlined (ellipseAt (fs * W * 0.52f, -H * 0.76f, 0.07f, 0.035f, fs * 0.35f + flick), Colour (0xffffc6de), 0.0f);
            }
            break;
        }
        case 7: // Zappy: antenna
        {
            const float wob = std::sin (time * 4.0f) * 0.015f + act * std::sin (time * 60.0f) * 0.02f;
            Path ant;
            ant.startNewSubPath (0.0f, -H + 0.02f);
            ant.quadraticTo (0.0f, -H - 0.1f, 0.05f + wob, -H - 0.16f);
            P.stroke (ant, ink, 1.1f);

            const auto tip = P.pt (0.05f + wob, -H - 0.16f);
            const float r = size * 0.034f * (1.0f + act * 0.35f);
            for (int i = 3; i > 0; --i)
            {
                g.setColour (Colour (0xfffff27a).withAlpha ((0.12f + act * 0.15f) / (float) i));
                g.fillEllipse (Rectangle<float> (r * (2.0f + (float) i * 1.4f), r * (2.0f + (float) i * 1.4f)).withCentre (tip));
            }
            g.setColour (Colour (0xfffff27a));
            g.fillEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (tip));
            g.setColour (ink);
            g.drawEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (tip), P.lw * 0.8f);

            if (act > 0.08f)
            {
                for (int k = 0; k < 3; ++k)
                {
                    const float a = time * 3.0f + (float) k * 2.1f;
                    const auto dir = Point<float> (std::cos (a), std::sin (a));
                    const auto p0 = tip + dir * (r * 1.6f);
                    const auto p1 = tip + dir * (r * (2.6f + act * 1.6f)) + Point<float> (-dir.y, dir.x) * r * 0.6f;
                    const auto p2 = tip + dir * (r * (3.4f + act * 2.4f));
                    Path bolt;
                    bolt.startNewSubPath (p0);
                    bolt.lineTo (p1);
                    bolt.lineTo (p2);
                    g.setColour (Colour (0xffffd84a).withAlpha (jmin (1.0f, act * 1.6f)));
                    g.strokePath (bolt, PathStrokeType (P.lw * 0.9f, PathStrokeType::curved, PathStrokeType::rounded));
                }
            }
            break;
        }
        default: break;
    }

    // ---- body ----------------------------------------------------------------
    const Path bodyUnit = kind == 7 ? makeJelly (W, H) : makeBlob (W, H);
    const Path body = P.t (bodyUnit);
    const auto bb = body.getBounds();

    g.setGradientFill (ColourGradient (base.brighter (0.32f), bb.getX() + bb.getWidth() * 0.32f, bb.getY() + bb.getHeight() * 0.2f,
                                       base.darker (0.1f), bb.getRight(), bb.getBottom(), true));
    g.fillPath (body);

    {
        const bool hasDetails = kind == 0 || kind == 4 || kind == 5 || kind == 6 || kind == 7;
        Graphics::ScopedSaveState ss (g);
        if (hasDetails)
            g.reduceClipRegion (body);

        switch (kind)
        {
            case 0: // stripes
            {
                g.setColour (Colour (0xff6b4a2f).withAlpha (0.62f));
                Path stripes;
                stripes.addRectangle (-W, -H * 0.28f, W * 2.0f, H * 0.1f);
                stripes.addRectangle (-W, -H * 0.1f, W * 2.0f, H * 0.12f);
                g.fillPath (P.t (stripes));
                break;
            }
            case 4: // diamonds
            {
                g.setColour (base.darker (0.25f).withAlpha (0.55f));
                for (auto [x, y] : { std::pair { -0.14f, -0.13f }, std::pair { 0.0f, -0.07f }, std::pair { 0.14f, -0.13f } })
                {
                    Path d;
                    d.startNewSubPath (x, y * H - 0.045f);
                    d.lineTo (x + 0.035f, y * H);
                    d.lineTo (x, y * H + 0.045f);
                    d.lineTo (x - 0.035f, y * H);
                    d.closeSubPath();
                    g.fillPath (P.t (d));
                }
                break;
            }
            case 5: // belly
                g.setColour (Colour (0xfffff1dc).withAlpha (0.4f));
                g.fillPath (P.t (ellipseAt (0.0f, -H * 0.06f, W * 0.62f, H * 0.36f)));
                break;
            case 6: // spots + collar
            {
                g.setColour (Colour (0xffd94f97).withAlpha (0.5f));
                g.fillPath (P.t (ellipseAt (-W * 0.3f, -H * 0.78f, 0.17f, 0.12f, 0.4f)));
                g.fillPath (P.t (ellipseAt (W * 0.34f, -H * 0.24f, 0.18f, 0.15f, -0.3f)));
                g.fillPath (P.t (ellipseAt (-W * 0.36f, -H * 0.12f, 0.12f, 0.09f)));
                Path collar;
                collar.startNewSubPath (-W, -H * 0.15f);
                collar.quadraticTo (0.0f, -H * 0.06f, W, -H * 0.15f);
                P.stroke (collar, Colour (0xff7b5cff), 2.4f);
                break;
            }
            case 7: // freckles
                g.setColour (Colours::white.withAlpha (0.45f));
                g.fillPath (P.t (ellipseAt (-W * 0.3f, -H * 0.76f, 0.05f, 0.05f)));
                g.fillPath (P.t (ellipseAt (W * 0.3f, -H * 0.82f, 0.04f, 0.04f)));
                g.fillPath (P.t (ellipseAt (W * 0.36f, -H * 0.66f, 0.03f, 0.03f)));
                break;
            default: break;
        }

        // glossy highlight
        g.setColour (Colours::white.withAlpha (0.42f));
        g.fillPath (P.t (ellipseAt (-W * 0.24f, -H * 0.8f, W * 0.24f, H * 0.11f, -0.45f)));
        g.setColour (Colours::white.withAlpha (0.6f));
        g.fillPath (P.t (ellipseAt (-W * 0.33f, -H * 0.66f, 0.035f, 0.035f)));
    }

    g.setColour (ink);
    g.strokePath (body, PathStrokeType (P.lw * 1.15f, PathStrokeType::curved, PathStrokeType::rounded));

    // ---- face -----------------------------------------------------------------
    const float eyeY = -H * shape.eyeY;
    const float mouthY = -H * shape.mouthY;
    const float open = pose.mouth;

    switch (kind)
    {
        case 1: // Snappy: eyes on the stalks
            P.eye (-0.12f, -H - 0.07f, 0.07f, 0.075f, pose);
            P.eye (0.12f, -H - 0.07f, 0.07f, 0.075f, pose);
            P.cheeks (W * 0.3f, -H * 0.42f);
            P.mouth (0.0f, mouthY, shape.mouthW, open);
            break;

        case 3: // Tiki: beak
        {
            P.eye (-W * shape.eyeDx, eyeY, 0.06f, 0.07f, pose);
            P.eye (W * shape.eyeDx, eyeY, 0.06f, 0.07f, pose);
            P.cheeks (W * 0.32f, -H * 0.4f);
            const float bo = jmin (1.2f, open) * 0.05f;
            Path lower;
            lower.startNewSubPath (-0.045f, mouthY + bo * 0.4f);
            lower.lineTo (0.045f, mouthY + bo * 0.4f);
            lower.lineTo (0.0f, mouthY + 0.04f + bo);
            lower.closeSubPath();
            P.fillOutlined (lower, Colour (0xffff8a2a), 0.85f);
            Path upper;
            upper.startNewSubPath (-0.06f, mouthY - 0.02f - bo * 0.3f);
            upper.lineTo (0.06f, mouthY - 0.02f - bo * 0.3f);
            upper.lineTo (0.0f, mouthY + 0.045f - bo * 0.4f);
            upper.closeSubPath();
            P.fillOutlined (upper, Colour (0xffffb340), 0.85f);
            break;
        }

        case 4: // Tsss: lazy eyes + forked tongue
        {
            const float lazy = pose.excited > 0.5f ? 0.0f : 0.42f * (1.0f - jlimit (0.0f, 1.0f, open * 2.0f));
            P.eye (-W * shape.eyeDx, eyeY, 0.065f, 0.075f, pose, lazy);
            P.eye (W * shape.eyeDx, eyeY, 0.065f, 0.075f, pose, lazy);
            P.cheeks (W * 0.3f, -H * 0.42f);
            if (act > 0.05f)
            {
                const float len = 0.05f + 0.13f * act;
                const float wig = std::sin (time * 40.0f) * 0.012f * act;
                Path tongue;
                tongue.startNewSubPath (0.0f, mouthY);
                tongue.lineTo (wig, mouthY + len);
                tongue.lineTo (wig - 0.025f, mouthY + len + 0.03f);
                tongue.startNewSubPath (wig, mouthY + len);
                tongue.lineTo (wig + 0.025f, mouthY + len + 0.03f);
                P.stroke (tongue, Colour (0xffff4f7b), 1.3f);
            }
            P.mouth (0.0f, mouthY, shape.mouthW, open);
            break;
        }

        case 5: // Boomer: muzzle + nose
        {
            P.eye (-W * shape.eyeDx, eyeY, 0.065f, 0.075f, pose);
            P.eye (W * shape.eyeDx, eyeY, 0.065f, 0.075f, pose);
            g.setColour (Colour (0xfffff1dc).withAlpha (0.92f));
            g.fillPath (P.t (ellipseAt (0.0f, mouthY - 0.02f, 0.3f, 0.19f)));
            P.cheeks (W * 0.36f, -H * 0.4f);
            g.setColour (ink);
            g.fillPath (P.t (ellipseAt (0.0f, mouthY - 0.07f, 0.075f, 0.048f)));
            P.mouth (0.0f, mouthY + 0.005f, shape.mouthW, open);
            break;
        }

        case 6: // Clink: snout, collar, bell
        {
            P.eye (-W * shape.eyeDx, eyeY, 0.06f, 0.07f, pose);
            P.eye (W * shape.eyeDx, eyeY, 0.06f, 0.07f, pose);
            P.cheeks (W * 0.34f, -H * 0.46f);
            P.fillOutlined (ellipseAt (0.0f, -H * 0.4f, 0.3f, 0.14f), Colour (0xffffc8df), 0.75f);
            g.setColour (ink.withAlpha (0.75f));
            g.fillPath (P.t (ellipseAt (-0.055f, -H * 0.4f, 0.03f, 0.04f)));
            g.fillPath (P.t (ellipseAt (0.055f, -H * 0.4f, 0.03f, 0.04f)));
            P.mouth (0.0f, mouthY, shape.mouthW, open);


            const float swing = std::sin (time * 15.0f) * 0.55f * act;
            const float hx = 0.0f, hy = -H * 0.105f;
            Path bell;
            bell.startNewSubPath (-0.03f, 0.0f);
            bell.lineTo (0.03f, 0.0f);
            bell.quadraticTo (0.045f, 0.05f, 0.065f, 0.085f);
            bell.lineTo (-0.065f, 0.085f);
            bell.quadraticTo (-0.045f, 0.05f, -0.03f, 0.0f);
            bell.closeSubPath();
            bell.addEllipse (-0.016f, 0.08f, 0.032f, 0.03f);
            bell.applyTransform (AffineTransform::rotation (swing).translated (hx, hy));
            P.fillOutlined (bell, Colour (0xffffd24a), 0.85f);
            break;
        }

        case 7: // Zappy: one big eye
            P.eye (0.0f, -H * 0.6f, 0.115f, 0.125f, pose);
            P.cheeks (W * 0.3f, -H * 0.36f);
            P.mouth (0.0f, mouthY, shape.mouthW, open);
            break;

        default: // Bumble, Clappo
            P.eye (-W * shape.eyeDx, eyeY, 0.065f, 0.075f, pose);
            P.eye (W * shape.eyeDx, eyeY, 0.065f, 0.075f, pose);
            P.cheeks (W * 0.31f, kind == 0 ? -H * 0.5f : -H * 0.41f);
            P.mouth (0.0f, mouthY, shape.mouthW, open);
            break;
    }

    // ---- in front -------------------------------------------------------------
    if (kind == 1)
    {
        // Snappy's claws: a notch that snaps open on every hit
        for (int s = -1; s <= 1; s += 2)
        {
            const float fs = (float) s;
            const float lift = act * 0.08f;
            const float notch = 0.32f + 0.55f * act;
            const float aim = fs * 0.7f;
            Path claw;
            claw.addPieSegment (-0.085f, -0.085f, 0.17f, 0.17f, aim + notch, aim + MathConstants<float>::twoPi - notch, 0.0f);
            claw.applyTransform (AffineTransform::translation (fs * (W * 0.5f + 0.09f), -H * 0.58f - lift));
            P.fillOutlined (claw, base.brighter (0.08f), 1.0f);
        }
    }
    else if (kind == 2)
    {
        // a single curl of hair
        Path curl;
        curl.addCentredArc (0.02f, -H - 0.035f, 0.035f, 0.035f, 0.0f, pi * 1.1f, pi * 2.6f, true);
        P.stroke (curl, ink, 1.0f);

        // Clappo's hands: down at the sides, swing up and clap above the head on a hit
        const float t = jlimit (0.0f, 1.0f, act * 1.3f);
        const float e = t * t * (3.0f - 2.0f * t);
        const Point<float> clapAt (0.0f, -H - 0.08f);
        for (int s = -1; s <= 1; s += 2)
        {
            const float fs = (float) s;
            const float restY = -H * 0.42f + std::sin (time * 2.4f + fs) * 0.01f;
            const float x = jmap (e, fs * (W * 0.5f + 0.045f), fs * 0.055f) + fs * std::sin (e * pi) * 0.1f;
            const float y = jmap (e, restY, clapAt.y);
            P.fillOutlined (ellipseAt (x, y, 0.12f, 0.12f), base.brighter (0.2f), 1.0f);
        }
        if (e > 0.7f)
        {
            for (int k = -1; k <= 1; ++k)
            {
                const float a = (float) k * 0.75f;
                Path line;
                line.startNewSubPath (clapAt.x + std::sin (a) * 0.1f, clapAt.y - std::cos (a) * 0.1f);
                line.lineTo (clapAt.x + std::sin (a) * 0.16f, clapAt.y - std::cos (a) * 0.16f);
                P.stroke (line, ink.withAlpha ((e - 0.7f) / 0.3f), 1.0f);
            }
        }
    }
}
} // namespace critter
