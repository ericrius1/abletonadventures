#include "BabbleFace.h"

using namespace juce;

namespace babble::ui
{
using namespace palette;

namespace
{
    constexpr float stageRadius = 22.0f;

    inline float smoothingCoeff (double dt, float tau) { return 1.0f - std::exp (-(float) dt / tau); }

    inline void spring (float& x, float& v, float target, float k, float c, float dt)
    {
        v += (k * (target - x) - c * v) * dt;
        x += v * dt;
    }

    Colour skinFor (int voiceType)
    {
        switch (voiceType)
        {
            case child: return Colour (0xffffd2b4);
            case robot: return Colour (0xffcfcbe3);
            default:    return peach;
        }
    }

    Colour shirtFor (int voiceType)
    {
        switch (voiceType)
        {
            case bass:    return plum;
            case tenor:   return Colour (0xff27867d);
            case alto:    return coral.darker (0.12f);
            case soprano: return plum.brighter (0.15f);
            case child:   return sunshine;
            case robot:   return Colour (0xff8f89ad);
            default:      return plum;
        }
    }

    void fillAndStroke (Graphics& g, const Path& p, Colour fill, float stroke)
    {
        g.setColour (fill);
        g.fillPath (p);
        g.setColour (ink);
        g.strokePath (p, PathStrokeType (stroke, PathStrokeType::curved, PathStrokeType::rounded));
    }

    void drawSparkle (Graphics& g, Point<float> c, float r, Colour col)
    {
        Path p;
        p.startNewSubPath (c.x, c.y - r);
        p.quadraticTo (c.x, c.y, c.x + r, c.y);
        p.quadraticTo (c.x, c.y, c.x, c.y + r);
        p.quadraticTo (c.x, c.y, c.x - r, c.y);
        p.quadraticTo (c.x, c.y, c.x, c.y - r);
        p.closeSubPath();
        g.setColour (col);
        g.fillPath (p);
    }
} // namespace

//==============================================================================
FaceStage::FaceStage (BabbleProcessor& p) : proc (p)
{
    voiceParam = p.apvts.getRawParameterValue ("voice");
    babbleParam = p.apvts.getRawParameterValue ("babble");
    notes.reserve (32);
    setOpaque (false);

    UiEvent stale; // events queued while the editor was closed
    while (p.engine.uiEvents.pop (stale)) {}
    setTooltip ("Meet Babs! Play notes and Babs sings them. Turn up Babble for gibberish. Click to say hello.");
}

void FaceStage::resized()
{
    headCentre = { (float) getWidth() * 0.5f, (float) getHeight() * 0.5f };
    headRx = jmin (104.0f, (float) getWidth() * 0.27f);
    headRy = headRx * 0.93f;
    background = {};
}

void FaceStage::mouseDown (const MouseEvent&)
{
    giggle = 1.0f;
    bounceVel -= 220.0f;
    squashVel += 3.0f;
    tiltVel += (random.nextBool() ? 1.0f : -1.0f) * 1.2f;
    poke = 1.6f;
    pushSyllable (random.nextBool() ? "hee hee!" : "hello!");
}

void FaceStage::pushSyllable (const String& s)
{
    syllables.add (s);
    while (syllables.size() > 3)
        syllables.remove (0);
    bubblePopVel += 9.0f;
}

void FaceStage::spawnNote()
{
    if (notes.size() >= 24)
        notes.erase (notes.begin());

    Note n;
    const bool left = (noteSide++ % 3) != 2;
    const float side = left ? -1.0f : 1.0f;
    n.pos = { headCentre.x + side * (headRx + 30.0f + random.nextFloat() * 18.0f), headCentre.y + 18.0f + random.nextFloat() * 30.0f };
    n.vel = { side * (5.0f + random.nextFloat() * 12.0f), -50.0f - random.nextFloat() * 26.0f };
    n.life = 1.9f + random.nextFloat() * 0.8f;
    n.size = 14.0f + random.nextFloat() * 7.0f;
    n.phase = random.nextFloat() * MathConstants<float>::twoPi;
    n.spin = (random.nextFloat() - 0.5f) * 0.5f;
    n.colour = random.nextInt (3);
    n.beamed = random.nextFloat() < 0.3f;
    notes.push_back (n);
}

void FaceStage::tick (double dt)
{
    const float fdt = (float) dt;
    clock += dt;
    auto& e = proc.engine;

    UiEvent ev;
    while (e.uiEvents.pop (ev))
    {
        if (ev.type == UiEvent::noteOn)
        {
            bounceVel -= 70.0f + 150.0f * ev.value;
            squashVel += 2.4f * ev.value;
            surprise = jmax (surprise, (ev.value > 0.85f || sleep > 0.3f) ? 1.0f : 0.35f);
            tiltVel += (random.nextFloat() - 0.5f) * 0.9f;
            if (onNote)
                onNote (ev.value);
        }
        else
        {
            pushSyllable (syllableText (ev.a, ev.b));
            tiltTarget = (random.nextFloat() - 0.5f) * 0.14f;
        }
    }

    voiceType = jlimit (0, numVoiceTypes - 1, (int) voiceParam->load());
    const float babbleAmount = babbleParam->load() / 100.0f;

    const float openTarget = jlimit (0.0f, 1.0f, e.uiOpen.load() * 1.3f);
    const float levelTarget = jmin (1.0f, e.uiLevel.load() * 1.7f);
    vowel += (e.uiVowel.load() - vowel) * smoothingCoeff (dt, 0.03f);
    open += (openTarget - open) * smoothingCoeff (dt, openTarget > open ? 0.02f : 0.06f);
    lips += (e.uiLips.load() - lips) * smoothingCoeff (dt, 0.02f);
    level += (levelTarget - level) * smoothingCoeff (dt, levelTarget > level ? 0.05f : 0.4f);
    vibrato += (e.uiVibrato.load() - vibrato) * smoothingCoeff (dt, 0.02f);

    const bool active = e.uiActiveNotes.load() > 0 || open > 0.04f;
    singing += ((active ? 1.0f : 0.0f) - singing) * smoothingCoeff (dt, 0.2f);
    silence = active ? 0.0f : silence + fdt;

    // springs: head bounce, squash, tilt and a hair tuft that lags behind
    spring (bounce, bounceVel, 0.0f, 240.0f, 13.0f, fdt);
    spring (squash, squashVel, 0.0f, 380.0f, 12.0f, fdt);
    spring (tilt, tiltVel, tiltTarget * (0.3f + 0.7f * singing) + 0.09f * sleep, 90.0f, 9.0f, fdt);
    spring (tuft, tuftVel, -bounceVel * 0.0035f + tilt * 2.0f, 150.0f, 5.0f, fdt);
    tiltTarget *= std::exp (-fdt * 1.2f);
    surprise = jmax (0.0f, surprise - fdt * 2.0f);
    giggle = jmax (0.0f, giggle - fdt * 1.6f);
    poke = jmax (0.0f, poke - fdt);

    // blinking
    blinkTimer -= fdt;
    if (blinkPhase < 0.0f && blinkTimer <= 0.0f)
        blinkPhase = 0.0f;
    if (blinkPhase >= 0.0f)
    {
        blinkPhase += fdt / 0.17f;
        blink = std::sin (MathConstants<float>::pi * jmin (1.0f, blinkPhase));
        if (blinkPhase >= 1.0f)
        {
            blinkPhase = -1.0f;
            blink = 0.0f;
            if (doubleBlink)
            {
                doubleBlink = false;
                blinkTimer = 0.07f;
            }
            else
            {
                blinkTimer = 1.8f + random.nextFloat() * 3.6f;
                doubleBlink = random.nextFloat() < 0.18f;
            }
        }
    }

    // looking around
    lookTimer -= fdt;
    if (lookTimer <= 0.0f)
    {
        const bool sing = singing > 0.5f;
        lookTimer = sing ? 0.7f + random.nextFloat() * 1.6f : 1.2f + random.nextFloat() * 2.8f;
        lookTarget = sing ? Point<float> (random.nextFloat() - 0.5f, -0.2f - random.nextFloat() * 0.5f)
                          : Point<float> (random.nextFloat() * 2.0f - 1.0f, random.nextFloat() * 1.2f - 0.6f);
    }
    look += (lookTarget - look) * smoothingCoeff (dt, 0.06f);

    // floating music notes
    if (open > 0.15f)
    {
        noteTimer -= fdt * (0.6f + 2.4f * open);
        if (noteTimer <= 0.0f)
        {
            spawnNote();
            noteTimer = 0.55f;
        }
    }
    for (auto& n : notes)
    {
        n.age += fdt;
        n.vel.x += std::sin (n.age * 3.0f + n.phase) * 24.0f * fdt;
        n.pos += n.vel * fdt;
    }
    notes.erase (std::remove_if (notes.begin(), notes.end(), [] (const Note& n) { return n.age >= n.life; }), notes.end());

    // speech bubble
    spring (bubblePop, bubblePopVel, 0.0f, 320.0f, 11.0f, fdt);
    const bool showBubble = singing > 0.3f || silence < 0.6f || poke > 0.0f;
    bubbleAlpha += ((showBubble ? 1.0f : 0.0f) - bubbleAlpha) * smoothingCoeff (dt, 0.1f);
    if (bubbleAlpha < 0.02f && ! showBubble)
        syllables.clear();
    sungText = babbleAmount < 0.005f ? String (sungVowel (jlimit (0, numVowels - 1, roundToInt (vowel)))) : String();
    hintAlpha += ((silence > 2.5f && poke <= 0.0f ? 1.0f : 0.0f) - hintAlpha) * smoothingCoeff (dt, 0.35f);

    // after a long quiet spell Babs dozes off (and wakes with a start on the next note)
    const bool drowsy = silence > 25.0f && poke <= 0.0f;
    sleep = drowsy ? jmin (1.0f, sleep + fdt * 0.5f) : jmax (0.0f, sleep - fdt * 4.0f);

    repaint();
}

//==============================================================================
void FaceStage::rebuildBackground (float scale)
{
    scale = jlimit (1.0f, 4.0f, scale);
    const int w = jmax (1, roundToInt ((float) getWidth() * scale));
    const int h = jmax (1, roundToInt ((float) getHeight() * scale));
    background = Image (Image::ARGB, w, h, true);
    Graphics g (background);
    g.addTransform (AffineTransform::scale (scale));

    auto b = getLocalBounds().toFloat();
    Path clip;
    clip.addRoundedRectangle (b.reduced (1.0f), stageRadius);
    g.reduceClipRegion (clip);

    g.setGradientFill (ColourGradient (Colour (0xff47d6c8), 0.0f, 0.0f, Colour (0xff1a9c92), 0.0f, b.getBottom(), false));
    g.fillRect (b);

    // sunburst
    const int rays = 22;
    const float far = b.getWidth() + b.getHeight();
    for (int i = 0; i < rays; i += 2)
    {
        const float a0 = MathConstants<float>::twoPi * (float) i / (float) rays;
        const float a1 = MathConstants<float>::twoPi * (float) (i + 1) / (float) rays;
        Path wedge;
        wedge.startNewSubPath (headCentre);
        wedge.lineTo (headCentre + Point<float> (std::sin (a0), -std::cos (a0)) * far);
        wedge.lineTo (headCentre + Point<float> (std::sin (a1), -std::cos (a1)) * far);
        wedge.closeSubPath();
        g.setColour (Colours::white.withAlpha (0.085f));
        g.fillPath (wedge);
    }

    // warm spotlight behind the head
    ColourGradient spot (cream.withAlpha (0.55f), headCentre.x, headCentre.y, cream.withAlpha (0.0f),
                         headCentre.x + headRx * 1.9f, headCentre.y, true);
    g.setGradientFill (spot);
    g.fillEllipse (Rectangle<float> (headRx * 3.8f, headRx * 3.8f).withCentre (headCentre));

    // sparkles
    Random r (7);
    for (int i = 0; i < 14; ++i)
    {
        Point<float> p (r.nextFloat() * b.getWidth(), r.nextFloat() * b.getHeight() * 0.75f);
        if (p.getDistanceFrom (headCentre) < headRx * 1.35f)
            continue;
        drawSparkle (g, p, 3.0f + r.nextFloat() * 4.0f, cream.withAlpha (0.35f + 0.3f * r.nextFloat()));
    }

    // soft vignette at the bottom
    g.setGradientFill (ColourGradient (Colours::transparentBlack, 0.0f, b.getBottom() - 90.0f, ink.withAlpha (0.22f), 0.0f,
                                       b.getBottom(), false));
    g.fillRect (b.withTop (b.getBottom() - 90.0f));
}

//==============================================================================
void FaceStage::drawNotes (Graphics& g)
{
    const Colour fills[] = { cream, coral, sunshine };
    for (const auto& n : notes)
    {
        const float alpha = jmin (1.0f, n.age / 0.15f) * jmin (1.0f, (n.life - n.age) / 0.5f);
        const float s = n.size;
        Path p;
        if (n.beamed)
        {
            p.addEllipse (-s * 0.75f, -s * 0.16f, s * 0.5f, s * 0.36f);
            p.addEllipse (s * 0.05f, -s * 0.06f, s * 0.5f, s * 0.36f);
            p.addRectangle (-s * 0.3f, -s * 1.05f, s * 0.08f, s * 0.95f);
            p.addRectangle (s * 0.5f, -s * 0.95f, s * 0.08f, s * 0.95f);
            Path beam;
            beam.startNewSubPath (-s * 0.3f, -s * 1.05f);
            beam.lineTo (s * 0.58f, -s * 0.95f);
            beam.lineTo (s * 0.58f, -s * 0.72f);
            beam.lineTo (-s * 0.3f, -s * 0.82f);
            beam.closeSubPath();
            p.addPath (beam);
        }
        else
        {
            p.addEllipse (-s * 0.3f, -s * 0.18f, s * 0.56f, s * 0.4f);
            p.addRectangle (s * 0.18f, -s * 1.05f, s * 0.08f, s * 0.95f);
            Path flag;
            flag.startNewSubPath (s * 0.22f, -s * 1.05f);
            flag.quadraticTo (s * 0.36f, -s * 0.7f, s * 0.62f, -s * 0.55f);
            flag.quadraticTo (s * 0.48f, -s * 0.72f, s * 0.26f, -s * 0.78f);
            flag.closeSubPath();
            p.addPath (flag);
        }

        const float rot = n.spin * std::sin (n.age * 2.5f + n.phase) - 0.18f;
        p.applyTransform (AffineTransform::rotation (rot).translated (n.pos));
        g.setColour (ink.withAlpha (alpha));
        g.strokePath (p, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (fills[n.colour].withAlpha (alpha));
        g.fillPath (p);
    }
}

void FaceStage::drawBody (Graphics& g, float bob)
{
    const float cx = headCentre.x;
    const float h = (float) getHeight();
    const float top = headCentre.y + headRy * 0.84f + bob;
    const auto skin = skinFor (voiceType);
    const auto shirt = shirtFor (voiceType);

    // neck
    auto neck = Rectangle<float> (cx - 26.0f, top - 20.0f, 52.0f, 48.0f);
    g.setColour (skin.darker (0.12f));
    g.fillRect (neck);
    g.setColour (ink);
    g.drawLine (neck.getX(), neck.getY(), neck.getX(), neck.getBottom(), 3.5f);
    g.drawLine (neck.getRight(), neck.getY(), neck.getRight(), neck.getBottom(), 3.5f);

    // shoulders
    Path body;
    body.startNewSubPath (cx - 168.0f, h + 12.0f);
    body.cubicTo (cx - 164.0f, top + 44.0f, cx - 124.0f, top + 22.0f, cx - 62.0f, top + 18.0f);
    body.lineTo (cx + 62.0f, top + 18.0f);
    body.cubicTo (cx + 124.0f, top + 22.0f, cx + 164.0f, top + 44.0f, cx + 168.0f, h + 12.0f);
    body.closeSubPath();
    g.setGradientFill (ColourGradient (shirt.brighter (0.15f), cx, top, shirt.darker (0.25f), cx, h, false));
    g.fillPath (body);
    g.setColour (ink);
    g.strokePath (body, PathStrokeType (3.5f, PathStrokeType::curved, PathStrokeType::rounded));

    if (voiceType == robot)
    {
        // chest panel with blinking lights
        auto panel = Rectangle<float> (64.0f, 30.0f).withCentre ({ cx, top + 50.0f });
        g.setColour (ink.withAlpha (0.25f));
        g.fillRoundedRectangle (panel, 6.0f);
        g.setColour (ink);
        g.drawRoundedRectangle (panel, 6.0f, 2.5f);
        const Colour leds[] = { coral, sunshine, teal };
        for (int i = 0; i < 3; ++i)
        {
            const float on = 0.35f + 0.65f * (0.5f + 0.5f * std::sin ((float) clock * (3.0f + (float) i * 2.3f) + (float) i));
            auto led = Rectangle<float> (10.0f, 10.0f).withCentre ({ panel.getX() + 14.0f + (float) i * 18.0f, panel.getCentreY() });
            g.setColour (leds[i].withAlpha (on));
            g.fillEllipse (led);
            g.setColour (ink);
            g.drawEllipse (led, 1.6f);
        }
        return;
    }

    // collar
    for (int side = -1; side <= 1; side += 2)
    {
        const float s = (float) side;
        Path collar;
        collar.startNewSubPath (cx + s * 4.0f, top + 16.0f);
        collar.lineTo (cx + s * 44.0f, top + 12.0f);
        collar.lineTo (cx + s * 30.0f, top + 40.0f);
        collar.closeSubPath();
        fillAndStroke (g, collar, cream, 3.0f);
    }

    if (voiceType == soprano)
    {
        // pearl necklace
        for (int i = -4; i <= 4; ++i)
        {
            const float t = (float) i / 4.0f;
            auto pearl = Rectangle<float> (9.0f, 9.0f).withCentre ({ cx + t * 40.0f, top + 22.0f + 12.0f * (1.0f - t * t) });
            g.setColour (cream);
            g.fillEllipse (pearl);
            g.setColour (ink);
            g.drawEllipse (pearl, 1.5f);
        }
        return;
    }

    // bow tie
    const float by = top + 22.0f;
    const auto tie = voiceType == tenor ? sunshine : (voiceType == child ? teal : coral);
    for (int side = -1; side <= 1; side += 2)
    {
        const float s = (float) side;
        Path wing;
        wing.startNewSubPath (cx, by);
        wing.lineTo (cx + s * 24.0f, by - 12.0f);
        wing.quadraticTo (cx + s * 28.0f, by, cx + s * 24.0f, by + 12.0f);
        wing.closeSubPath();
        fillAndStroke (g, wing, tie, 3.0f);
    }
    auto knot = Rectangle<float> (12.0f, 12.0f).withCentre ({ cx, by });
    g.setColour (tie.darker (0.2f));
    g.fillRoundedRectangle (knot, 3.0f);
    g.setColour (ink);
    g.drawRoundedRectangle (knot, 3.0f, 2.5f);
}

void FaceStage::drawCostume (Graphics& g, Point<float> c, float rx, float ry, bool front)
{
    const auto top = Point<float> (c.x, c.y - ry);

    if (! front)
    {
        // things behind the head: ears / bolts, hair bun
        for (int side = -1; side <= 1; side += 2)
        {
            const float s = (float) side;
            auto ear = Rectangle<float> (36.0f, 40.0f).withCentre ({ c.x + s * rx * 0.97f, c.y + 6.0f });
            if (voiceType == robot)
            {
                auto bolt = Rectangle<float> (30.0f, 30.0f).withCentre ({ c.x + s * (rx + 6.0f), c.y + 6.0f });
                g.setColour (Colour (0xffa9a3c4));
                g.fillRoundedRectangle (bolt, 7.0f);
                g.setColour (ink);
                g.drawRoundedRectangle (bolt, 7.0f, 3.0f);
                g.drawLine (bolt.getCentreX() - 6.0f, bolt.getCentreY(), bolt.getCentreX() + 6.0f, bolt.getCentreY(), 2.5f);
                g.drawLine (bolt.getCentreX(), bolt.getCentreY() - 6.0f, bolt.getCentreX(), bolt.getCentreY() + 6.0f, 2.5f);
            }
            else
            {
                const auto skin = skinFor (voiceType);
                g.setColour (skin.darker (0.05f));
                g.fillEllipse (ear);
                g.setColour (ink);
                g.drawEllipse (ear, 3.5f);
                Path inner;
                inner.addCentredArc (ear.getCentreX() + s * 2.0f, ear.getCentreY(), 8.0f, 10.0f, 0.0f,
                                     s < 0 ? MathConstants<float>::pi * 1.2f : MathConstants<float>::pi * 0.2f,
                                     s < 0 ? MathConstants<float>::pi * 1.8f : MathConstants<float>::pi * 0.8f, true);
                g.setColour (ink.withAlpha (0.5f));
                g.strokePath (inner, PathStrokeType (2.5f, PathStrokeType::curved, PathStrokeType::rounded));

                if (voiceType == soprano)
                {
                    auto pearl = Rectangle<float> (10.0f, 10.0f).withCentre ({ ear.getCentreX(), ear.getBottom() + 4.0f });
                    g.setColour (cream);
                    g.fillEllipse (pearl);
                    g.setColour (ink);
                    g.drawEllipse (pearl, 1.8f);
                }
            }
        }

        if (voiceType == soprano)
        {
            auto bun = Rectangle<float> (66.0f, 54.0f).withCentre (top.translated (0.0f, 2.0f));
            g.setColour (plum);
            g.fillEllipse (bun);
            g.setColour (ink);
            g.drawEllipse (bun, 3.5f);
        }
        return;
    }

    // ---- in front of the head --------------------------------------------------------
    Graphics::ScopedSaveState save (g);
    g.addTransform (AffineTransform::rotation (tuft * 0.6f, top.x, top.y + 6.0f));

    switch (voiceType)
    {
        case bass:
        {
            Path spikes;
            for (int i = -2; i <= 2; ++i)
            {
                const float x = top.x + (float) i * 13.0f;
                const float base = top.y + 6.0f + std::abs ((float) i) * 3.0f;
                spikes.startNewSubPath (x - 9.0f, base);
                spikes.lineTo (x + (float) i * 2.0f, base - 20.0f + std::abs ((float) i) * 3.0f);
                spikes.lineTo (x + 9.0f, base);
                spikes.closeSubPath();
            }
            fillAndStroke (g, spikes, plum, 3.0f);
            break;
        }
        case tenor:
        {
            Path quiff;
            quiff.startNewSubPath (top.x + 30.0f, top.y + 10.0f);
            quiff.cubicTo (top.x + 40.0f, top.y - 30.0f, top.x - 30.0f, top.y - 46.0f, top.x - 36.0f, top.y - 12.0f);
            quiff.cubicTo (top.x - 30.0f, top.y - 28.0f, top.x - 4.0f, top.y - 22.0f, top.x - 8.0f, top.y + 4.0f);
            quiff.closeSubPath();
            fillAndStroke (g, quiff, ink.brighter (0.25f), 3.5f);
            g.setColour (Colours::white.withAlpha (0.3f));
            g.drawLine (top.x - 12.0f, top.y - 26.0f, top.x + 12.0f, top.y - 28.0f, 3.0f);
            break;
        }
        case alto:
        case child:
        {
            if (voiceType == child)
            {
                Path curl;
                curl.startNewSubPath (top.x - 4.0f, top.y + 4.0f);
                curl.cubicTo (top.x - 10.0f, top.y - 22.0f, top.x + 22.0f, top.y - 30.0f, top.x + 20.0f, top.y - 12.0f);
                curl.cubicTo (top.x + 18.0f, top.y - 2.0f, top.x + 6.0f, top.y - 6.0f, top.x + 8.0f, top.y - 14.0f);
                g.setColour (ink);
                g.strokePath (curl, PathStrokeType (4.5f, PathStrokeType::curved, PathStrokeType::rounded));
            }

            if (voiceType == alto)
            {
                const auto bc = top.translated (-34.0f, 4.0f);
                for (int side = -1; side <= 1; side += 2)
                {
                    const float s = (float) side;
                    Path wing;
                    wing.startNewSubPath (bc);
                    wing.lineTo (bc.x + s * 15.0f, bc.y - 10.0f);
                    wing.quadraticTo (bc.x + s * 19.0f, bc.y, bc.x + s * 15.0f, bc.y + 9.0f);
                    wing.closeSubPath();
                    fillAndStroke (g, wing, teal, 2.5f);
                }
                g.setColour (teal.darker (0.2f));
                g.fillEllipse (Rectangle<float> (8.0f, 8.0f).withCentre (bc));
                g.setColour (ink);
                g.drawEllipse (Rectangle<float> (8.0f, 8.0f).withCentre (bc), 2.0f);
            }
            break;
        }
        case soprano:
        {
            // flower tucked into the bun
            const auto fc = top.translated (-32.0f, -2.0f);
            for (int i = 0; i < 5; ++i)
            {
                const float a = MathConstants<float>::twoPi * (float) i / 5.0f + 0.3f;
                auto petal = Rectangle<float> (14.0f, 14.0f).withCentre (fc + Point<float> (std::sin (a), -std::cos (a)) * 9.0f);
                g.setColour (coral);
                g.fillEllipse (petal);
                g.setColour (ink);
                g.drawEllipse (petal, 2.0f);
            }
            g.setColour (sunshine);
            g.fillEllipse (Rectangle<float> (10.0f, 10.0f).withCentre (fc));
            g.setColour (ink);
            g.drawEllipse (Rectangle<float> (10.0f, 10.0f).withCentre (fc), 2.0f);
            break;
        }
        case robot:
        {
            const auto tip = top.translated (0.0f, -34.0f);
            g.setColour (ink);
            g.drawLine (top.x, top.y + 2.0f, tip.x, tip.y, 4.0f);
            const float glow = 0.4f + 0.6f * level;
            g.setColour (coral.withAlpha (0.3f * glow));
            g.fillEllipse (Rectangle<float> (30.0f, 30.0f).withCentre (tip));
            g.setColour (coral.interpolatedWith (sunshine, level));
            g.fillEllipse (Rectangle<float> (15.0f, 15.0f).withCentre (tip));
            g.setColour (ink);
            g.drawEllipse (Rectangle<float> (15.0f, 15.0f).withCentre (tip), 3.0f);
            break;
        }
        default: break;
    }
}

void FaceStage::drawHair (Graphics& g, Point<float> c, float rx, float ry)
{
    if (voiceType != tenor && voiceType != alto && voiceType != soprano)
        return;

    const float sideAngle = voiceType == tenor ? 1.32f : 1.72f;
    const float hx = rx + 5.0f, hy = ry + 6.0f;
    const float cy = c.y + 3.0f;
    const float fy = c.y - ry * 0.63f;
    const Colour colour = voiceType == alto ? Colour (0xffc8503f) : (voiceType == tenor ? ink.brighter (0.25f) : plum);

    Path hair;
    hair.addCentredArc (c.x, cy, hx, hy, 0.0f, -sideAngle, sideAngle, true);
    const Point<float> leftEnd (c.x - hx * std::sin (sideAngle), cy - hy * std::cos (sideAngle));
    const Point<float> rightEnd (c.x + hx * std::sin (sideAngle), cy - hy * std::cos (sideAngle));

    if (voiceType == alto)
    {
        // a bob with a swoopy fringe
        hair.quadraticTo (rightEnd.x - 6.0f, rightEnd.y + 18.0f, rightEnd.x - 22.0f, rightEnd.y + 12.0f);
        hair.quadraticTo (c.x + rx * 0.72f, fy + 8.0f, c.x + rx * 0.55f, fy - 2.0f);
        hair.quadraticTo (c.x + rx * 0.1f, fy + 8.0f, c.x - rx * 0.45f, fy - 8.0f);
        hair.quadraticTo (c.x - rx * 0.78f, fy + 4.0f, leftEnd.x + 22.0f, leftEnd.y + 12.0f);
        hair.quadraticTo (leftEnd.x + 6.0f, leftEnd.y + 18.0f, leftEnd.x, leftEnd.y);
    }
    else
    {
        // scalloped fringe
        hair.quadraticTo (c.x + rx * 0.92f, fy + 2.0f, c.x + rx * 0.62f, fy);
        hair.quadraticTo (c.x + rx * 0.45f, fy - 16.0f, c.x + rx * 0.2f, fy + 3.0f);
        hair.quadraticTo (c.x, fy - 18.0f, c.x - rx * 0.24f, fy + 1.0f);
        hair.quadraticTo (c.x - rx * 0.48f, fy - 15.0f, c.x - rx * 0.68f, fy + 4.0f);
        hair.quadraticTo (c.x - rx * 0.92f, fy, leftEnd.x, leftEnd.y);
    }
    hair.closeSubPath();

    g.setGradientFill (ColourGradient (colour.brighter (0.25f), c.x - rx * 0.3f, c.y - ry, colour.darker (0.2f), c.x + rx * 0.5f, fy, false));
    g.fillPath (hair);
    g.setColour (ink);
    g.strokePath (hair, PathStrokeType (3.5f, PathStrokeType::curved, PathStrokeType::rounded));

    Path shine;
    shine.addCentredArc (c.x, cy, hx * 0.8f, hy * 0.82f, 0.0f, -0.9f, -0.35f, true);
    g.setColour (Colours::white.withAlpha (0.35f));
    g.strokePath (shine, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
}

void FaceStage::drawEyes (Graphics& g, Point<float> c, float rx, float ry)
{
    const float eyeScale = (voiceType == child ? 1.18f : 1.0f) * (1.0f + 0.12f * surprise);
    const float erx = 19.0f * eyeScale, ery = 23.0f * eyeScale;
    const float eyeY = c.y - ry * 0.14f;
    const float spacing = rx * 0.4f;

    for (int side = -1; side <= 1; side += 2)
    {
        const float s = (float) side;
        const Point<float> ec (c.x + s * spacing, eyeY);
        auto eye = Rectangle<float> (erx * 2.0f, ery * 2.0f).withCentre (ec);

        // eyebrow (wiggles with vibrato, jumps with surprise)
        {
            const float raise = 4.0f * singing + 9.0f * surprise + 3.0f * giggle;
            const float wig = vibrato * 4.5f;
            const Point<float> bc (ec.x, eye.getY() - 10.0f - raise - wig);
            const float angle = s * (0.08f + vibrato * 0.12f) - s * 0.1f * surprise;
            Path brow;
            brow.startNewSubPath (bc.x - 16.0f, bc.y + 4.0f);
            brow.quadraticTo (bc.x, bc.y - 6.0f, bc.x + 16.0f, bc.y + 4.0f);
            brow.applyTransform (AffineTransform::rotation (angle, bc.x, bc.y));
            g.setColour (voiceType == robot ? ink.withAlpha (0.8f) : ink);
            g.strokePath (brow, PathStrokeType (voiceType == bass ? 8.5f : 5.5f, PathStrokeType::curved, PathStrokeType::rounded));
        }

        const bool happy = giggle > 0.25f;
        const float closed = happy ? 1.0f : jmax (blink, sleep);

        if (closed > 0.92f)
        {
            Path lid;
            if (happy)
            {
                lid.startNewSubPath (ec.x - erx * 0.8f, ec.y + 4.0f);
                lid.quadraticTo (ec.x, ec.y - ery * 0.9f, ec.x + erx * 0.8f, ec.y + 4.0f);
            }
            else
            {
                lid.startNewSubPath (ec.x - erx * 0.85f, ec.y);
                lid.quadraticTo (ec.x, ec.y + ery * 0.55f, ec.x + erx * 0.85f, ec.y);
            }
            g.setColour (ink);
            g.strokePath (lid, PathStrokeType (4.5f, PathStrokeType::curved, PathStrokeType::rounded));
            continue;
        }

        g.setColour (Colours::white);
        g.fillEllipse (eye);

        // pupil
        const auto pc = ec + Point<float> (look.x * erx * 0.38f, look.y * ery * 0.32f + ery * 0.08f);
        const float pr = erx * 0.52f;
        if (voiceType == robot)
        {
            g.setColour (teal.darker (0.2f));
            g.fillEllipse (Rectangle<float> (pr * 2.0f, pr * 2.0f).withCentre (pc));
            g.setColour (Colours::white.withAlpha (0.85f));
            g.fillEllipse (Rectangle<float> (pr * 0.9f, pr * 0.9f).withCentre (pc));
        }
        else
        {
            g.setColour (ink);
            g.fillEllipse (Rectangle<float> (pr * 2.0f, pr * 2.2f).withCentre (pc));
            g.setColour (Colours::white);
            g.fillEllipse (Rectangle<float> (pr * 0.75f, pr * 0.75f).withCentre (pc + Point<float> (-pr * 0.35f, -pr * 0.45f)));
            g.fillEllipse (Rectangle<float> (pr * 0.32f, pr * 0.32f).withCentre (pc + Point<float> (pr * 0.4f, pr * 0.4f)));
        }

        // eyelid
        if (closed > 0.01f)
        {
            Graphics::ScopedSaveState save (g);
            Path clip;
            clip.addEllipse (eye);
            g.reduceClipRegion (clip);
            const float lidY = eye.getY() + eye.getHeight() * closed;
            g.setColour (skinFor (voiceType).darker (0.06f));
            g.fillRect (eye.withBottom (lidY));
            g.setColour (ink);
            g.drawLine (eye.getX(), lidY, eye.getRight(), lidY, 3.0f);
        }

        g.setColour (ink);
        g.drawEllipse (eye, 3.2f);

        // lashes
        if (voiceType == alto || voiceType == soprano)
        {
            const int count = voiceType == soprano ? 3 : 2;
            for (int i = 0; i < count; ++i)
            {
                const float a = s * (0.55f + 0.32f * (float) i);
                const auto from = ec + Point<float> (std::sin (a) * erx, -std::cos (a) * ery);
                const auto to = ec + Point<float> (std::sin (a) * (erx + 9.0f), -std::cos (a) * (ery + 8.0f));
                g.drawLine ({ from, to }, 3.0f);
            }
        }
    }
}

void FaceStage::drawHead (Graphics& g)
{
    const auto c = headCentre;
    const float rx = headRx * (voiceType == child ? 0.96f : 1.0f);
    const float ry = headRy * (voiceType == bass ? 1.02f : 1.0f);
    const auto skin = skinFor (voiceType);

    drawCostume (g, c, rx, ry, false);

    // head (a slightly pear-shaped blob)
    Path head;
    const float k = 0.5523f;
    const float midY = c.y + ry * 0.08f;
    head.startNewSubPath (c.x, c.y - ry);
    head.cubicTo (c.x + rx * k * 1.05f, c.y - ry, c.x + rx, midY - ry * k * 1.1f, c.x + rx, midY);
    head.cubicTo (c.x + rx, midY + ry * k * 0.95f, c.x + rx * k * 1.12f, c.y + ry, c.x, c.y + ry);
    head.cubicTo (c.x - rx * k * 1.12f, c.y + ry, c.x - rx, midY + ry * k * 0.95f, c.x - rx, midY);
    head.cubicTo (c.x - rx, midY - ry * k * 1.1f, c.x - rx * k * 1.05f, c.y - ry, c.x, c.y - ry);
    head.closeSubPath();

    g.setGradientFill (ColourGradient (skin.brighter (0.12f), c.x - rx * 0.4f, c.y - ry * 0.6f, skin.darker (0.1f),
                                       c.x + rx * 0.6f, c.y + ry, true));
    g.fillPath (head);

    {
        Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (head);
        // shading on the lower right, sheen on the upper left
        g.setColour (ink.withAlpha (0.07f));
        g.fillEllipse (Rectangle<float> (rx * 2.4f, ry * 2.4f).withCentre ({ c.x + rx * 0.45f, c.y + ry * 0.55f }));
        g.setColour (skin.brighter (0.2f).withAlpha (0.8f));
        g.fillEllipse (Rectangle<float> (rx * 0.62f, ry * 0.36f).withCentre ({ c.x - rx * 0.42f, c.y - ry * 0.6f }));

        if (voiceType == robot)
        {
            g.setColour (ink.withAlpha (0.25f));
            g.drawLine (c.x - rx, c.y - ry * 0.48f, c.x + rx, c.y - ry * 0.48f, 2.0f);
            for (int i = -1; i <= 1; i += 2)
            {
                g.setColour (ink.withAlpha (0.45f));
                g.fillEllipse (Rectangle<float> (6.0f, 6.0f).withCentre ({ c.x + (float) i * rx * 0.62f, c.y - ry * 0.62f }));
            }
        }
    }

    g.setColour (ink);
    g.strokePath (head, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));

    drawHair (g, c, rx, ry);

    // cheeks blush with loudness
    const float blush = 0.16f + 0.62f * level;
    for (int side = -1; side <= 1; side += 2)
    {
        const float s = (float) side;
        const Point<float> cc (c.x + s * rx * 0.62f, c.y + ry * 0.3f);
        const float grow = 1.0f + 0.18f * level;
        g.setColour ((voiceType == robot ? coral.interpolatedWith (plum, 0.3f) : coral).withAlpha (blush));
        g.fillEllipse (Rectangle<float> (36.0f * grow, 21.0f * grow).withCentre (cc));

        if (voiceType == child)
        {
            g.setColour (Colour (0xffc9785a).withAlpha (0.6f));
            for (int i = 0; i < 3; ++i)
                g.fillEllipse (Rectangle<float> (3.5f, 3.5f).withCentre (cc + Point<float> ((float) (i - 1) * 7.0f, (i == 1 ? -4.0f : 2.0f))));
        }
    }

    drawEyes (g, c, rx, ry);

    // nose
    {
        const Point<float> nc (c.x, c.y + ry * 0.14f);
        auto nose = Rectangle<float> (22.0f, 16.0f).withCentre (nc);
        g.setColour (skin.darker (0.1f));
        g.fillEllipse (nose);
        Path under;
        under.addCentredArc (nc.x, nc.y, 11.0f, 8.0f, 0.0f, MathConstants<float>::pi * 0.62f, MathConstants<float>::pi * 1.38f, true);
        g.setColour (ink);
        g.strokePath (under, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (Colours::white.withAlpha (0.55f));
        g.fillEllipse (Rectangle<float> (6.0f, 4.0f).withCentre (nc.translated (-4.0f, -3.0f)));
    }

    // mouth
    auto shape = mouthForVowel (vowel);
    const float sing = jlimit (0.0f, 1.0f, singing);
    shape.smile = jmap (sing, 0.6f + 0.3f * giggle, shape.smile);
    shape.width = jmap (sing, 0.5f, shape.width);
    shape.pucker = jmax (shape.pucker * sing, lips * 0.4f);
    const float snore = sleep * (0.25f + 0.2f * std::sin ((float) clock * 1.3f));
    if (sleep > 0.0f)
    {
        const auto o = mouthForVowel (3.4f);
        shape.width = jmap (sleep, shape.width, o.width * 0.7f);
        shape.smile = jmap (sleep, shape.smile, 0.0f);
        shape.round = jmap (sleep, shape.round, 1.0f);
        shape.teeth *= 1.0f - sleep;
    }
    const float mouthOpen = jlimit (0.0f, 1.0f, open * (1.0f - 0.92f * lips) + giggle * 0.35f * (1.0f - sing) + snore);
    const Point<float> mc (c.x, c.y + ry * 0.46f);
    const float mouthSize = voiceType == child ? 100.0f : 112.0f;
    drawMouth (g, mc, mouthSize, shape, mouthOpen, 3.8f, voiceType == robot ? 1.0f : 0.0f, level);

    if (voiceType == bass)
    {
        // a magnificent moustache that rides on the upper lip
        const float hUp = mouthSize * 0.62f * shape.height * mouthOpen * 0.3f;
        const Point<float> mo (mc.x, mc.y - hUp - 9.0f);
        for (int side = -1; side <= 1; side += 2)
        {
            const float s = (float) side;
            Path m;
            m.startNewSubPath (mo.x, mo.y - 4.0f);
            m.cubicTo (mo.x + s * 16.0f, mo.y - 14.0f, mo.x + s * 34.0f, mo.y - 6.0f, mo.x + s * 40.0f, mo.y + 4.0f);
            m.cubicTo (mo.x + s * 44.0f, mo.y - 4.0f, mo.x + s * 48.0f, mo.y - 4.0f, mo.x + s * 46.0f, mo.y - 10.0f);
            m.cubicTo (mo.x + s * 52.0f, mo.y + 2.0f, mo.x + s * 40.0f, mo.y + 12.0f, mo.x + s * 26.0f, mo.y + 6.0f);
            m.cubicTo (mo.x + s * 14.0f, mo.y + 2.0f, mo.x + s * 6.0f, mo.y + 4.0f, mo.x, mo.y + 2.0f);
            m.closeSubPath();
            fillAndStroke (g, m, plum, 2.5f);
        }
    }

    drawCostume (g, c, rx, ry, true);
}

void FaceStage::drawBubble (Graphics& g)
{
    String text;
    bool isHint = false;
    if (! syllables.isEmpty())
        text = syllables.joinIntoString ("-");
    else if (sungText.isNotEmpty() && singing > 0.2f)
        text = sungText;

    float alpha = bubbleAlpha;
    if (text.isEmpty())
    {
        if (hintAlpha < 0.01f)
            return;
        text = sleep > 0.5f ? "Zzz..." : "Play me!";
        isHint = true;
        alpha = hintAlpha;
    }
    if (alpha < 0.01f)
        return;

    const float w = (float) getWidth();
    float fontH = 26.0f;
    auto font = aa::Fonts::display (fontH);
    const float maxW = w * 0.5f - 44.0f;
    float tw = aa::Fonts::textWidth (font, text);
    if (tw > maxW - 30.0f)
    {
        fontH *= (maxW - 30.0f) / tw;
        font = aa::Fonts::display (fontH);
        tw = aa::Fonts::textWidth (font, text);
    }

    const float bw = jlimit (92.0f, maxW, tw + 34.0f), bh = 50.0f;
    auto bubble = Rectangle<float> (w - 14.0f - bw, 12.0f, bw, bh);
    const float pop = 1.0f + 0.1f * bubblePop;
    const auto pivot = Point<float> (bubble.getX() + 20.0f, bubble.getBottom());

    Graphics::ScopedSaveState save (g);
    g.addTransform (AffineTransform::scale (pop, pop, pivot.x, pivot.y));
    g.beginTransparencyLayer (alpha);

    Path shape;
    shape.addRoundedRectangle (bubble, bh * 0.5f);
    Path tail;
    tail.startNewSubPath (bubble.getX() + 22.0f, bubble.getBottom() - 6.0f);
    tail.quadraticTo (bubble.getX() + 14.0f, bubble.getBottom() + 12.0f, bubble.getX() - 6.0f, bubble.getBottom() + 22.0f);
    tail.quadraticTo (bubble.getX() + 22.0f, bubble.getBottom() + 14.0f, bubble.getX() + 40.0f, bubble.getBottom() - 6.0f);
    tail.closeSubPath();
    shape.addPath (tail);
    // merge the tail into the bubble outline
    Path merged = shape;

    g.setColour (ink.withAlpha (0.28f));
    g.fillPath (merged, AffineTransform::translation (0.0f, 4.0f));
    g.setColour (ink);
    g.strokePath (merged, PathStrokeType (6.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (cream);
    g.fillPath (merged);

    const float baseline = bubble.getCentreY() + fontH * 0.33f;
    if (isHint || syllables.isEmpty())
    {
        const float x = bubble.getCentreX() - tw * 0.5f;
        drawOutlinedText (g, text, font, { x, baseline }, isHint ? plum : coral, ink, isHint ? 0.01f : 4.0f);
    }
    else
    {
        float x = bubble.getCentreX() - tw * 0.5f;
        for (int i = 0; i < syllables.size(); ++i)
        {
            const bool last = i == syllables.size() - 1;
            const String part = syllables[i] + (last ? "" : "-");
            if (last)
                drawOutlinedText (g, part, font, { x, baseline }, coral, ink, 4.0f);
            else
            {
                g.setColour (plum.withAlpha (0.55f));
                g.setFont (font);
                g.drawSingleLineText (part, roundToInt (x), roundToInt (baseline));
            }
            x += aa::Fonts::textWidth (font, part);
        }
    }

    g.endTransparencyLayer();
}

//==============================================================================
void FaceStage::paint (Graphics& g)
{
    const float physScale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (background.isNull() || std::abs ((float) background.getWidth() - (float) getWidth() * jlimit (1.0f, 4.0f, physScale)) > 2.0f)
        rebuildBackground (physScale);

    auto b = getLocalBounds().toFloat();
    g.drawImage (background, b);

    {
        Graphics::ScopedSaveState save (g);
        Path clip;
        clip.addRoundedRectangle (b.reduced (1.0f), stageRadius);
        g.reduceClipRegion (clip);

        // the spotlight breathes with the loudness
        if (level > 0.01f)
        {
            g.setGradientFill (ColourGradient (sunshine.withAlpha (0.28f * level), headCentre.x, headCentre.y,
                                               sunshine.withAlpha (0.0f), headCentre.x + headRx * 2.0f, headCentre.y, true));
            g.fillEllipse (Rectangle<float> (headRx * 4.0f, headRx * 4.0f).withCentre (headCentre));
        }

        drawNotes (g);

        const float breathe = std::sin ((float) clock * (1.7f - 0.6f * sleep)) * (1.5f + 2.0f * sleep);
        drawBody (g, bounce * 0.25f + breathe * 0.5f);

        {
            Graphics::ScopedSaveState headState (g);
            const Point<float> pivot (headCentre.x, headCentre.y + headRy * 0.9f);
            const float sway = std::sin ((float) clock * 2.3f) * 0.025f * singing;
            g.addTransform (AffineTransform::translation (-pivot.x, -pivot.y)
                                .scaled (1.0f + squash * 0.45f, 1.0f - squash * 0.45f)
                                .rotated (tilt + sway)
                                .translated (pivot.x, pivot.y + bounce + breathe));
            drawHead (g);
        }
    }

    drawBubble (g);

    g.setColour (ink);
    g.drawRoundedRectangle (b.reduced (1.5f), stageRadius, 3.0f);
}
} // namespace babble::ui
