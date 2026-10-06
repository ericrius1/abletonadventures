#include "OrreryEditor.h"
#include <PluginAssets.h>

using namespace juce;
namespace pal = orrery::palette;

namespace
{
    constexpr int baseWidth = 900, baseHeight = 600;

    double frac (double x) { return x - std::floor (x); }
} // namespace

//==============================================================================
void PlanetPicker::refreshIfChanged()
{
    int mask = 0;
    for (int i = 0; i < orrery::numOrbits; ++i)
        if (proc.orbitParams[(size_t) i].on->load() > 0.5f)
            mask |= 1 << i;
    if (mask != lastOnMask)
    {
        lastOnMask = mask;
        repaint();
    }
}

void PlanetPicker::paint (Graphics& g)
{
    const float w = (float) getWidth() / (float) orrery::numOrbits;
    for (int i = 0; i < orrery::numOrbits; ++i)
    {
        const auto& pl = orrery::planets()[(size_t) i];
        const bool on = proc.orbitParams[(size_t) i].on->load() > 0.5f;
        const auto c = Point<float> (w * ((float) i + 0.5f), (float) getHeight() * 0.5f);
        const float rad = 3.6f + pl.size * 0.24f;
        auto colour = Colour (pl.colour);
        if (! on)
            colour = colour.withSaturation (0.2f).withMultipliedBrightness (0.55f);

        if (i == selected)
        {
            g.setColour (colour.withAlpha (0.22f));
            g.fillEllipse (Rectangle<float> (rad * 3.6f, rad * 3.6f).withCentre (c));
            g.setColour (pal::brassLight.withAlpha (0.9f));
            g.drawEllipse (Rectangle<float> (rad * 2.0f + 7.0f, rad * 2.0f + 7.0f).withCentre (c), 1.0f);
        }

        auto body = Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c);
        g.setGradientFill (ColourGradient (colour.brighter (0.6f), c.x - rad * 0.4f, c.y - rad * 0.4f, colour.darker (0.7f),
                                           c.x + rad, c.y + rad, true));
        g.fillEllipse (body);
        if (i == 5)
        {
            g.setColour (colour.brighter (0.3f).withAlpha (0.8f));
            g.drawEllipse (Rectangle<float> (rad * 3.4f, rad * 1.0f).withCentre (c), 1.0f);
        }
    }
}

void PlanetPicker::mouseDown (const MouseEvent& e)
{
    const int i = jlimit (0, orrery::numOrbits - 1, (int) (e.position.x / ((float) getWidth() / (float) orrery::numOrbits)));
    if (onSelect)
        onSelect (i);
}

//==============================================================================
void RhythmView::paint (Graphics& g)
{
    const auto& op = proc.orbitParams[(size_t) orbit];
    const auto colour = Colour (orrery::planets()[(size_t) orbit].colour);
    const bool on = op.on->load() > 0.5f;
    const int pulses = jlimit (1, 16, (int) std::round (op.pulses->load()));
    const int beats = jlimit (1, 16, (int) std::round (op.beats->load()));
    const float offset = op.offset->load() / 100.0f;
    auto b = getLocalBounds().toFloat();

    // ratio "3 : 4"
    auto top = b.removeFromTop (b.getHeight() * 0.5f);
    {
        const String left (pulses), right (beats);
        auto font = aa::Fonts::display (21.0f);
        const float lw = aa::Fonts::textWidth (font, left), rw = aa::Fonts::textWidth (font, right);
        const float gap = 22.0f;
        const float total = lw + gap + rw;
        const float x0 = top.getCentreX() - total * 0.5f;
        const float baseline = top.getY() + 25.0f;
        orrery::drawEngravedText (g, left, font, { x0, baseline });
        orrery::drawEngravedText (g, right, font, { x0 + lw + gap, baseline });
        g.setColour (pal::brassLight.withAlpha (0.8f));
        for (float dy : { -11.0f, -4.0f })
            g.fillEllipse (Rectangle<float> (3.0f, 3.0f).withCentre ({ x0 + lw + gap * 0.5f, baseline + dy + 1.0f }));

        g.setFont (aa::Fonts::uiBold (8.0f).withExtraKerningFactor (0.12f));
        g.setColour (pal::parchment.withAlpha (0.55f));
        g.drawText (String (pulses == 1 ? "PULSE" : "PULSES") + "  IN  " + (beats == 1 ? "BEAT" : "BEATS"),
                    top.withTop (top.getY() + 29.0f).withHeight (12.0f), Justification::centred, false);
    }

    // timeline
    auto line = b.reduced (8.0f, 0.0f).withSizeKeepingCentre (b.getWidth() - 16.0f, 20.0f).translated (0.0f, -2.0f);
    const float y = line.getCentreY();
    g.setColour (pal::ink.withAlpha (0.6f));
    g.fillRoundedRectangle (line.withSizeKeepingCentre (line.getWidth() + 10.0f, 16.0f), 8.0f);
    g.setColour (pal::brass.withAlpha (0.4f));
    g.drawRoundedRectangle (line.withSizeKeepingCentre (line.getWidth() + 10.0f, 16.0f), 8.0f, 0.8f);

    for (int k = 0; k <= beats; ++k)
    {
        const float x = line.getX() + line.getWidth() * (float) k / (float) beats;
        g.setColour (pal::parchment.withAlpha (k == 0 || k == beats ? 0.6f : 0.35f));
        g.drawLine (x, y - 6.0f, x, y + 6.0f, 1.0f);
    }

    const float phase = stage.getPlanetPhase (orbit);
    for (int k = 0; k < pulses; ++k)
    {
        const float p = (float) ((k + (double) offset) / pulses);
        const float x = line.getX() + line.getWidth() * (float) frac (p);
        float d = std::abs (phase - (float) frac (p));
        d = jmin (d, 1.0f - d);
        const float lit = on && stage.isRunning() ? jmax (0.0f, 1.0f - d * (float) pulses * 3.0f) : 0.0f;
        g.setColour (colour.withAlpha (0.25f * lit));
        g.fillEllipse (Rectangle<float> (13.0f, 13.0f).withCentre ({ x, y }));
        g.setColour (on ? colour.interpolatedWith (Colours::white, lit * 0.6f) : colour.withSaturation (0.2f).withAlpha (0.5f));
        g.fillEllipse (Rectangle<float> (k == 0 ? 6.5f : 5.0f, k == 0 ? 6.5f : 5.0f).withCentre ({ x, y }));
    }

    if (on)
    {
        const float x = line.getX() + line.getWidth() * phase;
        g.setColour (pal::parchment.withAlpha (0.85f));
        Path tri;
        tri.addTriangle (x - 3.5f, y - 11.0f, x + 3.5f, y - 11.0f, x, y - 6.0f);
        g.fillPath (tri);
        g.drawLine (x, y - 6.0f, x, y + 6.0f, 0.8f);
    }
}

//==============================================================================
void TransportReadout::tick()
{
    const int state = proc.uiRunState.load();
    const int bpb = jmax (1, proc.uiBeatsPerBar.load());
    const double ppq = stage.getVisualPpq();
    const int bar = (int) std::floor (ppq / bpb) + 1;
    const int beat = (int) std::floor (frac (ppq / bpb) * bpb) + 1;
    const String text = String (bar) + "." + String (beat) + "|" + String (roundToInt (proc.uiBpm.load()));

    const double beatIndex = std::floor (ppq);
    if (beatIndex != lastBeat)
    {
        lastBeat = beatIndex;
        if (state != OrreryProcessor::stopped)
            glow = 1.0f;
    }
    glow = jmax (0.0f, glow - 0.06f);

    if (text != lastText || state != lastState || glow > 0.0f)
    {
        lastText = text;
        lastState = state;
        repaint();
    }
}

void TransportReadout::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const float radius = b.getHeight() * 0.5f;
    g.setColour (pal::ink.withAlpha (0.55f));
    g.fillRoundedRectangle (b, radius);
    g.setColour (pal::brass.withAlpha (0.5f));
    g.drawRoundedRectangle (b.reduced (0.5f), radius, 1.0f);

    const int state = lastState;
    const Colour lamp = state == OrreryProcessor::hostPlaying ? Colour (0xff7df0a8)
                      : state == OrreryProcessor::freeRunning ? Colour (0xffffcf6a) : Colour (0xff6f7690);
    const String label = state == OrreryProcessor::hostPlaying ? "PLAYING" : state == OrreryProcessor::freeRunning ? "FREE RUN" : "STOPPED";

    auto lampArea = Rectangle<float> (9.0f, 9.0f).withCentre ({ b.getX() + radius + 2.0f, b.getCentreY() });
    g.setColour (lamp.withAlpha (0.15f + 0.35f * glow));
    g.fillEllipse (lampArea.expanded (4.0f + 2.0f * glow));
    g.setGradientFill (ColourGradient (Colours::white, lampArea.getX() + 2.0f, lampArea.getY() + 2.0f, lamp, lampArea.getRight(),
                                       lampArea.getBottom(), true));
    g.fillEllipse (lampArea);

    auto area = b.withTrimmedLeft (radius + 12.0f).withTrimmedRight (10.0f);
    g.setColour (pal::parchment.withAlpha (0.8f));
    g.setFont (aa::Fonts::uiBold (9.5f).withExtraKerningFactor (0.1f));
    g.drawText (label, area.removeFromLeft (66.0f), Justification::centredLeft, false);

    const auto barBeat = lastText.upToFirstOccurrenceOf ("|", false, false);
    const auto tempo = lastText.fromFirstOccurrenceOf ("|", false, false);
    auto tempoArea = area.removeFromRight (56.0f);
    g.setColour (pal::parchment.withAlpha (0.55f));
    g.setFont (aa::Fonts::uiBold (9.5f).withExtraKerningFactor (0.06f));
    g.drawText (tempo + " BPM", tempoArea, Justification::centredRight, false);

    g.setColour (pal::brassLight);
    g.setFont (aa::Fonts::display (13.0f));
    g.drawText (barBeat, area, Justification::centred, false);
}

//==============================================================================
void LevelGauge::tick (double dt)
{
    const float level = proc.uiLevel.load();
    const float db = aa::dsp::gainToDb (level);
    const float target = jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);
    const float coeff = target > needle ? 1.0f - std::exp ((float) -dt * 25.0f) : 1.0f - std::exp ((float) -dt * 4.0f);
    const float before = needle;
    needle += (target - needle) * coeff;
    if (std::abs (needle - before) > 0.001f)
        repaint();
}

void LevelGauge::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    auto caption = b.removeFromBottom (jlimit (12.0f, 18.0f, b.getHeight() * 0.2f));
    const float size = jmin (b.getWidth(), b.getHeight());
    const auto c = Point<float> (b.getCentreX(), b.getCentreY() + size * 0.2f);
    const float r = size * 0.46f;
    const float startA = -MathConstants<float>::pi * 0.38f, endA = MathConstants<float>::pi * 0.38f;

    // dial face
    Path face;
    face.addPieSegment (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), startA - 0.2f, endA + 0.2f, 0.0f);
    g.setGradientFill (ColourGradient (Colour (0xfff3e7c6), c.x, c.y - r, Colour (0xffcdb98a), c.x, c.y, false));
    g.fillPath (face);
    g.setColour (pal::brassDark);
    g.strokePath (face, PathStrokeType (1.2f));

    for (int i = 0; i <= 8; ++i)
    {
        const float a = startA + (endA - startA) * (float) i / 8.0f;
        const Point<float> d (std::sin (a), -std::cos (a));
        g.setColour (i >= 7 ? Colour (0xffb8432f) : pal::brassDeep);
        g.drawLine ({ c + d * (r * (i % 4 == 0 ? 0.72f : 0.8f)), c + d * (r * 0.92f) }, i % 4 == 0 ? 1.2f : 0.8f);
    }

    const float a = startA + (endA - startA) * needle;
    const Point<float> d (std::sin (a), -std::cos (a));
    g.setColour (pal::ink.withAlpha (0.85f));
    g.drawLine ({ c, c + d * (r * 0.9f) }, 1.3f);
    g.setGradientFill (ColourGradient (pal::brassLight, c.x - 2.0f, c.y - 2.0f, pal::brassDark, c.x + 3.0f, c.y + 3.0f, true));
    g.fillEllipse (Rectangle<float> (7.0f, 7.0f).withCentre (c));

    g.setFont (aa::Fonts::uiBold (jlimit (9.0f, 12.0f, caption.getHeight() * 0.72f) * 0.92f).withExtraKerningFactor (0.08f));
    g.setColour (pal::parchment.withAlpha (0.66f));
    g.drawText ("LEVEL", caption, Justification::centred, false);
}

//==============================================================================
OrreryEditor::OrreryEditor (OrreryProcessor& p)
    : aa::EditorBase (p, baseWidth, baseHeight, orrery::makeTheme()),
      proc (p),
      presets (p),
      scaleBox (p.apvts, "scale", "Scale"),
      rootBox (p.apvts, "root", "Root"),
      speedBox (p.apvts, "speed", "Speed"),
      echoTimeBox (p.apvts, "echotime", "Time"),
      freeRun (p.apvts, "freerun", "Free Run"),
      follow (p.apvts, "follow", "Follow MIDI"),
      sound (p.apvts, "sound", "Sound"),
      material (p.apvts, "material", "Material"),
      brightness (p.apvts, "bright", "Bright"),
      decay (p.apvts, "decay", "Decay"),
      spread (p.apvts, "spread", "Spread"),
      volume (p.apvts, "volume", "Volume"),
      echo (p.apvts, "echo", "Echo"),
      feedback (p.apvts, "feedback", "Repeats"),
      reverb (p.apvts, "reverb", "Reverb")
{
    useLookAndFeel (aa::makeLookAndFeel<orrery::OrreryLookAndFeel> (orrery::makeTheme()));
    aa::Fonts::setDisplayTypeface (PluginAssets::CinzelDecorativeBold_ttf, (size_t) PluginAssets::CinzelDecorativeBold_ttfSize);

    presets.setAccent (pal::brass);
    freeRun.setAccent (Colour (0xffffcf6a));
    follow.setAccent (Colour (0xff8fd3ff));
    sound.setAccent (Colour (0xff7df0a8));

    scaleBox.setTooltip ("Scale the planets pick their notes from");
    rootBox.setTooltip ("Key (root note) of the scale");
    speedBox.setTooltip ("Speeds up or slows down every orbit, still locked to the host tempo");
    echoTimeBox.setTooltip ("Echo time, synced to the host tempo");
    freeRun.setTooltip ("On: the orrery keeps turning at the host tempo while the transport is stopped. Off: it only moves while the host plays");
    follow.setTooltip ("Hold notes and the planets play from your chord instead of the scale. When off, your keys play the bell directly");
    sound.setTooltip ("The built-in celestial bell. Switch off to use Orrery purely as a MIDI generator - every note is always sent out as MIDI too");
    material.setTooltip ("Glass, wood or metal: morphs the bell's overtones and how long it rings");
    brightness.setTooltip ("Mallet hardness: soft and round to bright and sparkling");
    decay.setTooltip ("How long the bells ring");
    spread.setTooltip ("Spreads the planets across the stereo field");
    volume.setTooltip ("Output volume");
    echo.setTooltip ("Amount of tempo-synced ping-pong echo");
    feedback.setTooltip ("How many times the echo repeats");
    reverb.setTooltip ("Amount of starry reverb");

    for (auto* k : { &material, &brightness, &decay, &spread, &volume })
        k->setAccent (pal::brassLight);
    for (auto* k : { &echo, &feedback, &reverb })
        k->setAccent (Colour (0xff9fb8ff));

    for (auto* c : std::initializer_list<Component*> { &stage, &presets, &picker, &rhythm, &readout, &gauge, &scaleBox, &rootBox,
                                                       &speedBox, &echoTimeBox, &freeRun, &follow, &sound, &material, &brightness,
                                                       &decay, &spread, &volume, &echo, &feedback, &reverb })
        content.addAndMakeVisible (c);

    stage.onSelect = [this] (int i) { selectOrbit (i); };
    picker.onSelect = [this] (int i) { selectOrbit (i); };

    finishSetup();
    selectOrbit (proc.selectedOrbit);
}

OrreryEditor::~OrreryEditor() = default;

void OrreryEditor::selectOrbit (int orbit)
{
    orbit = jlimit (0, orrery::numOrbits - 1, orbit);
    proc.selectedOrbit = orbit;
    const auto colour = Colour (orrery::planets()[(size_t) orbit].colour);

    auto make = [&] (std::unique_ptr<orrery::OrreryKnob>& knob, const char* suffix, const String& label, const String& tip)
    {
        if (knob != nullptr)
            content.removeChildComponent (knob.get());
        knob = std::make_unique<orrery::OrreryKnob> (proc.apvts, orrery::orbitParamId (orbit, suffix), label);
        knob->setAccent (colour);
        knob->setTooltip (tip);
        content.addAndMakeVisible (*knob);
    };

    make (beatsKnob, "beats", "Beats", "How many beats one revolution of this planet takes");
    make (pulsesKnob, "pulses", "Pulses", "How many evenly spaced marks sit on this orbit - 3 pulses in 4 beats plays 3 against 4");
    make (offsetKnob, "offset", "Offset", "Rotates the marks along the orbit, as a fraction of the gap between them (50% = off-beat)");
    make (noteKnob, "note", "Note", "Scale step this planet plays (with Follow MIDI it steps through the chord you hold)");
    make (octaveKnob, "octave", "Octave", "Moves this planet's note up or down in octaves");
    make (velocityKnob, "vel", "Velocity", "How hard this planet strikes (loudness and MIDI velocity)");
    make (chanceKnob, "prob", "Chance", "Probability that each pulse plays - lower values let the pattern breathe and sparkle");
    make (gateKnob, "gate", "Gate", "Note length as a fraction of the gap between pulses (also damps the bell)");

    auto* bk = beatsKnob.get();
    auto* pk = pulsesKnob.get();
    auto* ok = octaveKnob.get();
    bk->centreText = [bk] { return String (roundToInt (bk->getValue())); };
    pk->centreText = [pk] { return String (roundToInt (pk->getValue())); };
    ok->centreText = [ok]
    {
        const int v = roundToInt (ok->getValue());
        return (v > 0 ? "+" : "") + String (v);
    };
    noteKnob->centreText = [this, orbit] { return orrery::noteName (proc.currentNoteForOrbit (orbit)); };
    lastNoteShown = -1;

    if (onToggle != nullptr)
        content.removeChildComponent (onToggle.get());
    onToggle = std::make_unique<orrery::JewelToggle> (proc.apvts, orrery::orbitParamId (orbit, "on"), "On");
    onToggle->setAccent (colour);
    onToggle->setTooltip ("Switch this planet on or off (or double-click it in the orrery)");
    content.addAndMakeVisible (*onToggle);

    layoutPlanetPanel();
    stage.setSelected (orbit);
    picker.setSelected (orbit);
    rhythm.setOrbit (orbit);
    content.repaint (planetTitleArea.expanded (4.0f).toNearestInt());
}

void OrreryEditor::layoutContent()
{
    stage.setBounds (10, 64, 528, 528);

    presets.setBounds (Rectangle<int> (232, 32).withCentre ({ 404, 31 }));
    readout.setBounds (558, 16, 196, 30);

    const float x0 = 550.0f, w = 338.0f;
    planetPanel = { x0, 66.0f, w, 238.0f };
    cosmosPanel = { x0, 312.0f, w, 86.0f };
    soundPanel = { x0, 406.0f, w, 186.0f };

    // Planet panel
    {
        auto c = planetPanel.reduced (12.0f, 8.0f);
        auto titleRow = c.removeFromTop (26.0f);
        titleRow.removeFromRight (6.0f);
        auto pickerArea = titleRow.removeFromRight (126.0f);
        picker.setBounds (pickerArea.toNearestInt());
        titleRow.removeFromRight (6.0f);
        onToggleArea = titleRow.removeFromRight (56.0f).reduced (0.0f, 2.0f).toNearestInt();
        planetTitleArea = titleRow;

        c.removeFromTop (6.0f);
        const float cellW = c.getWidth() / 5.0f;
        auto row1 = c.removeFromTop (88.0f);
        for (int k = 0; k < 5; ++k)
            planetKnobCells[(size_t) k] = Rectangle<float> (row1.getX() + cellW * (float) k, row1.getY(), cellW, row1.getHeight()).reduced (2.0f, 0.0f).toNearestInt();
        c.removeFromTop (4.0f);
        auto row2 = c.removeFromTop (88.0f);
        for (int k = 0; k < 3; ++k)
            planetKnobCells[(size_t) k + 5] = Rectangle<float> (row2.getX() + cellW * (float) k, row2.getY(), cellW, row2.getHeight()).reduced (2.0f, 0.0f).toNearestInt();
        rhythm.setBounds (Rectangle<float> (row2.getX() + cellW * 3.0f, row2.getY(), cellW * 2.0f, row2.getHeight()).reduced (4.0f, 2.0f).toNearestInt());
    }

    // Cosmos panel: the two switches live in the title row
    {
        auto c = cosmosPanel.reduced (12.0f, 8.0f);
        auto titleRow = c.removeFromTop (20.0f);
        follow.setBounds (titleRow.removeFromRight (98.0f).toNearestInt());
        titleRow.removeFromRight (6.0f);
        freeRun.setBounds (titleRow.removeFromRight (86.0f).toNearestInt());
        cosmosRuleEnd = titleRow.getRight() - 8.0f;
        c.removeFromTop (4.0f);
        auto row = c.removeFromTop (44.0f);
        scaleBox.setBounds (row.removeFromLeft (146.0f).toNearestInt());
        row.removeFromLeft (8.0f);
        rootBox.setBounds (row.removeFromLeft (76.0f).toNearestInt());
        row.removeFromLeft (8.0f);
        speedBox.setBounds (row.toNearestInt());
    }

    // Sound panel
    {
        auto c = soundPanel.reduced (12.0f, 8.0f);
        auto titleRow = c.removeFromTop (20.0f);
        sound.setBounds (titleRow.removeFromRight (78.0f).toNearestInt());
        soundRuleEnd = titleRow.getRight() - 8.0f;
        c.removeFromTop (4.0f);
        const float cellW = c.getWidth() / 5.0f;
        auto row1 = c.removeFromTop (76.0f);
        int k = 0;
        for (auto* comp : std::initializer_list<Component*> { &material, &brightness, &decay, &spread, &volume })
            comp->setBounds (Rectangle<float> (row1.getX() + cellW * (float) k++, row1.getY(), cellW, row1.getHeight()).reduced (2.0f, 0.0f).toNearestInt());
        c.removeFromTop (2.0f);
        auto row2 = c.removeFromTop (76.0f);
        k = 0;
        for (auto* comp : std::initializer_list<Component*> { &echo, &echoTimeBox, &feedback, &reverb, &gauge })
        {
            auto cell = Rectangle<float> (row2.getX() + cellW * (float) k++, row2.getY(), cellW, row2.getHeight()).reduced (2.0f, 0.0f);
            if (comp == &echoTimeBox)
                cell = cell.withSizeKeepingCentre (cell.getWidth(), 42.0f).translated (0.0f, -4.0f);
            comp->setBounds (cell.toNearestInt());
        }
    }

    layoutPlanetPanel();
    background = {};
}

void OrreryEditor::layoutPlanetPanel()
{
    orrery::OrreryKnob* knobs[] = { beatsKnob.get(), pulsesKnob.get(), offsetKnob.get(), noteKnob.get(), octaveKnob.get(),
                                    velocityKnob.get(), chanceKnob.get(), gateKnob.get() };
    for (size_t k = 0; k < 8; ++k)
        if (knobs[k] != nullptr)
            knobs[k]->setBounds (planetKnobCells[k]);
    if (onToggle != nullptr)
        onToggle->setBounds (onToggleArea);
}

//==============================================================================
void OrreryEditor::rebuildBackground (float scale)
{
    backgroundScale = scale;
    background = Image (Image::ARGB, roundToInt ((float) baseWidth * scale), roundToInt ((float) baseHeight * scale), true);
    Graphics g (background);
    g.addTransform (AffineTransform::scale (scale));
    const auto b = baseBounds().toFloat();

    // Deep navy sky
    g.setGradientFill (ColourGradient (pal::navyTop, 0.0f, 0.0f, pal::navyBottom, b.getRight(), b.getBottom(), false));
    g.fillAll();

    // Nebulae
    struct Blob { float x, y, r; uint32 colour; };
    const Blob blobs[] = { { 120.0f, 520.0f, 300.0f, 0x2c6a3cc8 }, { 820.0f, 90.0f, 260.0f, 0x1c2f8fb0 },
                           { 470.0f, 260.0f, 220.0f, 0x163a5bd0 }, { 700.0f, 560.0f, 240.0f, 0x1ec04a9a } };
    for (auto& bl : blobs)
    {
        g.setGradientFill (ColourGradient (Colour (bl.colour), bl.x, bl.y, Colour (bl.colour).withAlpha (0.0f), bl.x + bl.r, bl.y, true));
        g.fillEllipse (Rectangle<float> (bl.r * 2.0f, bl.r * 2.0f).withCentre ({ bl.x, bl.y }));
    }

    // Stars, denser along a faint milky way
    Random r (2024);
    for (int i = 0; i < 420; ++i)
    {
        float x = r.nextFloat() * b.getWidth();
        float y = r.nextFloat() * b.getHeight();
        if (i % 2 == 0)
        {
            const float t = r.nextFloat();
            x = t * b.getWidth();
            y = b.getHeight() * (0.95f - 0.8f * t) + (r.nextFloat() - 0.5f) * 120.0f * (0.6f + r.nextFloat());
        }
        const float s = 0.5f + std::pow (r.nextFloat(), 3.0f) * 2.0f;
        g.setColour (pal::parchment.withAlpha (0.15f + 0.55f * r.nextFloat()));
        g.fillEllipse (Rectangle<float> (s, s).withCentre ({ x, y }));
    }
    for (int i = 0; i < 14; ++i)
        orrery::drawSparkle (g, { r.nextFloat() * b.getWidth(), 70.0f + r.nextFloat() * (b.getHeight() - 70.0f) }, 2.5f + r.nextFloat() * 3.5f,
                             pal::parchment.withAlpha (0.35f + 0.3f * r.nextFloat()));

    // Header: engraved title, subtitle and maker's mark
    orrery::drawEngravedText (g, "Orrery", aa::Fonts::display (34.0f), { 22.0f, 40.0f }, 0.02f);
    g.setFont (aa::Fonts::uiBold (8.5f).withExtraKerningFactor (0.22f));
    g.setColour (pal::parchment.withAlpha (0.6f));
    g.drawText ("POLYRHYTHMIC PLANET SEQUENCER", Rectangle<float> (24.0f, 44.0f, 260.0f, 12.0f), Justification::centredLeft, false);

    g.setFont (aa::Fonts::uiBold (10.0f).withExtraKerningFactor (0.2f));
    g.setColour (pal::brass.withAlpha (0.7f));
    g.drawText ("ADVENTURE AUDIO", Rectangle<float> (b.getRight() - 140.0f, 16.0f, 124.0f, 30.0f), Justification::centredRight, false);

    // header rule
    {
        ColourGradient rule (pal::brass.withAlpha (0.0f), 16.0f, 0.0f, pal::brass.withAlpha (0.0f), b.getRight() - 16.0f, 0.0f, false);
        rule.addColour (0.2, pal::brass.withAlpha (0.35f));
        rule.addColour (0.8, pal::brass.withAlpha (0.35f));
        g.setGradientFill (rule);
        g.fillRect (Rectangle<float> (16.0f, 60.0f, b.getWidth() - 32.0f, 0.8f));
    }

    // Panels
    lnf().drawPanel (g, planetPanel, {});
    lnf().drawPanel (g, cosmosPanel, {});
    lnf().drawPanel (g, soundPanel, {});
    orrery::OrreryLookAndFeel::drawPanelTitle (g, cosmosPanel.reduced (12.0f, 8.0f).withHeight (20.0f), "Cosmos", cosmosRuleEnd);
    orrery::OrreryLookAndFeel::drawPanelTitle (g, soundPanel.reduced (12.0f, 8.0f).withHeight (20.0f), "Sound", soundRuleEnd);

    // divider between the voice and space rows of the sound panel
    {
        const float y = soundPanel.getY() + 8.0f + 20.0f + 4.0f + 76.0f + 1.0f;
        ColourGradient div (pal::brass.withAlpha (0.0f), soundPanel.getX() + 14.0f, y, pal::brass.withAlpha (0.0f), soundPanel.getRight() - 14.0f, y, false);
        div.addColour (0.5, pal::brass.withAlpha (0.3f));
        g.setGradientFill (div);
        g.fillRect (Rectangle<float> (soundPanel.getX() + 14.0f, y, soundPanel.getWidth() - 28.0f, 0.8f));
    }
}

void OrreryEditor::paintContent (Graphics& g)
{
    const float scale = jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (background.isNull() || std::abs (backgroundScale - scale) > 0.01f)
        rebuildBackground (scale);
    g.drawImage (background, baseBounds().toFloat());

    if (g.clipRegionIntersects (planetTitleArea.expanded (4.0f).toNearestInt()))
    {
        const int o = proc.selectedOrbit;
        const auto& pl = orrery::planets()[(size_t) o];
        auto area = planetTitleArea;
        auto font = aa::Fonts::display (13.0f);
        auto numeralArea = area.removeFromLeft (aa::Fonts::textWidth (font, pl.numeral) + 8.0f);
        orrery::drawEngravedText (g, pl.numeral, font, { numeralArea.getX() + 3.0f, area.getCentreY() + 5.0f });
        g.setColour (Colour (pl.colour));
        g.fillEllipse (Rectangle<float> (4.0f, 4.0f).withCentre ({ numeralArea.getRight() + 1.0f, area.getCentreY() }));
        area.removeFromLeft (8.0f);
        orrery::drawEngravedText (g, String (pl.name).toUpperCase(), aa::Fonts::display (16.0f),
                                  { area.getX(), area.getCentreY() + 6.0f }, 0.06f);
    }
}

void OrreryEditor::paintContentOver (Graphics&) {}

void OrreryEditor::onFrame (double, double dt)
{
    stage.tick (dt);
    rhythm.repaint();
    readout.tick();
    gauge.tick (dt);
    picker.refreshIfChanged();

    if (noteKnob != nullptr)
    {
        const int n = proc.currentNoteForOrbit (proc.selectedOrbit);
        if (n != lastNoteShown)
        {
            lastNoteShown = n;
            noteKnob->repaint();
        }
    }
}
