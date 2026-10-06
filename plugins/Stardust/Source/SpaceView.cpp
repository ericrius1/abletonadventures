#include "SpaceView.h"

using namespace juce;
using namespace stardust::ui;

namespace
{
    constexpr float cornerRadius = 16.0f;

    Colour starTint (Random& r)
    {
        const float t = r.nextFloat();
        if (t < 0.12f) return cyan.interpolatedWith (starWhite, 0.55f);
        if (t < 0.20f) return magenta.interpolatedWith (starWhite, 0.6f);
        if (t < 0.27f) return gold.interpolatedWith (starWhite, 0.5f);
        return starWhite;
    }
} // namespace

namespace stardust::ui
{
void drawSparkle (Graphics& g, Point<float> c, float size, Colour colour, float alpha, float rotation)
{
    if (alpha <= 0.003f || size <= 0.1f)
        return;

    // soft halo
    const float halo = size * 1.5f;
    g.setGradientFill (ColourGradient (colour.withAlpha (0.35f * alpha), c.x, c.y,
                                       colour.withAlpha (0.0f), c.x + halo, c.y, true));
    g.fillEllipse (Rectangle<float> (halo * 2.0f, halo * 2.0f).withCentre (c));

    auto ray = [&] (float length, float width, float angle, Colour col)
    {
        Path p;
        p.startNewSubPath (0.0f, -length);
        p.quadraticTo (width * 0.25f, -width * 0.25f, width, 0.0f);
        p.quadraticTo (width * 0.25f, width * 0.25f, 0.0f, length);
        p.quadraticTo (-width * 0.25f, width * 0.25f, -width, 0.0f);
        p.quadraticTo (-width * 0.25f, -width * 0.25f, 0.0f, -length);
        p.closeSubPath();
        g.setColour (col);
        g.fillPath (p, AffineTransform::rotation (angle).translated (c));
    };

    ray (size, size * 0.22f, rotation, colour.withAlpha (0.85f * alpha));
    ray (size * 0.55f, size * 0.16f, rotation + MathConstants<float>::halfPi * 0.5f, colour.withAlpha (0.45f * alpha));
    ray (size * 0.55f, size * 0.16f, rotation - MathConstants<float>::halfPi * 0.5f, colour.withAlpha (0.45f * alpha));
    ray (size * 0.6f, size * 0.14f, rotation, Colours::white.withAlpha (0.95f * alpha));
    g.setColour (Colours::white.withAlpha (alpha));
    g.fillEllipse (Rectangle<float> (size * 0.28f, size * 0.28f).withCentre (c));
}
} // namespace stardust::ui

//==============================================================================
SpaceView::SpaceView (StardustProcessor& p) : processor (p)
{
    setOpaque (false);
    setInterceptsMouseClicks (true, false);
    setTooltip ("Deep space: the glowing line is your sound, every note sends out a burst of stars "
                "and every Twinkle glint lights up a star.");
    particles.reserve (400);
    rings.reserve (40);
    sparkles.reserve (80);
}

void SpaceView::resized()
{
    imageScale = 0.0f; // rebuild on next paint

    const float w = (float) getWidth(), h = (float) getHeight();
    stars.clear();
    Random r (2024);
    const int counts[] = { 90, 46, 16 };
    for (int layer = 0; layer < 3; ++layer)
        for (int i = 0; i < counts[layer]; ++i)
        {
            Star s;
            s.x = r.nextFloat() * w;
            s.y = r.nextFloat() * h;
            s.layer = layer;
            s.size = layer == 0 ? 0.6f + r.nextFloat() * 0.6f : (layer == 1 ? 1.0f + r.nextFloat() * 0.8f : 1.6f + r.nextFloat() * 1.2f);
            s.brightness = 0.35f + 0.65f * r.nextFloat();
            s.phase = r.nextFloat() * MathConstants<float>::twoPi;
            s.rate = 0.6f + 2.5f * r.nextFloat();
            s.colour = starTint (r);
            stars.push_back (s);
        }
}

Colour SpaceView::noteColour (int note) const
{
    const Colour palette[] = { cyan, lavender, magenta, gold, cyan.interpolatedWith (starWhite, 0.4f), magenta.interpolatedWith (gold, 0.5f) };
    return palette[(note * 7) % 6];
}

//==============================================================================
void SpaceView::rebuildImages (float scale)
{
    imageScale = scale;
    const float w = (float) getWidth(), h = (float) getHeight();
    const int pw = jmax (1, roundToInt (w * scale)), ph = jmax (1, roundToInt (h * scale));
    auto bounds = Rectangle<float> (w, h);

    // ---- background: deep space, nebula, a ringed planet --------------------------------
    background = Image (Image::ARGB, pw, ph, true);
    {
        Graphics g (background);
        g.addTransform (AffineTransform::scale (scale));
        Path clip;
        clip.addRoundedRectangle (bounds, cornerRadius);
        g.reduceClipRegion (clip);

        g.setGradientFill (ColourGradient (Colour (0xff05061a), 0.0f, 0.0f, Colour (0xff170c38), w * 0.7f, h, false));
        g.fillAll();

        Random r (77);
        struct Cloud { float x, y, rx, ry; Colour c; float a; };
        const Cloud clouds[] = {
            { 0.18f, 0.30f, 0.34f, 0.55f, magenta, 0.20f },
            { 0.30f, 0.72f, 0.28f, 0.40f, Colour (0xff6a3cff), 0.22f },
            { 0.62f, 0.25f, 0.36f, 0.45f, cyan, 0.12f },
            { 0.78f, 0.62f, 0.30f, 0.50f, magenta, 0.12f },
            { 0.50f, 0.50f, 0.50f, 0.30f, Colour (0xff3a2a9c), 0.25f },
            { 0.90f, 0.20f, 0.18f, 0.30f, gold, 0.06f },
        };
        for (const auto& c : clouds)
            for (int k = 0; k < 7; ++k)
            {
                const float cx = (c.x + (r.nextFloat() - 0.5f) * 0.14f) * w;
                const float cy = (c.y + (r.nextFloat() - 0.5f) * 0.2f) * h;
                const float rx = c.rx * w * (0.45f + 0.5f * r.nextFloat());
                const float ry = c.ry * h * (0.45f + 0.5f * r.nextFloat());
                g.setGradientFill (ColourGradient (c.c.withAlpha (c.a * (0.4f + 0.5f * r.nextFloat())), cx, cy,
                                                   c.c.withAlpha (0.0f), cx + rx, cy, true));
                g.fillEllipse (cx - rx, cy - ry, rx * 2.0f, ry * 2.0f);
            }

        // star dust band
        for (int i = 0; i < 260; ++i)
        {
            const float t = r.nextFloat();
            const float x = t * w;
            const float y = h * (0.62f - 0.35f * t) + (r.nextFloat() - 0.5f) * h * 0.28f * (0.4f + r.nextFloat());
            g.setColour (starWhite.withAlpha (0.05f + 0.18f * r.nextFloat()));
            const float s = 0.5f + r.nextFloat() * 0.9f;
            g.fillEllipse (x, y, s, s);
        }

        // ringed planet peeking in at the bottom right
        const Point<float> pc (w - 58.0f, h - 6.0f);
        const float pr = 40.0f;
        auto ringPath = [&] (bool back)
        {
            Path ring;
            ring.addCentredArc (pc.x, pc.y, pr * 1.9f, pr * 0.42f, -0.28f,
                                back ? -MathConstants<float>::halfPi : MathConstants<float>::halfPi,
                                back ? MathConstants<float>::halfPi : MathConstants<float>::pi * 1.5f, true);
            return ring;
        };
        g.setColour (gold.withAlpha (0.22f));
        g.strokePath (ringPath (true), PathStrokeType (5.0f));
        g.setColour (lavender.withAlpha (0.15f));
        g.strokePath (ringPath (true), PathStrokeType (1.5f), AffineTransform::scale (1.08f, 1.08f, pc.x, pc.y));

        g.setGradientFill (ColourGradient (Colour (0xff8a6bff), pc.x - pr * 0.5f, pc.y - pr * 0.6f,
                                           Colour (0xff1c1250), pc.x + pr * 0.6f, pc.y + pr * 0.5f, true));
        g.fillEllipse (Rectangle<float> (pr * 2.0f, pr * 2.0f).withCentre (pc));
        // bands
        g.saveState();
        Path planetClip;
        planetClip.addEllipse (Rectangle<float> (pr * 2.0f, pr * 2.0f).withCentre (pc));
        g.reduceClipRegion (planetClip);
        for (int b = 0; b < 5; ++b)
        {
            g.setColour ((b % 2 == 0 ? magenta : cyan).withAlpha (0.10f));
            g.fillRect (Rectangle<float> (pc.x - pr, pc.y - pr + (float) b * pr * 0.32f + 6.0f, pr * 2.0f, pr * 0.12f)
                            .transformedBy (AffineTransform::rotation (-0.28f, pc.x, pc.y)));
        }
        g.setGradientFill (ColourGradient (Colours::transparentBlack, pc.x - pr * 0.2f, pc.y - pr * 0.2f,
                                           Colours::black.withAlpha (0.55f), pc.x + pr, pc.y + pr * 0.4f, true));
        g.fillEllipse (Rectangle<float> (pr * 2.0f, pr * 2.0f).withCentre (pc));
        g.restoreState();
        g.setColour (cyan.withAlpha (0.35f));
        g.drawEllipse (Rectangle<float> (pr * 2.0f, pr * 2.0f).withCentre (pc).reduced (0.5f), 1.0f);

        g.setColour (gold.withAlpha (0.55f));
        g.strokePath (ringPath (false), PathStrokeType (4.0f));
        g.setColour (starWhite.withAlpha (0.35f));
        g.strokePath (ringPath (false), PathStrokeType (1.0f));

        // a tiny moon
        const Point<float> mc (w - 132.0f, h - 34.0f);
        g.setGradientFill (ColourGradient (Colour (0xffe8e2ff), mc.x - 2.0f, mc.y - 3.0f, Colour (0xff5a4f99), mc.x + 5.0f, mc.y + 5.0f, true));
        g.fillEllipse (Rectangle<float> (9.0f, 9.0f).withCentre (mc));
    }

    // ---- overlay: vignette, glass sheen and the frame --------------------------------------
    overlay = Image (Image::ARGB, pw, ph, true);
    {
        Graphics g (overlay);
        g.addTransform (AffineTransform::scale (scale));
        Path clip;
        clip.addRoundedRectangle (bounds, cornerRadius);
        g.saveState();
        g.reduceClipRegion (clip);

        g.setGradientFill (ColourGradient (Colours::transparentBlack, w * 0.5f, h * 0.5f,
                                           Colours::black.withAlpha (0.55f), 0.0f, -h * 0.1f, true));
        g.fillAll();
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.07f), 0.0f, 0.0f,
                                           Colours::transparentWhite, 0.0f, h * 0.35f, false));
        g.fillRoundedRectangle (bounds.withHeight (h * 0.35f), cornerRadius);
        g.restoreState();

        g.setGradientFill (ColourGradient (cyan.withAlpha (0.55f), 0.0f, 0.0f, magenta.withAlpha (0.45f), w, h, false));
        g.drawRoundedRectangle (bounds.reduced (0.75f), cornerRadius, 1.5f);
        g.setColour (Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (bounds.reduced (3.0f), cornerRadius - 2.5f, 1.0f);
    }
}

//==============================================================================
void SpaceView::spawnBurst (int note, float velocity)
{
    const float w = (float) getWidth(), h = (float) getHeight();
    const float x = jmap ((float) jlimit (24, 108, note), 24.0f, 108.0f, w * 0.1f, w * 0.9f);
    const Point<float> origin (x, h * 0.5f + (random.nextFloat() - 0.5f) * 16.0f);
    const auto colour = noteColour (note);

    const int count = 10 + (int) (velocity * 16.0f);
    for (int i = 0; i < count && particles.size() < 380; ++i)
    {
        const float angle = random.nextFloat() * MathConstants<float>::twoPi;
        const float speed = (25.0f + random.nextFloat() * 95.0f) * (0.6f + 0.6f * velocity);
        Particle pt;
        pt.pos = origin;
        pt.vel = { std::cos (angle) * speed, std::sin (angle) * speed * 0.7f };
        pt.maxLife = pt.life = 0.6f + random.nextFloat() * 0.9f;
        pt.size = 0.8f + random.nextFloat() * 2.0f;
        pt.colour = random.nextFloat() < 0.35f ? starWhite : colour;
        particles.push_back (pt);
    }
    if (rings.size() < 36)
        rings.push_back ({ origin, 0.0f, 0.9f, 14.0f + velocity * 36.0f, colour });
}

void SpaceView::spawnSparkle (const stardust::GlintEvent& e)
{
    if (sparkles.size() >= 70)
        sparkles.erase (sparkles.begin());

    const float w = (float) getWidth(), h = (float) getHeight();
    Sparkle s;
    s.pos = { w * 0.5f + e.pan * (w * 0.5f - 34.0f) + (random.nextFloat() - 0.5f) * 20.0f,
              18.0f + (1.0f - jlimit (0.0f, 1.0f, e.height)) * (h * 0.62f) + (random.nextFloat() - 0.5f) * 14.0f };
    s.age = 0.0f;
    s.life = jlimit (0.4f, 2.2f, e.life * 1.1f);
    s.size = 5.0f + 9.0f * jlimit (0.0f, 1.0f, e.level);
    s.strength = 0.55f + 0.45f * jlimit (0.0f, 1.0f, e.level * 1.5f);
    s.rotation = (random.nextFloat() - 0.5f) * 0.4f;
    s.flicker = 5.0f + random.nextFloat() * 7.0f;
    const Colour tints[] = { starWhite, cyan.interpolatedWith (starWhite, 0.3f), gold.interpolatedWith (starWhite, 0.25f),
                             magenta.interpolatedWith (starWhite, 0.35f) };
    s.colour = tints[random.nextInt (4)];
    sparkles.push_back (s);
}

void SpaceView::updateScope()
{
    constexpr int size = StardustProcessor::scopeSize;
    constexpr int window = 1500;
    const int write = processor.scopeWrite.load (std::memory_order_acquire);

    auto sampleAt = [&] (int index, bool left)
    {
        const int i = (index % size + size) % size;
        return left ? processor.scopeL[(size_t) i].load (std::memory_order_relaxed)
                    : processor.scopeR[(size_t) i].load (std::memory_order_relaxed);
    };

    // Find a rising zero crossing (of the mono sum) so that the trace stands still for steady tones.
    int start = write - window - 1;
    for (int back = 0; back < 1800; ++back)
    {
        const int i = write - window - 1 - back;
        const float a = sampleAt (i - 1, true) + sampleAt (i - 1, false);
        const float b = sampleAt (i, true) + sampleAt (i, false);
        if (a <= 0.0f && b > 0.0f)
        {
            start = i;
            break;
        }
    }

    // Each point averages its whole span (a gentle low-pass) so the trace reads as a smooth, glowing line.
    float peak = 0.0f;
    std::array<float, scopePoints> newL {}, newR {};
    const int span = jmax (1, window / scopePoints);
    for (int p = 0; p < scopePoints; ++p)
    {
        const int i0 = start + (p * window) / scopePoints - span / 2;
        float l = 0.0f, r = 0.0f;
        for (int k = 0; k < span * 2; ++k)
        {
            l += sampleAt (i0 + k, true);
            r += sampleAt (i0 + k, false);
        }
        newL[(size_t) p] = l / (float) (span * 2);
        newR[(size_t) p] = r / (float) (span * 2);
    }
    for (auto* arr : { &newL, &newR })
    {
        auto& a = *arr;
        float prev = a[0];
        for (int p = 1; p < scopePoints - 1; ++p)
        {
            const float cur = a[(size_t) p];
            a[(size_t) p] = 0.25f * prev + 0.5f * cur + 0.25f * a[(size_t) p + 1];
            prev = cur;
        }
        for (auto v : a)
            peak = jmax (peak, std::abs (v));
    }

    level += 0.15f * (peak - level);
    const float targetGain = 0.82f / jmax (0.06f, peak);
    scopeGain += (targetGain < scopeGain ? 0.4f : 0.05f) * (targetGain - scopeGain);

    for (int p = 0; p < scopePoints; ++p)
    {
        dispL[(size_t) p] += 0.55f * (newL[(size_t) p] - dispL[(size_t) p]);
        dispR[(size_t) p] += 0.55f * (newR[(size_t) p] - dispR[(size_t) p]);
    }
}

//==============================================================================
void SpaceView::tick (double dtD)
{
    const float dt = (float) dtD;
    clock += dtD;
    const float w = (float) getWidth(), h = (float) getHeight();
    if (w <= 0.0f || h <= 0.0f)
        return;

    stardust::NoteEvent ne;
    while (processor.engine.noteEvents.pop (ne))
        spawnBurst (ne.note, ne.velocity);

    stardust::GlintEvent ge;
    while (processor.engine.glintEvents.pop (ge))
        spawnSparkle (ge);

    updateScope();
    voices = processor.engine.activeVoiceCount.load (std::memory_order_relaxed);

    // chord read-out from the keyboard state (covers incoming MIDI and on-screen keys)
    String chord;
    int shown = 0;
    for (int n = 0; n < 128 && shown < 6; ++n)
        if (processor.keyboardState.isNoteOnForChannels (0xffff, n))
        {
            chord << (shown > 0 ? "  " : "") << MidiMessage::getMidiNoteName (n, true, true, 3);
            ++shown;
        }
    chordText = chord;

    // warp: the starfield speeds up a little while you play
    warp += (jmin (1.0f, level * 3.0f) - warp) * jmin (1.0f, dt * 1.5f);
    const float speeds[] = { 2.5f, 7.0f, 17.0f };
    for (auto& s : stars)
    {
        s.x -= speeds[s.layer] * (1.0f + 2.5f * warp) * dt;
        if (s.x < -4.0f)
        {
            s.x += w + 8.0f;
            s.y = random.nextFloat() * h;
        }
        s.phase += s.rate * dt;
    }

    for (auto& pt : particles)
    {
        pt.pos += pt.vel * dt;
        pt.vel *= std::pow (0.35f, dt);
        pt.life -= dt;
    }
    particles.erase (std::remove_if (particles.begin(), particles.end(), [] (const Particle& p) { return p.life <= 0.0f; }),
                     particles.end());

    for (auto& r : rings)
        r.age += dt;
    rings.erase (std::remove_if (rings.begin(), rings.end(), [] (const Ring& r) { return r.age >= r.life; }), rings.end());

    for (auto& s : sparkles)
        s.age += dt;
    sparkles.erase (std::remove_if (sparkles.begin(), sparkles.end(), [] (const Sparkle& s) { return s.age >= s.life; }),
                    sparkles.end());

    // the occasional comet
    if (comet.active)
    {
        comet.age += dt;
        comet.pos += comet.vel * dt;
        if (comet.age > comet.life)
            comet.active = false;
    }
    else if ((cometTimer -= dt) <= 0.0f)
    {
        comet.active = true;
        comet.age = 0.0f;
        comet.life = 1.1f;
        comet.pos = { w * (0.35f + 0.6f * random.nextFloat()), -6.0f };
        comet.vel = { -(160.0f + 120.0f * random.nextFloat()), 70.0f + 60.0f * random.nextFloat() };
        cometTimer = 5.0f + random.nextFloat() * 7.0f;
    }

    repaint();
}

//==============================================================================
void SpaceView::paint (Graphics& g)
{
    const float scale = jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (background.isNull() || std::abs (scale - imageScale) > 0.01f)
        rebuildImages (scale);

    auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth(), h = bounds.getHeight();
    g.drawImage (background, bounds);

    g.saveState();
    Path clip;
    clip.addRoundedRectangle (bounds, cornerRadius);
    g.reduceClipRegion (clip);

    // ---- starfield (far + mid) ----
    for (const auto& s : stars)
    {
        if (s.layer == 2)
            continue;
        const float tw = 0.55f + 0.45f * std::sin (s.phase);
        g.setColour (s.colour.withAlpha (s.brightness * tw * (s.layer == 0 ? 0.55f : 0.8f)));
        g.fillEllipse (s.x - s.size * 0.5f, s.y - s.size * 0.5f, s.size, s.size);
    }

    // ---- comet ----
    if (comet.active)
    {
        const float a = std::sin (MathConstants<float>::pi * jlimit (0.0f, 1.0f, comet.age / comet.life));
        const auto tail = comet.pos - comet.vel * 0.28f;
        g.setGradientFill (ColourGradient (starWhite.withAlpha (0.8f * a), comet.pos.x, comet.pos.y,
                                           cyan.withAlpha (0.0f), tail.x, tail.y, false));
        g.drawLine ({ tail, comet.pos }, 1.6f);
        g.setColour (Colours::white.withAlpha (0.9f * a));
        g.fillEllipse (Rectangle<float> (2.6f, 2.6f).withCentre (comet.pos));
    }

    // ---- horizon + oscilloscope ----
    const float midY = h * 0.5f;
    const float amp = h * 0.32f;
    g.setGradientFill (ColourGradient (cyan.withAlpha (0.0f), 0.0f, midY, cyan.withAlpha (0.0f), w, midY, false));
    {
        ColourGradient horizon (cyan.withAlpha (0.0f), 0.0f, midY, cyan.withAlpha (0.0f), w, midY, false);
        horizon.addColour (0.5, lavender.withAlpha (0.18f));
        g.setGradientFill (horizon);
        g.fillRect (0.0f, midY - 0.5f, w, 1.0f);
    }

    auto tracePath = [&] (const std::array<float, scopePoints>& d)
    {
        Path p;
        const float x0 = 14.0f, x1 = w - 14.0f;
        for (int i = 0; i < scopePoints; ++i)
        {
            const float x = x0 + (x1 - x0) * (float) i / (float) (scopePoints - 1);
            // fade the ends into the horizon
            const float edge = jmin (1.0f, jmin ((float) i, (float) (scopePoints - 1 - i)) / 14.0f);
            const float y = midY - jlimit (-1.2f, 1.2f, d[(size_t) i] * scopeGain) * amp * edge;
            if (i == 0) p.startNewSubPath (x, y);
            else p.lineTo (x, y);
        }
        return p;
    };

    const float presence = jlimit (0.25f, 1.0f, level * 12.0f);
    const auto pathR = tracePath (dispR);
    const auto pathL = tracePath (dispL);
    for (auto [path, colour] : { std::pair { &pathR, magenta }, std::pair { &pathL, cyan } })
    {
        g.setColour (colour.withAlpha (0.07f * presence));
        g.strokePath (*path, PathStrokeType (9.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (colour.withAlpha (0.18f * presence));
        g.strokePath (*path, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (colour.interpolatedWith (Colours::white, 0.35f).withAlpha (0.9f * presence));
        g.strokePath (*path, PathStrokeType (1.5f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // ---- supernova rings + particles ----
    for (const auto& r : rings)
    {
        const float t = r.age / r.life;
        const float radius = r.size * (0.3f + 2.2f * (1.0f - (1.0f - t) * (1.0f - t)));
        g.setColour (r.colour.withAlpha (0.45f * (1.0f - t)));
        g.drawEllipse (Rectangle<float> (radius * 2.0f, radius * 1.3f).withCentre (r.pos), 1.4f);
    }
    for (const auto& pt : particles)
    {
        const float a = jlimit (0.0f, 1.0f, pt.life / pt.maxLife);
        g.setColour (pt.colour.withAlpha (0.25f * a));
        g.fillEllipse (Rectangle<float> (pt.size * 3.0f, pt.size * 3.0f).withCentre (pt.pos));
        g.setColour (pt.colour.withAlpha (0.95f * a));
        g.fillEllipse (Rectangle<float> (pt.size, pt.size).withCentre (pt.pos));
    }

    // ---- twinkles (one per glint) ----
    for (const auto& s : sparkles)
    {
        const float t = s.age / s.life;
        const float env = jmin (1.0f, s.age * 14.0f) * (1.0f - t) * (1.0f - t);
        const float flick = 0.7f + 0.3f * std::sin (s.age * s.flicker * MathConstants<float>::twoPi);
        drawSparkle (g, s.pos, s.size * (0.7f + 0.3f * env), s.colour, env * flick * s.strength, s.rotation + t * 0.6f);
    }

    // ---- near stars ----
    for (const auto& s : stars)
    {
        if (s.layer != 2)
            continue;
        const float tw = 0.6f + 0.4f * std::sin (s.phase);
        const auto c = Point<float> (s.x, s.y);
        g.setColour (s.colour.withAlpha (0.18f * tw));
        g.fillEllipse (Rectangle<float> (s.size * 3.2f, s.size * 3.2f).withCentre (c));
        g.setColour (s.colour.withAlpha (s.brightness * tw));
        g.fillEllipse (Rectangle<float> (s.size, s.size).withCentre (c));
        if (s.size > 2.2f)
        {
            g.setColour (s.colour.withAlpha (0.35f * tw));
            g.fillRect (Rectangle<float> (s.size * 4.0f, 0.6f).withCentre (c));
            g.fillRect (Rectangle<float> (0.6f, s.size * 4.0f).withCentre (c));
        }
    }

    g.restoreState();
    g.drawImage (overlay, bounds);

    // ---- HUD ----
    g.setFont (aa::Fonts::uiBold (9.5f).withExtraKerningFactor (0.14f));
    g.setColour (cyan.withAlpha (0.75f));
    const String voiceText = String (voices) + (voices == 1 ? " VOICE" : " VOICES");
    g.drawText (voiceText, Rectangle<float> (16.0f, 9.0f, 120.0f, 14.0f), Justification::centredLeft);
    g.setColour (starWhite.withAlpha (0.55f));
    g.drawText (chordText, Rectangle<float> (w - 236.0f, 9.0f, 220.0f, 14.0f), Justification::centredRight);
}
