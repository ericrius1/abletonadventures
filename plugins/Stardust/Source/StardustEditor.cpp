#include "StardustEditor.h"
#include <PluginAssets.h>

using namespace juce;
using namespace stardust::ui;

namespace
{
    constexpr int baseWidth = 920, baseHeight = 600;
    constexpr float knobW = 52.0f, knobH = 66.0f;

    aa::Theme makeTheme()
    {
        aa::Theme t;
        t.background = navy;
        t.backgroundAlt = violet;
        t.panel = Colours::white.withAlpha (0.06f);
        t.panelOutline = Colours::white.withAlpha (0.14f);
        t.text = Colour (0xffeef0ff);
        t.textDim = Colour (0xffc8c4f2).withAlpha (0.72f);
        t.accent = cyan;
        t.accent2 = magenta;
        t.knobBody = Colour (0xff2b2560);
        t.knobTrack = Colours::white.withAlpha (0.08f);
        t.shadow = Colours::black.withAlpha (0.55f);
        t.popupBackground = Colour (0xf4141233);
        t.pill = Colours::black.withAlpha (0.3f);
        t.cornerRadius = 14.0f;
        t.glow = true;
        return t;
    }

    Rectangle<float> panelContent (Rectangle<float> p)
    {
        return p.reduced (10.0f, 0.0f).withTrimmedTop (32.0f).withTrimmedBottom (8.0f);
    }

    void placeKnob (Component& c, Rectangle<float> cell, float w = knobW, float h = knobH)
    {
        c.setBounds (cell.withSizeKeepingCentre (w, h).toNearestInt());
    }

    void placeRow (Rectangle<float> row, std::initializer_list<Component*> comps)
    {
        const float w = row.getWidth() / (float) comps.size();
        for (auto* c : comps)
            placeKnob (*c, row.removeFromLeft (w));
    }

    float displayForShape (float shape, float p)
    {
        return stardust::osc::morph (jlimit (0.0f, 3.0f, shape), p, 0.0005f);
    }
} // namespace

//==============================================================================
// StardustLook
//==============================================================================
void StardustLook::drawKnob (Graphics& g, Rectangle<float> bounds, float proportion, bool bipolar, bool hovered,
                             bool dragging, Colour accent, aa::Knob&)
{
    const float size = jmin (bounds.getWidth(), bounds.getHeight());
    const auto c = bounds.getCentre();
    const float r = size * 0.5f - 1.5f;

    const float startA = -MathConstants<float>::pi * 0.75f;
    const float endA = MathConstants<float>::pi * 0.75f;
    const float valueA = startA + proportion * (endA - startA);
    const float arcR = r * 0.86f;
    const float arcW = jmax (2.2f, r * 0.1f);
    const bool active = hovered || dragging;

    // track
    {
        Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startA, endA, true);
        g.setColour (Colours::white.withAlpha (0.08f));
        g.strokePath (track, PathStrokeType (arcW, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // value arc
    const float fromA = bipolar ? 0.0f : startA;
    const auto arcColour = active ? accent.brighter (0.25f) : accent;
    drawGlowArc (g, c, arcR, jmin (fromA, valueA), jmax (fromA, valueA), arcW, arcColour, true);

    // orb body
    const float bodyR = r * 0.62f;
    const auto body = Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c);
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillEllipse (body.expanded (2.0f).translated (0.0f, 1.5f));

    ColourGradient grad (Colour (active ? 0xff5a4ba8 : 0xff4a3d8f), c.x - bodyR * 0.4f, c.y - bodyR * 0.55f,
                         Colour (0xff0e0b2a), c.x + bodyR * 0.55f, c.y + bodyR * 0.85f, true);
    grad.addColour (0.55, Colour (0xff241b56));
    g.setGradientFill (grad);
    g.fillEllipse (body);

    // reflected glow of the arc colour in the lower half
    g.setGradientFill (ColourGradient (accent.withAlpha (0.12f + 0.22f * proportion), c.x, c.y + bodyR * 0.8f,
                                       accent.withAlpha (0.0f), c.x, c.y - bodyR * 0.2f, false));
    g.fillEllipse (body);

    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.42f), c.x, body.getY(),
                                       Colours::white.withAlpha (0.03f), c.x, body.getBottom(), false));
    g.drawEllipse (body.reduced (0.5f), 1.0f);

    g.setColour (Colours::white.withAlpha (0.1f));
    g.fillEllipse (body.getX() + bodyR * 0.42f, body.getY() + bodyR * 0.16f, bodyR * 0.8f, bodyR * 0.42f);

    // pointer
    const Point<float> dir (std::sin (valueA), -std::cos (valueA));
    g.setColour (Colours::white.withAlpha (0.9f));
    g.drawLine ({ c + dir * (bodyR * 0.2f), c + dir * (bodyR * 0.74f) }, jmax (1.6f, bodyR * 0.1f));

    // a little star riding the tip of the arc
    drawSparkle (g, c + dir * arcR, arcW * (active ? 2.6f : 2.1f), accent.interpolatedWith (Colours::white, 0.35f), 1.0f);
}

void StardustLook::drawKnobCaption (Graphics& g, Rectangle<float> area, const String& text, bool showingValue,
                                    Colour accent, aa::Knob&)
{
    const float h = jlimit (9.0f, 11.5f, area.getHeight() * 0.82f);
    if (showingValue)
    {
        g.setFont (aa::Fonts::uiBold (h));
        g.setColour (accent.interpolatedWith (Colours::white, 0.45f));
        g.drawFittedText (text, area.toNearestInt(), Justification::centred, 1, 0.75f);
    }
    else
    {
        g.setFont (aa::Fonts::ui (h).withExtraKerningFactor (0.06f));
        g.setColour (theme().textDim);
        g.drawFittedText (text.toUpperCase(), area.toNearestInt(), Justification::centred, 1, 0.75f);
    }
}

void StardustLook::drawLinearSlider (Graphics& g, int x, int y, int width, int height, float sliderPos,
                                     float minSliderPos, float maxSliderPos, Slider::SliderStyle style, Slider& slider)
{
    if (style != Slider::LinearVertical)
    {
        aa::LookAndFeel::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const auto accent = slider.findColour (Slider::trackColourId);
    const bool active = slider.isMouseOverOrDragging();
    auto r = Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    auto track = r.withSizeKeepingCentre (4.0f, r.getHeight() + 6.0f);

    g.setColour (Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (track, 2.0f);
    g.setColour (Colours::white.withAlpha (0.07f));
    g.drawRoundedRectangle (track.expanded (0.5f), 2.5f, 1.0f);

    auto filled = track.withTop (sliderPos);
    g.setColour (accent.withAlpha (0.18f));
    g.fillRoundedRectangle (filled.expanded (2.5f, 1.0f), 4.0f);
    g.setGradientFill (ColourGradient (accent.interpolatedWith (Colours::white, 0.25f), 0.0f, filled.getY(),
                                       accent.withAlpha (0.35f), 0.0f, filled.getBottom(), false));
    g.fillRoundedRectangle (filled, 2.0f);

    const Point<float> thumb (track.getCentreX(), sliderPos);
    g.setColour (accent.withAlpha (active ? 0.4f : 0.25f));
    g.fillEllipse (Rectangle<float> (16.0f, 16.0f).withCentre (thumb));
    g.setColour (Colour (0xff1a1440));
    g.fillEllipse (Rectangle<float> (10.0f, 10.0f).withCentre (thumb));
    g.setColour (active ? Colours::white : accent.interpolatedWith (Colours::white, 0.5f));
    g.drawEllipse (Rectangle<float> (10.0f, 10.0f).withCentre (thumb), 1.6f);
    g.fillEllipse (Rectangle<float> (3.5f, 3.5f).withCentre (thumb));
}

//==============================================================================
// Envelope faders + graph
//==============================================================================
EnvSlider::EnvSlider (AudioProcessorValueTreeState& state, const String& paramID, Colour accent, const String& tooltip)
    : Slider (Slider::LinearVertical, Slider::NoTextBox)
{
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (state, paramID, *this);
    setColour (Slider::trackColourId, accent);
    setScrollWheelEnabled (true);
    setSliderSnapsToMousePosition (false);
    setVelocityBasedMode (false);
    setMouseDragSensitivity (180);
    setTooltip (tooltip);
    setRepaintsOnMouseActivity (true);
    if (auto* p = state.getParameter (paramID))
    {
        setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        stageName = p->getName (32).fromFirstOccurrenceOf (" ", false, false);
    }
}

EnvGroup::EnvGroup (StardustProcessor& p, const String& t, const StringArray& ids, const StringArray& tips,
                    std::atomic<float>& liveLevel, Colour a)
    : processor (p), title (t), accent (a), live (liveLevel)
{
    for (int i = 0; i < 4; ++i)
    {
        auto* s = sliders.add (new EnvSlider (p.apvts, ids[i], a, tips[i]));
        addAndMakeVisible (s);
        params[(size_t) i] = p.param (ids[i]);
    }
}

void EnvGroup::resized()
{
    auto b = getLocalBounds().toFloat();
    labelArea = b.removeFromTop (14.0f);
    b.removeFromTop (4.0f);
    graphArea = b.removeFromTop (38.0f);
    b.removeFromTop (6.0f);
    letterArea = b.removeFromBottom (13.0f);
    const float w = b.getWidth() / 4.0f;
    for (int i = 0; i < 4; ++i)
        sliders[i]->setBounds (b.withX (b.getX() + w * (float) i).withWidth (w).toNearestInt());
}

void EnvGroup::tick()
{
    shownLevel += 0.5f * (live.load (std::memory_order_relaxed) - shownLevel);
    float signature = std::round (shownLevel * 200.0f) * 5.0f;
    for (int i = 0; i < 4; ++i)
        signature += params[(size_t) i]->getValue() * (float) (i + 3) * 7.31f + (sliders[i]->isMouseOverOrDragging() ? 50.0f * (float) (i + 1) : 0.0f);
    if (std::abs (signature - lastSignature) > 0.01f)
    {
        lastSignature = signature;
        repaint();
    }
}

void EnvGroup::paint (Graphics& g)
{
    const auto& th = aa::themeFor (*this);

    // label row: title, or the hovered stage's value
    int hovered = -1;
    for (int i = 0; i < 4; ++i)
        if (sliders[i]->isMouseOverOrDragging())
            hovered = i;

    if (hovered >= 0)
    {
        auto* p = params[(size_t) hovered];
        g.setFont (aa::Fonts::uiBold (10.0f));
        g.setColour (accent.interpolatedWith (Colours::white, 0.4f));
        g.drawFittedText (sliders[hovered]->getStageName().toUpperCase() + "  " + p->getCurrentValueAsText(),
                          labelArea.toNearestInt(), Justification::centredLeft, 1, 0.7f);
    }
    else
    {
        g.setFont (aa::Fonts::display (10.0f).withExtraKerningFactor (0.12f));
        g.setColour (accent.withAlpha (0.9f));
        g.drawText (title.toUpperCase(), labelArea, Justification::centredLeft);
    }

    // envelope drawing
    auto ga = graphArea;
    g.setColour (Colours::black.withAlpha (0.25f));
    g.fillRoundedRectangle (ga, 6.0f);
    g.setColour (Colours::white.withAlpha (0.06f));
    g.drawRoundedRectangle (ga.reduced (0.5f), 6.0f, 1.0f);

    auto plot = ga.reduced (5.0f, 5.0f);
    const float a = params[0]->convertFrom0to1 (params[0]->getValue()) * 0.001f;
    const float d = params[1]->convertFrom0to1 (params[1]->getValue()) * 0.001f;
    const float s = params[2]->convertFrom0to1 (params[2]->getValue()) * 0.01f;
    const float r = params[3]->convertFrom0to1 (params[3]->getValue()) * 0.001f;

    const float wa = std::sqrt (a), wd = std::sqrt (d), wr = std::sqrt (r), hold = 0.9f;
    const float total = wa + wd + wr + hold;
    const float sx = plot.getWidth() / total;
    const float x0 = plot.getX(), top = plot.getY(), bottom = plot.getBottom();
    const float sy = bottom - s * plot.getHeight();
    const float xa = x0 + wa * sx, xd = xa + wd * sx, xs = xd + hold * sx, xr = xs + wr * sx;

    Path curve;
    curve.startNewSubPath (x0, bottom);
    curve.quadraticTo (x0 + (xa - x0) * 0.45f, top + (bottom - top) * 0.15f, xa, top);
    curve.quadraticTo (xa + (xd - xa) * 0.25f, sy, xd, sy);
    curve.lineTo (xs, sy);
    curve.quadraticTo (xs + (xr - xs) * 0.25f, bottom, xr, bottom);

    Path fill (curve);
    fill.lineTo (x0, bottom);
    fill.closeSubPath();
    g.setGradientFill (ColourGradient (accent.withAlpha (0.32f), 0.0f, top, accent.withAlpha (0.02f), 0.0f, bottom, false));
    g.fillPath (fill);
    g.setColour (accent.withAlpha (0.2f));
    g.strokePath (curve, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (accent.interpolatedWith (Colours::white, 0.3f));
    g.strokePath (curve, PathStrokeType (1.4f, PathStrokeType::curved, PathStrokeType::rounded));

    // live level of the newest voice
    if (shownLevel > 0.002f)
    {
        const float ly = bottom - jlimit (0.0f, 1.0f, shownLevel) * plot.getHeight();
        g.setColour (Colours::white.withAlpha (0.25f));
        g.fillRect (Rectangle<float> (plot.getX(), ly - 0.5f, plot.getWidth(), 1.0f));
        drawSparkle (g, { plot.getX() + 2.5f, ly }, 5.0f, accent.interpolatedWith (Colours::white, 0.4f), 1.0f);
    }

    // stage letters
    static const char* letters[] = { "A", "D", "S", "R" };
    g.setFont (aa::Fonts::uiBold (9.5f));
    for (int i = 0; i < 4; ++i)
    {
        const auto sb = sliders[i]->getBounds().toFloat();
        g.setColour (i == hovered ? accent.interpolatedWith (Colours::white, 0.4f) : th.textDim);
        g.drawText (letters[i], Rectangle<float> (sb.getX(), letterArea.getY(), sb.getWidth(), letterArea.getHeight()),
                    Justification::centred);
    }
}

//==============================================================================
// Wave preview / LFO glyph / meter
//==============================================================================
WavePreview::WavePreview (std::atomic<float>& s, Colour a) : shape (s), accent (a)
{
    setInterceptsMouseClicks (false, false);
}

void WavePreview::tick()
{
    const float v = shape.load (std::memory_order_relaxed);
    if (std::abs (v - shown) > 0.002f)
    {
        shown = v;
        repaint();
    }
}

void WavePreview::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (2.0f, 3.0f);
    Path p;
    constexpr int n = 72;
    for (int i = 0; i <= n; ++i)
    {
        const float t = (float) i / (float) n;
        const float v = displayForShape (shown, std::fmod (t * 2.0f, 1.0f));
        const float x = b.getX() + t * b.getWidth();
        const float y = b.getCentreY() - v * b.getHeight() * 0.45f;
        if (i == 0) p.startNewSubPath (x, y);
        else p.lineTo (x, y);
    }
    g.setColour (accent.withAlpha (0.18f));
    g.strokePath (p, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (accent.interpolatedWith (Colours::white, 0.25f));
    g.strokePath (p, PathStrokeType (1.4f, PathStrokeType::curved, PathStrokeType::rounded));
}

LfoGlyph::LfoGlyph (StardustProcessor& p, Colour a) : processor (p), accent (a)
{
    shapeParam = p.apvts.getRawParameterValue ("lfoShape");
    setInterceptsMouseClicks (false, false);
}

void LfoGlyph::tick()
{
    phase = processor.engine.lfoPhaseOut.load (std::memory_order_relaxed);
    repaint();
}

void LfoGlyph::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (3.0f, 3.0f);
    const int shape = shapeParam != nullptr ? (int) shapeParam->load() : 0;
    static const float randomSteps[] = { 0.3f, -0.7f, 0.9f, -0.2f, 0.55f, -0.9f, 0.1f, 0.75f };

    auto valueAt = [shape] (float ph)
    {
        switch (shape)
        {
            case 0: return std::sin (ph * MathConstants<float>::twoPi);
            case 1: return ph < 0.25f ? 4.0f * ph : (ph < 0.75f ? 2.0f - 4.0f * ph : 4.0f * ph - 4.0f);
            case 2: return 1.0f - 2.0f * ph;
            case 3: return ph < 0.5f ? 1.0f : -1.0f;
            case 4: return randomSteps[jlimit (0, 7, (int) (ph * 4.0f))];
            default:
            {
                const float x = ph * 4.0f;
                const int i = jlimit (0, 3, (int) x);
                const float f = x - (float) i;
                const float s = f * f * (3.0f - 2.0f * f);
                return randomSteps[i] + (randomSteps[i + 1] - randomSteps[i]) * s;
            }
        }
    };

    Path p;
    constexpr int n = 64;
    for (int i = 0; i <= n; ++i)
    {
        const float t = (float) i / (float) n;
        const float x = b.getX() + t * b.getWidth();
        const float y = b.getCentreY() - valueAt (jmin (t, 0.9999f)) * b.getHeight() * 0.45f;
        if (i == 0) p.startNewSubPath (x, y);
        else p.lineTo (x, y);
    }
    g.setColour (accent.withAlpha (0.16f));
    g.strokePath (p, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (accent.withAlpha (0.75f));
    g.strokePath (p, PathStrokeType (1.3f, PathStrokeType::curved, PathStrokeType::rounded));

    const float ph = jlimit (0.0f, 0.9999f, phase);
    const Point<float> dot (b.getX() + ph * b.getWidth(), b.getCentreY() - valueAt (ph) * b.getHeight() * 0.45f);
    drawSparkle (g, dot, 6.5f, accent.interpolatedWith (Colours::white, 0.5f), 1.0f);
}

StereoMeter::StereoMeter (StardustProcessor& p) : processor (p)
{
    setTooltip ("Output level (left / right)");
}

void StereoMeter::tick (float dt)
{
    auto follow = [dt] (float& shown, float& hold, float& holdTime, float target)
    {
        shown = target > shown ? target : shown + (target - shown) * jmin (1.0f, dt * 6.0f);
        if (shown >= hold) { hold = shown; holdTime = 0.0f; }
        else if ((holdTime += dt) > 0.9f) hold = jmax (shown, hold - dt * 0.8f);
    };
    auto toNorm = [] (float lin) { return jlimit (0.0f, 1.0f, (aa::dsp::gainToDb (lin) + 48.0f) / 48.0f); };
    follow (levelL, holdL, holdTimeL, toNorm (processor.outputLevelL.load (std::memory_order_relaxed)));
    follow (levelR, holdR, holdTimeR, toNorm (processor.outputLevelR.load (std::memory_order_relaxed)));
    repaint();
}

void StereoMeter::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const auto& th = aa::themeFor (*this);
    auto labels = b.removeFromBottom (11.0f);
    const float barH = 6.0f, gap = 5.0f;
    auto bars = b.withSizeKeepingCentre (b.getWidth(), barH * 2.0f + gap);

    for (int ch = 0; ch < 2; ++ch)
    {
        auto bar = bars.removeFromTop (barH);
        bars.removeFromTop (gap);
        g.setColour (Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (bar, barH * 0.5f);

        const float v = ch == 0 ? levelL : levelR;
        const float hold = ch == 0 ? holdL : holdR;
        if (v > 0.001f)
        {
            ColourGradient grad (cyan, bar.getX(), 0.0f, gold, bar.getRight(), 0.0f, false);
            grad.addColour (0.7, magenta);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (bar.withWidth (jmax (barH, bar.getWidth() * v)), barH * 0.5f);
        }
        if (hold > 0.01f)
        {
            g.setColour (Colours::white.withAlpha (0.85f));
            g.fillRect (Rectangle<float> (bar.getX() + bar.getWidth() * hold - 1.0f, bar.getY() - 1.0f, 2.0f, barH + 2.0f));
        }
    }

    g.setFont (aa::Fonts::ui (8.5f));
    g.setColour (th.textDim.withMultipliedAlpha (0.8f));
    for (auto [db, text] : { std::pair { -48.0f, "-48" }, std::pair { -24.0f, "-24" }, std::pair { -12.0f, "-12" }, std::pair { 0.0f, "0 dB" } })
    {
        const float x = labels.getX() + labels.getWidth() * (db + 48.0f) / 48.0f;
        auto area = Rectangle<float> (x - 20.0f, labels.getY(), 40.0f, labels.getHeight());
        auto just = Justification::centred;
        if (db == 0.0f) { area = area.withRightX (labels.getRight()); just = Justification::centredRight; }
        if (db == -48.0f) { area = area.withX (labels.getX()); just = Justification::centredLeft; }
        g.drawText (text, area, just);
    }
}

//==============================================================================
// Keyboard
//==============================================================================
CosmicKeyboard::CosmicKeyboard (MidiKeyboardState& state)
    : MidiKeyboardComponent (state, MidiKeyboardComponent::horizontalKeyboard)
{
    setColour (MidiKeyboardComponent::whiteNoteColourId, Colours::transparentBlack);
    setColour (MidiKeyboardComponent::blackNoteColourId, Colour (0xff17123a));
    setColour (MidiKeyboardComponent::keySeparatorLineColourId, Colours::transparentBlack);
    setColour (MidiKeyboardComponent::shadowColourId, Colours::transparentBlack);
    setOpaque (false);
    setWantsKeyboardFocus (false);
    setMidiChannel (1);
    setMidiChannelsToDisplay (0xffff);
    setVelocity (0.85f, true);
    setScrollButtonsVisible (false);
    setAvailableRange (36, 79); // C1..G4 (Live's octave naming), shifted with the octave buttons
    setBlackNoteLengthProportion (0.62f);
    setTooltip ("Click to audition (higher on the key = softer). Lights up for incoming MIDI too.");
}

String CosmicKeyboard::getWhiteNoteText (int note)
{
    return note % 12 == 0 ? MidiMessage::getMidiNoteName (note, true, true, 3) : String();
}

void CosmicKeyboard::drawWhiteNote (int note, Graphics& g, Rectangle<float> area, bool isDown, bool isOver, Colour,
                                    Colour)
{
    auto key = area.reduced (0.75f, 0.0f).withTrimmedBottom (1.0f);
    Path shape;
    shape.addRoundedRectangle (key.getX(), key.getY(), key.getWidth(), key.getHeight(), 4.0f, 4.0f, false, false, true, true);

    if (isDown)
    {
        g.setGradientFill (ColourGradient (Colour (0xffe9fbff), 0.0f, key.getY(), cyan, 0.0f, key.getBottom(), false));
        g.fillPath (shape);
        g.setColour (cyan.withAlpha (0.45f));
        g.strokePath (shape, PathStrokeType (2.0f));
    }
    else
    {
        g.setGradientFill (ColourGradient (Colour (0xffd9d4ff).withAlpha (isOver ? 0.95f : 0.82f), 0.0f, key.getY(),
                                           Colour (0xff9a8fe0).withAlpha (isOver ? 0.9f : 0.75f), 0.0f, key.getBottom(), false));
        g.fillPath (shape);
        g.setColour (Colours::white.withAlpha (0.35f));
        g.fillRect (key.withHeight (1.0f));
    }

    const auto text = getWhiteNoteText (note);
    if (text.isNotEmpty())
    {
        g.setColour (isDown ? Colour (0xff0b0d2b) : Colour (0xff2a1f6e).withAlpha (0.75f));
        g.setFont (aa::Fonts::uiBold (jmin (9.0f, key.getWidth() * 0.55f)));
        g.drawText (text, key.withTrimmedBottom (3.0f), Justification::centredBottom);
    }
}

void CosmicKeyboard::drawBlackNote (int, Graphics& g, Rectangle<float> area, bool isDown, bool isOver, Colour fill)
{
    auto key = area.withTrimmedBottom (1.0f);
    if (isDown)
    {
        g.setColour (magenta.withAlpha (0.35f));
        g.fillRoundedRectangle (key.expanded (2.0f, 1.0f), 4.0f);
        g.setGradientFill (ColourGradient (Colour (0xffffc6ef), 0.0f, key.getY(), magenta, 0.0f, key.getBottom(), false));
        g.fillRoundedRectangle (key, 3.0f);
        return;
    }
    g.setGradientFill (ColourGradient (fill.brighter (isOver ? 0.6f : 0.3f), 0.0f, key.getY(), fill.darker (0.4f), 0.0f,
                                       key.getBottom(), false));
    g.fillRoundedRectangle (key, 3.0f);
    g.setColour (Colours::white.withAlpha (0.18f));
    g.fillRoundedRectangle (key.reduced (key.getWidth() * 0.22f, 0.0f).withTrimmedBottom (key.getHeight() * 0.18f)
                                .withTrimmedTop (2.0f).withWidth (1.5f), 0.75f);
}

OctaveButton::OctaveButton (bool pointsUp) : up (pointsUp)
{
    setTooltip (pointsUp ? "Shift the keyboard up an octave" : "Shift the keyboard down an octave");
}

void OctaveButton::mouseDown (const MouseEvent&)
{
    pressed = true;
    repaint();
    if (onClick)
        onClick();
}

void OctaveButton::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool over = isMouseOver();
    g.setColour (Colours::black.withAlpha (0.28f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Colours::white.withAlpha (pressed ? 0.2f : (over ? 0.13f : 0.07f)));
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    auto a = r.withSizeKeepingCentre (6.0f, 11.0f);
    Path p;
    p.startNewSubPath (up ? a.getX() : a.getRight(), a.getY());
    p.lineTo (up ? a.getRight() : a.getX(), a.getCentreY());
    p.lineTo (up ? a.getX() : a.getRight(), a.getBottom());
    g.setColour (over ? cyan : Colour (0xffc8c4f2).withAlpha (0.75f));
    g.strokePath (p, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
}

//==============================================================================
// Editor
//==============================================================================
StardustEditor::StardustEditor (StardustProcessor& p)
    : aa::EditorBase (p, baseWidth, baseHeight, makeTheme()),
      proc (p),
      shapeA (p.apvts, "shapeA", "Shape"),
      unison (p.apvts, "unison", "Unison"),
      detune (p.apvts, "detune", "Detune"),
      spread (p.apvts, "spread", "Spread"),
      levelA (p.apvts, "levelA", "Level"),
      octaveA (p.apvts, "octaveA", "Octave"),
      shapeB (p.apvts, "shapeB", "Shape"),
      octaveB (p.apvts, "octaveB", "Octave"),
      semiB (p.apvts, "semiB", "Semi"),
      fineB (p.apvts, "fineB", "Fine"),
      levelB (p.apvts, "levelB", "Level"),
      sub (p.apvts, "sub", "Sub"),
      noise (p.apvts, "noise", "Noise"),
      waveA (*p.apvts.getRawParameterValue ("shapeA"), cyan),
      waveB (*p.apvts.getRawParameterValue ("shapeB"), magenta),
      filterType (p.apvts, "filterType", "", aa::ChoiceBox::Style::segmented),
      cutoff (p.apvts, "cutoff", "Cutoff"),
      resonance (p.apvts, "resonance", "Reso"),
      drive (p.apvts, "drive", "Drive"),
      filterEnv (p.apvts, "filterEnv", "Env"),
      keyTrack (p.apvts, "keyTrack", "Key"),
      ampEnv (p, "Amp", { "aAttack", "aDecay", "aSustain", "aRelease" },
              { "Amp attack: how slowly each note fades in", "Amp decay: time to fall to the sustain level",
                "Amp sustain: level held while the key is down", "Amp release: how long notes ring after you let go" },
              p.engine.ampEnvLevel, magenta),
      filterEnvGroup (p, "Filter", { "fAttack", "fDecay", "fSustain", "fRelease" },
                      { "Filter attack: how slowly the filter envelope rises", "Filter decay: time to fall to the sustain level",
                        "Filter sustain: envelope level held while the key is down",
                        "Filter release: envelope fall time after you let go" },
                      p.engine.filterEnvLevel, lavender),
      lfoShape (p.apvts, "lfoShape", "Shape"),
      lfoDivision (p.apvts, "lfoDivision", ""),
      lfoSync (p.apvts, "lfoSync", "Sync", aa::Toggle::Style::pad),
      lfoRate (p.apvts, "lfoRate", "Rate"),
      lfoPitch (p.apvts, "lfoPitch", "Pitch"),
      lfoCutoff (p.apvts, "lfoCutoff", "Cutoff"),
      lfoShapeMod (p.apvts, "lfoShapeMod", "Shape"),
      lfoGlyph (p, gold),
      chorus (p.apvts, "chorus", "Chorus"),
      spaceSize (p.apvts, "spaceSize", "Size"),
      spaceMix (p.apvts, "spaceMix", "Space"),
      volume (p.apvts, "volume", "Volume"),
      meter (p),
      twinkle (p.apvts, "twinkle", "Twinkle"),
      drift (p.apvts, "drift", "Drift"),
      gravity (p.apvts, "gravity", "Gravity"),
      voiceMode (p.apvts, "voiceMode", "Voices"),
      glide (p.apvts, "glide", "Glide"),
      velocity (p.apvts, "velocity", "Velocity"),
      keyboard (p.keyboardState),
      presets (p)
{
    useLookAndFeel (std::make_unique<StardustLook> (makeTheme()));
    aa::Fonts::setDisplayTypeface (PluginAssets::AudiowideRegular_ttf, (size_t) PluginAssets::AudiowideRegular_ttfSize);

    // ---- accents + bipolar ----
    for (auto* k : { &shapeA, &unison, &detune, &spread, &levelA, &octaveA })
        k->setAccent (cyan);
    for (auto* k : { &shapeB, &octaveB, &semiB, &fineB, &levelB, &sub, &noise })
        k->setAccent (magenta);
    for (auto* k : { &cutoff, &resonance, &drive, &filterEnv, &keyTrack })
        k->setAccent (lavender);
    for (auto* k : { &lfoRate, &lfoPitch, &lfoCutoff, &lfoShapeMod })
        k->setAccent (gold);
    for (auto* k : { &chorus, &spaceSize, &spaceMix, &volume })
        k->setAccent (cyan);
    for (auto* k : { &twinkle, &drift, &gravity })
        k->setAccent (gold);
    glide.setAccent (lavender);
    velocity.setAccent (magenta);

    for (auto* k : { &fineB, &filterEnv, &lfoCutoff, &gravity })
        k->setBipolar (true);

    filterType.setAccent (lavender);
    filterType.setShortNames ({ "LP", "BP", "HP" });
    lfoShape.setAccent (gold);
    lfoDivision.setAccent (gold);
    lfoSync.setAccent (gold);
    voiceMode.setAccent (lavender);
    presets.setAccent (cyan);

    // ---- tooltips ----
    shapeA.setTooltip ("Osc A waveform - sweeps smoothly from sine to triangle to saw to square");
    unison.setTooltip ("How many detuned copies of Osc A are stacked (7 = the classic supersaw)");
    detune.setTooltip ("How far apart the unison copies are tuned - more = wider, shimmering chorus");
    spread.setTooltip ("Stereo width of the unison copies");
    levelA.setTooltip ("Osc A volume");
    octaveA.setTooltip ("Osc A octave");
    shapeB.setTooltip ("Osc B waveform - sine, triangle, saw, square and everything between");
    octaveB.setTooltip ("Osc B octave relative to the note");
    semiB.setTooltip ("Osc B tuning in semitones (+7 = a fifth above)");
    fineB.setTooltip ("Osc B fine tuning in cents - a few cents gives a slow, warm beating");
    levelB.setTooltip ("Osc B volume");
    sub.setTooltip ("Sine sub oscillator one octave below the note");
    noise.setTooltip ("Breathy noise layer (goes through the filter)");
    filterType.setTooltip ("Filter type: low-pass, band-pass or high-pass");
    cutoff.setTooltip ("Filter cutoff frequency");
    resonance.setTooltip ("Filter resonance: emphasis around the cutoff");
    drive.setTooltip ("Warm saturation before the filter");
    filterEnv.setTooltip ("How far the filter envelope opens (+) or closes (-) the cutoff");
    keyTrack.setTooltip ("Key tracking: higher notes open the filter further");
    lfoShape.setTooltip ("LFO waveform");
    lfoRate.setTooltip ("LFO speed in Hz (turn on Sync to lock it to the song tempo)");
    lfoSync.setTooltip ("Lock the LFO to the host tempo");
    lfoDivision.setTooltip ("LFO cycle length when synced to tempo");
    lfoPitch.setTooltip ("LFO -> pitch: vibrato depth (the mod wheel adds vibrato too)");
    lfoCutoff.setTooltip ("LFO -> cutoff: filter sweep depth (negative = inverted)");
    lfoShapeMod.setTooltip ("LFO -> shape: morphs both oscillator waveforms back and forth");
    chorus.setTooltip ("Stereo chorus: lush width and shimmer");
    spaceSize.setTooltip ("Size and decay time of the Space reverb");
    spaceMix.setTooltip ("How much of the big Space reverb you hear");
    volume.setTooltip ("Master output volume");
    twinkle.setTooltip ("Twinkle: soft star-like glints at high harmonics of the notes you hold, sparkling into the reverb");
    drift.setTooltip ("Drift: slow random pitch and filter wandering per voice, like a warm analog synth");
    gravity.setTooltip ("Gravity: positive pulls each note's unison cloud together after it starts (wide to focused); "
                        "negative lets it slowly bloom apart");
    voiceMode.setTooltip ("Poly: up to 16 notes. Mono: one note at a time. Legato: one note that glides between overlapping notes");
    glide.setTooltip ("Glide (portamento) time between notes");
    velocity.setTooltip ("How much key velocity affects loudness and filter envelope depth");

    for (auto* c : std::initializer_list<Component*> {
             &space, &shapeA, &unison, &detune, &spread, &levelA, &octaveA, &shapeB, &octaveB, &semiB, &fineB, &levelB,
             &sub, &noise, &waveA, &waveB, &filterType, &cutoff, &resonance, &drive, &filterEnv, &keyTrack, &ampEnv,
             &filterEnvGroup, &lfoShape, &lfoDivision, &lfoSync, &lfoRate, &lfoPitch, &lfoCutoff, &lfoShapeMod,
             &lfoGlyph, &chorus, &spaceSize, &spaceMix, &volume, &meter, &twinkle, &drift, &gravity, &voiceMode, &glide,
             &velocity, &keyboard, &octaveDown, &octaveUp, &presets })
        content.addAndMakeVisible (c);

    octaveDown.onClick = [this] { shiftKeyboard (-12); };
    octaveUp.onClick = [this] { shiftKeyboard (12); };

    lfoSync.onStateChange = [this] { updateSyncState(); };
    updateSyncState();
    finishSetup();
}

void StardustEditor::shiftKeyboard (int semitones)
{
    const int low = jlimit (0, 84, keyboard.getRangeStart() + semitones);
    keyboard.setAvailableRange (low, low + 43);
}

void StardustEditor::updateSyncState()
{
    const bool synced = lfoSync.getToggleState();
    lfoRate.setEnabled (! synced);
    lfoRate.setAlpha (synced ? 0.35f : 1.0f);
    lfoDivision.setEnabled (synced);
    lfoDivision.setAlpha (synced ? 1.0f : 0.4f);
}

//==============================================================================
void StardustEditor::layoutContent()
{
    constexpr float margin = 12.0f, gap = 8.0f, sideW = 236.0f, centreW = 408.0f;
    const float xA = margin, xC = xA + sideW + gap, xB = xC + centreW + gap;
    constexpr float y1 = 62.0f, h1 = 198.0f, y2 = 268.0f, h2 = 196.0f, y3 = 472.0f, h3 = 116.0f;
    const float halfC = (centreW - gap) * 0.5f;

    titleArea = { 16.0f, 4.0f, 320.0f, 54.0f };
    oscAPanel = { xA, y1, sideW, h1 };
    spaceArea = { xC, y1, centreW, h1 };
    oscBPanel = { xB, y1, sideW, h1 };
    filterPanel = { xA, y2, sideW, h2 };
    envPanel = { xC, y2, halfC, h2 };
    lfoPanel = { xC + halfC + gap, y2, halfC, h2 };
    fxPanel = { xB, y2, sideW, h2 };
    cosmosPanel = { xA, y3, sideW, h3 };
    keysPanel = { xC, y3, centreW, h3 };
    voicePanel = { xB, y3, sideW, h3 };
    backgroundScale = 0.0f;

    presets.setBounds (Rectangle<float> (250.0f, 32.0f).withCentre ({ (float) baseWidth * 0.5f, 31.0f }).toNearestInt());
    space.setBounds (spaceArea.toNearestInt());

    // OSC A
    {
        waveA.setBounds (Rectangle<float> (oscAPanel.getRight() - 84.0f, oscAPanel.getY() + 8.0f, 72.0f, 20.0f).toNearestInt());
        auto c = panelContent (oscAPanel);
        auto row1 = c.removeFromTop (c.getHeight() * 0.5f);
        placeRow (row1, { &shapeA, &unison, &detune });
        placeRow (c, { &spread, &levelA, &octaveA });
    }
    // OSC B
    {
        waveB.setBounds (Rectangle<float> (oscBPanel.getRight() - 84.0f, oscBPanel.getY() + 8.0f, 72.0f, 20.0f).toNearestInt());
        auto c = panelContent (oscBPanel);
        auto row1 = c.removeFromTop (c.getHeight() * 0.5f);
        placeRow (row1, { &shapeB, &octaveB, &semiB, &fineB });
        placeRow (c, { &levelB, &sub, &noise });
    }
    // FILTER
    {
        filterType.setBounds (Rectangle<float> (filterPanel.getRight() - 112.0f, filterPanel.getY() + 7.0f, 100.0f, 22.0f).toNearestInt());
        auto c = panelContent (filterPanel);
        auto hero = c.removeFromLeft (100.0f);
        placeKnob (cutoff, hero, 92.0f, 112.0f);
        auto row1 = c.removeFromTop (c.getHeight() * 0.5f);
        placeRow (row1, { &resonance, &drive });
        placeRow (c, { &filterEnv, &keyTrack });
    }
    // ENVELOPES
    {
        auto c = envPanel.reduced (12.0f, 0.0f).withTrimmedTop (30.0f).withTrimmedBottom (10.0f);
        const float w = (c.getWidth() - 12.0f) * 0.5f;
        ampEnv.setBounds (c.removeFromLeft (w).toNearestInt());
        c.removeFromLeft (12.0f);
        filterEnvGroup.setBounds (c.toNearestInt());
    }
    // LFO
    {
        lfoGlyph.setBounds (Rectangle<float> (lfoPanel.getRight() - 78.0f, lfoPanel.getY() + 8.0f, 66.0f, 20.0f).toNearestInt());
        auto c = panelContent (lfoPanel);
        auto top = c.removeFromTop (c.getHeight() * 0.5f);
        placeKnob (lfoRate, top.removeFromLeft (60.0f));
        top.removeFromLeft (4.0f);
        auto right = top.withSizeKeepingCentre (top.getWidth(), 64.0f);
        lfoShape.setBounds (right.removeFromTop (36.0f).toNearestInt());
        right.removeFromTop (5.0f);
        lfoSync.setBounds (right.removeFromLeft (44.0f).toNearestInt());
        right.removeFromLeft (6.0f);
        lfoDivision.setBounds (right.toNearestInt());
        placeRow (c, { &lfoPitch, &lfoCutoff, &lfoShapeMod });
    }
    // FX
    {
        auto c = panelContent (fxPanel);
        auto row1 = c.removeFromTop (c.getHeight() * 0.5f);
        placeRow (row1, { &chorus, &spaceSize, &spaceMix });
        placeKnob (volume, c.removeFromLeft (c.getWidth() / 3.0f));
        meter.setBounds (c.reduced (6.0f, 0.0f).withSizeKeepingCentre (c.getWidth() - 14.0f, 36.0f).toNearestInt());
    }
    // COSMOS
    placeRow (panelContent (cosmosPanel), { &twinkle, &drift, &gravity });
    // VOICE
    {
        auto c = panelContent (voicePanel);
        const float w = c.getWidth() / 3.0f;
        placeKnob (glide, c.removeFromLeft (w));
        placeKnob (velocity, c.removeFromLeft (w));
        voiceMode.setBounds (c.withSizeKeepingCentre (c.getWidth() - 4.0f, 40.0f).toNearestInt());
    }
    // KEYS
    {
        auto k = keysPanel.reduced (8.0f, 9.0f);
        octaveDown.setBounds (k.removeFromLeft (20.0f).toNearestInt());
        octaveUp.setBounds (k.removeFromRight (20.0f).toNearestInt());
        k.reduce (5.0f, 0.0f);
        keyboard.setKeyWidth (k.getWidth() / 26.0f);
        keyboard.setBounds (k.toNearestInt());
    }
}

//==============================================================================
void StardustEditor::drawGlassPanel (Graphics& g, Rectangle<float> r, const String& title, Colour accent,
                                     float titleLineEnd) const
{
    constexpr float radius = 14.0f;

    // soft drop shadow
    for (int i = 3; i > 0; --i)
    {
        g.setColour (Colours::black.withAlpha (0.07f));
        g.fillRoundedRectangle (r.translated (0.0f, 2.0f).expanded ((float) i * 1.5f), radius + (float) i * 1.5f);
    }

    g.setGradientFill (ColourGradient (Colour (0xff1d1846).withAlpha (0.78f), r.getX(), r.getY(),
                                       Colour (0xff120e33).withAlpha (0.82f), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);

    // accent haze in the corner
    g.saveState();
    Path clip;
    clip.addRoundedRectangle (r, radius);
    g.reduceClipRegion (clip);
    g.setGradientFill (ColourGradient (accent.withAlpha (0.10f), r.getX() + 10.0f, r.getY() + 6.0f,
                                       accent.withAlpha (0.0f), r.getX() + r.getWidth() * 0.6f, r.getY() + r.getHeight() * 0.5f, true));
    g.fillRect (r);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.06f), 0.0f, r.getY(), Colours::transparentWhite, 0.0f, r.getY() + 30.0f, false));
    g.fillRect (r.withHeight (30.0f));
    g.restoreState();

    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.22f), 0.0f, r.getY(), Colours::white.withAlpha (0.06f), 0.0f, r.getBottom(), false));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);

    if (title.isEmpty())
        return;

    const auto font = aa::Fonts::display (11.5f).withExtraKerningFactor (0.14f);
    const auto text = title.toUpperCase();
    const float tw = aa::Fonts::textWidth (font, text);
    const Point<float> star (r.getX() + 18.0f, r.getY() + 18.0f);
    drawSparkle (g, star, 6.5f, accent, 1.0f);
    g.setFont (font);
    g.setColour (Colour (0xffeef0ff).withAlpha (0.92f));
    g.drawText (text, Rectangle<float> (r.getX() + 29.0f, r.getY() + 10.0f, tw + 10.0f, 16.0f), Justification::centredLeft);

    const float lineX = r.getX() + 29.0f + tw + 10.0f;
    if (titleLineEnd > lineX + 10.0f)
    {
        g.setGradientFill (ColourGradient (accent.withAlpha (0.45f), lineX, 0.0f, accent.withAlpha (0.0f), titleLineEnd, 0.0f, false));
        g.fillRect (Rectangle<float> (lineX, r.getY() + 17.5f, titleLineEnd - lineX, 1.0f));
    }
}

void StardustEditor::rebuildBackground (float scale)
{
    backgroundScale = scale;
    const auto b = baseBounds().toFloat();
    background = Image (Image::ARGB, roundToInt (b.getWidth() * scale), roundToInt (b.getHeight() * scale), true);
    Graphics g (background);
    g.addTransform (AffineTransform::scale (scale));

    // deep space gradient
    {
        ColourGradient grad (navy, 0.0f, 0.0f, violet, b.getWidth() * 0.85f, b.getHeight(), false);
        grad.addColour (0.55, Colour (0xff17113d));
        g.setGradientFill (grad);
        g.fillAll();
    }

    // nebula clouds
    Random r (31337);
    struct Cloud { float x, y, rx, ry; Colour c; float a; };
    const Cloud clouds[] = {
        { 0.12f, 0.88f, 0.30f, 0.35f, magenta, 0.10f }, { 0.30f, 0.62f, 0.25f, 0.3f, Colour (0xff5b3cff), 0.12f },
        { 0.84f, 0.18f, 0.28f, 0.32f, cyan, 0.07f },    { 0.70f, 0.85f, 0.30f, 0.28f, Colour (0xff7a3cff), 0.10f },
        { 0.95f, 0.65f, 0.18f, 0.30f, magenta, 0.07f }, { 0.05f, 0.15f, 0.25f, 0.25f, Colour (0xff4a6bff), 0.08f },
        { 0.52f, 0.10f, 0.25f, 0.12f, magenta, 0.05f },
    };
    for (const auto& c : clouds)
        for (int k = 0; k < 6; ++k)
        {
            const float cx = (c.x + (r.nextFloat() - 0.5f) * 0.1f) * b.getWidth();
            const float cy = (c.y + (r.nextFloat() - 0.5f) * 0.12f) * b.getHeight();
            const float rx = c.rx * b.getWidth() * (0.5f + 0.5f * r.nextFloat());
            const float ry = c.ry * b.getHeight() * (0.5f + 0.5f * r.nextFloat());
            g.setGradientFill (ColourGradient (c.c.withAlpha (c.a * (0.5f + 0.5f * r.nextFloat())), cx, cy,
                                               c.c.withAlpha (0.0f), cx + rx, cy, true));
            g.fillEllipse (cx - rx, cy - ry, rx * 2.0f, ry * 2.0f);
        }

    // background stars
    for (int i = 0; i < 260; ++i)
    {
        const float x = r.nextFloat() * b.getWidth(), y = r.nextFloat() * b.getHeight();
        const float s = 0.5f + r.nextFloat() * r.nextFloat() * 1.8f;
        const float t = r.nextFloat();
        const auto col = t < 0.1f ? cyan : (t < 0.18f ? magenta : (t < 0.24f ? gold : starWhite));
        g.setColour (col.interpolatedWith (starWhite, 0.4f).withAlpha (0.15f + 0.45f * r.nextFloat()));
        g.fillEllipse (x, y, s, s);
    }
    for (int i = 0; i < 9; ++i)
        drawSparkle (g, { r.nextFloat() * b.getWidth(), r.nextFloat() * b.getHeight() }, 3.0f + r.nextFloat() * 3.0f,
                     starWhite, 0.35f + 0.3f * r.nextFloat());

    // ---- header ----
    {
        const auto font = aa::Fonts::display (31.0f);
        GlyphArrangement ga;
        ga.addLineOfText (font, "STARDUST", titleArea.getX() + 4.0f, 41.0f);
        Path title;
        ga.createPath (title);
        const auto tb = title.getBounds();

        for (auto [width, alpha] : { std::pair { 12.0f, 0.05f }, std::pair { 7.0f, 0.08f }, std::pair { 3.5f, 0.14f } })
        {
            g.setGradientFill (ColourGradient (cyan.withAlpha (alpha), tb.getX(), 0.0f, magenta.withAlpha (alpha), tb.getRight(), 0.0f, false));
            g.strokePath (title, PathStrokeType (width, PathStrokeType::curved, PathStrokeType::rounded));
        }
        ColourGradient fill (Colour (0xffbffaff), tb.getX(), tb.getY(), Colour (0xffffb3ea), tb.getRight(), tb.getBottom(), false);
        fill.addColour (0.5, Colour (0xffe8e2ff));
        g.setGradientFill (fill);
        g.fillPath (title);

        g.setFont (aa::Fonts::uiBold (9.5f).withExtraKerningFactor (0.32f));
        g.setColour (Colour (0xffc8c4f2).withAlpha (0.7f));
        g.drawText ("COSMIC SUPERSAW", Rectangle<float> (tb.getRight() + 14.0f, 22.0f, 160.0f, 12.0f), Justification::centredLeft);
        g.setColour (gold.withAlpha (0.75f));
        g.drawText ("SYNTHESIZER", Rectangle<float> (tb.getRight() + 14.0f, 35.0f, 160.0f, 12.0f), Justification::centredLeft);

        g.setFont (aa::Fonts::uiBold (10.5f).withExtraKerningFactor (0.24f));
        g.setColour (Colour (0xffc8c4f2).withAlpha (0.6f));
        g.drawText ("ADVENTURE AUDIO", Rectangle<float> (b.getRight() - 230.0f, 16.0f, 214.0f, 30.0f), Justification::centredRight);
    }

    // ---- panels ----
    drawGlassPanel (g, oscAPanel, "Osc A", cyan, oscAPanel.getRight() - 92.0f);
    drawGlassPanel (g, oscBPanel, "Osc B", magenta, oscBPanel.getRight() - 92.0f);
    drawGlassPanel (g, filterPanel, "Filter", lavender, filterPanel.getRight() - 120.0f);
    drawGlassPanel (g, envPanel, "Envelopes", magenta, envPanel.getRight() - 14.0f);
    drawGlassPanel (g, lfoPanel, "LFO", gold, lfoPanel.getRight() - 86.0f);
    drawGlassPanel (g, fxPanel, "Space FX", cyan, fxPanel.getRight() - 14.0f);
    drawGlassPanel (g, cosmosPanel, "Cosmos", gold, cosmosPanel.getRight() - 14.0f);
    drawGlassPanel (g, keysPanel, {}, lavender, 0.0f);
    drawGlassPanel (g, voicePanel, "Voice", lavender, voicePanel.getRight() - 14.0f);

    // little divider between Osc B's tone row and its sub / noise row
    {
        const float y = panelContent (oscBPanel).getCentreY();
        g.setGradientFill (ColourGradient (magenta.withAlpha (0.0f), oscBPanel.getX() + 20.0f, 0.0f,
                                           magenta.withAlpha (0.0f), oscBPanel.getRight() - 20.0f, 0.0f, false));
        ColourGradient line (magenta.withAlpha (0.0f), oscBPanel.getX() + 20.0f, 0.0f, magenta.withAlpha (0.0f), oscBPanel.getRight() - 20.0f, 0.0f, false);
        line.addColour (0.5, magenta.withAlpha (0.22f));
        g.setGradientFill (line);
        g.fillRect (Rectangle<float> (oscBPanel.getX() + 20.0f, y, oscBPanel.getWidth() - 40.0f, 1.0f));
    }
}

void StardustEditor::paintContent (Graphics& g)
{
    const float scale = jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (background.isNull() || std::abs (scale - backgroundScale) > 0.01f)
        rebuildBackground (scale);
    g.setOpacity (1.0f);
    g.drawImage (background, baseBounds().toFloat());
}

void StardustEditor::paintContentOver (Graphics& g)
{
    // twinkling stars around the title
    const Point<float> spots[] = { { 22.0f, 14.0f }, { 104.0f, 10.0f }, { 186.0f, 48.0f }, { 150.0f, 13.0f }, { 58.0f, 50.0f } };
    const Colour tints[] = { cyan, starWhite, magenta, gold, starWhite };
    for (int i = 0; i < 5; ++i)
    {
        const float ph = (float) clock * (0.7f + 0.23f * (float) i) + (float) i * 1.7f;
        const float a = std::pow (jmax (0.0f, std::sin (ph)), 3.0f);
        drawSparkle (g, spots[i], 4.0f + 3.0f * a, tints[i].interpolatedWith (starWhite, 0.3f), a, ph * 0.1f);
    }
}

void StardustEditor::onFrame (double, double dtFrame)
{
    // Animate at most ~60 fps, whatever the display refresh rate: keeps CPU modest on 120/144 Hz screens.
    frameAccumulator += dtFrame;
    if (frameAccumulator < 1.0 / 62.0)
        return;
    const double dt = jmin (0.1, frameAccumulator);
    frameAccumulator = 0.0;

    clock += dt;
    space.tick (dt);
    waveA.tick();
    waveB.tick();
    ampEnv.tick();
    filterEnvGroup.tick();
    lfoGlyph.tick();
    meter.tick ((float) dt);
    content.repaint (titleArea.toNearestInt());
}
