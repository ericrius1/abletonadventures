#include "CrystalEditor.h"
#include "CrystalLookAndFeel.h"
#include <PluginAssets.h>

using namespace juce;
namespace C = cave::colours;

namespace
{
    constexpr int baseWidth = 880, baseHeight = 560;
    constexpr float stageCorner = 18.0f;
    constexpr float curveMaxSeconds = 32.0f;

    aa::Theme makeTheme()
    {
        aa::Theme t;
        t.background = C::night;
        t.backgroundAlt = C::nightAlt;
        t.panel = C::ice.withAlpha (0.07f);
        t.panelOutline = C::ice.withAlpha (0.2f);
        t.text = C::text;
        t.textDim = C::text.withAlpha (0.58f);
        t.accent = C::ice;
        t.accent2 = C::violet;
        t.knobBody = Colour (0xff22305e);
        t.knobTrack = Colours::white.withAlpha (0.07f);
        t.shadow = Colours::black.withAlpha (0.6f);
        t.popupBackground = Colour (0xf50e1738);
        t.pill = Colour (0xff070d24).withAlpha (0.62f);
        t.cornerRadius = 14.0f;
        t.glow = true;
        return t;
    }

    float smoothstep (float x)
    {
        x = jlimit (0.0f, 1.0f, x);
        return x * x * (3.0f - 2.0f * x);
    }

    float decayBuildUp (float size) { return 0.012f + 0.05f * size; }
    float highDecay (float rt, float damping) { return jmax (0.08f, rt * (1.0f - 0.9f * std::pow (jlimit (0.0f, 1.0f, damping), 0.8f))); }
} // namespace

//==============================================================================
CaveStage::CaveStage (CrystalProcessor& p) : processor (p)
{
    setOpaque (true); // the backdrop behind the rounded corners is baked into the background image
    setTooltip ("The crystals glow with the reverb tail and sparkles rise with Shimmer. "
                "Inside the cave mouth: the decay envelope. Click the cave to ping a crystal.");
}

void CaveStage::resized()
{
    buildScene();
    background = {};
    curveLayer = {};
}

void CaveStage::buildScene()
{
    crystals.clear();
    stars.clear();

    const float W = (float) getWidth(), H = (float) getHeight(), cx = W * 0.5f;
    Random r (1234);

    // ---- cave mouth: a faceted arch ----------------------------------------------------
    const Point<float> arch[] = { { -192.0f, H + 4.0f }, { -186.0f, 204.0f }, { -179.0f, 154.0f }, { -166.0f, 108.0f },
                                  { -146.0f, 72.0f },  { -118.0f, 45.0f },  { -84.0f, 27.0f },   { -42.0f, 16.0f },
                                  { 2.0f, 12.0f },     { 44.0f, 16.0f },    { 88.0f, 28.0f },    { 124.0f, 47.0f },
                                  { 152.0f, 75.0f },   { 170.0f, 112.0f },  { 182.0f, 156.0f },  { 188.0f, 206.0f },
                                  { 192.0f, H + 4.0f } };
    mouthPath.clear();
    for (size_t i = 0; i < std::size (arch); ++i)
    {
        const bool edge = i == 0 || i == std::size (arch) - 1;
        const Point<float> p { cx + arch[i].x + (edge ? 0.0f : r.nextFloat() * 5.0f - 2.5f),
                               arch[i].y + (edge ? 0.0f : r.nextFloat() * 4.0f - 2.0f) };
        if (i == 0)
            mouthPath.startNewSubPath (p);
        else
            mouthPath.lineTo (p);
    }
    mouthPath.closeSubPath();

    mouthLight = { cx, H - 4.0f };
    plotArea = { cx - 138.0f, 84.0f, 276.0f, H - 84.0f - 42.0f };

    for (int i = 0; i < 90 && stars.size() < 46; ++i)
    {
        const Point<float> p { cx - 180.0f + r.nextFloat() * 360.0f, 14.0f + r.nextFloat() * (H - 110.0f) };
        if (mouthPath.contains (p))
            stars.push_back (p);
    }

    // ---- crystals ------------------------------------------------------------------------
    auto add = [&] (float x, float y, float h, float w, float degrees, float hue)
    {
        cave::Crystal c;
        c.base = { x, y };
        c.height = h;
        c.width = w;
        c.angle = degreesToRadians (degrees);
        c.hue = hue;
        c.tipSkew = (r.nextFloat() - 0.5f) * 0.3f;
        crystals.push_back (c);
    };

    // stalactites hanging from the ceiling: irregular clusters, kept short around the mouth top
    for (float x = 6.0f; x < W;)
    {
        const float d = std::abs (x - cx);
        if (d < 64.0f)
        {
            x += 24.0f;
            continue;
        }
        const bool big = d > 230.0f && r.nextFloat() < 0.35f;
        const float h = d < 210.0f ? 8.0f + r.nextFloat() * 13.0f
                                   : (big ? 44.0f + r.nextFloat() * 30.0f : 12.0f + r.nextFloat() * 26.0f);
        const float deg = 180.0f + (r.nextFloat() - 0.5f) * 22.0f;
        add (x, -4.0f, h, 6.0f + h * 0.22f, deg, r.nextFloat());
        if (r.nextFloat() < 0.55f)
        {
            const float h2 = h * (0.35f + r.nextFloat() * 0.3f);
            add (x + (r.nextBool() ? 1.0f : -1.0f) * (5.0f + r.nextFloat() * 5.0f), -4.0f, h2, 5.0f + h2 * 0.22f,
                 deg + (r.nextFloat() - 0.5f) * 30.0f, r.nextFloat());
        }
        x += 18.0f + r.nextFloat() * 44.0f;
    }

    // side walls, pointing into the cave
    add (-6.0f, 78.0f, 44.0f, 15.0f, 102.0f, 0.75f);
    add (-6.0f, 118.0f, 62.0f, 19.0f, 80.0f, 0.2f);
    add (-6.0f, 162.0f, 38.0f, 13.0f, 116.0f, 0.9f);
    add (W + 6.0f, 72.0f, 50.0f, 16.0f, -98.0f, 0.3f);
    add (W + 6.0f, 124.0f, 36.0f, 13.0f, -78.0f, 0.85f);
    add (W + 6.0f, 158.0f, 58.0f, 18.0f, -112.0f, 0.1f);

    // floor clusters
    struct Spec { float x, h, w, deg, hue; };
    const Spec cluster[] = { { 92.0f, 178.0f, 40.0f, -8.0f, 0.05f },  { 52.0f, 122.0f, 30.0f, -27.0f, 0.45f },
                             { 140.0f, 134.0f, 31.0f, 15.0f, 0.75f }, { 16.0f, 98.0f, 26.0f, -44.0f, 0.85f },
                             { 180.0f, 88.0f, 24.0f, 31.0f, 0.15f },  { 212.0f, 54.0f, 17.0f, 47.0f, 0.55f },
                             { 116.0f, 70.0f, 20.0f, 4.0f, 1.0f },    { 70.0f, 60.0f, 18.0f, -13.0f, 0.2f },
                             { 160.0f, 46.0f, 15.0f, -8.0f, 0.35f },  { 240.0f, 32.0f, 12.0f, 62.0f, 0.9f },
                             { 36.0f, 44.0f, 14.0f, -60.0f, 0.6f },   { 196.0f, 28.0f, 11.0f, 12.0f, 0.3f } };

    const size_t floorStart = crystals.size();
    for (const auto& s : cluster)
        add (s.x, H + 10.0f, s.h, s.w, s.deg, s.hue);
    for (const auto& s : cluster)
        add (W - s.x + (r.nextFloat() - 0.5f) * 10.0f, H + 10.0f, s.h * (0.88f + r.nextFloat() * 0.18f), s.w,
             -s.deg + (r.nextFloat() - 0.5f) * 8.0f, 1.0f - s.hue);

    std::stable_sort (crystals.begin() + (long) floorStart, crystals.end(),
                      [] (const cave::Crystal& a, const cave::Crystal& b) { return a.height > b.height; });
}

CaveStage::CurveKey CaveStage::currentKey() const
{
    CurveKey k;
    k.size = processor.apvts.getRawParameterValue ("size")->load();
    k.decay = processor.apvts.getRawParameterValue ("decay")->load();
    k.damping = processor.apvts.getRawParameterValue ("damping")->load();
    k.predelay = processor.apvts.getRawParameterValue ("predelay")->load();
    k.shimmer = processor.apvts.getRawParameterValue ("shimmer")->load();
    k.interval = (int) processor.apvts.getRawParameterValue ("interval")->load();
    k.freeze = processor.apvts.getRawParameterValue ("freeze")->load() > 0.5f;
    return k;
}

float CaveStage::timeToX (float seconds) const
{
    return plotArea.getX() + plotArea.getWidth() * std::sqrt (jlimit (0.0f, 1.0f, seconds / curveMaxSeconds));
}

float CaveStage::dbToY (float db) const
{
    return plotArea.getY() + plotArea.getHeight() * jlimit (0.0f, 1.0f, -db / 60.0f);
}

float CaveStage::envelopeDb (float t, float rt, float buildUp) const
{
    if (t <= 0.0f)
        return -60.0f;
    if (t < buildUp)
        return -60.0f + 60.0f * std::sqrt (t / buildUp);
    return jmax (-60.0f, -60.0f * (t - buildUp) / jmax (0.05f, rt));
}

//==============================================================================
void CaveStage::rebuildImages (float scale)
{
    imageScale = scale;
    const auto b = getLocalBounds().toFloat();
    const float W = b.getWidth(), H = b.getHeight();
    const int pw = jmax (1, roundToInt (W * scale)), ph = jmax (1, roundToInt (H * scale));

    Path clip;
    clip.addRoundedRectangle (b, stageCorner);

    // ---- background: low-poly rock, the sky through the cave mouth, dim crystals -------------
    background = Image (Image::ARGB, pw, ph, true);
    {
        Graphics g (background);
        g.addTransform (AffineTransform::scale (scale));

        // the editor backdrop (same gradient as CrystalEditor::rebuildBackdrop) shows in the rounded corners
        const auto origin = getPosition().toFloat();
        g.setGradientFill (ColourGradient (C::night, -origin.x, -origin.y, C::nightAlt, (float) baseWidth * 0.4f - origin.x,
                                           (float) baseHeight - origin.y, false));
        g.fillRect (b);
        g.reduceClipRegion (clip);

        g.setGradientFill (ColourGradient (Colour (0xff0b1536), 0.0f, 0.0f, Colour (0xff120d2d), 0.0f, H, false));
        g.fillRect (b);

        Random r (99);
        const float cell = 46.0f;
        const int cols = (int) std::ceil (W / cell) + 1, rows = (int) std::ceil (H / cell) + 1;
        std::vector<Point<float>> pts ((size_t) ((cols + 1) * (rows + 1)));
        auto at = [&] (int i, int j) -> Point<float>& { return pts[(size_t) (j * (cols + 1) + i)]; };
        for (int j = 0; j <= rows; ++j)
            for (int i = 0; i <= cols; ++i)
                at (i, j) = { (float) i * cell + (r.nextFloat() - 0.5f) * cell * 0.7f,
                              (float) j * cell + (r.nextFloat() - 0.5f) * cell * 0.7f };

        const Colour rockDark (0xff060b20), rockLit (0xff24316c), rockViolet (0xff2a1f5c);
        for (int j = 0; j < rows; ++j)
            for (int i = 0; i < cols; ++i)
                for (int half = 0; half < 2; ++half)
                {
                    const auto a = at (i, j), b2 = at (i + 1, j), c = at (i, j + 1), d = at (i + 1, j + 1);
                    Path tri;
                    if (half == 0)
                        tri.addTriangle (a, b2, c);
                    else
                        tri.addTriangle (b2, d, c);
                    const auto centre = tri.getBounds().getCentre();
                    const float lit = jlimit (0.0f, 1.0f, 1.0f - centre.getDistanceFrom (mouthLight) / 520.0f);
                    Colour col = rockDark.interpolatedWith (rockLit, lit * 0.55f + r.nextFloat() * 0.22f);
                    col = col.interpolatedWith (rockViolet, r.nextFloat() * 0.35f);
                    g.setColour (col);
                    g.fillPath (tri);
                    g.setColour (C::ice.withAlpha (0.025f + 0.03f * lit));
                    g.strokePath (tri, PathStrokeType (0.6f));
                }

        // the sky beyond the mouth
        {
            Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (mouthPath);
            const auto mb = mouthPath.getBounds();
            ColourGradient sky (Colour (0xff101c50), 0.0f, mb.getY(), Colour (0xff2b2775), 0.0f, mb.getBottom(), false);
            sky.addColour (0.7, Colour (0xff1f2363));
            g.setGradientFill (sky);
            g.fillRect (mb);

            g.setGradientFill (ColourGradient (C::ice.withAlpha (0.28f), mouthLight, C::ice.withAlpha (0.0f),
                                               mouthLight.translated (0.0f, -230.0f), true));
            g.fillRect (mb);

            for (size_t i = 0; i < stars.size(); ++i)
            {
                const float s = 0.7f + r.nextFloat() * 1.3f;
                g.setColour (C::frost.withAlpha (0.25f + r.nextFloat() * 0.55f));
                g.fillEllipse (Rectangle<float> (s, s).withCentre (stars[i]));
                if (i % 9 == 0)
                    cave::drawGlint (g, stars[i], 3.5f, C::ice, 0.0f);
            }

            // distant ice peaks
            Path peaks;
            const float baseY = H + 2.0f, cx = W * 0.5f;
            const float xs[] = { -200.0f, -160.0f, -128.0f, -92.0f, -60.0f, -22.0f, 14.0f, 50.0f, 84.0f, 118.0f, 150.0f, 200.0f };
            const float ys[] = { 40.0f, 62.0f, 46.0f, 74.0f, 52.0f, 66.0f, 44.0f, 70.0f, 50.0f, 68.0f, 48.0f, 38.0f };
            peaks.startNewSubPath (cx - 200.0f, baseY);
            for (size_t i = 0; i < std::size (xs); ++i)
                peaks.lineTo (cx + xs[i], baseY - ys[i] * 0.55f);
            peaks.lineTo (cx + 200.0f, baseY);
            peaks.closeSubPath();
            g.setGradientFill (ColourGradient (Colour (0xff3a4a9a).withAlpha (0.55f), 0.0f, baseY - 44.0f,
                                               Colour (0xff1a2058).withAlpha (0.7f), 0.0f, baseY, false));
            g.fillPath (peaks);
            g.setColour (C::ice.withAlpha (0.18f));
            g.strokePath (peaks, PathStrokeType (0.8f));
        }

        // rim of the mouth
        g.setColour (Colour (0xff050918).withAlpha (0.55f));
        g.strokePath (mouthPath, PathStrokeType (7.0f, PathStrokeType::mitered));
        g.setColour (C::ice.withAlpha (0.3f));
        g.strokePath (mouthPath, PathStrokeType (1.2f, PathStrokeType::mitered));

        for (const auto& c : crystals)
            cave::drawCrystal (g, c, 0.6f, 0.0f);
        for (const auto& c : crystals) // the resting glow, so the glow layer can be skipped in silence
            cave::drawCrystal (g, c, 1.18f, 1.0f, 0.1f);

        // vignette
        g.setGradientFill (ColourGradient (Colours::transparentBlack, W * 0.5f, H * 0.62f,
                                           Colours::black.withAlpha (0.45f), 0.0f, 0.0f, true));
        g.fillRect (b);
    }

    // ---- breathing light (ice and frost versions, crossfaded) and particle sprites ------------------
    lightRect = Rectangle<float> (mouthLight.x - 260.0f, mouthLight.y - 262.0f, 520.0f, 266.0f).getIntersection (b);
    auto makeLight = [&] (Colour colour)
    {
        Image img (Image::ARGB, jmax (1, roundToInt (lightRect.getWidth() * scale)), jmax (1, roundToInt (lightRect.getHeight() * scale)), true);
        Graphics g (img);
        g.addTransform (AffineTransform::translation (-lightRect.getX(), -lightRect.getY()).scaled (scale));
        ColourGradient grad (colour, mouthLight, colour.withAlpha (0.0f), mouthLight.translated (0.0f, -250.0f), true);
        grad.addColour (0.35, colour.withAlpha (0.45f));
        g.setGradientFill (grad);
        g.fillRect (lightRect);
        return img;
    };
    lightIce = makeLight (C::ice);
    lightFrost = makeLight (C::frost);

    for (size_t i = 0; i < glintSprites.size(); ++i)
    {
        const Colour tint = i == 6 ? C::frost : C::ice.interpolatedWith (C::violet, (float) i / 5.0f).interpolatedWith (C::frost, 0.35f);
        const int px = 48;
        Image img (Image::ARGB, px, px, true);
        Graphics g (img);
        const float h = (float) px * 0.5f;
        const Point<float> c (h, h);
        g.setGradientFill (ColourGradient (tint.withAlpha (0.45f), c, tint.withAlpha (0.0f), c.translated (h, 0.0f), true));
        g.fillEllipse (0.0f, 0.0f, (float) px, (float) px);

        const float len = h / 1.1f, w = len * 0.16f;
        Path star;
        star.startNewSubPath (0.0f, -len);
        star.lineTo (w, -w);
        star.lineTo (len, 0.0f);
        star.lineTo (w, w);
        star.lineTo (0.0f, len);
        star.lineTo (-w, w);
        star.lineTo (-len, 0.0f);
        star.lineTo (-w, -w);
        star.closeSubPath();
        g.setColour (tint.interpolatedWith (Colours::white, 0.6f));
        g.fillPath (star, AffineTransform::translation (h, h));
        glintSprites[i] = img;
    }

    // only the parts of the glow layer that actually contain crystals get blended every frame
    {
        const int side = 300, wInt = getWidth(), hInt = getHeight();
        glowRegions[0] = { 0, 0, side, hInt };
        glowRegions[1] = { wInt - side, 0, side, hInt };
        glowRegions[2] = { side, 0, wInt - 2 * side, 80 };
    }

    // ---- glow layer: the same crystals, lit up -------------------------------------------------
    glowLayer = Image (Image::ARGB, pw, ph, true);
    {
        Graphics g (glowLayer);
        g.addTransform (AffineTransform::scale (scale));
        g.reduceClipRegion (clip);
        for (const auto& c : crystals)
            cave::drawCrystal (g, c, 1.18f, 1.0f);
    }

    // ---- frost: icy edges, ferns and snowflakes ------------------------------------------------
    frostLayer = Image (Image::ARGB, pw, ph, true);
    {
        Graphics g (frostLayer);
        g.addTransform (AffineTransform::scale (scale));
        g.reduceClipRegion (clip);

        g.setColour (C::ice.withAlpha (0.07f));
        g.fillRect (b);

        const float edge = 70.0f;
        const Colour f = C::frost.withAlpha (0.32f), z = C::frost.withAlpha (0.0f);
        g.setGradientFill (ColourGradient (f, 0.0f, 0.0f, z, 0.0f, edge, false));
        g.fillRect (b.withHeight (edge));
        g.setGradientFill (ColourGradient (f, 0.0f, H, z, 0.0f, H - edge, false));
        g.fillRect (b.withTop (H - edge));
        g.setGradientFill (ColourGradient (f, 0.0f, 0.0f, z, edge * 1.4f, 0.0f, false));
        g.fillRect (b.withWidth (edge * 1.4f));
        g.setGradientFill (ColourGradient (f, W, 0.0f, z, W - edge * 1.4f, 0.0f, false));
        g.fillRect (b.withLeft (W - edge * 1.4f));

        Random r (2024);
        std::function<void (Point<float>, float, float, int)> fern = [&] (Point<float> p, float angle, float length, int depth)
        {
            const Point<float> dir (std::cos (angle), std::sin (angle));
            const auto end = p + dir * length;
            g.setColour (C::frost.withAlpha (0.18f + 0.1f * (float) depth));
            g.drawLine ({ p, end }, 0.5f + 0.25f * (float) depth);
            if (depth <= 0)
                return;
            const int branches = 3;
            for (int k = 1; k <= branches; ++k)
            {
                const auto root = p + dir * (length * (float) k / (float) (branches + 1));
                const float spread = 0.9f + r.nextFloat() * 0.3f;
                fern (root, angle + spread, length * 0.42f, depth - 1);
                fern (root, angle - spread, length * 0.42f, depth - 1);
            }
        };

        for (int i = 0; i < 26; ++i)
        {
            Point<float> start;
            float angle;
            const int side = i % 4;
            const float u = r.nextFloat();
            switch (side)
            {
                case 0:  start = { u * W, 0.0f }; angle = MathConstants<float>::halfPi; break;
                case 1:  start = { u * W, H }; angle = -MathConstants<float>::halfPi; break;
                case 2:  start = { 0.0f, u * H }; angle = 0.0f; break;
                default: start = { W, u * H }; angle = MathConstants<float>::pi; break;
            }
            angle += (r.nextFloat() - 0.5f) * 0.9f;
            fern (start, angle, 26.0f + r.nextFloat() * 34.0f, 2);
        }

        // the crystals themselves frost over
        for (const auto& c : crystals)
        {
            const auto tip = c.tip();
            const Point<float> across (std::cos (c.angle) * c.width * 0.5f, std::sin (c.angle) * c.width * 0.5f);
            const auto shoulder = c.base + (tip - c.base) * 0.72f;
            Path silhouette;
            silhouette.startNewSubPath (c.base - across);
            silhouette.lineTo (shoulder - across);
            silhouette.lineTo (tip);
            silhouette.lineTo (shoulder + across);
            silhouette.lineTo (c.base + across);
            silhouette.closeSubPath();
            g.setGradientFill (ColourGradient (C::frost.withAlpha (0.05f), c.base, C::frost.withAlpha (0.42f), tip, false));
            g.fillPath (silhouette);
            g.setColour (Colours::white.withAlpha (0.45f));
            g.strokePath (silhouette, PathStrokeType (0.8f));
        }

        for (int i = 0; i < 16; ++i)
        {
            const bool horizontal = r.nextBool();
            const Point<float> p = horizontal ? Point<float> (r.nextFloat() * W, r.nextBool() ? 10.0f + r.nextFloat() * 30.0f : H - 10.0f - r.nextFloat() * 30.0f)
                                              : Point<float> (r.nextBool() ? 10.0f + r.nextFloat() * 40.0f : W - 10.0f - r.nextFloat() * 40.0f, r.nextFloat() * H);
            g.setColour (C::frost.withAlpha (0.35f + r.nextFloat() * 0.3f));
            g.strokePath (cave::snowflakePath (p, 3.0f + r.nextFloat() * 6.0f),
                          PathStrokeType (0.8f, PathStrokeType::curved, PathStrokeType::rounded));
        }
    }
}

void CaveStage::drawSpriteGlint (Graphics& g, Point<float> centre, float size, float hue, float alpha, float rotation) const
{
    if (size < 0.3f || alpha < 0.01f)
        return;
    const auto& sprite = glintSprites[hue < 0.0f ? 6 : (size_t) jlimit (0, 5, roundToInt (hue * 5.0f))];
    if (sprite.isNull())
        return;
    const float sw = (float) sprite.getWidth();
    g.setOpacity (jmin (1.0f, alpha));
    g.drawImageTransformed (sprite, AffineTransform::translation (-sw * 0.5f, -sw * 0.5f).scaled (size * 2.2f / sw)
                                        .rotated (rotation).translated (centre));
    g.setOpacity (1.0f);
}

void CaveStage::rebuildCurve (float scale)
{
    curveScale = scale;
    const auto mb = mouthPath.getBounds().getIntersection (getLocalBounds().toFloat()).getSmallestIntegerContainer().toFloat();
    curveLayer = Image (Image::ARGB, jmax (1, roundToInt (mb.getWidth() * scale)), jmax (1, roundToInt (mb.getHeight() * scale)), true);

    Graphics g (curveLayer);
    g.addTransform (AffineTransform::translation (-mb.getX(), -mb.getY()).scaled (scale));
    g.reduceClipRegion (mouthPath);

    const float rt = curveKey.decay, size = curveKey.size / 100.0f, damping = curveKey.damping / 100.0f;
    const float pre = curveKey.predelay * 0.001f, shimmer = curveKey.shimmer / 100.0f;
    const bool frozen = curveKey.freeze;
    const float build = decayBuildUp (size);
    const float rtHf = highDecay (rt, damping);

    // ---- grid -------------------------------------------------------------------
    g.setColour (C::ice.withAlpha (0.07f));
    for (float db : { -20.0f, -40.0f })
        g.fillRect (Rectangle<float> (plotArea.getX(), dbToY (db), plotArea.getWidth(), 1.0f));
    for (float t : { 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 20.0f })
        g.fillRect (Rectangle<float> (timeToX (t), plotArea.getY(), 1.0f, plotArea.getHeight()));
    g.setColour (C::ice.withAlpha (0.16f));
    g.fillRect (Rectangle<float> (plotArea.getX(), plotArea.getBottom(), plotArea.getWidth(), 1.0f));

    g.setFont (aa::Fonts::ui (9.0f));
    g.setColour (C::text.withAlpha (0.42f));
    const std::pair<float, const char*> labels[] = { { 0.0f, "0" }, { 1.0f, "1s" }, { 5.0f, "5s" }, { 10.0f, "10s" }, { 30.0f, "30s" } };
    for (const auto& [t, text] : labels)
        g.drawText (text, Rectangle<float> (timeToX (t) - 15.0f, plotArea.getBottom() + 6.0f, 30.0f, 12.0f), Justification::centred);

    // ---- curves -------------------------------------------------------------------
    auto makeCurve = [&] (auto&& dbAt)
    {
        Path p;
        p.startNewSubPath (timeToX (pre), dbToY (-60.0f));
        constexpr int n = 200;
        for (int k = 1; k <= n; ++k)
        {
            const float u = (float) k / (float) n;
            const float t = pre + u * u * (curveMaxSeconds - pre);
            const float db = dbAt (t);
            p.lineTo (timeToX (t), dbToY (db));
            if (db <= -60.0f && t > pre + 0.1f)
                break;
        }
        return p;
    };

    auto mainDb = [&] (float t)
    {
        const float local = t - pre;
        if (frozen)
            return local <= 0.0f ? -60.0f : (local < build ? envelopeDb (local, rt, build) : -5.0f);
        return envelopeDb (local, rt, build);
    };
    auto highDb = [&] (float t)
    {
        const float local = t - pre;
        if (frozen)
            return local <= 0.0f ? -60.0f : (local < build ? envelopeDb (local, rt, build) - 3.0f : -8.0f);
        return envelopeDb (local, rtHf, build) - 2.0f;
    };

    const Path main = makeCurve (mainDb);
    const Path high = makeCurve (highDb);

    // fill under the main curve
    {
        Path fill (main);
        fill.lineTo (main.getCurrentPosition().x, plotArea.getBottom());
        fill.closeSubPath();
        g.setGradientFill (ColourGradient (C::ice.withAlpha (0.32f), 0.0f, plotArea.getY(),
                                           C::violet.withAlpha (0.02f), 0.0f, plotArea.getBottom(), false));
        g.fillPath (fill);
    }

    // shimmer bloom: a dashed sparkle line that swells after the hit
    if (shimmer > 0.01f && ! frozen)
    {
        const float loop = 0.95f * std::pow (shimmer, 0.8f);
        const float bloom = 0.15f + 0.25f * std::sqrt (rt);
        const float rtS = rt * (1.0f + 1.4f * loop);
        auto shimmerDb = [&] (float t)
        {
            const float local = t - pre;
            if (local <= 0.0f)
                return -60.0f;
            const float a = loop * (1.0f - std::exp (-local / bloom)) * std::pow (10.0f, -3.0f * local / rtS);
            return jmax (-60.0f, 20.0f * std::log10 (jmax (1.0e-4f, a)));
        };
        Path dashed;
        const float dashes[] = { 2.0f, 4.0f };
        PathStrokeType (1.4f).createDashedStroke (dashed, makeCurve (shimmerDb), dashes, 2);
        g.setColour (C::lilac.withAlpha (0.85f));
        g.fillPath (dashed);
    }

    g.setColour (C::violet.withAlpha (0.18f));
    g.strokePath (high, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (C::violet.withAlpha (0.85f));
    g.strokePath (high, PathStrokeType (1.3f, PathStrokeType::curved, PathStrokeType::rounded));

    const Colour mainColour = frozen ? C::frost : C::ice;
    g.setColour (mainColour.withAlpha (0.1f));
    g.strokePath (main, PathStrokeType (8.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (mainColour.withAlpha (0.22f));
    g.strokePath (main, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (mainColour.interpolatedWith (Colours::white, 0.4f));
    g.strokePath (main, PathStrokeType (1.7f, PathStrokeType::curved, PathStrokeType::rounded));

    // ---- legend (bottom-right of the plot) ------------------------------------------------
    {
        const bool showShimmer = shimmer > 0.01f && ! frozen;
        struct Row { Colour colour; bool dashed; const char* text; };
        const Row rows[] = { { C::ice.interpolatedWith (Colours::white, 0.4f), false, "TAIL" },
                             { C::violet, false, "HIGHS" },
                             { C::lilac, true, "SHIMMER" } };
        const int numRows = showShimmer ? 3 : 2;
        float y = plotArea.getBottom() - 10.0f - 11.0f * (float) (numRows - 1);
        const float x = plotArea.getRight() - 58.0f;
        g.setFont (aa::Fonts::ui (8.0f).withExtraKerningFactor (0.12f));
        for (int i = 0; i < numRows; ++i, y += 11.0f)
        {
            g.setColour (rows[i].colour);
            if (rows[i].dashed)
                for (float dx = 0.0f; dx < 12.0f; dx += 4.0f)
                    g.fillRect (Rectangle<float> (x + dx, y - 0.6f, 2.0f, 1.3f));
            else
                g.fillRect (Rectangle<float> (x, y - 0.7f, 12.0f, 1.5f));
            g.setColour (C::text.withAlpha (0.5f));
            g.drawText (rows[i].text, Rectangle<float> (x + 17.0f, y - 6.0f, 50.0f, 12.0f), Justification::centredLeft);
        }
    }

    // ---- readout ---------------------------------------------------------------------
    const float cx = (float) getWidth() * 0.5f;
    g.setFont (aa::Fonts::uiBold (8.5f).withExtraKerningFactor (0.3f));
    g.setColour (C::text.withAlpha (0.5f));
    g.drawText (frozen ? "FROZEN" : "DECAY", Rectangle<float> (cx - 60.0f, 26.0f, 120.0f, 12.0f), Justification::centred);

    if (frozen)
    {
        // a hand-drawn infinity sign (lemniscate), so we don't depend on the font having the glyph
        Path inf;
        const Point<float> c (cx, 52.0f);
        for (int k = 0; k <= 64; ++k)
        {
            const float a = MathConstants<float>::twoPi * (float) k / 64.0f;
            const float d = 1.0f + std::sin (a) * std::sin (a);
            const Point<float> p (c.x + 17.0f * std::cos (a) / d, c.y + 17.0f * std::sin (a) * std::cos (a) / d);
            if (k == 0)
                inf.startNewSubPath (p);
            else
                inf.lineTo (p);
        }
        inf.closeSubPath();
        g.setColour (C::ice.withAlpha (0.3f));
        g.strokePath (inf, PathStrokeType (4.5f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (C::frost);
        g.strokePath (inf, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    else
    {
        const String value = aa::params::formatValue (rt, aa::params::Unit::seconds);
        g.setFont (aa::Fonts::display (21.0f).withExtraKerningFactor (0.06f));
        g.setColour (C::ice.withAlpha (0.25f));
        g.drawText (value, Rectangle<float> (cx - 80.0f, 39.0f, 160.0f, 28.0f).translated (0.0f, 1.0f), Justification::centred);
        g.setColour (C::frost);
        g.drawText (value, Rectangle<float> (cx - 80.0f, 39.0f, 160.0f, 28.0f), Justification::centred);
    }
}

//==============================================================================
void CaveStage::spawnSparkle (bool fromMouth)
{
    if (sparkles.size() >= 260)
        return;

    Sparkle s;
    if (crystals.empty())
        fromMouth = true;
    if (! fromMouth)
    {
        // pick a crystal whose tip points upward-ish (floor and wall crystals)
        for (int tries = 0; tries < 6; ++tries)
        {
            const auto idx = (size_t) random.nextInt ((int) crystals.size());
            if (std::cos (crystals[idx].angle) > 0.0f)
            {
                s.pos = crystals[idx].tip() + Point<float> ((random.nextFloat() - 0.5f) * 8.0f, random.nextFloat() * 10.0f);
                s.hue = crystals[idx].hue;
                break;
            }
            fromMouth = true;
        }
    }
    if (fromMouth)
    {
        s.pos = mouthLight + Point<float> ((random.nextFloat() - 0.5f) * 260.0f, -random.nextFloat() * 40.0f);
        s.hue = random.nextFloat();
    }

    s.vel = { (random.nextFloat() - 0.5f) * 10.0f, -(14.0f + 75.0f * shimmerAmt) * (0.6f + 0.8f * random.nextFloat()) };
    s.life = 1.8f + random.nextFloat() * 2.6f;
    s.size = 0.9f + random.nextFloat() * 2.3f * (0.6f + 0.6f * shimmerAmt);
    s.twinkle = 2.0f + random.nextFloat() * 6.0f;
    s.swayPhase = random.nextFloat() * MathConstants<float>::twoPi;
    s.spin = (random.nextFloat() - 0.5f) * 1.6f;
    sparkles.push_back (s);
}

void CaveStage::spawnGlint (float strength)
{
    if (glints.size() >= 24 || crystals.empty())
        return;
    const auto& c = crystals[(size_t) random.nextInt ((int) crystals.size())];
    Glint gl;
    // glints sit on the upper part of a crystal's edge
    gl.pos = c.base + (c.tip() - c.base) * (0.72f + 0.28f * random.nextFloat());
    gl.size = (4.0f + 7.0f * random.nextFloat()) * (0.6f + 0.6f * strength);
    gl.life = 0.45f + 0.5f * random.nextFloat();
    gl.rot = random.nextFloat() * 0.6f;
    gl.hue = c.hue;
    glints.push_back (gl);
}

void CaveStage::mouseDown (const MouseEvent& e)
{
    processor.pingPitch.store (jlimit (0.0f, 1.0f, e.position.x / (float) jmax (1, getWidth())));
    processor.pingRequested.store (true);

    for (int i = 0; i < 3; ++i)
        spawnGlint (1.0f);
    flash = jmax (flash, 0.6f);
}

void CaveStage::tick (double dtSeconds)
{
    const float dt = (float) dtSeconds;
    clock += dtSeconds;

    const float tailDb = aa::dsp::gainToDb (processor.tailLevel.load());
    const float glowTarget = jlimit (0.0f, 1.0f, (tailDb + 50.0f) / 38.0f);
    glow += (1.0f - std::exp (-dt / (glowTarget > glow ? 0.06f : 0.5f))) * (glowTarget - glow);
    flash = jmax (0.0f, flash - dt * 2.2f);
    freezeVis += (1.0f - std::exp (-dt / 0.2f)) * (processor.freezeLevel.load() - freezeVis);
    shimmerAmt = processor.apvts.getRawParameterValue ("shimmer")->load() / 100.0f;

    const float decay = processor.apvts.getRawParameterValue ("decay")->load();
    breathPhase += dt / jlimit (2.5f, 9.0f, 1.5f + decay * 0.5f);
    breathPhase -= std::floor (breathPhase);

    const float motion = 1.0f - freezeVis;

    CrystalProcessor::Hit hit;
    while (processor.hits.pop (hit))
    {
        flash = jmax (flash, hit.amplitude);
        if (freezeVis < 0.5f)
        {
            dots.push_back ({ 0.0f, hit.amplitude });
            if (dots.size() > 5)
                dots.erase (dots.begin());
            const int burst = 1 + (int) (hit.amplitude * (2.0f + 8.0f * shimmerAmt));
            for (int i = 0; i < burst; ++i)
                spawnSparkle (false);
            spawnGlint (hit.amplitude);
        }
    }

    const float rate = (2.0f + 55.0f * std::pow (shimmerAmt, 1.1f)) * (0.35f + 1.4f * glow) * motion;
    spawnAccumulator += rate * dt;
    while (spawnAccumulator >= 1.0f)
    {
        spawnSparkle (random.nextFloat() < 0.3f);
        spawnAccumulator -= 1.0f;
    }

    glintAccumulator += (0.4f + 4.0f * glow + 3.0f * shimmerAmt * glow) * dt * (1.0f - 0.7f * freezeVis);
    while (glintAccumulator >= 1.0f)
    {
        spawnGlint (glow);
        glintAccumulator -= 1.0f;
    }

    for (auto& s : sparkles)
    {
        s.age += dt * (0.08f + 0.92f * motion);
        s.pos += s.vel * (dt * motion);
        s.pos.x += std::sin ((float) clock * 1.3f + s.swayPhase) * 9.0f * dt * motion;
    }
    sparkles.erase (std::remove_if (sparkles.begin(), sparkles.end(),
                                    [] (const Sparkle& s) { return s.age > s.life || s.pos.y < -12.0f; }),
                    sparkles.end());

    for (auto& gl : glints)
        gl.age += dt;
    glints.erase (std::remove_if (glints.begin(), glints.end(), [] (const Glint& gl) { return gl.age > gl.life; }),
                  glints.end());

    for (auto& d : dots)
        d.age += dt;
    dots.erase (std::remove_if (dots.begin(), dots.end(), [this, decay] (const TailDot& d)
                                {
                                    return d.age > curveMaxSeconds || freezeVis > 0.9f
                                           || envelopeDb (d.age, decay, 0.05f) < -59.0f;
                                }),
                dots.end());

    // The scene simulates every frame but repaints at ~30 fps: plenty for slow drifting light, half the CPU.
    repaintClock += dt;
    if (repaintClock >= 1.0f / 32.0f)
    {
        repaintClock = 0.0f;
        repaint();
    }
}

void CaveStage::paint (Graphics& g)
{
    const float scale = jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (background.isNull() || std::abs (scale - imageScale) > 0.01f)
        rebuildImages (scale);

    const auto key = currentKey();
    if (curveLayer.isNull() || ! (key == curveKey) || std::abs (scale - curveScale) > 0.01f)
    {
        curveKey = key;
        rebuildCurve (scale);
    }

    const auto b = getLocalBounds().toFloat();
    g.drawImage (background, b);

    // ---- breathing light at the cave mouth -------------------------------------------------
    const float breath = 0.5f + 0.5f * std::sin (MathConstants<float>::twoPi * breathPhase);
    const float lightA = jlimit (0.0f, 0.85f, 0.14f + 0.12f * breath * (1.0f - freezeVis) + 0.36f * glow + 0.2f * flash
                                                  + 0.2f * freezeVis);
    const float frostMix = freezeVis * 0.7f;
    g.setOpacity (lightA * (1.0f - frostMix));
    g.drawImage (lightIce, lightRect);
    if (frostMix > 0.01f)
    {
        g.setOpacity (lightA * frostMix);
        g.drawImage (lightFrost, lightRect);
    }
    g.setOpacity (1.0f);

    // ---- decay envelope -----------------------------------------------------------------
    const auto mb = mouthPath.getBounds().getIntersection (b).getSmallestIntegerContainer().toFloat();
    g.drawImage (curveLayer, mb);

    // live dots riding the envelope, one per hit
    {
        const float rt = curveKey.decay, pre = curveKey.predelay * 0.001f;
        const float build = decayBuildUp (curveKey.size / 100.0f);
        for (const auto& d : dots)
        {
            const float db = envelopeDb (d.age, rt, build);
            const float alpha = jlimit (0.0f, 1.0f, (db + 60.0f) / 30.0f) * (1.0f - freezeVis) * (0.5f + 0.5f * d.amp);
            if (alpha <= 0.01f)
                continue;
            for (int k = 4; k >= 1; --k)
            {
                const float tk = d.age - (float) k * 0.03f * (1.0f + d.age);
                if (tk <= 0.0f)
                    continue;
                const Point<float> p (timeToX (tk + pre), dbToY (envelopeDb (tk, rt, build)));
                const float s = 3.2f - 0.5f * (float) k;
                g.setColour (C::frost.withAlpha (alpha * (0.5f - 0.1f * (float) k)));
                g.fillEllipse (Rectangle<float> (s, s).withCentre (p));
            }
            drawSpriteGlint (g, { timeToX (d.age + pre), dbToY (db) }, 2.5f + 3.5f * alpha, -1.0f, alpha, (float) clock * 0.8f);
        }
    }

    // ---- crystals light up with the tail ------------------------------------------------------
    const float glowOpacity = jlimit (0.0f, 1.0f, 0.9f * jmax (glow, flash * 0.85f, freezeVis * 0.55f));
    if (glowOpacity > 0.01f)
    {
        g.setOpacity (glowOpacity);
        for (const auto& region : glowRegions)
        {
            const auto src = region.toFloat() * imageScale;
            g.drawImage (glowLayer, region.getX(), region.getY(), region.getWidth(), region.getHeight(),
                         roundToInt (src.getX()), roundToInt (src.getY()), roundToInt (src.getWidth()), roundToInt (src.getHeight()));
        }
        g.setOpacity (1.0f);
    }

    for (const auto& gl : glints)
    {
        const float t = gl.age / gl.life;
        const float env = std::sin (MathConstants<float>::pi * t);
        drawSpriteGlint (g, gl.pos, gl.size * env, gl.hue, env, gl.rot + t * 0.5f);
    }

    // ---- rising sparkles ------------------------------------------------------------------
    for (const auto& s : sparkles)
    {
        const float t = s.age / s.life;
        const float fade = smoothstep (t * 6.0f) * smoothstep ((1.0f - t) * 3.0f);
        const float tw = 0.55f + 0.45f * std::sin (s.age * s.twinkle * (0.2f + 0.8f * (1.0f - freezeVis)) + s.swayPhase);
        const float a = fade * tw;
        if (a < 0.02f)
            continue;
        Colour c = C::ice.interpolatedWith (C::violet, s.hue).interpolatedWith (C::frost, 0.35f + 0.4f * freezeVis);
        if (s.size > 2.2f)
            drawSpriteGlint (g, s.pos, s.size * 2.0f, freezeVis > 0.5f ? -1.0f : s.hue, a, (s.swayPhase - 3.14159f) * 0.12f + s.spin * s.age * 0.5f);
        else
        {
            g.setColour (c.withAlpha (a * 0.25f));
            g.fillEllipse (Rectangle<float> (s.size * 3.2f, s.size * 3.2f).withCentre (s.pos));
            g.setColour (c.withAlpha (a));
            g.fillEllipse (Rectangle<float> (s.size, s.size).withCentre (s.pos));
        }
    }

    // ---- frost -------------------------------------------------------------------------------
    if (freezeVis > 0.01f)
    {
        g.setOpacity (jlimit (0.0f, 1.0f, freezeVis));
        g.drawImage (frostLayer, b);
        g.setOpacity (1.0f);
    }

    if (hover)
    {
        const auto hintArea = Rectangle<float> (14.0f, 12.0f, 212.0f, 20.0f);
        g.setColour (C::night.withAlpha (0.7f));
        g.fillRoundedRectangle (hintArea, 10.0f);
        g.setColour (C::text.withAlpha (0.75f));
        g.setFont (aa::Fonts::uiBold (9.0f).withExtraKerningFactor (0.12f));
        g.drawText ("CLICK THE CAVE TO PING A CRYSTAL", hintArea, Justification::centred);
    }

    g.setGradientFill (ColourGradient (C::ice.withAlpha (0.5f), 0.0f, 0.0f, C::violet.withAlpha (0.22f), b.getRight(), b.getBottom(), false));
    g.drawRoundedRectangle (b.reduced (0.75f), stageCorner, 1.5f);
}

//==============================================================================
FreezeButton::FreezeButton (AudioProcessorValueTreeState& state)
{
    attachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (state, "freeze", *this);
    setClickingTogglesState (true);
    setTooltip ("Freeze: holds the current tail forever. New sound stops entering the cave while frozen");
}

FreezeButton::~FreezeButton() = default;

void FreezeButton::paintButton (Graphics& g, bool highlighted, bool down)
{
    const bool on = getToggleState();
    auto b = getLocalBounds().toFloat().reduced (2.0f);
    const float radius = b.getHeight() * 0.5f;

    if (on)
    {
        g.setColour (C::ice.withAlpha (0.25f));
        g.fillRoundedRectangle (b.expanded (2.0f), radius + 2.0f);
        g.setGradientFill (ColourGradient (C::frost, b.getX(), b.getY(), C::ice, b.getX(), b.getBottom(), false));
    }
    else
    {
        g.setGradientFill (ColourGradient (Colour (0xff111c45).withAlpha (0.92f), b.getX(), b.getY(),
                                           Colour (0xff0a1230).withAlpha (0.92f), b.getX(), b.getBottom(), false));
    }
    g.fillRoundedRectangle (b.reduced (down ? 1.0f : 0.0f), radius);

    g.setColour (on ? Colours::white.withAlpha (0.8f) : C::ice.withAlpha (highlighted ? 0.85f : 0.5f));
    g.drawRoundedRectangle (b.reduced (0.5f), radius, 1.0f);

    const Colour ink = on ? C::night : C::ice.interpolatedWith (Colours::white, highlighted ? 0.4f : 0.15f);
    const auto iconCentre = Point<float> (b.getX() + radius + 2.0f, b.getCentreY());
    g.setColour (ink);
    g.strokePath (cave::snowflakePath (iconCentre, radius * 0.62f),
                  PathStrokeType (1.3f, PathStrokeType::curved, PathStrokeType::rounded));

    g.setFont (aa::Fonts::display (14.0f).withExtraKerningFactor (0.22f));
    g.drawText ("FREEZE", b.withTrimmedLeft (radius * 2.0f + 2.0f).withTrimmedRight (radius * 0.6f), Justification::centred);
}

//==============================================================================
CrystalEditor::CrystalEditor (CrystalProcessor& p)
    : aa::EditorBase (p, baseWidth, baseHeight, makeTheme()),
      proc (p),
      size (p.apvts, "size", "Size"),
      decay (p.apvts, "decay", "Decay"),
      damping (p.apvts, "damping", "Damping"),
      predelay (p.apvts, "predelay", "Pre-Delay"),
      shimmer (p.apvts, "shimmer", "Shimmer"),
      sparkle (p.apvts, "sparkle", "Sparkle"),
      lowCut (p.apvts, "lowcut", "Low Cut"),
      highCut (p.apvts, "highcut", "High Cut"),
      modulation (p.apvts, "modulation", "Modulation"),
      width (p.apvts, "width", "Width"),
      mix (p.apvts, "mix", "Mix"),
      duck (p.apvts, "duck", "Ducking"),
      output (p.apvts, "output", "Output"),
      interval (p.apvts, "interval", "Interval", aa::ChoiceBox::Style::segmented),
      freeze (p.apvts),
      presets (p)
{
    useLookAndFeel (aa::makeLookAndFeel<CrystalLookAndFeel> (makeTheme()));
    aa::Fonts::setDisplayTypeface (PluginAssets::JuliusSansOneRegular_ttf, (size_t) PluginAssets::JuliusSansOneRegular_ttfSize);

    for (auto* k : { &size, &decay, &predelay, &damping })
        k->setAccent (C::ice);
    shimmer.setAccent (C::violet);
    sparkle.setAccent (C::lilac);
    for (auto* k : { &lowCut, &highCut, &modulation, &width })
        k->setAccent (Colour (0xff9fd2ff));
    for (auto* k : { &mix, &duck, &output })
        k->setAccent (Colour (0xffc9f1ff));
    output.setBipolar (false);
    interval.setAccent (C::violet);
    interval.setShortNames ({ "+12", "+7", "+19", "+24", "-12" });
    presets.setAccent (C::ice);

    size.setTooltip ("Size: from a tiny geode to a vast cavern");
    decay.setTooltip ("Decay: how long the tail takes to fade away (RT60)");
    predelay.setTooltip ("Pre-Delay: a gap before the cave answers - keeps the dry attack clear");
    damping.setTooltip ("Damping: high frequencies fade faster than lows, like soft frost on the walls");
    shimmer.setTooltip ("Shimmer: every echo climbs by the chosen interval, like light refracting through ice");
    sparkle.setTooltip ("Sparkle: brightens and scatters the shimmer into glittering dust");
    interval.setTooltip ("Interval of the shimmer: octave up, fifth, octave + fifth, two octaves, or octave down");
    lowCut.setTooltip ("Low Cut: removes rumble and mud from the reverb");
    highCut.setTooltip ("High Cut: rolls off the top of the reverb");
    modulation.setTooltip ("Modulation: slow movement inside the tail - lush and chorused, never metallic");
    width.setTooltip ("Width: stereo spread of the reverb");
    mix.setTooltip ("Mix: dry / wet balance");
    duck.setTooltip ("Ducking: pushes the reverb down while you play so it blooms in the gaps");
    output.setTooltip ("Output level");

    for (auto* c : std::initializer_list<Component*> { &stage, &size, &decay, &predelay, &damping, &shimmer, &sparkle,
                                                       &interval, &lowCut, &highCut, &modulation, &width, &mix, &duck,
                                                       &output, &freeze, &presets })
        content.addAndMakeVisible (c);

    finishSetup();
}

void CrystalEditor::layoutContent()
{
    stage.setBounds (12, 64, 856, 258);
    freeze.setBounds (868 - 132, 64 + 12, 120, 32);
    presets.setBounds (Rectangle<int> (250, 34).withCentre ({ 566, 33 }));

    const float panelY = 332.0f, panelH = 216.0f;
    const float widths[] = { 228.0f, 236.0f, 200.0f, 168.0f };
    float x = 12.0f;
    for (size_t i = 0; i < panels.size(); ++i)
    {
        panels[i] = { x, panelY, widths[i], panelH };
        x += widths[i] + 8.0f;
    }

    auto contentOf = [] (Rectangle<float> panel) { return panel.reduced (12.0f, 10.0f).withTrimmedTop (22.0f); };
    auto grid = [] (Rectangle<float> c, Component& a, Component& b, Component& d, Component& e)
    {
        const float cw = c.getWidth() * 0.5f, ch = c.getHeight() * 0.5f;
        a.setBounds (Rectangle<float> (c.getX(), c.getY(), cw, ch).reduced (3.0f, 1.0f).toNearestInt());
        b.setBounds (Rectangle<float> (c.getX() + cw, c.getY(), cw, ch).reduced (3.0f, 1.0f).toNearestInt());
        d.setBounds (Rectangle<float> (c.getX(), c.getY() + ch, cw, ch).reduced (3.0f, 1.0f).toNearestInt());
        e.setBounds (Rectangle<float> (c.getX() + cw, c.getY() + ch, cw, ch).reduced (3.0f, 1.0f).toNearestInt());
    };

    grid (contentOf (panels[0]), size, decay, predelay, damping);

    {
        auto c = contentOf (panels[1]);
        interval.setBounds (c.removeFromBottom (46.0f).toNearestInt());
        c.removeFromBottom (4.0f);
        auto hero = c.removeFromLeft (c.getWidth() * 0.56f);
        shimmer.setBounds (hero.toNearestInt());
        sparkle.setBounds (c.withSizeKeepingCentre (c.getWidth(), jmin (c.getHeight(), 100.0f)).toNearestInt());
    }

    grid (contentOf (panels[2]), lowCut, highCut, modulation, width);

    {
        auto c = contentOf (panels[3]);
        mix.setBounds (c.removeFromTop (c.getHeight() * 0.56f).toNearestInt());
        auto left = c.removeFromLeft (c.getWidth() * 0.5f);
        duck.setBounds (left.reduced (2.0f, 0.0f).toNearestInt());
        output.setBounds (c.reduced (2.0f, 0.0f).toNearestInt());
    }

    // title glyphs (for the glint sweep)
    const auto titleFont = aa::Fonts::display (30.0f).withExtraKerningFactor (0.14f);
    GlyphArrangement ga;
    ga.addLineOfText (titleFont, "CRYSTAL CAVE", 62.0f, 39.0f);
    titlePath.clear();
    ga.createPath (titlePath);
    titleArea = titlePath.getBounds().expanded (6.0f, 4.0f);

    backdrop = {};
}

void CrystalEditor::rebuildBackdrop (float scale)
{
    backdropScale = scale;
    const auto b = baseBounds().toFloat();
    backdrop = Image (Image::ARGB, jmax (1, roundToInt (b.getWidth() * scale)), jmax (1, roundToInt (b.getHeight() * scale)), true);
    Graphics g (backdrop);
    g.addTransform (AffineTransform::scale (scale));

    g.setGradientFill (ColourGradient (C::night, 0.0f, 0.0f, C::nightAlt, b.getRight() * 0.4f, b.getBottom(), false));
    g.fillRect (b);

    // faint shards of light behind everything
    Random r (5);
    for (int i = 0; i < 14; ++i)
    {
        const float x = r.nextFloat() * b.getWidth(), y = 330.0f + r.nextFloat() * 240.0f;
        const float s = 40.0f + r.nextFloat() * 120.0f;
        Path shard;
        shard.addTriangle (x, y - s, x + s * 0.35f, y + s * 0.2f, x - s * 0.3f, y + s * 0.25f);
        g.setColour ((i % 2 == 0 ? C::ice : C::violet).withAlpha (0.025f));
        g.fillPath (shard);
    }
    for (int i = 0; i < 70; ++i)
    {
        const float s = 0.6f + r.nextFloat() * 1.4f;
        g.setColour (C::frost.withAlpha (0.1f + r.nextFloat() * 0.25f));
        g.fillEllipse (r.nextFloat() * b.getWidth(), r.nextFloat() * b.getHeight(), s, s);
    }

    // ---- header -------------------------------------------------------------------------
    cave::drawGemIcon (g, { 38.0f, 31.0f }, 12.0f);

    {
        ColourGradient grad (C::ice, titleArea.getX(), 0.0f, C::violet, titleArea.getRight(), 0.0f, false);
        grad.addColour (0.45, C::frost);
        g.setColour (C::ice.withAlpha (0.18f));
        g.strokePath (titlePath, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setGradientFill (grad);
        g.fillPath (titlePath);
    }

    g.setColour (C::text.withAlpha (0.55f));
    g.setFont (aa::Fonts::uiBold (9.0f).withExtraKerningFactor (0.42f));
    g.drawText ("SHIMMER REVERB", Rectangle<float> (63.0f, 43.0f, 220.0f, 14.0f), Justification::centredLeft);

    g.setColour (C::text.withAlpha (0.45f));
    g.setFont (aa::Fonts::uiBold (10.0f).withExtraKerningFactor (0.25f));
    g.drawText ("ADVENTURE AUDIO", Rectangle<float> (b.getRight() - 230.0f, 18.0f, 212.0f, 30.0f), Justification::centredRight);

    // ---- panels ------------------------------------------------------------------------------
    const char* titles[] = { "Space", "Shimmer", "Colour", "Output" };
    for (size_t i = 0; i < panels.size(); ++i)
        lnf().drawPanel (g, panels[i], titles[i]);
}

void CrystalEditor::paintContent (Graphics& g)
{
    const float scale = jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (backdrop.isNull() || std::abs (scale - backdropScale) > 0.01f)
        rebuildBackdrop (scale);

    g.drawImage (backdrop, baseBounds().toFloat());

    if (glintActive)
    {
        const float t = (float) (glintClock / 1.4);
        const float x = titleArea.getX() - 40.0f + (titleArea.getWidth() + 80.0f) * t;
        Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (titlePath);
        ColourGradient band (Colours::white.withAlpha (0.0f), x - 36.0f, 0.0f, Colours::white.withAlpha (0.0f), x + 36.0f, 0.0f, false);
        band.addColour (0.5, Colours::white.withAlpha (0.95f));
        g.setGradientFill (band);
        g.fillRect (titleArea);
    }
}

void CrystalEditor::onFrame (double, double dt)
{
    stage.tick (dt);

    // an occasional glint sweeps across the title
    glintClock += dt;
    const bool wasActive = glintActive;
    if (! glintActive && glintClock > 7.0)
    {
        glintActive = true;
        glintClock = 0.0;
    }
    if (glintActive && glintClock > 1.4)
    {
        glintActive = false;
        glintClock = 0.0;
    }
    if (glintActive || wasActive)
        content.repaint (titleArea.expanded (2.0f).toNearestInt());
}
