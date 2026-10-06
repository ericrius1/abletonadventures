#include "BoingEditor.h"
#include <PluginAssets.h>

using namespace juce;

namespace
{
    constexpr int baseWidth = 880, baseHeight = 560;

    const Colour ink { 0xff2a2350 };
    const Colour tomato { 0xffff5a5f };
    const Colour sunny { 0xffffb830 };
    const Colour mint { 0xff3ddc97 };
    const Colour lilac { 0xff9b7bff };
    const Colour sky { 0xff5ec8ff };
    const Colour bubblegum { 0xffff7eb6 };

    aa::Theme makeTheme()
    {
        aa::Theme t;
        t.background = Colour (0xffbfe6ff);
        t.backgroundAlt = Colour (0xffd9ccff);
        t.panel = Colours::white.withAlpha (0.62f);
        t.panelOutline = Colours::white.withAlpha (0.9f);
        t.text = ink;
        t.textDim = ink.withAlpha (0.6f);
        t.accent = tomato;
        t.accent2 = sunny;
        t.knobBody = Colour (0xfff7f4ff);
        t.knobTrack = ink.withAlpha (0.13f);
        t.shadow = ink.withAlpha (0.28f);
        t.popupBackground = Colour (0xfffdfcff);
    t.pill = Colours::white.withAlpha (0.7f);
        t.cornerRadius = 16.0f;
        t.glow = true;
        return t;
    }

    void fillOutlinedText (Graphics& g, const String& text, const Font& font, Point<float> baseline, Colour fill,
                           Colour outline, float outlineWidth)
    {
        GlyphArrangement ga;
        ga.addLineOfText (font, text, baseline.x, baseline.y);
        Path p;
        ga.createPath (p);
        g.setColour (outline);
        g.strokePath (p, PathStrokeType (outlineWidth, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (fill);
        g.fillPath (p);
    }
} // namespace

//==============================================================================
BounceStage::BounceStage (BoingProcessor& p) : processor (p)
{
    setOpaque (false);

    // Forget hits that queued up while the editor was closed.
    BoingProcessor::Hit stale;
    while (processor.hits.pop (stale)) {}

    setTooltip ("Every hit launches a ball - each landing is an echo. Click to drop a test ball.");
}

void BounceStage::resized()
{
    auto b = getLocalBounds().toFloat();
    floorArea = b.removeFromBottom (54.0f);
    skyArea = b;
    background = {};
}

float BounceStage::timeToX (double t, const boing::Schedule& s) const
{
    const float x0 = 64.0f, x1 = (float) getWidth() - 34.0f;
    const double span = jmax (0.05, s.totalTime * 1.02);
    return x0 + (float) (t / span) * (x1 - x0);
}

float BounceStage::apexFor (double interval, const boing::Schedule& s) const
{
    double maxInterval = 0.0;
    for (int k = 0; k < s.count; ++k)
        maxInterval = jmax (maxInterval, s.interval[(size_t) k]);
    if (maxInterval <= 0.0)
        return 0.0f;
    const float maxArc = floorArea.getY() + 8.0f - skyArea.getY() - 34.0f;
    const double n = interval / maxInterval;
    return maxArc * (float) (0.08 + 0.92 * n * n);
}

Point<float> BounceStage::positionFor (const boing::Schedule& s, double t, float& heightNorm) const
{
    const float floorY = floorArea.getY() + 8.0f;
    heightNorm = 0.0f;
    if (s.count == 0 || t <= 0.0)
        return { timeToX (0.0, s), floorY };

    double start = 0.0;
    for (int k = 0; k < s.count; ++k)
    {
        const double end = s.time[(size_t) k];
        if (t <= end)
        {
            const double interval = end - start;
            const float u = (float) ((t - start) / jmax (1.0e-6, interval));
            const float h = 4.0f * u * (1.0f - u);
            const float apex = apexFor (interval, s);
            heightNorm = h * apex / jmax (1.0f, floorY - skyArea.getY());
            return { timeToX (t, s), floorY - apex * h };
        }
        start = end;
    }

    // After the last bounce: roll a little further and settle.
    const double extra = jmin (0.6, t - s.totalTime);
    return { timeToX (s.totalTime, s) + (float) (extra * 30.0), floorY };
}

void BounceStage::spawnBall (float amplitude)
{
    if (balls.size() >= 10)
        balls.erase (balls.begin());

    Ball b;
    b.size = jmap (jlimit (0.0f, 1.0f, amplitude), 15.0f, 27.0f);
    b.schedule = processor.currentSchedule();
    b.hue = random.nextFloat();
    balls.push_back (b);

    // the dry hit itself: a puff at the launch pad
    ripples.push_back ({ { timeToX (0.0, b.schedule), floorArea.getY() + 8.0f }, 0.0f, 0.8f, tomato });
}

void BounceStage::mouseDown (const MouseEvent&)
{
    processor.testDropRequested.store (true);
}

void BounceStage::tick (double dt)
{
    clock += dt;
    cloudOffset += (float) dt * 6.0f;
    preview = processor.currentSchedule();

    BoingProcessor::Hit hit;
    while (processor.hits.pop (hit))
        spawnBall (hit.amplitude);

    const float floorY = floorArea.getY() + 8.0f;

    for (auto& b : balls)
    {
        b.age += dt;
        b.squash = jmax (0.0f, b.squash - (float) dt * 7.0f);

        while (b.nextImpact < b.schedule.count && b.age >= b.schedule.time[(size_t) b.nextImpact])
        {
            const int k = b.nextImpact++;
            const float g = b.schedule.gain[(size_t) k];
            b.squash = jmin (0.85f, 0.35f + g);
            if (k < (int) padFlash.size())
                padFlash[(size_t) k] = jmax (padFlash[(size_t) k], 0.3f + g);

            const float x = timeToX (b.schedule.time[(size_t) k], b.schedule);
            ripples.push_back ({ { x, floorY }, 0.0f, g, k % 2 == 0 ? sky : bubblegum });
            const int nDust = 3 + (int) (g * 6.0f);
            for (int d = 0; d < nDust; ++d)
                dust.push_back ({ { x, floorY },
                                  { (random.nextFloat() - 0.5f) * 140.0f, -40.0f - random.nextFloat() * 120.0f * g },
                                  1.0f, 1.5f + random.nextFloat() * 2.5f });
            if (onImpact)
                onImpact();
        }

        float hn = 0.0f;
        auto pos = positionFor (b.schedule, b.age, hn);
        if (b.trailCount < (int) b.trail.size())
            ++b.trailCount;
        for (int i = b.trailCount - 1; i > 0; --i)
            b.trail[(size_t) i] = b.trail[(size_t) i - 1];
        b.trail[0] = pos;
    }

    balls.erase (std::remove_if (balls.begin(), balls.end(),
                                 [] (const Ball& b) { return b.age > b.schedule.totalTime + 1.2; }),
                 balls.end());

    for (auto& r : ripples)
        r.age += (float) dt;
    ripples.erase (std::remove_if (ripples.begin(), ripples.end(), [] (const Ripple& r) { return r.age > 0.7f; }),
                   ripples.end());

    for (auto& d : dust)
    {
        d.vel.y += 420.0f * (float) dt;
        d.pos += d.vel * (float) dt;
        d.life -= (float) dt * 1.6f;
    }
    dust.erase (std::remove_if (dust.begin(), dust.end(),
                                [floorY] (const Dust& d) { return d.life <= 0.0f || d.pos.y > floorY + 4.0f; }),
                dust.end());

    for (auto& f : padFlash)
        f = jmax (0.0f, f - (float) dt * 2.2f);

    repaint();
}

void BounceStage::rebuildBackground (float scale)
{
    scale = jlimit (1.0f, 4.0f, scale);
    const int w = jmax (1, roundToInt ((float) getWidth() * scale));
    const int h = jmax (1, roundToInt ((float) getHeight() * scale));
    background = Image (Image::ARGB, w, h, true);
    Graphics g (background);
    g.addTransform (AffineTransform::scale (scale));

    auto b = getLocalBounds().toFloat();
    Path clip;
    clip.addRoundedRectangle (b, 18.0f);
    g.reduceClipRegion (clip);

    // Sky
    g.setGradientFill (ColourGradient (Colour (0xff7fd0ff), 0.0f, 0.0f, Colour (0xffe9dcff), 0.0f, floorArea.getY(), false));
    g.fillRect (b);

    // Sun
    const Point<float> sun (b.getRight() - 90.0f, 52.0f);
    for (int i = 6; i > 0; --i)
    {
        g.setColour (Colour (0xffffe08a).withAlpha (0.08f));
        g.fillEllipse (Rectangle<float> (40.0f + (float) i * 16.0f, 40.0f + (float) i * 16.0f).withCentre (sun));
    }
    g.setGradientFill (ColourGradient (Colour (0xfffff3b0), sun.x - 10.0f, sun.y - 12.0f, sunny, sun.x + 18.0f, sun.y + 20.0f, true));
    g.fillEllipse (Rectangle<float> (46.0f, 46.0f).withCentre (sun));

    // Distant hills
    Path hills;
    hills.startNewSubPath (0.0f, floorArea.getY());
    for (float x = 0.0f; x <= b.getWidth() + 10.0f; x += 10.0f)
        hills.lineTo (x, floorArea.getY() - 26.0f - 16.0f * std::sin (x * 0.012f) - 9.0f * std::sin (x * 0.031f + 1.3f));
    hills.lineTo (b.getWidth(), floorArea.getY());
    hills.closeSubPath();
    g.setColour (Colour (0xffb7a7ff).withAlpha (0.55f));
    g.fillPath (hills);

    // Floor with perspective tiles
    g.setGradientFill (ColourGradient (Colour (0xff5b46c9), 0.0f, floorArea.getY(), Colour (0xff3a2a8c), 0.0f, floorArea.getBottom(), false));
    g.fillRect (floorArea);
    g.setColour (Colours::white.withAlpha (0.08f));
    const float vpX = b.getCentreX();
    for (float x = -400.0f; x < b.getWidth() + 400.0f; x += 46.0f)
        g.drawLine (vpX + (x - vpX) * 0.55f, floorArea.getY(), x, floorArea.getBottom(), 1.0f);
    for (float yy : { 0.18f, 0.42f, 0.72f })
        g.drawHorizontalLine ((int) (floorArea.getY() + floorArea.getHeight() * yy), 0.0f, b.getWidth());
    g.setColour (Colours::white.withAlpha (0.35f));
    g.fillRect (floorArea.withHeight (2.0f));

    // Launch spring
    const float sx = timeToX (0.0, preview), floorY = floorArea.getY() + 8.0f;
    Path spring;
    spring.startNewSubPath (sx - 12.0f, floorY + 14.0f);
    for (int i = 0; i < 6; ++i)
        spring.lineTo (sx + (i % 2 == 0 ? 12.0f : -12.0f), floorY + 14.0f - (float) (i + 1) * 2.6f);
    g.setColour (Colour (0xffffd9e8));
    g.strokePath (spring, PathStrokeType (2.5f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (tomato);
    g.fillRoundedRectangle (Rectangle<float> (34.0f, 6.0f).withCentre ({ sx, floorY - 1.0f }), 3.0f);
}

void BounceStage::drawBall (Graphics& g, Point<float> c, float size, float squash, Colour colour, float lookDir)
{
    const float sx = 1.0f + squash * 0.35f, sy = 1.0f - squash * 0.32f;
    auto r = Rectangle<float> (size * sx, size * sy).withCentre ({ c.x, c.y - size * sy * 0.5f });

    g.setColour (ink.withAlpha (0.25f));
    g.drawEllipse (r.expanded (1.0f), 2.0f);
    g.setGradientFill (ColourGradient (colour.brighter (0.6f), r.getX() + r.getWidth() * 0.3f, r.getY() + r.getHeight() * 0.25f,
                                       colour.darker (0.35f), r.getRight(), r.getBottom(), true));
    g.fillEllipse (r);

    // shine
    g.setColour (Colours::white.withAlpha (0.75f));
    g.fillEllipse (r.getX() + r.getWidth() * 0.2f, r.getY() + r.getHeight() * 0.14f, r.getWidth() * 0.26f, r.getHeight() * 0.18f);

    // face
    const float eyeY = r.getY() + r.getHeight() * 0.45f;
    const float eyeDx = r.getWidth() * 0.17f;
    const float eyeR = jmax (1.6f, size * 0.12f);
    for (int side = -1; side <= 1; side += 2)
    {
        const float ex = r.getCentreX() + (float) side * eyeDx + lookDir * size * 0.06f;
        if (squash > 0.35f)
        {
            Path squint; // happy ^ ^ eyes on impact
            squint.startNewSubPath (ex - eyeR, eyeY + eyeR * 0.4f);
            squint.lineTo (ex, eyeY - eyeR * 0.6f);
            squint.lineTo (ex + eyeR, eyeY + eyeR * 0.4f);
            g.setColour (ink);
            g.strokePath (squint, PathStrokeType (jmax (1.2f, size * 0.07f), PathStrokeType::curved, PathStrokeType::rounded));
        }
        else
        {
            g.setColour (Colours::white);
            g.fillEllipse (Rectangle<float> (eyeR * 2.2f, eyeR * 2.4f).withCentre ({ ex, eyeY }));
            g.setColour (ink);
            g.fillEllipse (Rectangle<float> (eyeR * 1.2f, eyeR * 1.3f).withCentre ({ ex + lookDir * eyeR * 0.45f, eyeY + eyeR * 0.15f }));
        }
    }
    // smile
    Path smile;
    smile.addCentredArc (r.getCentreX() + lookDir * size * 0.05f, r.getY() + r.getHeight() * 0.6f, size * 0.14f, size * 0.1f,
                         0.0f, MathConstants<float>::pi * 0.6f, MathConstants<float>::pi * 1.4f, true);
    g.setColour (ink);
    g.strokePath (smile, PathStrokeType (jmax (1.2f, size * 0.07f), PathStrokeType::curved, PathStrokeType::rounded));
}

void BounceStage::paint (Graphics& g)
{
    const float physScale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (background.isNull() || std::abs ((float) background.getWidth() - (float) getWidth() * jlimit (1.0f, 4.0f, physScale)) > 2.0f)
        rebuildBackground (physScale);

    auto b = getLocalBounds().toFloat();
    g.drawImage (background, b);

    Path clip;
    clip.addRoundedRectangle (b, 18.0f);
    g.saveState();
    g.reduceClipRegion (clip);

    // Clouds
    for (int i = 0; i < 4; ++i)
    {
        const float w = b.getWidth() + 200.0f;
        const float x = std::fmod ((float) i * 260.0f + cloudOffset * (0.6f + 0.25f * (float) i), w) - 100.0f;
        const float y = 26.0f + (float) ((i * 37) % 70);
        const float s = 0.7f + 0.15f * (float) (i % 3);
        g.setColour (Colours::white.withAlpha (0.75f));
        g.fillEllipse (x, y, 70.0f * s, 26.0f * s);
        g.fillEllipse (x + 16.0f * s, y - 14.0f * s, 38.0f * s, 34.0f * s);
        g.fillEllipse (x + 36.0f * s, y - 8.0f * s, 30.0f * s, 26.0f * s);
    }

    const float floorY = floorArea.getY() + 8.0f;
    const float toneAmt = processor.toneAmount();

    // Predicted trajectory (dotted arcs)
    if (preview.count > 0)
    {
        double start = 0.0;
        for (int k = 0; k < preview.count; ++k)
        {
            const double end = preview.time[(size_t) k];
            const float x0 = timeToX (start, preview), x1 = timeToX (end, preview);
            const float apex = apexFor (end - start, preview);
            const float alpha = 0.25f + 0.6f * preview.gain[(size_t) k];
            const int dots = jmax (4, (int) ((x1 - x0 + apex * 1.6f) / 9.0f));
            g.setColour (Colours::white.withAlpha (alpha * 0.9f));
            for (int d = 1; d < dots; ++d)
            {
                const float u = (float) d / (float) dots;
                const float x = x0 + (x1 - x0) * u;
                const float y = floorY - apex * 4.0f * u * (1.0f - u);
                g.fillEllipse (Rectangle<float> (3.0f, 3.0f).withCentre ({ x, y }));
            }
            start = end;
        }

        // Landing pads
        for (int k = 0; k < preview.count; ++k)
        {
            const float x = timeToX (preview.time[(size_t) k], preview);
            const float gn = preview.gain[(size_t) k];
            const float kk = (float) k / (float) jmax (1, preview.count - 1);
            Colour c = toneAmt < 0.0f ? sky.interpolatedWith (Colour (0xff3b2bb0), kk * -toneAmt)
                                      : sky.interpolatedWith (sunny, kk * toneAmt);
            const float flash = padFlash[(size_t) k];
            auto pad = Rectangle<float> (12.0f + 22.0f * gn, 5.0f + 4.0f * gn).withCentre ({ x, floorY + 3.0f });
            if (flash > 0.0f)
            {
                g.setColour (c.withAlpha (0.35f * flash));
                g.fillEllipse (pad.expanded (10.0f * flash, 5.0f * flash));
            }
            g.setColour (c.withAlpha (0.55f + 0.45f * jmin (1.0f, flash + gn)));
            g.fillEllipse (pad);
        }
    }

    // Ripples
    for (auto& r : ripples)
    {
        const float t = r.age / 0.7f;
        const float w = 16.0f + t * 70.0f * (0.5f + r.strength);
        g.setColour (r.colour.withAlpha ((1.0f - t) * 0.7f));
        g.drawEllipse (Rectangle<float> (w, w * 0.22f).withCentre (r.pos.translated (0.0f, 3.0f)), 2.0f);
    }

    // Dust
    for (auto& d : dust)
    {
        g.setColour (Colours::white.withAlpha (jlimit (0.0f, 1.0f, d.life) * 0.8f));
        g.fillEllipse (Rectangle<float> (d.size, d.size).withCentre (d.pos));
    }

    // Balls (shadow, trail, body)
    const Colour ballColours[] = { tomato, sunny, mint, lilac, bubblegum, sky };
    for (auto& ball : balls)
    {
        const auto colour = ballColours[(int) (ball.hue * 6.0f) % 6];
        const float fadeOut = (float) jlimit (0.0, 1.0, (ball.schedule.totalTime + 1.2 - ball.age) / 0.5);
        float hn = 0.0f;
        auto pos = positionFor (ball.schedule, ball.age, hn);

        g.setColour (ink.withAlpha (0.25f * fadeOut * (1.0f - 0.6f * hn)));
        const float sw = ball.size * (1.2f - 0.6f * hn);
        g.fillEllipse (Rectangle<float> (sw, sw * 0.28f).withCentre ({ pos.x, floorY + 2.0f }));

        for (int i = 1; i < ball.trailCount; ++i)
        {
            const float a = (1.0f - (float) i / (float) ball.trailCount) * 0.25f * fadeOut;
            const float s = ball.size * (1.0f - (float) i * 0.06f);
            g.setColour (colour.withAlpha (a));
            g.fillEllipse (Rectangle<float> (s, s).withCentre (ball.trail[(size_t) i].translated (0.0f, -s * 0.5f)));
        }

        g.setOpacity (fadeOut);
        drawBall (g, pos, ball.size, ball.squash, colour, 1.0f);
        g.setOpacity (1.0f);
    }

    // Idle resting ball on the spring so the stage never looks empty
    if (balls.empty())
    {
        const float bob = std::sin ((float) clock * 2.2f) * 2.0f;
        drawBall (g, { timeToX (0.0, preview), floorY - 6.0f + bob }, 20.0f, 0.0f, tomato, 1.0f);
    }

    g.restoreState();

    // Hint
    g.setColour (ink.withAlpha (hoverHint ? 0.75f : 0.4f));
    g.setFont (aa::Fonts::uiBold (11.0f).withExtraKerningFactor (0.08f));
    g.drawText ("CLICK THE SKY TO DROP A TEST BALL", Rectangle<float> (18.0f, 12.0f, 300.0f, 18.0f), Justification::centredLeft);

    g.setColour (Colours::white.withAlpha (0.9f));
    g.drawRoundedRectangle (b.reduced (1.0f), 18.0f, 2.0f);
}

//==============================================================================
BoingEditor::BoingEditor (BoingProcessor& p)
    : aa::EditorBase (p, baseWidth, baseHeight, makeTheme()),
      proc (p),
      time (p.apvts, "time", "Drop Time"),
      bounciness (p.apvts, "bounciness", "Bouncy"),
      bounces (p.apvts, "bounces", "Bounces"),
      damping (p.apvts, "damping", "Damping"),
      tone (p.apvts, "tone", "Tone"),
      wobble (p.apvts, "wobble", "Wobble"),
      spread (p.apvts, "spread", "Spread"),
      rethrow (p.apvts, "rethrow", "Re-throw"),
      duck (p.apvts, "duck", "Duck"),
      mix (p.apvts, "mix", "Mix"),
      sync (p.apvts, "sync", "Sync"),
      division (p.apvts, "division", "Division"),
      mode (p.apvts, "mode", "Mode", aa::ChoiceBox::Style::segmented),
      presets (p)
{
    aa::Fonts::setDisplayTypeface (PluginAssets::LuckiestGuyRegular_ttf, (size_t) PluginAssets::LuckiestGuyRegular_ttfSize);

    tone.setBipolar (true);
    time.setAccent (tomato);
    bounciness.setAccent (sunny);
    bounces.setAccent (mint);
    damping.setAccent (lilac);
    tone.setAccent (sky);
    wobble.setAccent (bubblegum);
    spread.setAccent (mint);
    rethrow.setAccent (lilac);
    duck.setAccent (sky);
    mix.setAccent (tomato);
    presets.setAccent (tomato);

    time.setTooltip ("Time until the first bounce");
    bounciness.setTooltip ("How much of each flight survives the bounce (higher = more, faster echoes)");
    bounces.setTooltip ("How many times the ball bounces");
    damping.setTooltip ("How much quieter each bounce gets");
    tone.setTooltip ("Left: every bounce gets darker. Right: every bounce gets thinner and brighter");
    wobble.setTooltip ("Springy pitch wobble on the echoes (more on later bounces)");
    spread.setTooltip ("Ping-pong the bounces left and right");
    rethrow.setTooltip ("Feeds the last bounce back in: the ball gets thrown again");
    duck.setTooltip ("Pushes the echoes down while you're playing");
    mix.setTooltip ("Dry/wet balance");

    for (auto* c : std::initializer_list<Component*> { &stage, &time, &bounciness, &bounces, &damping, &tone, &wobble,
                                                       &spread, &rethrow, &duck, &mix, &sync, &division, &mode, &presets })
        content.addAndMakeVisible (c);

    stage.onImpact = [this]
    {
        const int i = Random::getSystemRandom().nextInt ((int) letterVel.size());
        letterVel[(size_t) i] -= 90.0f;
    };

    sync.onStateChange = [this] { updateSyncState(); };
    updateSyncState();
    finishSetup();
}

void BoingEditor::updateSyncState()
{
    const bool synced = sync.getToggleState();
    time.setEnabled (! synced);
    time.setAlpha (synced ? 0.35f : 1.0f);
    division.setEnabled (synced);
    division.setAlpha (synced ? 1.0f : 0.4f);
}

void BoingEditor::layoutContent()
{
    auto area = baseBounds().toFloat();
    auto header = area.removeFromTop (64.0f);
    titleArea = header.withWidth (300.0f).translated (22.0f, 0.0f);
    presets.setBounds (Rectangle<float> (260.0f, 34.0f).withCentre ({ header.getCentreX() + 20.0f, header.getCentreY() + 2.0f }).toNearestInt());

    stage.setBounds (Rectangle<float> (16.0f, 64.0f, (float) baseWidth - 32.0f, 262.0f).toNearestInt());

    const float panelY = 338.0f, panelH = 208.0f;
    const float widths[] = { 262.0f, 196.0f, 196.0f, 170.0f };
    float x = 16.0f;
    for (size_t i = 0; i < panels.size(); ++i)
    {
        panels[i] = { x, panelY, widths[i], panelH };
        x += widths[i] + 8.0f;
    }

    auto heroAndPair = [] (Rectangle<float> panel, Component& hero, Component& a, Component& b)
    {
        auto c = panel.reduced (10.0f).withTrimmedTop (22.0f);
        auto heroArea = c.removeFromLeft (c.getWidth() * 0.56f);
        hero.setBounds (heroArea.withSizeKeepingCentre (heroArea.getWidth(), 140.0f).toNearestInt());
        a.setBounds (c.removeFromTop (c.getHeight() * 0.5f).reduced (2.0f, 2.0f).toNearestInt());
        b.setBounds (c.reduced (2.0f, 2.0f).toNearestInt());
    };

    // Panel A: drop
    {
        auto c = panels[0].reduced (10.0f).withTrimmedTop (22.0f);
        auto heroArea = c.removeFromLeft (122.0f);
        time.setBounds (heroArea.withSizeKeepingCentre (heroArea.getWidth(), 140.0f).toNearestInt());
        c.removeFromLeft (6.0f);
        mode.setBounds (c.removeFromTop (50.0f).toNearestInt());
        c.removeFromTop (12.0f);
        sync.setBounds (c.removeFromTop (24.0f).toNearestInt());
        c.removeFromTop (8.0f);
        division.setBounds (c.removeFromTop (48.0f).toNearestInt());
    }
    heroAndPair (panels[1], bounciness, bounces, damping);
    heroAndPair (panels[2], tone, wobble, spread);
    heroAndPair (panels[3], mix, rethrow, duck);
}

void BoingEditor::paintContent (Graphics& g)
{
    auto b = baseBounds().toFloat();
    g.setGradientFill (ColourGradient (theme().background, 0.0f, 0.0f, theme().backgroundAlt, 0.0f, b.getBottom(), false));
    g.fillAll();

    // playful confetti dots in the background
    Random r (42);
    for (int i = 0; i < 70; ++i)
    {
        const Colour cs[] = { tomato, sunny, mint, lilac, bubblegum, sky };
        g.setColour (cs[i % 6].withAlpha (0.18f));
        const float s = 3.0f + r.nextFloat() * 6.0f;
        g.fillEllipse (r.nextFloat() * b.getWidth(), r.nextFloat() * b.getHeight(), s, s);
    }

    // Title with bouncing letters
    const String title = "BOING!";
    const Colour letterColours[] = { tomato, sunny, mint, lilac, bubblegum, sky };
    auto font = aa::Fonts::display (40.0f);
    float x = titleArea.getX();
    for (int i = 0; i < title.length(); ++i)
    {
        const String ch = title.substring (i, i + 1);
        const float y = 50.0f + letterBounce[(size_t) i];
        fillOutlinedText (g, ch, font, { x, y }, letterColours[i % 6], ink, 5.0f);
        x += aa::Fonts::textWidth (font, ch) + 1.0f;
    }
    g.setColour (theme().textDim);
    g.setFont (aa::Fonts::ui (11.0f).withExtraKerningFactor (0.1f));
    g.drawText ("BOUNCING BALL DELAY", Rectangle<float> (x + 12.0f, 26.0f, 200.0f, 30.0f), Justification::centredLeft);

    g.setColour (theme().textDim);
    g.setFont (aa::Fonts::uiBold (11.0f).withExtraKerningFactor (0.2f));
    g.drawText ("ADVENTURE AUDIO", Rectangle<float> (b.getRight() - 230.0f, 18.0f, 210.0f, 30.0f), Justification::centredRight);

    const char* titles[] = { "Drop", "Bounce", "Colour", "Output" };
    for (size_t i = 0; i < panels.size(); ++i)
        lnf().drawPanel (g, panels[i], titles[i]);
}

void BoingEditor::onFrame (double, double dt)
{
    stage.tick (dt);

    bool moving = false;
    for (size_t i = 0; i < letterBounce.size(); ++i)
    {
        // damped spring
        const float k = 220.0f, c = 9.0f;
        letterVel[i] += (-k * letterBounce[i] - c * letterVel[i]) * (float) dt;
        letterBounce[i] += letterVel[i] * (float) dt;
        if (std::abs (letterBounce[i]) > 0.05f || std::abs (letterVel[i]) > 0.5f)
            moving = true;
    }
    if (moving)
        content.repaint (titleArea.withHeight (64.0f).expanded (8.0f).toNearestInt());
}
