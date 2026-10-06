#include "TapeDreamsEditor.h"
#include <PluginAssets.h>

using namespace juce;
namespace pal = tapeui::palette;

namespace
{
    constexpr int baseWidth = 880, baseHeight = 560;

    aa::Theme makeTheme()
    {
        aa::Theme t;
        t.background = pal::cream;
        t.backgroundAlt = Colour (0xffe9d4ab);
        t.panel = pal::creamLight;
        t.panelOutline = pal::brown.withAlpha (0.28f);
        t.text = pal::brown;
        t.textDim = pal::brown.withAlpha (0.62f);
        t.accent = pal::orange;
        t.accent2 = pal::mustard;
        t.knobBody = pal::creamLight;
        t.knobTrack = pal::brown.withAlpha (0.1f);
        t.shadow = pal::brownDark.withAlpha (0.3f);
        t.popupBackground = Colour (0xfffcf5e6);
        t.pill = Colour (0xfffcf4e2);
        t.cornerRadius = 14.0f;
        t.glow = false;
        return t;
    }

    std::vector<std::vector<Point<float>>> titleContours;
} // namespace

//==============================================================================
TapeDreamsEditor::TapeDreamsEditor (TapeDreamsProcessor& p)
    : aa::EditorBase (p, baseWidth, baseHeight, makeTheme()),
      proc (p),
      stopKey (p.apvts, "tapeStop"),
      drive (p.apvts, "drive", "Drive"),
      squash (p.apvts, "squash", "Squash"),
      wow (p.apvts, "wow", "Wow"),
      flutter (p.apvts, "flutter", "Flutter"),
      crinkle (p.apvts, "crinkle", "Crinkle"),
      age (p.apvts, "age", "Age"),
      wear (p.apvts, "wear", "Wear"),
      hiss (p.apvts, "hiss", "Hiss"),
      hum (p.apvts, "hum", "Hum"),
      mix (p.apvts, "mix", "Mix"),
      input (p.apvts, "input", "Input"),
      output (p.apvts, "output", "Output"),
      stopTime (p.apvts, "stopTime", "Stop Time"),
      mains (p.apvts, "mains", {}, aa::ChoiceBox::Style::segmented),
      presets (p)
{
    useLookAndFeel (std::make_unique<tapeui::TapeLookAndFeel> (makeTheme()));
    aa::Fonts::setDisplayTypeface (PluginAssets::PacificoRegular_ttf, (size_t) PluginAssets::PacificoRegular_ttfSize);
    aa::Fonts::setAccentTypeface (PluginAssets::PermanentMarkerRegular_ttf, (size_t) PluginAssets::PermanentMarkerRegular_ttfSize);

    drive.setAccent (pal::orange);
    squash.setAccent (pal::mustard);
    wow.setAccent (pal::teal);
    flutter.setAccent (pal::teal);
    crinkle.setAccent (pal::orange);
    age.setAccent (pal::mustard);
    wear.setAccent (pal::orange);
    hiss.setAccent (pal::teal);
    hum.setAccent (pal::mustard);
    mix.setAccent (pal::orange);
    input.setAccent (pal::mustard);
    output.setAccent (pal::mustard);
    stopTime.setAccent (pal::orange);
    mains.setAccent (pal::mustard);
    presets.setAccent (pal::orange);

    for (auto* k : { &input, &output, &stopTime })
        k->getProperties().set ("onDark", true);

    drive.setTooltip ("How hard you hit the tape: warm, rounded saturation (level-compensated)");
    squash.setTooltip ("Tape compression: glues and softens transients");
    wow.setTooltip ("Slow, drifting pitch wobble from an uneven tape transport");
    flutter.setTooltip ("Fast, fluttery pitch jitter from the capstan");
    crinkle.setTooltip ("Occasional warps, like a creased or stretched bit of tape");
    age.setTooltip ("Older tape: the highs and lows close in and the stereo narrows");
    wear.setTooltip ("Worn oxide: random dropouts where the sound dips and dulls");
    hiss.setTooltip ("Tape hiss - it breathes with the music");
    hum.setTooltip ("Mains hum from a not-quite-grounded machine");
    mix.setTooltip ("Blend the dry signal with the tape");
    input.setTooltip ("Record level into the tape (not compensated - use it to match quiet or hot sources)");
    output.setTooltip ("Playback level");
    stopTime.setTooltip ("How long the tape takes to grind to a halt (it spins back up twice as fast)");
    mains.setTooltip ("Mains frequency for the hum: 50 Hz (Europe & most of the world) or 60 Hz (Americas)");
    stopKey.setTooltip ("Tape Stop: the tape slows to a halt and spins back up when released - automate me!");

    for (auto* c : std::initializer_list<Component*> { &cassette, &inMeter, &outMeter, &recLamp, &playLamp, &stopKey,
                                                       &drive, &squash, &wow, &flutter, &crinkle, &age, &wear, &hiss,
                                                       &hum, &mix, &input, &output, &stopTime, &mains, &presets })
        content.addAndMakeVisible (c);

    cassette.onHold = [this] (bool held) { setMomentaryStop (held); };
    cassette.setTrackName (proc.getCurrentPresetName());
    proc.presetChanged.addChangeListener (this);

    // Flatten the title once; it's warped live with the tape wobble.
    if (titleContours.empty())
    {
        GlyphArrangement ga;
        ga.addLineOfText (aa::Fonts::display (56.0f), "Tape Dreams", 0.0f, 0.0f);
        Path path;
        ga.createPath (path);
        std::vector<Point<float>> current;
        for (PathFlatteningIterator it (path, {}, 0.1f); it.next();)
        {
            if (it.subPathIndex == 0)
            {
                if (current.size() > 2)
                    titleContours.push_back (current);
                current.clear();
                current.push_back ({ it.x1, it.y1 });
            }
            current.push_back ({ it.x2, it.y2 });
        }
        if (current.size() > 2)
            titleContours.push_back (current);
    }

    finishSetup();
}

TapeDreamsEditor::~TapeDreamsEditor()
{
    proc.presetChanged.removeChangeListener (this);
    if (holdActive)
        setMomentaryStop (false);
}

void TapeDreamsEditor::changeListenerCallback (ChangeBroadcaster*)
{
    cassette.setTrackName (proc.getCurrentPresetName());
}

void TapeDreamsEditor::setMomentaryStop (bool held)
{
    auto* p = proc.param ("tapeStop");
    if (p == nullptr)
        return;

    if (held && ! holdActive)
    {
        holdPrevious = p->getValue() > 0.5f;
        holdActive = true;
        p->beginChangeGesture();
        p->setValueNotifyingHost (holdPrevious ? 0.0f : 1.0f);
    }
    else if (! held && holdActive)
    {
        p->setValueNotifyingHost (holdPrevious ? 1.0f : 0.0f);
        p->endChangeGesture();
        holdActive = false;
    }
}

//==============================================================================
void TapeDreamsEditor::layoutContent()
{
    titleArea = { 14.0f, 10.0f, 250.0f, 62.0f };
    presets.setBounds (Rectangle<float> (250.0f, 32.0f).withCentre ({ 590.0f, 38.0f }).toNearestInt());

    deckArea = { 16.0f, 72.0f, 848.0f, 288.0f };
    cassette.setBounds (26, 82, 416, 274);
    cassetteWell = { 26.0f + 8.32f - 7.0f, 82.0f + 5.82f - 7.0f, 399.4f + 14.0f, 255.6f + 14.0f };

    // meters with the lamps between them
    inMeter.setBounds (464, 86, 162, 114);
    outMeter.setBounds (690, 86, 162, 114);
    recLamp.setBounds (629, 94, 58, 46);
    playLamp.setBounds (629, 148, 58, 46);

    input.setBounds (506, 212, 80, 104);
    output.setBounds (732, 212, 80, 104);
    stopKey.setBounds (599, 216, 120, 50);
    stopTime.setBounds (629, 272, 60, 80);

    const float panelY = 374.0f, panelH = 172.0f;
    const float widths[] = { 160.0f, 232.0f, 160.0f, 160.0f, 88.0f };
    float x = 16.0f;
    for (size_t i = 0; i < panels.size(); ++i)
    {
        panels[i] = { x, panelY, widths[i], panelH };
        x += widths[i] + 12.0f;
    }

    auto placeRow = [] (Rectangle<float> panel, std::initializer_list<Component*> knobs)
    {
        auto area = panel.reduced (8.0f).withTrimmedTop (24.0f).withTrimmedBottom (4.0f);
        const float slot = area.getWidth() / (float) knobs.size();
        float kx = area.getX();
        for (auto* k : knobs)
        {
            k->setBounds (Rectangle<float> (kx, area.getY(), slot, area.getHeight()).withSizeKeepingCentre (72.0f, 112.0f).toNearestInt());
            kx += slot;
        }
    };
    placeRow (panels[0], { &drive, &squash });
    placeRow (panels[1], { &wow, &flutter, &crinkle });
    placeRow (panels[2], { &age, &wear });
    placeRow (panels[3], { &hiss, &hum });
    placeRow (panels[4], { &mix });

    mains.setBounds (Rectangle<float> (panels[3].getRight() - 86.0f, panels[3].getY() + 7.0f, 78.0f, 20.0f).toNearestInt());

    background = {};
}

void TapeDreamsEditor::rebuildBackground (float scale)
{
    backgroundScale = scale;
    background = tapeui::makeLayer (baseWidth, baseHeight, scale);
    Graphics g (background);
    g.addTransform (AffineTransform::scale (scale));
    const auto b = baseBounds().toFloat();

    // Paper
    g.setGradientFill (ColourGradient (Colour (0xfff7eacd), 0.0f, 0.0f, Colour (0xffe8d3aa), 0.0f, b.getBottom(), false));
    g.fillRect (b);
    {
        Random r (1977);
        for (int i = 0; i < 2600; ++i)
        {
            const bool dark = r.nextFloat() < 0.7f;
            g.setColour ((dark ? pal::brown : Colours::white).withAlpha (0.03f + r.nextFloat() * 0.04f));
            const float s = 0.6f + r.nextFloat() * 1.2f;
            g.fillEllipse (r.nextFloat() * b.getWidth(), r.nextFloat() * b.getHeight(), s, s);
        }
    }

    // 70s stripes across the top
    {
        const Colour stripes[] = { pal::brown, pal::orange, pal::mustard, pal::teal };
        float y = 0.0f;
        for (auto c : stripes)
        {
            g.setColour (c);
            g.fillRect (0.0f, y, b.getWidth(), 3.0f);
            y += 3.0f;
        }
    }

    // Header text
    g.setColour (pal::brown.withAlpha (0.6f));
    g.setFont (aa::Fonts::uiBold (10.0f).withExtraKerningFactor (0.22f));
    g.drawText ("WARM  -  WOBBLY  -  NOSTALGIC", Rectangle<float> (232.0f, 25.0f, 200.0f, 14.0f), Justification::centredLeft, false);
    g.setColour (pal::brown.withAlpha (0.42f));
    g.setFont (aa::Fonts::ui (9.5f).withExtraKerningFactor (0.18f));
    g.drawText ("CASSETTE TAPE MACHINE", Rectangle<float> (232.0f, 39.0f, 200.0f, 14.0f), Justification::centredLeft, false);

    g.setColour (pal::brown.withAlpha (0.55f));
    g.setFont (aa::Fonts::uiBold (10.5f).withExtraKerningFactor (0.22f));
    g.drawText ("ADVENTURE AUDIO", Rectangle<float> (b.getRight() - 196.0f, 30.0f, 180.0f, 16.0f), Justification::centredRight, false);

    // Deck: walnut-brown faceplate
    {
        auto d = deckArea;
        g.setColour (pal::brownDark.withAlpha (0.18f));
        g.fillRoundedRectangle (d.translated (0.0f, 4.0f).expanded (1.0f), 20.0f);
        g.setColour (pal::brownDark.withAlpha (0.12f));
        g.fillRoundedRectangle (d.translated (0.0f, 8.0f).expanded (3.0f), 22.0f);

        g.setGradientFill (ColourGradient (Colour (0xff60442f), 0.0f, d.getY(), Colour (0xff3a281c), 0.0f, d.getBottom(), false));
        g.fillRoundedRectangle (d, 18.0f);

        Graphics::ScopedSaveState s (g);
        Path deckPath;
        deckPath.addRoundedRectangle (d, 18.0f);
        g.reduceClipRegion (deckPath);

        // wood grain
        Random r (8);
        for (int i = 0; i < 46; ++i)
        {
            Path grain;
            const float y0 = d.getY() + r.nextFloat() * d.getHeight();
            const float amp = 1.5f + r.nextFloat() * 4.0f, freq = 0.004f + r.nextFloat() * 0.01f, ph = r.nextFloat() * 6.0f;
            grain.startNewSubPath (d.getX(), y0);
            for (float gx = d.getX(); gx <= d.getRight(); gx += 8.0f)
                grain.lineTo (gx, y0 + amp * std::sin (gx * freq + ph) + 1.2f * std::sin (gx * 0.05f + ph * 2.0f));
            g.setColour ((i % 3 == 0 ? Colours::white : Colours::black).withAlpha (0.035f + r.nextFloat() * 0.03f));
            g.strokePath (grain, PathStrokeType (0.6f + r.nextFloat() * 1.4f));
        }

        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.1f), 0.0f, d.getY(), Colours::transparentWhite, 0.0f, d.getY() + 30.0f, false));
        g.fillRect (d.withHeight (30.0f));
    }
    g.setColour (Colours::white.withAlpha (0.16f));
    g.drawRoundedRectangle (deckArea.reduced (1.0f), 17.0f, 1.0f);
    g.setColour (pal::brownDark.withAlpha (0.7f));
    g.drawRoundedRectangle (deckArea, 18.0f, 1.2f);

    // Cassette well (recess the shell sits in)
    {
        auto w = cassetteWell;
        g.setColour (Colour (0xff1e140e));
        g.fillRoundedRectangle (w, 20.0f);
        g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.5f), 0.0f, w.getY(), Colours::transparentBlack, 0.0f, w.getY() + 18.0f, false));
        g.fillRoundedRectangle (w.withHeight (24.0f), 20.0f);
        g.setColour (Colours::white.withAlpha (0.12f));
        g.drawRoundedRectangle (w.expanded (1.0f), 21.0f, 1.0f);
    }

    // Model plate under the transport controls
    g.setColour (pal::cream.withAlpha (0.4f));
    g.setFont (aa::Fonts::uiBold (8.5f).withExtraKerningFactor (0.3f));
    g.drawText ("TD-77  STEREO", Rectangle<float> (486.0f, 334.0f, 120.0f, 12.0f), Justification::centred, false);
    g.drawText ("AUTO REVERSE", Rectangle<float> (712.0f, 334.0f, 120.0f, 12.0f), Justification::centred, false);

    // Little chrome screws on the deck corners
    tapeui::drawScrew (g, { deckArea.getX() + 10.0f, deckArea.getY() + 10.0f }, 3.6f, 0.5f);
    tapeui::drawScrew (g, { deckArea.getRight() - 10.0f, deckArea.getY() + 10.0f }, 3.6f, 1.2f);
    tapeui::drawScrew (g, { deckArea.getX() + 10.0f, deckArea.getBottom() - 10.0f }, 3.6f, 0.2f);
    tapeui::drawScrew (g, { deckArea.getRight() - 10.0f, deckArea.getBottom() - 10.0f }, 3.6f, 0.9f);

    // Knob panels
    const char* titles[] = { "Tape", "Wobble", "Age", "Noise", "Blend" };
    const Colour tags[] = { pal::orange, pal::teal, pal::mustard, pal::teal, pal::orange };
    for (size_t i = 0; i < panels.size(); ++i)
    {
        auto p = panels[i];
        g.setColour (pal::brownDark.withAlpha (0.1f));
        g.fillRoundedRectangle (p.translated (0.0f, 3.0f), 14.0f);
        g.setGradientFill (ColourGradient (Colour (0xfffcf5e4), 0.0f, p.getY(), Colour (0xfff4e6c8), 0.0f, p.getBottom(), false));
        g.fillRoundedRectangle (p, 14.0f);
        g.setColour (pal::brown.withAlpha (0.25f));
        g.drawRoundedRectangle (p.reduced (0.5f), 14.0f, 1.0f);

        // title with a little three-colour tag
        float tx = p.getX() + 14.0f;
        const Colour bars[] = { tags[i], pal::mustard, pal::brown };
        for (int k = 0; k < 3; ++k)
        {
            g.setColour (bars[k].withAlpha (k == 2 ? 0.5f : 1.0f));
            g.fillRoundedRectangle (tx + (float) k * 5.0f, p.getY() + 12.0f, 3.0f, 10.0f, 1.5f);
        }
        tx += 20.0f;
        g.setColour (pal::brown.withAlpha (0.85f));
        g.setFont (aa::Fonts::uiBold (11.5f).withExtraKerningFactor (0.16f));
        g.drawText (String (titles[i]).toUpperCase(), Rectangle<float> (tx, p.getY() + 9.0f, 120.0f, 16.0f), Justification::centredLeft, false);
    }
}

void TapeDreamsEditor::paintContent (Graphics& g)
{
    const float physScale = jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (background.isNull() || std::abs (physScale - backgroundScale) > 0.01f)
        rebuildBackground (physScale);

    g.drawImage (background, baseBounds().toFloat());
    paintTitle (g);
}

void TapeDreamsEditor::paintTitle (Graphics& g)
{
    if (! g.clipRegionIntersects (titleArea.toNearestInt()))
        return;

    const float x0 = titleArea.getX() + 10.0f, baseline = titleArea.getY() + 43.0f, width = 200.0f;
    Path p;
    for (const auto& contour : titleContours)
    {
        bool first = true;
        for (auto pt : contour)
        {
            const float x = pt.x + x0;
            const float u = jlimit (0.0f, 1.0f, pt.x / width);
            const float dy = wobble * std::sin (x * 0.05f - titleClock * 2.3f) + sag * 15.0f * u * u
                             + sag * 1.5f * std::sin (x * 0.11f);
            const Point<float> q (x, pt.y + baseline + dy);
            if (first)
                p.startNewSubPath (q);
            else
                p.lineTo (q);
            first = false;
        }
        p.closeSubPath();
    }

    g.setColour (pal::brown);
    g.fillPath (p, AffineTransform::translation (2.2f, 2.6f));
    g.setColour (pal::orange);
    g.fillPath (p);
    g.setColour (pal::mustard.withAlpha (0.55f));
    g.fillPath (p, AffineTransform::translation (-0.7f, -0.8f));
    g.setColour (pal::orange);
    g.fillPath (p, AffineTransform::translation (0.2f, 0.2f));
}

void TapeDreamsEditor::onFrame (double, double dt)
{
    const float t = (float) dt;
    const float speed = proc.tapeSpeed.load();
    const float motor = proc.motorSpeed.load();

    cassette.setCharacter (age.getProportion(), wear.getProportion(), hiss.getProportion(), proc.dropoutLevel.load());
    cassette.tick (dt, speed, motor);

    inMeter.setLevel (proc.inputLevel.load());
    inMeter.tick (dt);
    outMeter.setLevel (proc.outputLevel.load());
    outMeter.tick (dt);

    recLamp.setLevel (proc.recGlow.load());
    blinkClock += t;
    const float play = motor >= 0.999f ? 1.0f
                     : (motor <= 0.0f ? 0.0f : (std::fmod (blinkClock * 4.0f, 1.0f) < 0.5f ? 1.0f : 0.15f));
    playLamp.setLevel (play);

    // The title waves with the wow and droops when the tape stops.
    const float wowAmt = wow.getProportion();
    titleClock += t * (0.25f + 0.75f * motor);
    wobble += (0.3f + 2.2f * wowAmt - wobble) * jmin (1.0f, t * 3.0f);
    sag += ((1.0f - motor) - sag) * jmin (1.0f, t * 5.0f);
    content.repaint (titleArea.expanded (2.0f, 6.0f).toNearestInt());
}
