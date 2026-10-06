#include "GremlinEditor.h"
#include <PluginAssets.h>

using namespace juce;
using namespace gremlin;

namespace
{
    constexpr int baseWidth = 880, baseHeight = 560;

    aa::Theme makeTheme()
    {
        aa::Theme t;
        t.background = pal::bg;
        t.backgroundAlt = pal::bgAlt;
        t.panel = Colour (0xff1a1228);
        t.panelOutline = pal::purple.withAlpha (0.3f);
        t.text = pal::text;
        t.textDim = pal::textDim;
        t.accent = pal::green;
        t.accent2 = pal::pink;
        t.knobBody = Colour (0xff2a1f3d);
        t.knobTrack = Colour (0xff0b0713);
        t.shadow = Colours::black.withAlpha (0.6f);
        t.popupBackground = Colour (0xf4150e22);
        t.pill = Colour (0xff0b0713);
        t.cornerRadius = 12.0f;
        t.glow = true;
        return t;
    }

    const Fx faderFx[] = { Fx::stutter, Fx::reverse, Fx::tapeStop, Fx::halfSpeed, Fx::crush, Fx::gate, Fx::scatter };
    const char* faderLabels[] = { "Stutter", "Reverse", "Tape Stop", "Half Spd", "Crush", "Gate", "Scatter" };
    const char* faderTips[] = {
        "Stutter odds: loop a slice from the start of the step (Slice sets the size, Ratchet pitches each repeat)",
        "Reverse odds: play the step that just went by backwards",
        "Tape Stop odds: the tape grinds to a halt (at least an eighth note long)",
        "Half Speed odds: play the step an octave down at half speed",
        "Bitcrush odds: a burst of low bit depth and sample-rate grit (Crush Depth sets how nasty)",
        "Gate odds: chop the step into rhythmic stabs (Slice sets the chop size)",
        "Scatter odds: replay a random step from the last couple of bars",
    };

    const Fx padFx[] = { Fx::stutter, Fx::reverse, Fx::tapeStop, Fx::halfSpeed };
    const char* padLabels[] = { "Stutter", "Reverse", "Tape Stop", "Half Speed" };
} // namespace

//==============================================================================
GremlinEditor::GremlinEditor (GremlinProcessor& p)
    : aa::EditorBase (p, baseWidth, baseHeight, makeTheme()),
      proc (p),
      chaos (p.apvts, "chaos", "Chaos"),
      mix (p.apvts, "mix", "Mix"),
      ratchet (p.apvts, "ratchet", "Ratchet"),
      crush (p.apvts, "crush", "Crush"),
      smooth (p.apvts, "smooth", "Smooth"),
      grid (p.apvts, "grid", "Grid", aa::ChoiceBox::Style::segmented),
      lock (p.apvts, "lock", "Lock", aa::ChoiceBox::Style::segmented),
      slice (p.apvts, "slice", {}, aa::ChoiceBox::Style::segmented),
      seed (p.apvts, "seed"),
      presets (p)
{
    useLookAndFeel (aa::makeLookAndFeel<GremlinLookAndFeel> (makeTheme()));
    aa::Fonts::setDisplayTypeface (PluginAssets::RubikGlitchRegular_ttf, (size_t) PluginAssets::RubikGlitchRegular_ttfSize);
    aa::Fonts::setAccentTypeface (PluginAssets::VT323Regular_ttf, (size_t) PluginAssets::VT323Regular_ttfSize);

    chaos.getProperties().set ("hero", true);
    chaos.setAccent (pal::green);
    mix.setAccent (pal::cyan);
    ratchet.setAccent (pal::green);
    ratchet.setBipolar (true);
    crush.setAccent (pal::yellow);
    smooth.setAccent (pal::purple);
    grid.setAccent (pal::green);
    lock.setAccent (pal::purple);
    lock.setShortNames ({ "FREE", "1 BAR", "2 BAR", "4 BAR" });
    slice.setAccent (pal::green);
    slice.setShortNames ({ "1/2", "1/4", "1/8", "1/16", "DICE", "ACCEL" });
    presets.setAccent (pal::green);

    chaos.setTooltip ("Chaos: the chance that the gremlin messes with any given step. Its grin gets wider as you turn it up");
    mix.setTooltip ("Mix: dry/wet. At 100% the gremlin replaces the audio, lower blends the clean signal underneath");
    ratchet.setTooltip ("Ratchet: pitch each stutter repeat up (or down) by this many semitones for rising rolls");
    crush.setTooltip ("Crush Depth: how many bits and how much sample rate the bitcrush bursts take away");
    smooth.setTooltip ("Smooth: crossfade length at every slice and effect boundary. Low = snappy and clicky-tight, high = soft");
    grid.setTooltip ("Grid: how often the gremlin rolls its dice (locked to the host tempo)");
    lock.setTooltip ("Lock: repeat the same glitch pattern every 1, 2 or 4 bars (pick the pattern with Seed). Free = never the same twice");
    slice.setTooltip ("Slice: stutter repeat / gate chop size as a fraction of the step. Dice = random, Accel = accelerating roll");

    for (int i = 0; i < numDiceFx; ++i)
    {
        auto f = std::make_unique<FxFader> (p.apvts, GremlinProcessor::weightIDs()[i], faderFx[i], faderLabels[i]);
        f->setTooltip (faderTips[i]);
        content.addAndMakeVisible (*f);
        faders.push_back (std::move (f));
    }

    for (int i = 0; i < 4; ++i)
    {
        auto pad = std::make_unique<ForcePad> (p.apvts, GremlinProcessor::forceIDs()[i], padFx[i], padLabels[i]);
        pad->setTooltip (String ("Force ") + padLabels[i] + ": hold to make the gremlin do it from the next grid step, overriding the dice "
                         "(shift-click to latch). Map it or automate it for performances");
        content.addAndMakeVisible (*pad);
        pads.push_back (std::move (pad));
    }

    for (auto* c : std::initializer_list<Component*> { &face, &timeline, &chaos, &mix, &ratchet, &crush, &smooth, &grid,
                                                       &lock, &slice, &seed, &presets })
        content.addAndMakeVisible (c);

    finishSetup();
}

//==============================================================================
void GremlinEditor::layoutContent()
{
    titleArea = { 20.0f, 6.0f, 236.0f, 46.0f };
    titlePath.clear();
    presets.setBounds (452, 13, 250, 32);

    face.setBounds (16, 62, 320, 272);

    mischiefPanel = { 346.0f, 62.0f, 518.0f, 156.0f };
    forcePanel = { 346.0f, 226.0f, 518.0f, 108.0f };
    timeline.setBounds (16, 342, 848, 54);
    dicePanel = { 16.0f, 404.0f, 536.0f, 144.0f };
    tweakPanel = { 560.0f, 404.0f, 304.0f, 144.0f };

    // mischief: chaos | grid + lock | seed + mix
    {
        auto c = mischiefPanel.reduced (12.0f, 9.0f).withTrimmedTop (18.0f);
        chaos.setBounds (c.removeFromLeft (132.0f).toNearestInt());
        c.removeFromLeft (12.0f);
        auto col = c.removeFromLeft (214.0f);
        grid.setBounds (col.removeFromTop (54.0f).reduced (0.0f, 2.0f).toNearestInt());
        col.removeFromTop (8.0f);
        lock.setBounds (col.removeFromTop (54.0f).reduced (0.0f, 2.0f).toNearestInt());
        c.removeFromLeft (14.0f);
        seed.setBounds (c.removeFromTop (50.0f).toNearestInt());
        c.removeFromTop (6.0f);
        mix.setBounds (c.toNearestInt());
    }

    // force pads
    {
        auto c = forcePanel.reduced (12.0f, 9.0f).withTrimmedTop (18.0f);
        const float w = (c.getWidth() - 3.0f * 10.0f) / 4.0f;
        for (size_t i = 0; i < pads.size(); ++i)
            pads[i]->setBounds (Rectangle<float> (c.getX() + (float) i * (w + 10.0f), c.getY(), w, c.getHeight()).toNearestInt());
    }

    // dice faders
    {
        auto c = dicePanel.reduced (10.0f, 9.0f).withTrimmedTop (18.0f);
        const float w = c.getWidth() / (float) faders.size();
        for (size_t i = 0; i < faders.size(); ++i)
            faders[i]->setBounds (Rectangle<float> (c.getX() + (float) i * w, c.getY(), w, c.getHeight()).toNearestInt());
    }

    // tweaks
    {
        auto c = tweakPanel.reduced (12.0f, 9.0f).withTrimmedTop (18.0f);
        slice.setBounds (c.removeFromTop (30.0f).toNearestInt());
        c.removeFromTop (8.0f);
        aa::layoutRow (c.toNearestInt(), { &ratchet, &crush, &smooth }, 6);
    }

    background = {};
}

//==============================================================================
void GremlinEditor::rebuildBackground (float scale)
{
    backgroundScale = scale;
    background = Image (Image::RGB, roundToInt ((float) baseWidth * scale), roundToInt ((float) baseHeight * scale), false);
    Graphics g (background);
    g.addTransform (AffineTransform::scale (scale));
    auto b = baseBounds().toFloat();

    g.setGradientFill (ColourGradient (pal::bg, 0.0f, 0.0f, pal::bgAlt, 0.0f, b.getBottom(), false));
    g.fillRect (b);

    // faint glitch confetti
    Random r (1337);
    const Colour cs[] = { pal::green, pal::pink, pal::cyan, pal::purple };
    for (int i = 0; i < 46; ++i)
    {
        const float x = r.nextFloat() * b.getWidth();
        const float y = r.nextFloat() * b.getHeight();
        g.setColour (cs[i % 4].withAlpha (0.035f + r.nextFloat() * 0.04f));
        g.fillRect (x, y, 8.0f + r.nextFloat() * 60.0f, 1.0f + (float) r.nextInt (3));
    }

    // pixel dot grid
    g.setColour (Colours::white.withAlpha (0.025f));
    for (float y = 4.0f; y < b.getHeight(); y += 12.0f)
        for (float x = 4.0f; x < b.getWidth(); x += 12.0f)
            g.fillRect (x, y, 1.0f, 1.0f);

    // header glow + rule
    g.setGradientFill (ColourGradient (pal::green.withAlpha (0.08f), 130.0f, 28.0f, pal::green.withAlpha (0.0f), 330.0f, 28.0f, true));
    g.fillRect (0.0f, 0.0f, 360.0f, 56.0f);
    g.setGradientFill (ColourGradient (pal::purple.withAlpha (0.0f), 16.0f, 0.0f, pal::purple.withAlpha (0.35f), b.getCentreX(), 0.0f, false));
    g.fillRect (16.0f, 55.0f, b.getCentreX() - 16.0f, 1.0f);
    g.setGradientFill (ColourGradient (pal::purple.withAlpha (0.35f), b.getCentreX(), 0.0f, pal::purple.withAlpha (0.0f), b.getRight() - 16.0f, 0.0f, false));
    g.fillRect (b.getCentreX(), 55.0f, b.getCentreX() - 16.0f, 1.0f);

    // subtitle + brand
    g.setFont (aa::Fonts::accent (19.0f));
    g.setColour (pal::pink.withAlpha (0.9f));
    g.drawText ("TEMPO-SYNCED", Rectangle<float> (262.0f, 11.0f, 160.0f, 18.0f), Justification::centredLeft, false);
    g.setColour (pal::cyan.withAlpha (0.9f));
    g.drawText ("GLITCH GOBLIN", Rectangle<float> (262.0f, 28.0f, 160.0f, 18.0f), Justification::centredLeft, false);

    g.setColour (pal::textDim);
    g.setFont (aa::Fonts::uiBold (11.0f).withExtraKerningFactor (0.2f));
    g.drawText ("ADVENTURE AUDIO", Rectangle<float> (b.getRight() - 200.0f, 14.0f, 184.0f, 30.0f), Justification::centredRight, false);

    auto& l = lnf();
    l.drawPanel (g, mischiefPanel, "Mischief", pal::green);
    l.drawPanel (g, forcePanel, "Force pads  -  hold to unleash", pal::pink);
    l.drawPanel (g, dicePanel, "The dice  -  odds per trick", pal::cyan);
    l.drawPanel (g, tweakPanel, "Slice  &  tweaks", pal::yellow);

    g.setFont (aa::Fonts::accent (15.0f));
    g.setColour (pal::textDim.withAlpha (0.55f));
    g.drawText ("SHIFT-CLICK TO LATCH", forcePanel.reduced (12.0f, 7.0f).withHeight (16.0f), Justification::centredRight, false);

    // tiny legend in the dice panel title row
    g.setFont (aa::Fonts::accent (15.0f));
    g.setColour (pal::textDim.withAlpha (0.55f));
    g.drawText ("CHAOS DECIDES IF, THE DICE DECIDE WHAT", dicePanel.reduced (12.0f, 7.0f).withHeight (16.0f),
                Justification::centredRight, false);
}

void GremlinEditor::paintTitle (Graphics& g)
{
    if (titlePath.isEmpty())
    {
        auto font = aa::Fonts::display (47.0f);
        GlyphArrangement ga;
        const float baseline = titleArea.getCentreY() + (font.getAscent() - font.getDescent()) * 0.5f;
        ga.addLineOfText (font, "GREMLIN", titleArea.getX(), baseline);
        ga.createPath (titlePath);
    }

    // soft neon halo
    for (auto [w, a] : { std::pair { 18.0f, 0.025f }, std::pair { 11.0f, 0.04f }, std::pair { 5.0f, 0.06f } })
    {
        g.setColour (pal::green.withAlpha (a * (1.0f + titleGlitch)));
        g.strokePath (titlePath, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded));
    }

    const float split = 1.0f + 4.5f * titleGlitch;
    const int bands = (int) titleSlices.size();
    const float bh = titleArea.getHeight() / (float) bands;
    const auto glyphs = titlePath.getBounds();

    for (int i = 0; i < bands; ++i)
    {
        Graphics::ScopedSaveState s (g);
        auto band = Rectangle<float> (titleArea.getX() - 30.0f, titleArea.getY() + bh * (float) i, titleArea.getWidth() + 60.0f, bh + 0.6f);
        if (i == 0)
            band = band.withTop (band.getY() - 20.0f);
        if (i == bands - 1)
            band = band.withBottom (band.getBottom() + 20.0f);
        g.reduceClipRegion (band.toNearestInt());

        const float off = titleSlices[(size_t) i] * titleGlitch * 10.0f;
        g.setColour (pal::pink.withAlpha (0.85f));
        g.fillPath (titlePath, AffineTransform::translation (off - split, 0.0f));
        g.setColour (pal::cyan.withAlpha (0.85f));
        g.fillPath (titlePath, AffineTransform::translation (off + split, 0.0f));
        g.setGradientFill (ColourGradient (Colour (0xffd8ff9a), 0.0f, glyphs.getY(), pal::green, 0.0f,
                                           glyphs.getBottom(), false));
        g.fillPath (titlePath, AffineTransform::translation (off, 0.0f));

        g.reduceClipRegion (titlePath, AffineTransform::translation (off, 0.0f));
        drawScanlines (g, glyphs.expanded (12.0f, 2.0f), 3.0f, 0.14f + 0.2f * titleGlitch);
    }
}

void GremlinEditor::paintContent (Graphics& g)
{
    const float scale = jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (background.isNull() || std::abs (scale - backgroundScale) > 0.01f)
        rebuildBackground (scale);

    g.drawImage (background, baseBounds().toFloat());
    if (g.getClipBounds().intersects (titleArea.expanded (30.0f, 10.0f).toNearestInt()))
        paintTitle (g);
}

//==============================================================================
void GremlinEditor::onFrame (double, double dt)
{
    const float fdt = (float) dt;
    auto& eng = proc.engine;

    StepEvent e;
    while (eng.events.pop (e))
    {
        face.onStep (e);
        timeline.addEvent (e);
        if (e.fx > 0)
        {
            const int idx = e.fx - 1;
            if (isPositiveAndBelow (idx, (int) faders.size()))
                faders[(size_t) idx]->flash (1.0f);
            if (random.nextFloat() < 0.35f)
                titleGlitch = 1.0f;
        }
    }

    face.tick (dt);
    timeline.tick (dt);

    const auto active = (Fx) eng.activeFx.load();
    for (size_t i = 0; i < faders.size(); ++i)
    {
        faders[i]->tick (fdt);
        faders[i]->setLive (active == faderFx[i]);
    }
    for (size_t i = 0; i < pads.size(); ++i)
        pads[i]->tick (fdt, active == padFx[i]);

    seed.tick (fdt);
    seed.setDimmed (proc.lockBars() == 0);

    // the title glitches now and then (and whenever the gremlin strikes)
    titleTimer -= fdt;
    if (titleTimer <= 0.0f)
    {
        titleTimer = 2.5f + random.nextFloat() * 4.0f;
        titleGlitch = jmax (titleGlitch, 0.7f);
    }
    if (titleGlitch > 0.0f)
    {
        for (auto& s : titleSlices)
            s = random.nextFloat() < 0.5f ? 0.0f : random.nextFloat() * 2.0f - 1.0f;
        titleGlitch = jmax (0.0f, titleGlitch - fdt * 5.0f);
        if (titleGlitch <= 0.0f)
            titleSlices.fill (0.0f);
        content.repaint (titleArea.expanded (24.0f, 4.0f).toNearestInt());
    }
}
