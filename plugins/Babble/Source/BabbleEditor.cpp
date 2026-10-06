#include "BabbleEditor.h"
#include <PluginAssets.h>

using namespace juce;
using namespace babble::ui::palette;

namespace
{
    constexpr int baseWidth = 880, baseHeight = 580;
    const Colour gold { 0xfff2a93b };

    aa::Theme makeTheme()
    {
        aa::Theme t;
        t.background = Colour (0xffffd2b2);
        t.backgroundAlt = Colour (0xff6b3391);
        t.panel = cream;
        t.panelOutline = ink;
        t.text = ink;
        t.textDim = ink.withAlpha (0.65f);
        t.accent = coral;
        t.accent2 = teal;
        t.knobBody = cream;
        t.knobTrack = ink.withAlpha (0.14f);
        t.shadow = ink.withAlpha (0.3f);
        t.popupBackground = cream;
        t.pill = cream.withAlpha (0.92f);
        t.cornerRadius = 18.0f;
        t.glow = false;
        return t;
    }

    void drawCloud (Graphics& g, Point<float> c, float s, Colour col)
    {
        g.setColour (col);
        g.fillEllipse (c.x - 40.0f * s, c.y - 10.0f * s, 80.0f * s, 24.0f * s);
        g.fillEllipse (c.x - 24.0f * s, c.y - 26.0f * s, 36.0f * s, 34.0f * s);
        g.fillEllipse (c.x - 2.0f * s, c.y - 20.0f * s, 30.0f * s, 26.0f * s);
    }
} // namespace

//==============================================================================
BabbleEditor::BabbleEditor (BabbleProcessor& p)
    : aa::EditorBase (p, baseWidth, baseHeight, makeTheme()),
      proc (p),
      babbleKnob (p.apvts, "babble", "Babble"),
      babbleRate (p.apvts, "babbleRate", "Rate"),
      vibRate (p.apvts, "vibRate", "Rate"),
      vibDepth (p.apvts, "vibDepth", "Depth"),
      vibDelay (p.apvts, "vibDelay", "Delay"),
      breath (p.apvts, "breath", "Breath"),
      bright (p.apvts, "bright", "Bright"),
      detune (p.apvts, "detune", "Detune"),
      attack (p.apvts, "attack", "Attack"),
      release (p.apvts, "release", "Release"),
      glide (p.apvts, "glide", "Glide"),
      chorus (p.apvts, "chorus", "Chorus"),
      reverb (p.apvts, "reverb", "Reverb"),
      volume (p.apvts, "volume", "Volume"),
      babbleSync (p.apvts, "babbleSync", "Sync"),
      babbleDiv (p.apvts, "babbleDiv", ""),
      presets (p),
      mouthReadout ({}, 11.0f, true)
{
    useLookAndFeel (std::make_unique<babble::ui::BabbleLookAndFeel> (makeTheme()));
    aa::Fonts::setDisplayTypeface (PluginAssets::ChewyRegular_ttf, (size_t) PluginAssets::ChewyRegular_ttfSize);

    vowelValue = p.apvts.getRawParameterValue ("vowel");
    shiftValue = p.apvts.getRawParameterValue ("shift");

    babbleKnob.setAccent (coral);
    babbleRate.setAccent (teal);
    vibRate.setAccent (plum);
    vibDepth.setAccent (coral);
    vibDelay.setAccent (teal);
    breath.setAccent (teal);
    bright.setAccent (gold);
    detune.setAccent (plum);
    attack.setAccent (coral);
    release.setAccent (teal);
    glide.setAccent (plum);
    chorus.setAccent (teal);
    reverb.setAccent (plum);
    volume.setAccent (coral);
    babbleSync.setAccent (teal);
    babbleDiv.setAccent (teal);
    presets.setAccent (coral);
    mouthReadout.setColour (ink.withAlpha (0.6f));
    mouthReadout.setJustification (Justification::centredRight);

    babbleKnob.setTooltip ("Babble: turns held notes into gibberish singing. Higher = wilder vowels and crunchier consonants");
    babbleRate.setTooltip ("Babble rate: how many syllables per second");
    babbleSync.setTooltip ("Lock the syllables to the host tempo");
    babbleDiv.setTooltip ("Syllable length when synced to tempo");
    vibRate.setTooltip ("Vibrato speed");
    vibDepth.setTooltip ("Vibrato depth (the mod wheel adds more)");
    vibDelay.setTooltip ("How long a note is held before the vibrato blooms in");
    breath.setTooltip ("Breathiness: mixes air into the voice. All the way up for a whisper");
    bright.setTooltip ("Brightness: from a soft, covered tone to a buzzy, cutting one");
    detune.setTooltip ("How out of tune (and how wide) the choir singers are");
    attack.setTooltip ("Fade-in time of each note");
    release.setTooltip ("Fade-out time after you let go");
    glide.setTooltip ("Portamento: slide between notes");
    chorus.setTooltip ("Ensemble chorus for extra width");
    reverb.setTooltip ("Concert-hall reverb");
    volume.setTooltip ("Output volume");

    for (auto* c : std::initializer_list<Component*> { &stage, &mouthPad, &voicePicker, &choirPicker, &babbleKnob, &babbleRate,
                                                       &vibRate, &vibDepth, &vibDelay, &breath, &bright, &detune, &attack,
                                                       &release, &glide, &chorus, &reverb, &volume, &babbleSync, &babbleDiv,
                                                       &presets, &mouthReadout })
        content.addAndMakeVisible (c);

    stage.onNote = [this] (float velocity)
    {
        const int i = Random::getSystemRandom().nextInt ((int) letterVel.size());
        letterVel[(size_t) i] -= 60.0f + 80.0f * velocity;
    };

    babbleSync.onStateChange = [this] { updateSyncState(); };
    updateSyncState();
    finishSetup();
}

void BabbleEditor::updateSyncState()
{
    const bool synced = babbleSync.getToggleState();
    babbleRate.setEnabled (! synced);
    babbleRate.setAlpha (synced ? 0.4f : 1.0f);
    babbleDiv.setEnabled (synced);
    babbleDiv.setAlpha (synced ? 1.0f : 0.45f);
}

//==============================================================================
void BabbleEditor::layoutContent()
{
    panels.clear();
    titleArea = { 18.0f, 6.0f, 330.0f, 56.0f };
    presets.setBounds (Rectangle<float> (250.0f, 34.0f).withCentre ({ 470.0f, 33.0f }).toNearestInt());

    const float rowY = 70.0f, rowH = 326.0f;
    const Rectangle<float> mouthPanel (16.0f, rowY, 222.0f, rowH);
    const Rectangle<float> stageArea (248.0f, rowY, 384.0f, rowH);
    const Rectangle<float> babblePanel (642.0f, rowY, 222.0f, 158.0f);
    const Rectangle<float> vibratoPanel (642.0f, rowY + 168.0f, 222.0f, 158.0f);
    panels.push_back ({ mouthPanel, "Mouth" });
    panels.push_back ({ babblePanel, "Babble" });
    panels.push_back ({ vibratoPanel, "Vibrato" });

    stage.setBounds (stageArea.toNearestInt());

    auto contentOf = [] (Rectangle<float> panel) { return panel.reduced (12.0f, 9.0f).withTrimmedTop (20.0f); };

    // Mouth: XY pad + voice picker
    {
        auto c = contentOf (mouthPanel);
        mouthReadout.setBounds (Rectangle<float> (mouthPanel.getRight() - 132.0f, mouthPanel.getY() + 9.0f, 118.0f, 18.0f).toNearestInt());
        mouthPad.setBounds (c.removeFromTop (216.0f).toNearestInt());
        c.removeFromTop (6.0f);
        voicePicker.setBounds (c.toNearestInt());
    }

    // Babble
    {
        auto c = contentOf (babblePanel);
        auto knobs = c.removeFromTop (88.0f);
        babbleKnob.setBounds (knobs.removeFromLeft (knobs.getWidth() * 0.5f).toNearestInt());
        babbleRate.setBounds (knobs.toNearestInt());
        c.removeFromTop (4.0f);
        auto row = c.removeFromTop (28.0f);
        babbleSync.setBounds (row.removeFromLeft (84.0f).reduced (2.0f, 3.0f).toNearestInt());
        row.removeFromLeft (4.0f);
        babbleDiv.setBounds (row.toNearestInt());
    }

    // Vibrato
    aa::layoutRow (contentOf (vibratoPanel).withSizeKeepingCentre (198.0f, 100.0f).toNearestInt(), { &vibRate, &vibDepth, &vibDelay }, 0);

    // Bottom row
    const float bottomY = 406.0f, bottomH = 158.0f;
    const float widths[] = { 169.0f, 169.0f, 243.0f, 243.0f };
    const char* titles[] = { "Tone", "Choir", "Shape", "Output" };
    float x = 16.0f;
    std::array<Rectangle<float>, 4> bottom;
    for (size_t i = 0; i < 4; ++i)
    {
        bottom[i] = { x, bottomY, widths[i], bottomH };
        panels.push_back ({ bottom[i], titles[i] });
        x += widths[i] + 8.0f;
    }

    auto knobRow = [&] (Rectangle<float> panel, std::initializer_list<Component*> comps)
    {
        auto c = contentOf (panel);
        aa::layoutRow (c.withSizeKeepingCentre (c.getWidth(), 104.0f).toNearestInt(), comps, 0);
    };
    knobRow (bottom[0], { &breath, &bright });
    knobRow (bottom[1], { &choirPicker, &detune });
    knobRow (bottom[2], { &attack, &release, &glide });
    knobRow (bottom[3], { &chorus, &reverb, &volume });

    background = {};
}

//==============================================================================
void BabbleEditor::rebuildBackground (float scale)
{
    scale = jlimit (1.0f, 4.0f, scale);
    background = Image (Image::ARGB, roundToInt (baseWidth * scale), roundToInt (baseHeight * scale), true);
    Graphics g (background);
    g.addTransform (AffineTransform::scale (scale));

    auto b = baseBounds().toFloat();
    ColourGradient sky (Colour (0xffffdcbf), 0.0f, 0.0f, Colour (0xff6b3391), 0.0f, b.getBottom(), false);
    sky.addColour (0.42, Colour (0xffffa48c));
    sky.addColour (0.68, Colour (0xffd9677e));
    g.setGradientFill (sky);
    g.fillRect (b);

    // a big low sun peeking out behind everything
    const Point<float> sun (b.getCentreX(), b.getBottom() - 150.0f);
    for (int i = 5; i > 0; --i)
    {
        g.setColour (sunshine.withAlpha (0.07f));
        g.fillEllipse (Rectangle<float> (520.0f + (float) i * 60.0f, 520.0f + (float) i * 60.0f).withCentre (sun));
    }

    drawCloud (g, { 760.0f, 40.0f }, 0.9f, Colours::white.withAlpha (0.35f));
    drawCloud (g, { 610.0f, 22.0f }, 0.6f, Colours::white.withAlpha (0.25f));

    // brand
    g.setColour (ink.withAlpha (0.6f));
    g.setFont (aa::Fonts::uiBold (11.0f).withExtraKerningFactor (0.2f));
    g.drawText ("ADVENTURE AUDIO", Rectangle<float> (b.getRight() - 230.0f, 18.0f, 212.0f, 30.0f), Justification::centredRight);

    for (const auto& panel : panels)
        lnf().drawPanel (g, panel.bounds, panel.title, {});
}

void BabbleEditor::drawTitle (Graphics& g)
{
    const String title = "Babble";
    const Colour fills[] = { coral, teal, sunshine, coral, teal, sunshine };
    auto font = aa::Fonts::display (46.0f);
    float x = titleArea.getX() + 4.0f;
    const float baseline = 48.0f;

    for (int i = 0; i < title.length(); ++i)
    {
        const String ch = title.substring (i, i + 1);
        const float wob = std::sin (titleClock * 7.0f + (float) i * 0.9f) * 3.0f * titleEnergy;
        const float y = baseline + letterY[(size_t) i] + wob;
        const float cw = aa::Fonts::textWidth (font, ch);
        Graphics::ScopedSaveState save (g);
        g.addTransform (AffineTransform::rotation ((letterY[(size_t) i] * 0.012f) + wob * 0.02f, x + cw * 0.5f, y - 16.0f));
        babble::ui::drawOutlinedText (g, ch, font, { x + 1.5f, y + 3.5f }, ink.withAlpha (0.35f), ink.withAlpha (0.0f), 0.01f);
        babble::ui::drawOutlinedText (g, ch, font, { x, y }, fills[i], ink, 6.0f);
        x += cw - 1.0f;
    }

    g.setColour (ink.withAlpha (0.62f));
    g.setFont (aa::Fonts::uiBold (10.5f).withExtraKerningFactor (0.14f));
    g.drawText ("SINGING FORMANT SYNTH", Rectangle<float> (x + 14.0f, 22.0f, 200.0f, 30.0f), Justification::centredLeft);
}

void BabbleEditor::paintContent (Graphics& g)
{
    const float physScale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (background.isNull() || std::abs ((float) background.getWidth() - baseWidth * jlimit (1.0f, 4.0f, physScale)) > 2.0f)
        rebuildBackground (physScale);

    g.drawImage (background, baseBounds().toFloat());
    drawTitle (g);
}

void BabbleEditor::onFrame (double, double dt)
{
    stage.tick (dt);
    mouthPad.tick (dt);
    choirPicker.tick (dt);

    const auto readout = babble::vowelPositionText (vowelValue->load()) + "  /  "
                         + aa::params::formatValue (shiftValue->load(), aa::params::Unit::semitones);
    if (readout != lastReadout)
    {
        lastReadout = readout;
        mouthReadout.setText (readout);
    }

    // title letters: springy hops on note-ons, a gentle wave while singing
    const float fdt = (float) dt;
    titleClock += fdt;
    titleEnergy += (stage.getSinging() - titleEnergy) * (1.0f - std::exp (-fdt / 0.3f));
    bool moving = titleEnergy > 0.005f;
    for (size_t i = 0; i < letterY.size(); ++i)
    {
        letterVel[i] += (-260.0f * letterY[i] - 10.0f * letterVel[i]) * fdt;
        letterY[i] += letterVel[i] * fdt;
        moving = moving || std::abs (letterY[i]) > 0.05f || std::abs (letterVel[i]) > 0.5f;
    }
    if (moving)
        content.repaint (titleArea.expanded (6.0f).toNearestInt());
}
