#include "BabbleWidgets.h"

using namespace juce;

namespace babble::ui
{
using namespace palette;

//==============================================================================
MouthShape mouthForVowel (float vowel)
{
    //                         width  height smile  round  teeth  pucker
    static const MouthShape keys[numVowels] = {
        { 0.62f, 0.66f, 0.18f, 0.55f, 0.55f, 0.05f },  // A: big open
        { 0.80f, 0.40f, 0.45f, 0.22f, 0.85f, 0.00f },  // E: wide
        { 0.88f, 0.25f, 0.62f, 0.10f, 1.00f, 0.00f },  // I: grin
        { 0.44f, 0.54f, 0.00f, 1.00f, 0.12f, 0.55f },  // O: round
        { 0.30f, 0.34f, -0.05f, 1.00f, 0.00f, 1.00f }, // U: pucker
    };

    vowel = jlimit (0.0f, (float) (numVowels - 1), vowel);
    const int i = jmin (numVowels - 2, (int) vowel);
    const float t = vowel - (float) i;
    const auto& a = keys[i];
    const auto& b = keys[i + 1];
    auto mix = [t] (float x, float y) { return x + (y - x) * t; };
    return { mix (a.width, b.width), mix (a.height, b.height), mix (a.smile, b.smile),
             mix (a.round, b.round), mix (a.teeth, b.teeth), mix (a.pucker, b.pucker) };
}

Path mouthPath (Point<float> c, float size, const MouthShape& m, float open)
{
    open = jlimit (0.0f, 1.0f, open);
    const float halfW = size * m.width * 0.5f;
    const float h = size * 0.62f * m.height * open;
    const float lift = size * 0.11f * m.smile;
    const float kx = jmap (m.round, 0.42f, 1.0f);
    const float cornerY = c.y - lift;
    auto ctrlY = [cornerY] (float midY) { return (8.0f * midY - 2.0f * cornerY) / 6.0f; };
    const float yTop = ctrlY (c.y - h * 0.3f), yBot = ctrlY (c.y + h * 0.7f);

    Path mouth;
    mouth.startNewSubPath (c.x - halfW, cornerY);
    mouth.cubicTo (c.x - halfW * kx, yTop, c.x + halfW * kx, yTop, c.x + halfW, cornerY);
    mouth.cubicTo (c.x + halfW * kx, yBot, c.x - halfW * kx, yBot, c.x - halfW, cornerY);
    mouth.closeSubPath();
    return mouth;
}

void drawMouth (Graphics& g, Point<float> c, float size, const MouthShape& m, float open, float inkWidth,
                float robot, float glow)
{
    open = jlimit (0.0f, 1.0f, open);
    const float halfW = size * m.width * 0.5f;
    const float h = size * 0.62f * m.height * open;
    const float lift = size * 0.11f * m.smile;
    const float hUp = h * 0.3f, hDown = h * 0.7f;
    const float cornerY = c.y - lift;
    auto ctrlY = [cornerY] (float midY) { return (8.0f * midY - 2.0f * cornerY) / 6.0f; };
    const float yTop = ctrlY (c.y - hUp);
    const auto mouth = mouthPath (c, size, m, open);

    if (h > 0.6f)
    {
        g.setGradientFill (ColourGradient (palette::mouth, c.x, c.y - hUp, palette::mouth.darker (0.5f), c.x, c.y + hDown, false));
        g.fillPath (mouth);

        Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (mouth);

        if (robot > 0.5f)
        {
            // speaker grille with a glow that follows the voice
            const auto led = teal.interpolatedWith (Colours::white, 0.25f);
            for (int i = 0; i < 5; ++i)
            {
                const float y = c.y - hUp + (hUp + hDown) * ((float) i + 0.7f) / 5.5f;
                g.setColour (led.withAlpha (0.25f + 0.65f * glow));
                g.fillRoundedRectangle (c.x - halfW, y - 1.2f, halfW * 2.0f, 2.4f, 1.2f);
            }
        }
        else
        {
            // tongue
            const float tw = halfW * 1.25f, th = jmax (4.0f, h * 0.55f);
            auto tongueArea = Rectangle<float> (tw, th).withCentre ({ c.x, c.y + hDown - th * 0.18f });
            g.setColour (tongue);
            g.fillEllipse (tongueArea);
            g.setColour (tongue.darker (0.25f));
            g.drawLine (c.x, tongueArea.getY() + th * 0.22f, c.x, tongueArea.getY() + th * 0.5f, jmax (1.0f, size * 0.012f));

            // upper teeth
            if (m.teeth > 0.02f)
            {
                const float teethH = jmin (h * 0.3f, size * 0.09f) * m.teeth + 1.0f;
                auto teeth = Rectangle<float> (c.x - halfW, jmin (yTop, cornerY) - 4.0f, halfW * 2.0f,
                                               (c.y - hUp) - jmin (yTop, cornerY) + 4.0f + teethH);
                g.setColour (cream);
                g.fillRect (teeth);
                g.setColour (ink.withAlpha (0.18f));
                g.drawLine (c.x, c.y - hUp, c.x, c.y - hUp + teethH, 1.0f);
            }
        }
    }

    const float lipW = size * 0.08f * m.pucker;
    if (lipW > 0.6f)
    {
        g.setColour (ink);
        g.strokePath (mouth, PathStrokeType (lipW + inkWidth * 2.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (coral.interpolatedWith (tongue, 0.3f));
        g.strokePath (mouth, PathStrokeType (lipW, PathStrokeType::curved, PathStrokeType::rounded));
    }
    else
    {
        g.setColour (ink);
        g.strokePath (mouth, PathStrokeType (inkWidth, PathStrokeType::curved, PathStrokeType::rounded));
    }
}

void drawOutlinedText (Graphics& g, const String& text, const Font& font, Point<float> baseline, Colour fill,
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

//==============================================================================
void BabbleLookAndFeel::drawKnob (Graphics& g, Rectangle<float> bounds, float proportion, bool bipolar, bool hovered,
                                  bool dragging, Colour accent, aa::Knob& knob)
{
    const float size = jmin (bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const float r = size * 0.5f - 2.0f;
    const float startA = -MathConstants<float>::pi * 0.75f;
    const float endA = MathConstants<float>::pi * 0.75f;
    const float valueA = startA + proportion * (endA - startA);
    const float alpha = knob.isEnabled() ? 1.0f : 0.45f;

    const float arcR = r * 0.84f;
    const float arcW = jmax (3.0f, r * 0.17f);

    Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startA, endA, true);
    g.setColour (ink.withAlpha (0.13f * alpha));
    g.strokePath (track, PathStrokeType (arcW, PathStrokeType::curved, PathStrokeType::rounded));

    const float fromA = bipolar ? 0.0f : startA;
    if (std::abs (valueA - fromA) > 0.01f)
    {
        Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, jmin (fromA, valueA), jmax (fromA, valueA), true);
        const auto c = (hovered || dragging) ? accent.brighter (0.15f) : accent;
        g.setColour (c.withAlpha (alpha));
        g.strokePath (value, PathStrokeType (arcW, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // body with a hard sticker shadow
    const float bodyR = r * 0.6f * (dragging ? 1.04f : 1.0f);
    auto body = Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre);
    g.setColour (ink.withAlpha (0.22f * alpha));
    g.fillEllipse (body.translated (0.0f, bodyR * 0.14f));
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (alpha), body.getCentreX(), body.getY(),
                                       cream.interpolatedWith (peach, 0.35f).withAlpha (alpha), body.getCentreX(),
                                       body.getBottom(), false));
    g.fillEllipse (body);
    g.setColour (ink.withAlpha (alpha));
    g.drawEllipse (body, jmax (1.6f, bodyR * 0.08f));

    // pointer: a fat dot near the rim
    const float px = std::sin (valueA), py = -std::cos (valueA);
    const auto dot = centre + Point<float> (px, py) * (bodyR * 0.58f);
    const float dotR = jmax (2.2f, bodyR * 0.2f);
    g.setColour (accent.withAlpha (alpha));
    g.fillEllipse (Rectangle<float> (dotR * 2.0f, dotR * 2.0f).withCentre (dot));
    g.setColour (ink.withAlpha (alpha));
    g.drawEllipse (Rectangle<float> (dotR * 2.0f, dotR * 2.0f).withCentre (dot), jmax (1.2f, bodyR * 0.06f));
}

void BabbleLookAndFeel::drawKnobCaption (Graphics& g, Rectangle<float> area, const String& text, bool showingValue,
                                         Colour accent, aa::Knob& knob)
{
    const float h = jlimit (9.0f, 13.0f, area.getHeight() * 0.8f);
    g.setFont (aa::Fonts::uiBold (h).withExtraKerningFactor (showingValue ? 0.0f : 0.06f));
    const auto c = showingValue ? accent.interpolatedWith (ink, 0.35f) : ink.withAlpha (0.72f);
    g.setColour (knob.isEnabled() ? c : c.withMultipliedAlpha (0.45f));
    g.drawFittedText (showingValue ? text : text.toUpperCase(), area.toNearestInt(), Justification::centred, 1, 0.8f);
}

Rectangle<float> BabbleLookAndFeel::drawPanel (Graphics& g, Rectangle<float> bounds, const String& title, Colour tint)
{
    const float radius = 18.0f;
    const auto fill = tint.isTransparent() ? cream : tint;

    g.setColour (ink.withAlpha (0.3f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 4.0f), radius);
    g.setGradientFill (ColourGradient (fill.brighter (0.03f), bounds.getX(), bounds.getY(), fill.interpolatedWith (peach, 0.18f),
                                       bounds.getX(), bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (ink);
    g.drawRoundedRectangle (bounds.reduced (1.0f), radius, 2.2f);

    auto content = bounds.reduced (12.0f, 9.0f);
    if (title.isNotEmpty())
    {
        auto titleArea = content.removeFromTop (18.0f);
        const auto font = aa::Fonts::uiBold (11.5f).withExtraKerningFactor (0.14f);
        const auto upper = title.toUpperCase();
        const float tw = aa::Fonts::textWidth (font, upper);

        g.setColour (coral);
        g.fillEllipse (titleArea.getX() + 1.0f, titleArea.getCentreY() - 4.0f, 8.0f, 8.0f);
        g.setColour (ink);
        g.drawEllipse (titleArea.getX() + 1.0f, titleArea.getCentreY() - 4.0f, 8.0f, 8.0f, 1.4f);

        g.setFont (font);
        g.setColour (ink.withAlpha (0.85f));
        g.drawText (upper, titleArea.withTrimmedLeft (15.0f).withWidth (tw + 8.0f), Justification::centredLeft, false);
        content.removeFromTop (2.0f);
    }
    return content;
}

//==============================================================================
MouthPad::MouthPad (BabbleProcessor& p) : proc (p)
{
    vowelParam = p.apvts.getParameter ("vowel");
    shiftParam = p.apvts.getParameter ("shift");
    vowelAttachment = std::make_unique<ParameterAttachment> (*vowelParam, [this] (float v) { vowel = v; repaint(); });
    shiftAttachment = std::make_unique<ParameterAttachment> (*shiftParam, [this] (float v) { shift = v; repaint(); });
    vowelAttachment->sendInitialUpdate();
    shiftAttachment->sendInitialUpdate();
    ghostVowel = vowel;
    setTooltip ("Mouth pad: left-right sweeps the vowel A-E-I-O-U, up-down changes the throat size (formant shift). "
                "Double-click to reset. The teal dot shows the vowel actually being sung.");
    setRepaintsOnMouseActivity (false);
}

MouthPad::~MouthPad() = default;

Rectangle<float> MouthPad::padArea() const
{
    return getLocalBounds().toFloat().reduced (2.0f).withTrimmedBottom (22.0f);
}

Point<float> MouthPad::toScreen (float vowelPos, float shiftSt) const
{
    auto a = padArea().reduced (18.0f, 16.0f);
    return { a.getX() + a.getWidth() * vowelPos / 4.0f, a.getCentreY() - a.getHeight() * 0.5f * shiftSt / 12.0f };
}

void MouthPad::setFromPosition (Point<float> pos)
{
    auto a = padArea().reduced (18.0f, 16.0f);
    const float v = jlimit (0.0f, 4.0f, (pos.x - a.getX()) / a.getWidth() * 4.0f);
    const float s = jlimit (-12.0f, 12.0f, (a.getCentreY() - pos.y) / (a.getHeight() * 0.5f) * 12.0f);
    vowelAttachment->setValueAsPartOfGesture (v);
    shiftAttachment->setValueAsPartOfGesture (std::abs (s) < 0.35f ? 0.0f : s); // gentle detent at 0
}

void MouthPad::mouseDown (const MouseEvent& e)
{
    dragging = true;
    vowelAttachment->beginGesture();
    shiftAttachment->beginGesture();
    setFromPosition (e.position);
}

void MouthPad::mouseDrag (const MouseEvent& e)
{
    if (dragging)
        setFromPosition (e.position);
}

void MouthPad::mouseUp (const MouseEvent&)
{
    if (! dragging)
        return;
    dragging = false;
    vowelAttachment->endGesture();
    shiftAttachment->endGesture();
    repaint();
}

void MouthPad::mouseDoubleClick (const MouseEvent&)
{
    vowelAttachment->setValueAsCompleteGesture (vowelParam->convertFrom0to1 (vowelParam->getDefaultValue()));
    shiftAttachment->setValueAsCompleteGesture (shiftParam->convertFrom0to1 (shiftParam->getDefaultValue()));
}

void MouthPad::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    const float d = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
    if (d != 0.0f)
        vowelAttachment->setValueAsCompleteGesture (jlimit (0.0f, 4.0f, vowel + (d > 0.0f ? 0.1f : -0.1f)));
}

void MouthPad::tick (double dt)
{
    const float sung = proc.engine.uiVowel.load();
    const bool singing = proc.engine.uiActiveNotes.load() > 0 || proc.engine.uiOpen.load() > 0.03f;
    const float prevGhost = ghostVowel, prevAlpha = ghostAlpha;
    ghostVowel += (sung - ghostVowel) * (1.0f - std::exp (-(float) dt / 0.03f));
    ghostAlpha += ((singing ? 1.0f : 0.0f) - ghostAlpha) * (1.0f - std::exp (-(float) dt / 0.2f));

    trailTimer += (float) dt;
    bool trailChanged = false;
    if (trailTimer > 0.03f)
    {
        trailTimer = 0.0f;
        const auto p = toScreen (ghostVowel, shift);
        if (trailCount == 0 || trail[0].getDistanceFrom (p) > 0.5f || trailCount > 1)
        {
            for (int i = (int) trail.size() - 1; i > 0; --i)
                trail[(size_t) i] = trail[(size_t) i - 1];
            trail[0] = p;
            trailCount = jmin ((int) trail.size(), trailCount + 1);
            if (trail[0].getDistanceFrom (trail[(size_t) jmax (0, trailCount - 1)]) < 0.5f)
                trailCount = 1;
            trailChanged = true;
        }
    }

    if (std::abs (prevGhost - ghostVowel) > 0.001f || std::abs (prevAlpha - ghostAlpha) > 0.002f || trailChanged)
        repaint();
}

void MouthPad::paint (Graphics& g)
{
    auto area = padArea();
    const float radius = 14.0f;

    // pad body
    g.setColour (ink.withAlpha (0.25f));
    g.fillRoundedRectangle (area.translated (0.0f, 3.0f), radius);
    g.setGradientFill (ColourGradient (Colour (0xff3d1d5e), area.getX(), area.getY(), Colour (0xff2a1245), area.getX(),
                                       area.getBottom(), false));
    g.fillRoundedRectangle (area, radius);

    auto inner = area.reduced (18.0f, 16.0f);

    // vowel columns
    for (int i = 0; i < numVowels; ++i)
    {
        const float x = inner.getX() + inner.getWidth() * (float) i / 4.0f;
        const float near = 1.0f - jlimit (0.0f, 1.0f, std::abs (vowel - (float) i));
        g.setColour (cream.withAlpha (0.07f + 0.12f * near));
        for (float y = area.getY() + 8.0f; y < area.getBottom() - 6.0f; y += 7.0f)
            g.fillEllipse (x - 1.0f, y, 2.0f, 2.0f);
    }

    // throat centre line
    const float cy = inner.getCentreY();
    g.setColour (peach.withAlpha (0.25f));
    for (float x = area.getX() + 8.0f; x < area.getRight() - 8.0f; x += 9.0f)
        g.fillRect (x, cy - 0.5f, 5.0f, 1.0f);

    // vowel mouth watermarks
    for (int i = 0; i < numVowels; ++i)
    {
        const float x = inner.getX() + inner.getWidth() * (float) i / 4.0f;
        const float near = 1.0f - jlimit (0.0f, 1.0f, std::abs (vowel - (float) i));
        const auto wm = mouthPath ({ x, inner.getY() + inner.getHeight() * 0.78f }, 36.0f, mouthForVowel ((float) i), 0.9f);
        g.setColour (cream.withAlpha (0.06f + 0.1f * near));
        g.fillPath (wm);
        g.setColour (cream.withAlpha (0.16f + 0.2f * near));
        g.strokePath (wm, PathStrokeType (1.5f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    g.setFont (aa::Fonts::uiBold (9.0f).withExtraKerningFactor (0.12f));
    g.setColour (peach.withAlpha (0.55f));
    g.drawText ("TINY THROAT", area.reduced (10.0f, 5.0f).removeFromTop (12.0f), Justification::centredRight);
    g.drawText ("BIG THROAT", area.reduced (10.0f, 5.0f).removeFromBottom (12.0f), Justification::centredRight);

    // ghost trail (the vowel being sung)
    if (ghostAlpha > 0.01f)
    {
        for (int i = trailCount - 1; i > 0; --i)
        {
            const float t = 1.0f - (float) i / (float) trail.size();
            const float s = 3.0f + 5.0f * t;
            g.setColour (teal.withAlpha (ghostAlpha * 0.5f * t));
            g.fillEllipse (Rectangle<float> (s, s).withCentre (trail[(size_t) i]));
        }
        const auto gp = toScreen (ghostVowel, shift);
        g.setColour (teal.withAlpha (0.25f * ghostAlpha));
        g.fillEllipse (Rectangle<float> (22.0f, 22.0f).withCentre (gp));
        g.setColour (teal.withAlpha (ghostAlpha));
        g.fillEllipse (Rectangle<float> (10.0f, 10.0f).withCentre (gp));
    }

    // puck: a little mouth
    const auto pp = toScreen (vowel, shift);
    const float pr = (hover || dragging) ? 17.0f : 15.5f;
    auto puck = Rectangle<float> (pr * 2.0f, pr * 2.0f).withCentre (pp);
    g.setColour (Colours::black.withAlpha (0.3f));
    g.fillEllipse (puck.translated (0.0f, 3.0f));
    g.setColour (coral);
    g.fillEllipse (puck);
    g.setColour (ink);
    g.drawEllipse (puck, 2.2f);
    drawMouth (g, pp.translated (0.0f, 1.5f), pr * 1.35f, mouthForVowel (vowel), 0.85f, 1.6f);

    // vowel letters
    auto letters = getLocalBounds().toFloat().removeFromBottom (22.0f);
    for (int i = 0; i < numVowels; ++i)
    {
        const float x = inner.getX() + inner.getWidth() * (float) i / 4.0f;
        const float near = 1.0f - jlimit (0.0f, 1.0f, std::abs (vowel - (float) i));
        g.setFont (aa::Fonts::display (17.0f + 4.0f * near));
        g.setColour (ink.withAlpha (0.45f + 0.55f * near).interpolatedWith (coral.darker (0.15f), near));
        g.drawText (vowelLetter (i), Rectangle<float> (30.0f, letters.getHeight()).withCentre ({ x, letters.getCentreY() + 1.0f }),
                    Justification::centred, false);
    }
}

//==============================================================================
VoicePicker::VoicePicker (BabbleProcessor& p)
{
    auto* param = p.apvts.getParameter ("voice");
    attachment = std::make_unique<ParameterAttachment> (*param, [this] (float v) { index = roundToInt (v); repaint(); });
    attachment->sendInitialUpdate();
    setTooltip ("Voice type: who's singing. Bass, Tenor, Alto and Soprano use classic formant tables; "
                "Child is small and bright; Robot is monotone, buzzy and crunchy.");
}

VoicePicker::~VoicePicker() = default;

Rectangle<float> VoicePicker::cell (int i) const
{
    auto b = getLocalBounds().toFloat().reduced (1.0f, 1.0f).withTrimmedBottom (3.0f);
    const float gap = 6.0f;
    const float w = (b.getWidth() - gap * 2.0f) / 3.0f, h = (b.getHeight() - gap) / 2.0f;
    return { b.getX() + (float) (i % 3) * (w + gap), b.getY() + (float) (i / 3) * (h + gap), w, h };
}

int VoicePicker::indexAt (Point<float> p) const
{
    for (int i = 0; i < numVoiceTypes; ++i)
        if (cell (i).expanded (3.0f).contains (p))
            return i;
    return -1;
}

void VoicePicker::mouseMove (const MouseEvent& e)
{
    const int i = indexAt (e.position);
    if (i != hoverIndex)
    {
        hoverIndex = i;
        repaint();
    }
}

void VoicePicker::mouseDown (const MouseEvent& e)
{
    const int i = indexAt (e.position);
    if (i >= 0 && i != index)
        attachment->setValueAsCompleteGesture ((float) i);
}

void VoicePicker::paint (Graphics& g)
{
    const auto& names = voiceTypeNames();
    for (int i = 0; i < numVoiceTypes; ++i)
    {
        auto r = cell (i);
        const bool sel = i == index, hov = i == hoverIndex;
        const float rad = r.getHeight() * 0.5f;

        g.setColour (ink.withAlpha (sel ? 0.35f : 0.18f));
        g.fillRoundedRectangle (r.translated (0.0f, sel ? 3.0f : 2.0f), rad);
        g.setColour (sel ? coral : (hov ? peach.interpolatedWith (cream, 0.4f) : cream));
        g.fillRoundedRectangle (r, rad);
        g.setColour (ink);
        g.drawRoundedRectangle (r.reduced (0.8f), rad, sel ? 2.2f : 1.6f);

        g.setFont (aa::Fonts::uiBold (12.0f));
        g.setColour (sel ? cream : ink.withAlpha (0.85f));
        g.drawFittedText (names[i], r.reduced (4.0f, 0.0f).toNearestInt(), Justification::centred, 1, 0.8f);
    }
}

//==============================================================================
ChoirPicker::ChoirPicker (BabbleProcessor& p) : proc (p)
{
    auto* param = p.apvts.getParameter ("choir");
    attachment = std::make_unique<ParameterAttachment> (*param, [this] (float v)
    {
        const int n = jlimit (1, maxChoir, roundToInt (v));
        if (n > count)
            for (int i = count; i < n; ++i)
                pop[(size_t) i] = 1.0f;
        count = n;
        repaint();
    });
    attachment->sendInitialUpdate();
    setTooltip ("Choir voices: how many singers sing every note (1-4). Click a singer.");
}

ChoirPicker::~ChoirPicker() = default;

Rectangle<float> ChoirPicker::faceArea (int i) const
{
    auto b = getLocalBounds().toFloat().withTrimmedBottom (18.0f).reduced (4.0f);
    const float s = jmin (b.getWidth() / 2.0f, b.getHeight() / 2.0f);
    auto grid = Rectangle<float> (s * 2.0f, s * 2.0f).withCentre (b.getCentre());
    return Rectangle<float> (grid.getX() + (float) (i % 2) * s, grid.getY() + (float) (i / 2) * s, s, s).reduced (s * 0.08f);
}

int ChoirPicker::indexAt (Point<float> p) const
{
    for (int i = 0; i < maxChoir; ++i)
        if (faceArea (i).contains (p))
            return i;
    return -1;
}

void ChoirPicker::mouseMove (const MouseEvent& e)
{
    const int i = indexAt (e.position);
    if (i != hoverIndex)
    {
        hoverIndex = i;
        repaint();
    }
}

void ChoirPicker::mouseDown (const MouseEvent& e)
{
    const int i = indexAt (e.position);
    if (i >= 0)
    {
        pop[(size_t) i] = 1.0f;
        attachment->setValueAsCompleteGesture ((float) (i + 1));
    }
}

void ChoirPicker::tick (double dt)
{
    clock += (float) dt;
    const float target = jlimit (0.0f, 1.0f, proc.engine.uiOpen.load() * 1.3f);
    const float before = open;
    open += (target - open) * (1.0f - std::exp (-(float) dt / 0.05f));
    bool popping = false;
    for (auto& p : pop)
    {
        p = jmax (0.0f, p - (float) dt * 3.0f);
        popping = popping || p > 0.0f;
    }
    if (popping || open > 0.01f || std::abs (before - open) > 0.001f)
        repaint();
}

void ChoirPicker::paint (Graphics& g)
{
    {
        auto caption = getLocalBounds().toFloat().removeFromBottom (18.0f);
        g.setFont (aa::Fonts::uiBold (11.0f).withExtraKerningFactor (0.06f));
        g.setColour (ink.withAlpha (0.72f));
        g.drawFittedText (String (count) + (count == 1 ? " SINGER" : " SINGERS"), caption.toNearestInt(), Justification::centred, 1, 0.8f);
    }

    const float vowel = proc.engine.uiVowel.load();
    const auto shape = mouthForVowel (vowel);
    const Colour shirts[] = { teal, coral, plum, sunshine };

    for (int i = 0; i < maxChoir; ++i)
    {
        auto r = faceArea (i);
        const bool on = i < count;
        const float bob = on ? std::sin (clock * 5.0f + (float) i * 1.7f) * 1.6f * open : 0.0f;
        const float popScale = 1.0f + 0.25f * std::sin (pop[(size_t) i] * MathConstants<float>::pi);
        r = r.withSizeKeepingCentre (r.getWidth() * popScale, r.getHeight() * popScale).translated (0.0f, -bob);
        const float alpha = on ? 1.0f : 0.35f;

        auto head = r.reduced (r.getWidth() * 0.1f);
        g.setColour (ink.withAlpha (0.25f * alpha));
        g.fillEllipse (head.translated (0.0f, 2.0f));
        g.setColour ((on ? peach : cream).withAlpha (on ? 1.0f : 0.6f));
        g.fillEllipse (head);
        g.setColour (ink.withAlpha (alpha));
        g.drawEllipse (head, i == hoverIndex ? 2.2f : 1.6f);

        // little shirt-coloured tuft
        g.setColour (shirts[i].withAlpha (alpha));
        g.fillEllipse (Rectangle<float> (head.getWidth() * 0.28f, head.getWidth() * 0.2f)
                           .withCentre ({ head.getCentreX(), head.getY() + 1.0f }));
        g.setColour (ink.withAlpha (alpha));
        g.drawEllipse (Rectangle<float> (head.getWidth() * 0.28f, head.getWidth() * 0.2f)
                           .withCentre ({ head.getCentreX(), head.getY() + 1.0f }), 1.2f);

        const float ex = head.getWidth() * 0.17f, ey = head.getCentreY() - head.getHeight() * 0.1f;
        const float er = jmax (1.5f, head.getWidth() * 0.06f);
        g.setColour (ink.withAlpha (alpha));
        if (on)
        {
            g.fillEllipse (Rectangle<float> (er * 2.0f, er * 2.4f).withCentre ({ head.getCentreX() - ex, ey }));
            g.fillEllipse (Rectangle<float> (er * 2.0f, er * 2.4f).withCentre ({ head.getCentreX() + ex, ey }));
            drawMouth (g, { head.getCentreX(), head.getCentreY() + head.getHeight() * 0.18f }, head.getWidth() * 0.5f,
                       shape, open, 1.3f);
        }
        else
        {
            // asleep
            g.drawLine (head.getCentreX() - ex - er, ey, head.getCentreX() - ex + er, ey, 1.3f);
            g.drawLine (head.getCentreX() + ex - er, ey, head.getCentreX() + ex + er, ey, 1.3f);
            g.fillEllipse (Rectangle<float> (er * 1.6f, er * 1.6f).withCentre ({ head.getCentreX(), head.getCentreY() + head.getHeight() * 0.2f }));
        }
    }
}
} // namespace babble::ui
