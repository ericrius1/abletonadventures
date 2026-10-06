#include "CritterEditor.h"
#include <PluginAssets.h>

using namespace juce;
namespace cc = critter::colours;

namespace
{
    constexpr int baseWidth = 940, baseHeight = 640;
    constexpr float pi = MathConstants<float>::pi;

    const Rectangle<float> padsArea { 16.0f, 64.0f, 516.0f, 270.0f };
    const Rectangle<float> cardArea { 542.0f, 64.0f, 382.0f, 270.0f };
    const Rectangle<float> patternArea { 16.0f, 344.0f, 696.0f, 280.0f };
    const Rectangle<float> masterArea { 722.0f, 344.0f, 202.0f, 280.0f };

    const Colour pink { 0xffff6f91 };
    const Colour mint { 0xff2fbf86 };
    const Colour sky { 0xff3aa8f0 };
    const Colour grape { 0xff8b6cf0 };
    const Colour tangerine { 0xffff8a3d };

    const char* noteNames[critter::numVoices] = { "C1", "C#1", "D1", "D#1", "E1", "F1", "F#1", "G1" };

    const char* personalities[critter::numVoices] = {
        "Big-hearted bass bug. Thumps when happy, which is always.",
        "A crab with a crack. Snaps on the two and the four.",
        "Applauds everything. Even silence. Especially silence.",
        "Tiny chick, tight beak. Ticks all day long.",
        "Lazy snake. Hisses until Tiki shushes him.",
        "Bouncy bear drum. Rolls downhill for fun.",
        "Wears a cowbell. Keeps asking for more cowbell.",
        "Space jelly from planet Pew. Pew pew!"
    };

    const char* toneTips[critter::numVoices] = {
        "Tone: clean sub (left) to warm, driven thump (right)",
        "Tone: darker or brighter snares",
        "Tone: pitch of the clap noise",
        "Tone: brightness of the metal",
        "Tone: brightness of the metal",
        "Tone: pure sine (left) to growly triangle (right)",
        "Tone: colour of the bell",
        "Tone: FM grit of the laser"
    };

    const char* snapTips[critter::numVoices] = {
        "Snap: beater click and pitch punch",
        "Snap: more snares and crack, less drum body",
        "Snap: tighter, faster hand claps",
        "Snap: crisper attack and more sizzle",
        "Snap: crisper attack and more sizzle",
        "Snap: deeper pitch drop and stick attack",
        "Snap: cowbell (left) to wooden clave (right)",
        "Snap: sweep depth, from blip to full laser"
    };

    aa::Theme makeTheme()
    {
        aa::Theme t;
        t.background = Colour (0xfffff4e8);
        t.backgroundAlt = Colour (0xffeadcff);
        t.panel = Colours::white.withAlpha (0.6f);
        t.panelOutline = Colours::white.withAlpha (0.95f);
        t.text = cc::ink;
        t.textDim = cc::ink.withAlpha (0.6f);
        t.accent = pink;
        t.accent2 = Colour (0xffffb84d);
        t.knobBody = Colour (0xfffffbf6);
        t.knobTrack = cc::ink.withAlpha (0.1f);
        t.shadow = cc::ink.withAlpha (0.22f);
        t.popupBackground = Colour (0xfffffaf4);
        t.pill = Colours::white.withAlpha (0.85f);
        t.cornerRadius = 18.0f;
        t.glow = false;
        return t;
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

    void drawPanel (Graphics& g, Rectangle<float> r, float radius = 18.0f)
    {
        g.setColour (cc::ink.withAlpha (0.05f));
        g.fillRoundedRectangle (r.translated (0.0f, 3.0f).expanded (1.0f), radius + 1.0f);
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.72f), 0.0f, r.getY(),
                                           Colours::white.withAlpha (0.5f), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, radius);
        g.setColour (Colours::white.withAlpha (0.95f));
        g.drawRoundedRectangle (r.reduced (0.75f), radius, 1.5f);
    }

    void drawSectionTitle (Graphics& g, Point<float> topLeft, const String& title, Colour dot)
    {
        const auto font = aa::Fonts::uiBold (11.5f).withExtraKerningFactor (0.14f);
        g.setFont (font);
        g.setColour (cc::ink.withAlpha (0.62f));
        g.drawText (title.toUpperCase(), Rectangle<float> (topLeft.x, topLeft.y, 200.0f, 16.0f), Justification::centredLeft, false);
        const float w = aa::Fonts::textWidth (font, title.toUpperCase());
        g.setColour (dot);
        g.fillEllipse (topLeft.x + w + 6.0f, topLeft.y + 5.5f, 5.0f, 5.0f);
    }

    Image makeImage (int w, int h, float scale, bool opaque = false)
    {
        return Image (opaque ? Image::RGB : Image::ARGB, jmax (1, roundToInt ((float) w * scale)),
                      jmax (1, roundToInt ((float) h * scale)), true);
    }

    float physicalScale (Graphics& g) { return jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor()); }
} // namespace

//==============================================================================
// Look & feel
//==============================================================================
void CritterLookAndFeel::drawKnob (Graphics& g, Rectangle<float> bounds, float proportion, bool bipolar, bool hovered,
                                   bool dragging, Colour accent, aa::Knob&)
{
    const float size = jmin (bounds.getWidth(), bounds.getHeight());
    const auto c = bounds.getCentre();
    const float r = size * 0.5f - 1.5f;
    const float startA = -pi * 0.75f, endA = pi * 0.75f;
    const float valueA = startA + jlimit (0.0f, 1.0f, proportion) * (endA - startA);
    const float arcR = r * 0.82f, arcW = jmax (3.0f, r * 0.17f);
    const auto stroke = PathStrokeType (arcW, PathStrokeType::curved, PathStrokeType::rounded);

    Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startA, endA, true);
    g.setColour (cc::ink.withAlpha (0.09f));
    g.strokePath (track, stroke);

    const float fromA = bipolar ? 0.0f : startA;
    if (std::abs (valueA - fromA) > 0.01f)
    {
        Path value;
        value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, jmin (fromA, valueA), jmax (fromA, valueA), true);
        g.setColour (accent.withMultipliedAlpha (hovered || dragging ? 1.0f : 0.92f));
        g.strokePath (value, stroke);
    }

    // body: a little white candy with a cartoon outline
    const float bodyR = r * 0.6f;
    auto body = Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c);
    g.setColour (cc::ink.withAlpha (0.1f));
    g.fillEllipse (body.translated (0.0f, bodyR * 0.14f).expanded (bodyR * 0.03f));
    g.setGradientFill (ColourGradient (Colours::white, body.getX() + bodyR * 0.55f, body.getY() + bodyR * 0.35f,
                                       Colour (0xfff2e8fb), body.getRight(), body.getBottom(), true));
    g.fillEllipse (body);
    g.setColour (accent.withAlpha (hovered || dragging ? 0.3f : 0.18f));
    g.drawEllipse (body.reduced (bodyR * 0.24f), jmax (1.5f, bodyR * 0.13f));
    g.setColour (cc::ink.withAlpha (0.9f));
    g.drawEllipse (body, jmax (1.4f, r * 0.05f));

    const Point<float> dir (std::sin (valueA), -std::cos (valueA));
    Path pointer;
    pointer.startNewSubPath (c + dir * (bodyR * 0.22f));
    pointer.lineTo (c + dir * (bodyR * 0.7f));
    g.setColour (cc::ink);
    g.strokePath (pointer, PathStrokeType (jmax (2.0f, bodyR * 0.17f), PathStrokeType::curved, PathStrokeType::rounded));

    const auto tip = c + dir * arcR;
    g.setColour (Colours::white);
    g.fillEllipse (Rectangle<float> (arcW * 1.05f, arcW * 1.05f).withCentre (tip));
    g.setColour (cc::ink.withAlpha (0.8f));
    g.drawEllipse (Rectangle<float> (arcW * 1.05f, arcW * 1.05f).withCentre (tip), 1.0f);
}

void CritterLookAndFeel::drawKnobCaption (Graphics& g, Rectangle<float> area, const String& text, bool showingValue,
                                          Colour accent, aa::Knob&)
{
    const float h = jlimit (9.5f, 12.5f, area.getHeight() * 0.8f);
    if (showingValue)
    {
        g.setFont (aa::Fonts::uiBold (h + 0.5f));
        g.setColour (accent.withBrightness (jmin (accent.getBrightness(), 0.62f)));
        g.drawFittedText (text, area.toNearestInt(), Justification::centred, 1, 0.8f);
    }
    else
    {
        g.setFont (aa::Fonts::uiBold (h).withExtraKerningFactor (0.07f));
        g.setColour (cc::ink.withAlpha (0.64f));
        g.drawFittedText (text.toUpperCase(), area.toNearestInt(), Justification::centred, 1, 0.8f);
    }
}

//==============================================================================
MiniKnob::MiniKnob (AudioProcessorValueTreeState& state, const String& paramID, const String& label)
    : aa::Knob (state, paramID, label), caption (label)
{
    setCaptionVisible (false);
}

void MiniKnob::paint (Graphics& g)
{
    auto* lnf = aa::findLookAndFeel (*this);
    if (lnf == nullptr)
        return;

    auto b = getLocalBounds().toFloat();
    auto dial = b.removeFromLeft (b.getHeight());
    const bool active = isMouseOverOrDragging();
    lnf->drawKnob (g, dial, getProportion(), false, active, isMouseButtonDown(), getAccent(), *this);

    b.removeFromLeft (6.0f);
    auto top = b.removeFromTop (b.getHeight() * 0.5f);
    g.setFont (aa::Fonts::uiBold (10.0f).withExtraKerningFactor (0.08f));
    g.setColour (cc::ink.withAlpha (0.58f));
    g.drawText (caption.toUpperCase(), top, Justification::bottomLeft, false);
    g.setFont (aa::Fonts::uiBold (13.0f));
    const auto acc = getAccent();
    g.setColour (active ? acc.withBrightness (jmin (acc.getBrightness(), 0.6f)) : cc::ink);
    g.drawText (getTextFromValue (getValue()), b, Justification::topLeft, false);
}

//==============================================================================
// Critter pad
//==============================================================================
CritterPad::CritterPad (CritterEditor& e, CritterProcessor& p, int v) : editor (e), proc (p), voice (v)
{
    setTooltip (String (critter::critterName (v)) + " the " + String (critter::roleName (v)).toLowerCase()
                + " (" + noteNames[v] + "). Click to play, drop an audio file to feed, right-click for options.");
    setMouseCursor (MouseCursor::PointingHandCursor);
    random.setSeed ((int64) v * 977 + 13);
    blinkTimer = 1.0f + random.nextFloat() * 4.0f;
    fidgetTimer = 4.0f + random.nextFloat() * 9.0f;
    time = random.nextFloat() * 10.0f;
    lastHasSample = proc.getSample (v) != nullptr;
    badgePop = lastHasSample ? 1.0f : 0.0f;
}

Rectangle<float> CritterPad::critterArea() const
{
    auto b = getLocalBounds().toFloat();
    return { b.getX() + 8.0f, b.getY() + 5.0f, b.getWidth() - 16.0f, b.getHeight() - 49.0f };
}

void CritterPad::setSelected (bool s)
{
    if (selected != s)
    {
        selected = s;
        if (s)
            squashVel -= 4.0f; // a happy little hop
        repaint();
    }
}

void CritterPad::hit (float velocity)
{
    velocity = jlimit (0.1f, 1.0f, velocity);
    squashVel += 6.0f + 7.0f * velocity;
    mouth = jmax (mouth, 0.55f + 0.45f * velocity);
    excited = 1.0f;
    action = 1.0f;
    flash = jmax (flash, 0.5f + 0.5f * velocity);
    blinkPhase = -1.0f;
    blink = 0.0f;
    spawnParticles (velocity > 0.8f ? 3 : 2, velocity);
}

void CritterPad::spawnParticles (int count, float strength)
{
    const auto area = critterArea();
    const Point<float> origin (area.getCentreX(), area.getY() + area.getHeight() * 0.35f);
    for (auto& p : particles)
    {
        if (count <= 0)
            break;
        if (p.life > 0.0f)
            continue;
        const float a = (random.nextFloat() - 0.5f) * 2.4f;
        const float speed = 50.0f + 60.0f * strength * random.nextFloat();
        p.pos = origin + Point<float> ((random.nextFloat() - 0.5f) * 30.0f, 0.0f);
        p.vel = { std::sin (a) * speed, -std::cos (a) * speed - 30.0f };
        p.life = 1.0f;
        p.size = 3.0f + random.nextFloat() * 3.5f;
        p.spin = random.nextFloat() * pi;
        p.shape = random.nextInt (3);
        --count;
    }
}

void CritterPad::showBubble (const String& text, float seconds)
{
    bubbleText = text;
    bubbleTime = bubbleDuration = seconds;
    repaint();
}

void CritterPad::startNom()
{
    nomTime = 0.0f;
    showBubble ("nom nom!", 1.3f);
    excited = 0.0f;
}

void CritterPad::onSampleChanged (bool nowHasSample)
{
    if (nowHasSample)
    {
        badgePop = 0.0f;
        if (nomTime < 0.0f)
            nomTime = 0.35f;
        action = 1.0f;
        showBubble ("yum!", 1.1f);
        spawnParticles (5, 0.8f);
    }
    else if (lastHasSample)
    {
        showBubble ("synth again!", 1.2f);
        squashVel -= 5.0f;
    }
    lastHasSample = nowHasSample;
    repaint();
}

void CritterPad::onLoadFailed()
{
    nomTime = -1.0f;
    shake = 1.0f;
    showBubble ("yuck!", 1.6f);
}

void CritterPad::tick (double dtSeconds, Point<float> mouse, bool mouseNear)
{
    const float dt = (float) dtSeconds;
    time += dt;
    bool animating = false;

    // squash & stretch spring (sub-stepped so it stays stable at low frame rates)
    {
        const int steps = jmax (1, (int) std::ceil (dt / 0.004f));
        const float h = dt / (float) steps;
        for (int i = 0; i < steps; ++i)
        {
            const float acc = -340.0f * squash - 15.0f * squashVel;
            squashVel += acc * h;
            squash += squashVel * h;
        }
        if (std::abs (squash) > 0.002f || std::abs (squashVel) > 0.02f)
            animating = true;
        else
            squash = squashVel = 0.0f;
    }

    auto decay = [&] (float& v, float rate)
    {
        if (v > 0.0f)
        {
            v = jmax (0.0f, v - dt * rate);
            animating = true;
        }
    };
    decay (mouth, 3.2f);
    decay (excited, 4.5f);
    decay (action, 2.4f);
    decay (flash, 3.5f);
    decay (shake, 1.6f);

    // blinking
    blinkTimer -= dt;
    if (blinkTimer <= 0.0f && blinkPhase < 0.0f)
    {
        blinkPhase = 0.0f;
        blinkTimer = 1.8f + random.nextFloat() * 4.5f;
        if (random.nextFloat() < 0.2f)
            blinkTimer = 0.25f; // double blink
    }
    if (blinkPhase >= 0.0f)
    {
        blinkPhase += dt;
        const float t = blinkPhase / 0.15f;
        blink = t < 0.5f ? t * 2.0f : jmax (0.0f, 2.0f - t * 2.0f);
        if (t >= 1.0f)
        {
            blinkPhase = -1.0f;
            blink = 0.0f;
        }
        animating = true;
    }

    // eyes follow the mouse (or wander about when it's far away)
    {
        const auto area = critterArea();
        const Point<float> eyes ((float) getX() + area.getCentreX(), (float) getY() + area.getY() + area.getHeight() * 0.45f);
        if (mouseNear)
        {
            const auto d = mouse - eyes;
            const float len = d.getDistanceFromOrigin();
            lookTarget = d * (1.0f / (len + 70.0f));
        }
        else
        {
            idleLookTimer -= dt;
            if (idleLookTimer <= 0.0f)
            {
                idleLookTimer = 1.5f + random.nextFloat() * 3.0f;
                idleLook = { (random.nextFloat() - 0.5f) * 1.4f, (random.nextFloat() - 0.5f) * 0.9f };
            }
            lookTarget = idleLook;
        }
        if (shake > 0.0f)
            lookTarget.x = std::sin (time * 40.0f) * shake;

        const auto old = look;
        look += (lookTarget - look) * jmin (1.0f, dt * 12.0f);
        if (old.getDistanceFrom (look) > 0.002f)
            animating = true;
    }

    // drag-hover hunger + nom-nom
    {
        const float target = dragHover ? 1.0f : 0.0f;
        if (std::abs (hungry - target) > 0.001f)
        {
            hungry += (target - hungry) * jmin (1.0f, dt * 10.0f);
            if (std::abs (hungry - target) <= 0.001f)
                hungry = target;
            animating = true;
            fullRepaint = true; // the drop highlight covers the whole card
        }
        if (nomTime >= 0.0f)
        {
            nomTime += dt;
            if ((int) (nomTime * 7.0f) != (int) ((nomTime - dt) * 7.0f))
                spawnParticles (1, 0.3f);
            if (nomTime > 1.2f)
                nomTime = -1.0f;
            animating = true;
        }
    }

    // occasional fidget so idle critters feel alive
    fidgetTimer -= dt;
    if (fidgetTimer <= 0.0f)
    {
        fidgetTimer = 6.0f + random.nextFloat() * 10.0f;
        action = jmax (action, 0.75f);
        squashVel -= 3.5f;
    }

    // badge pop-in
    const float badgeTarget = proc.getSample (voice) != nullptr ? 1.0f : 0.0f;
    if (std::abs (badgePop - badgeTarget) > 0.001f)
    {
        badgePop += (badgeTarget - badgePop) * jmin (1.0f, dt * 9.0f);
        animating = true;
    }
    if (proc.loading[(size_t) voice].load())
        animating = true;

    if (bubbleTime > 0.0f)
    {
        bubbleTime -= dt;
        animating = true;
        if (bubbleTime <= 0.0f && dragHover)
            bubbleTime = 0.5f;
    }

    for (auto& p : particles)
        if (p.life > 0.0f)
        {
            p.vel.y += 160.0f * dt;
            p.pos += p.vel * dt;
            p.life -= dt * 1.7f;
            p.spin += dt * 4.0f;
            animating = true;
        }

    // only the critter's stage animates; the name plate below it is static
    if (fullRepaint)
        repaint();
    else if (animating)
        repaint (0, 0, getWidth(), roundToInt (critterArea().getBottom()) + 4);
    fullRepaint = false;
}

void CritterPad::rebuildBackground (float scale)
{
    cachedBackground = makeImage (getWidth(), getHeight(), scale);
    Graphics g (cachedBackground);
    g.addTransform (AffineTransform::scale (scale));

    const auto col = cc::critter (voice);
    auto b = getLocalBounds().toFloat().reduced (1.5f);

    Path card;
    card.addRoundedRectangle (b, 16.0f);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.9f), 0.0f, b.getY(),
                                       Colours::white.interpolatedWith (col, 0.32f), 0.0f, b.getBottom(), false));
    g.fillPath (card);

    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (card);
        // polka dots that thicken towards the bottom
        const float spacing = 13.0f;
        for (int row = 0; row < 20; ++row)
        {
            const float y = b.getY() + 6.0f + (float) row * spacing * 0.866f;
            const float fade = jlimit (0.0f, 1.0f, (y - b.getY()) / b.getHeight());
            for (int c = 0; c < 14; ++c)
            {
                const float x = b.getX() + 4.0f + (float) c * spacing + ((row % 2) ? spacing * 0.5f : 0.0f);
                g.setColour (col.withAlpha (0.05f + 0.13f * fade));
                g.fillEllipse (Rectangle<float> (2.6f, 2.6f).withCentre ({ x, y }));
            }
        }
        // little stage under the critter
        const auto area = critterArea();
        g.setColour (col.withAlpha (0.2f));
        g.fillEllipse (Rectangle<float> (area.getWidth() * 0.86f, 12.0f).withCentre ({ area.getCentreX(), area.getBottom() - area.getHeight() * 0.04f }));
    }

    g.setColour (Colours::white.withAlpha (0.95f));
    g.drawRoundedRectangle (b, 16.0f, 1.5f);

    // name + role
    auto text = b.withTop (b.getBottom() - 37.0f);
    g.setColour (cc::ink);
    g.setFont (aa::Fonts::display (19.0f));
    g.drawText (critter::critterName (voice), text.removeFromTop (20.0f), Justification::centred, false);
    g.setColour (cc::ink.withAlpha (0.5f));
    g.setFont (aa::Fonts::uiBold (9.0f).withExtraKerningFactor (0.12f));
    g.drawText (String (critter::roleName (voice)).toUpperCase() + "  " + noteNames[voice], text.removeFromTop (12.0f),
                Justification::centred, false);
}

void CritterPad::paint (Graphics& g)
{
    const float scale = physicalScale (g);
    if (cachedBackground.isNull() || cachedBackground.getWidth() != roundToInt ((float) getWidth() * scale))
        rebuildBackground (scale);
    g.drawImage (cachedBackground, getLocalBounds().toFloat());

    const auto col = cc::critter (voice);
    const auto deep = cc::deep (voice);
    auto b = getLocalBounds().toFloat().reduced (1.5f);

    if (flash > 0.0f)
    {
        // a soft halo pops behind the critter on every hit
        const auto area = critterArea();
        const float d = area.getHeight() * (0.8f + 0.35f * (1.0f - flash));
        const auto halo = Rectangle<float> (d * 1.25f, d).withCentre ({ area.getCentreX(), area.getY() + area.getHeight() * 0.55f });
        g.setColour (col.withAlpha (0.28f * flash));
        g.fillEllipse (halo);
        g.setColour (Colours::white.withAlpha (0.35f * flash));
        g.fillEllipse (halo.reduced (d * 0.16f));
    }
    if (hovered && ! selected)
    {
        g.setColour (Colours::white.withAlpha (0.22f));
        g.fillRoundedRectangle (b, 16.0f);
    }
    if (selected)
    {
        g.setColour (deep);
        g.drawRoundedRectangle (b.reduced (1.0f), 15.0f, 3.0f);
    }
    if (hungry > 0.02f)
    {
        g.setColour (col.withAlpha (0.25f * hungry));
        g.fillRoundedRectangle (b, 16.0f);
        Path outline;
        outline.addRoundedRectangle (b.reduced (2.5f), 14.0f);
        Path dashed;
        const float dashes[] = { 7.0f, 5.0f };
        PathStrokeType (2.5f).createDashedStroke (dashed, outline, dashes, 2,
                                                  AffineTransform::translation (0.0f, 0.0f));
        g.setColour (deep.withAlpha (hungry));
        g.fillPath (dashed);
    }

    // the critter
    critter::Pose pose;
    pose.squash = squash;
    pose.bob = jmax (0.0f, -squash) * 10.0f;
    pose.blink = blink;
    pose.excited = excited > 0.45f ? 1.0f : 0.0f;
    pose.action = action;
    pose.time = time;
    pose.look = look;
    float chew = 0.0f;
    if (nomTime >= 0.0f)
        chew = (0.5f + 0.5f * std::sin (nomTime * MathConstants<float>::twoPi * 3.2f)) * (1.0f - nomTime / 1.2f);
    pose.mouth = jmax (mouth, hungry * 1.25f, chew);
    if (hungry > 0.5f)
        pose.look = { 0.0f, -0.6f };

    auto area = critterArea();
    if (shake > 0.0f)
        area = area.translated (std::sin (time * 45.0f) * 3.0f * shake, 0.0f);
    critter::drawCritter (g, voice, area, pose);

    // particles: sparkles, dots and crumbs
    for (const auto& p : particles)
    {
        if (p.life <= 0.0f)
            continue;
        const float a = jlimit (0.0f, 1.0f, p.life * 1.4f);
        if (p.shape == 0)
        {
            const auto s = critter::sparklePath (p.pos, p.size * 1.3f, p.spin);
            g.setColour (deep.withAlpha (a));
            g.fillPath (s);
        }
        else if (p.shape == 1)
        {
            g.setColour (col.withAlpha (a));
            g.fillEllipse (Rectangle<float> (p.size, p.size).withCentre (p.pos));
            g.setColour (cc::ink.withAlpha (a * 0.6f));
            g.drawEllipse (Rectangle<float> (p.size, p.size).withCentre (p.pos), 1.0f);
        }
        else
        {
            g.setColour (Colours::white.withAlpha (a));
            g.fillEllipse (Rectangle<float> (p.size * 0.8f, p.size * 0.8f).withCentre (p.pos));
        }
    }

    if (auto* s = proc.getSample (voice))
        drawBadge (g, *s);

    if (proc.loading[(size_t) voice].load())
    {
        const auto c = Point<float> ((float) getWidth() - 20.0f, 20.0f);
        for (int i = 0; i < 3; ++i)
        {
            const float a = 0.3f + 0.7f * (0.5f + 0.5f * std::sin (time * 8.0f - (float) i * 0.9f));
            g.setColour (deep.withAlpha (a));
            g.fillEllipse (Rectangle<float> (5.0f, 5.0f).withCentre (c.translated ((float) (i - 1) * 7.0f, 0.0f)));
        }
    }

    drawBubble (g);
}

void CritterPad::drawBadge (Graphics& g, const critter::SampleData& s)
{
    const float pop = badgePop;
    if (pop < 0.02f)
        return;
    const float overshoot = 1.0f + 0.25f * std::sin (jlimit (0.0f, 1.0f, pop) * pi);
    const float r = 13.0f * pop * overshoot;
    const Point<float> c ((float) getWidth() - 20.0f, 20.0f);
    const auto deep = cc::deep (voice);

    g.setColour (cc::ink.withAlpha (0.15f));
    g.fillEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c.translated (0.0f, 1.5f)));
    g.setColour (Colours::white);
    g.fillEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
    g.setColour (deep);
    g.drawEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), 1.8f);

    // mini waveform of what it ate
    const int bars = 9;
    const float w = r * 1.2f;
    for (int i = 0; i < bars; ++i)
    {
        const int idx = jmin ((int) s.overview.size() - 1, i * (int) s.overview.size() / bars);
        const float h = jmax (1.5f, s.overview[(size_t) idx] * r * 1.1f);
        const float x = c.x - w * 0.5f + w * (float) i / (float) (bars - 1);
        g.setColour (deep);
        g.fillRoundedRectangle (Rectangle<float> (1.6f, h).withCentre ({ x, c.y }), 0.8f);
    }
}

void CritterPad::drawBubble (Graphics& g)
{
    if (bubbleTime <= 0.0f || bubbleText.isEmpty())
        return;

    const float age = bubbleDuration - bubbleTime;
    const float in = jlimit (0.0f, 1.0f, age / 0.12f);
    const float out = jlimit (0.0f, 1.0f, bubbleTime / 0.2f);
    const float a = jmin (in, out);
    const float s = 0.8f + 0.2f * in;

    const auto font = aa::Fonts::uiBold (11.0f);
    const float tw = aa::Fonts::textWidth (font, bubbleText);
    auto r = Rectangle<float> (tw + 16.0f, 20.0f).withPosition (7.0f, 6.0f);
    r = r.withSizeKeepingCentre (r.getWidth() * s, r.getHeight() * s);

    Path bubble;
    bubble.addRoundedRectangle (r, 10.0f);
    Path tail;
    tail.startNewSubPath (r.getX() + 16.0f, r.getBottom() - 1.0f);
    tail.lineTo (r.getX() + 28.0f, r.getBottom() + 7.0f);
    tail.lineTo (r.getX() + 26.0f, r.getBottom() - 1.0f);
    tail.closeSubPath();
    bubble.addPath (tail);

    g.setColour (Colours::white.withAlpha (a));
    g.fillPath (bubble);
    g.setColour (cc::ink.withAlpha (a));
    g.strokePath (bubble, PathStrokeType (1.5f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (Colours::white.withAlpha (a));
    g.fillRect (Rectangle<float> (11.0f, 3.0f).withPosition (r.getX() + 16.0f, r.getBottom() - 2.5f));
    g.setFont (font);
    g.setColour (cc::ink.withAlpha (a));
    g.drawText (bubbleText, r, Justification::centred, false);
}

void CritterPad::mouseDown (const MouseEvent& e)
{
    editor.selectCritter (voice);
    if (e.mods.isPopupMenu())
    {
        editor.showSampleMenu (voice, this);
        return;
    }
    editor.audition (voice);
}

bool CritterPad::isInterestedInFileDrag (const StringArray& files)
{
    for (const auto& f : files)
        if (proc.canLoadFile (f))
            return true;
    return false;
}

void CritterPad::fileDragEnter (const StringArray&, int, int)
{
    dragHover = true;
    showBubble ("nom nom?", 0.8f);
}

void CritterPad::fileDragExit (const StringArray&)
{
    dragHover = false;
    bubbleTime = jmin (bubbleTime, 0.2f);
}

void CritterPad::filesDropped (const StringArray& files, int, int)
{
    dragHover = false;
    for (const auto& f : files)
        if (proc.canLoadFile (f))
        {
            editor.selectCritter (voice);
            proc.loadSampleAsync (voice, File (f));
            startNom();
            return;
        }
}

//==============================================================================
// Step grid
//==============================================================================
StepGrid::StepGrid (CritterEditor& e, CritterProcessor& p) : editor (e), proc (p)
{
    setTooltip ("Click to add or remove a hit (drag to paint). Shift-click or right-click a hit to accent it. "
                "Right-click a critter's name for row tricks.");
}

float StepGrid::rowHeight() const { return ((float) getHeight() - laneHeight) / (float) critter::numVoices; }
float StepGrid::cellPitch() const { return ((float) getWidth() - labelWidth - 3.0f * groupGap) / (float) critter::numSteps; }
float StepGrid::stepX (int step) const { return labelWidth + (float) step * cellPitch() + (float) (step / 4) * groupGap; }

Rectangle<float> StepGrid::cellRect (int row, int step) const
{
    return { stepX (step) + 2.0f, laneHeight + (float) row * rowHeight() + 2.0f, cellPitch() - 4.0f, rowHeight() - 4.0f };
}

Rectangle<float> StepGrid::columnRect (int step) const
{
    return { stepX (step) - 4.0f, 0.0f, cellPitch() + 8.0f, (float) getHeight() };
}

Rectangle<float> StepGrid::laneRect() const
{
    return { labelWidth - 6.0f, 0.0f, (float) getWidth() - labelWidth + 12.0f, laneHeight + 1.0f };
}

bool StepGrid::cellAt (Point<float> p, int& row, int& step) const
{
    if (p.x < labelWidth || p.y < laneHeight)
        return false;
    row = (int) ((p.y - laneHeight) / rowHeight());
    if (! isPositiveAndBelow (row, critter::numVoices))
        return false;
    for (int s = 0; s < critter::numSteps; ++s)
        if (p.x >= stepX (s) && p.x < stepX (s) + cellPitch() + (s % 4 == 3 ? groupGap : 0.0f))
        {
            step = s;
            return true;
        }
    return false;
}

int StepGrid::labelRowAt (Point<float> p) const
{
    if (p.x >= labelWidth - 4.0f || p.y < laneHeight)
        return -1;
    const int row = (int) ((p.y - laneHeight) / rowHeight());
    return isPositiveAndBelow (row, critter::numVoices) ? row : -1;
}

void StepGrid::drawEmptyCell (Graphics& g, Rectangle<float> cell, bool hover, int row) const
{
    constexpr float radius = 6.0f;
    g.setColour (Colours::white.withAlpha (hover ? 1.0f : 0.82f));
    g.fillRoundedRectangle (cell, radius);
    g.setColour (hover ? cc::deep (row).withAlpha (0.6f) : cc::ink.withAlpha (0.07f));
    g.drawRoundedRectangle (cell.reduced (0.5f), radius, hover ? 1.5f : 1.0f);
}

void StepGrid::rebuildBackground (float scale)
{
    background = makeImage (getWidth(), getHeight(), scale);
    Graphics g (background);
    g.addTransform (AffineTransform::scale (scale));

    const float rh = rowHeight();

    // beat groups (alternate tints so the bar reads at a glance)
    for (int grp = 0; grp < 4; ++grp)
    {
        auto r = Rectangle<float> (stepX (grp * 4) - 2.0f, laneHeight - 1.0f, cellPitch() * 4.0f + 4.0f,
                                   (float) getHeight() - laneHeight + 1.0f);
        g.setColour ((grp % 2 == 0 ? Colours::white : cc::lilac).withAlpha (0.55f));
        g.fillRoundedRectangle (r, 10.0f);
    }

    // lane: beat numbers and dots
    for (int s = 0; s < critter::numSteps; ++s)
    {
        const float cx = stepX (s) + cellPitch() * 0.5f;
        if (s % 4 == 0)
        {
            g.setColour (cc::ink.withAlpha (0.5f));
            g.setFont (aa::Fonts::uiBold (10.5f));
            g.drawText (String (s / 4 + 1), Rectangle<float> (cx - 10.0f, 2.0f, 20.0f, laneHeight - 4.0f), Justification::centred, false);
        }
        else
        {
            g.setColour (cc::ink.withAlpha (0.2f));
            g.fillEllipse (Rectangle<float> (3.0f, 3.0f).withCentre ({ cx, laneHeight * 0.5f }));
        }
    }

    // empty trays (lit cells are drawn live on top)
    for (int r = 0; r < critter::numVoices; ++r)
        for (int st = 0; st < critter::numSteps; ++st)
            drawEmptyCell (g, cellRect (r, st), false, r);

    // row labels: tiny critter + name
    for (int r = 0; r < critter::numVoices; ++r)
    {
        auto row = Rectangle<float> (0.0f, laneHeight + (float) r * rh, labelWidth - 8.0f, rh).reduced (0.0f, 1.5f);
        if (r == selectedRow)
        {
            g.setColour (cc::critter (r).withAlpha (0.3f));
            g.fillRoundedRectangle (row, row.getHeight() * 0.5f);
        }
        auto icon = row.removeFromLeft (row.getHeight() + 6.0f).reduced (2.0f, 0.0f);
        critter::Pose pose;
        pose.look = { 0.4f, 0.0f };
        critter::drawCritter (g, r, icon.withSizeKeepingCentre (icon.getHeight() * 1.1f, icon.getHeight() * 1.1f).translated (0.0f, 1.0f), pose);
        g.setColour (cc::ink.withAlpha (r == selectedRow ? 1.0f : 0.78f));
        g.setFont (aa::Fonts::uiBold (12.0f));
        g.drawText (critter::critterName (r), row.withTrimmedLeft (3.0f), Justification::centredLeft, false);
    }
}

void StepGrid::paint (Graphics& g)
{
    const float scale = physicalScale (g);
    if (background.isNull() || background.getWidth() != roundToInt ((float) getWidth() * scale))
        rebuildBackground (scale);
    g.drawImage (background, getLocalBounds().toFloat());

    // playhead column
    if (running && playStep >= 0)
    {
        auto col = columnRect (playStep).withTrimmedTop (laneHeight - 1.0f).reduced (2.0f, 0.0f);
        g.setColour (Colour (0xffffe27a).withAlpha (0.55f));
        g.fillRoundedRectangle (col, 9.0f);
        g.setColour (Colour (0xffffc23d).withAlpha (0.8f));
        g.drawRoundedRectangle (col.reduced (0.5f), 9.0f, 1.5f);
    }

    for (int r = 0; r < critter::numVoices; ++r)
    {
        const auto bits = proc.getRowBits (r);
        const auto col = cc::critter (r);
        const auto deep = cc::deep (r);
        for (int s = 0; s < critter::numSteps; ++s)
        {
            auto cell = cellRect (r, s);
            if (! g.clipRegionIntersects (cell.expanded (5.0f).toNearestInt()))
                continue;
            const bool on = ((bits >> s) & 1u) != 0;
            const bool acc = ((bits >> (16 + s)) & 1u) != 0;
            const bool hover = r == hoverRow && s == hoverStep;
            const float radius = 6.0f;

            if (! on)
            {
                // empty trays live in the cached background, except under the playhead or the mouse
                if (hover || (running && s == playStep))
                    drawEmptyCell (g, cell, hover, r);
                continue;
            }

            const float p = pop[(size_t) s];
            if (! acc)
                cell = cell.reduced (2.0f, 1.5f);
            if (p > 0.0f)
                cell = cell.expanded (cell.getWidth() * 0.12f * p, cell.getHeight() * 0.12f * p);

            const auto fill = acc ? deep : col;
            g.setGradientFill (ColourGradient (fill.brighter (acc ? 0.15f : 0.3f), 0.0f, cell.getY(), fill, 0.0f, cell.getBottom(), false));
            g.fillRoundedRectangle (cell, radius);
            if (p > 0.0f)
            {
                g.setColour (Colours::white.withAlpha (0.45f * p));
                g.fillRoundedRectangle (cell, radius);
            }
            g.setColour (Colours::white.withAlpha (0.5f));
            g.fillRoundedRectangle (cell.withHeight (cell.getHeight() * 0.32f).reduced (3.0f, 1.5f).translated (0.0f, 1.0f), 3.0f);
            g.setColour (cc::ink.withAlpha (hover ? 0.9f : 0.62f));
            g.drawRoundedRectangle (cell.reduced (0.5f), radius, 1.3f);
            if (acc)
            {
                g.setColour (Colours::white);
                g.fillPath (critter::sparklePath (cell.getCentre().translated (0.0f, 0.5f), jmin (cell.getHeight(), cell.getWidth()) * 0.3f));
            }
        }
    }

    // bouncing playhead ball
    if (running && playStep >= 0)
    {
        // sits on the playing step, then hops over to the next one just before it plays
        const float p = (float) phase;
        const float x0 = stepX (playStep) + cellPitch() * 0.5f;
        const float x1 = playStep == critter::numSteps - 1 ? x0 + cellPitch() : stepX (playStep + 1) + cellPitch() * 0.5f;
        const float u = jlimit (0.0f, 1.0f, (p - 0.55f) / 0.45f);
        const float x = x0 + (x1 - x0) * u;
        const float hop = 4.0f * u * (1.0f - u);
        const float r = 5.5f;
        const float ground = laneHeight - r - 1.0f;
        const float y = ground - hop * (laneHeight - 2.0f * r - 1.0f);
        const float sq = 0.32f * jmax (0.0f, 1.0f - p / 0.18f) - 0.12f * hop; // squash on landing, stretch mid-air
        const auto c = cc::critter (playStep % critter::numVoices);
        auto ball = Rectangle<float> (r * 2.0f * (1.0f + sq), r * 2.0f * (1.0f - sq)).withCentre ({ x, y + r * sq });
        g.setColour (c);
        g.fillEllipse (ball);
        g.setColour (cc::ink);
        g.drawEllipse (ball, 1.3f);
        g.fillEllipse (Rectangle<float> (1.8f, 2.2f).withCentre (ball.getCentre().translated (-1.8f, -0.5f)));
        g.fillEllipse (Rectangle<float> (1.8f, 2.2f).withCentre (ball.getCentre().translated (1.8f, -0.5f)));
    }
}

void StepGrid::tick (double dt)
{
    const bool nowRunning = proc.uiRunning.load();
    const int step = proc.uiStep.load();
    const double ph = proc.uiStepPhase.load();

    if (nowRunning != running)
    {
        running = nowRunning;
        repaint();
    }

    if (running)
    {
        if (step != playStep)
        {
            if (playStep >= 0)
                repaint (columnRect (playStep).toNearestInt());
            if (step >= 0)
            {
                repaint (columnRect (step).toNearestInt());
                bool any = false;
                for (int r = 0; r < critter::numVoices; ++r)
                    any = any || proc.isStepOn (r, step);
                if (any)
                    pop[(size_t) step] = 1.0f;
            }
            playStep = step;
        }
        phase = ph;
        repaint (laneRect().toNearestInt());
    }
    else if (playStep >= 0)
    {
        playStep = -1;
        repaint();
    }

    for (int s = 0; s < critter::numSteps; ++s)
        if (pop[(size_t) s] > 0.0f)
        {
            pop[(size_t) s] = jmax (0.0f, pop[(size_t) s] - (float) dt * 6.0f);
            repaint (columnRect (s).toNearestInt());
        }
}

void StepGrid::mouseDown (const MouseEvent& e)
{
    int row = -1, step = -1;
    if (cellAt (e.position, row, step))
    {
        const bool on = proc.isStepOn (row, step);
        const bool acc = proc.isStepAccent (row, step);
        editor.selectCritter (row);

        if (e.mods.isPopupMenu() || e.mods.isShiftDown() || e.mods.isCommandDown())
        {
            proc.setStep (row, step, true, on ? ! acc : true);
            dragging = false;
        }
        else
        {
            dragValue = ! on;
            dragging = true;
            dragRow = row;
            lastDragStep = step;
            proc.setStep (row, step, dragValue, false);
        }
        repaint();
        return;
    }

    const int labelRow = labelRowAt (e.position);
    if (labelRow >= 0)
    {
        editor.selectCritter (labelRow);
        if (e.mods.isPopupMenu())
            showRowMenu (labelRow);
        else
            editor.audition (labelRow);
    }
}

void StepGrid::mouseDrag (const MouseEvent& e)
{
    if (! dragging)
        return;
    int row = -1, step = -1;
    if (cellAt (e.position, row, step) && (row != dragRow || step != lastDragStep))
    {
        dragRow = row;
        lastDragStep = step;
        if (proc.isStepOn (row, step) != dragValue)
        {
            proc.setStep (row, step, dragValue, false);
            repaint (cellRect (row, step).expanded (6.0f).toNearestInt());
        }
    }
}

void StepGrid::mouseUp (const MouseEvent&)
{
    if (dragging || hoverRow >= 0)
        proc.notifyStateChanged();
    dragging = false;
}

void StepGrid::mouseMove (const MouseEvent& e)
{
    int row = -1, step = -1;
    if (! cellAt (e.position, row, step))
        row = step = -1;
    if (row != hoverRow || step != hoverStep)
    {
        if (hoverRow >= 0)
            repaint (cellRect (hoverRow, hoverStep).expanded (6.0f).toNearestInt());
        hoverRow = row;
        hoverStep = step;
        if (hoverRow >= 0)
            repaint (cellRect (hoverRow, hoverStep).expanded (6.0f).toNearestInt());
    }
    setMouseCursor (row >= 0 || labelRowAt (e.position) >= 0 ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
}

void StepGrid::mouseExit (const MouseEvent&)
{
    if (hoverRow >= 0)
        repaint (cellRect (hoverRow, hoverStep).expanded (6.0f).toNearestInt());
    hoverRow = hoverStep = -1;
}

void StepGrid::showRowMenu (int row)
{
    PopupMenu m;
    m.setLookAndFeel (&getLookAndFeel());
    m.addSectionHeader (String (critter::critterName (row)) + "'s row");
    m.addItem (1, "Critter dice (random groove)");
    m.addItem (2, "Every beat");
    m.addItem (3, "Every 8th");
    m.addItem (4, "Every 16th");
    m.addItem (5, "Off-beats");
    m.addSeparator();
    m.addItem (6, "Nudge left");
    m.addItem (7, "Nudge right");
    m.addItem (8, "Clear row");
    m.addSeparator();
    m.addItem (9, "Clear the whole pattern");

    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                     [safe = Component::SafePointer<StepGrid> (this), row] (int result)
                     {
                         if (safe == nullptr || result <= 0)
                             return;
                         auto& p = safe->proc;
                         const auto bits = p.getRowBits (row);
                         const uint32_t on = bits & 0xffffu, acc = bits >> 16;
                         auto rot = [] (uint32_t v, int by) { by = (by + 16) % 16; return ((v << by) | (v >> (16 - by))) & 0xffffu; };

                         switch (result)
                         {
                             case 1:
                             {
                                 // density and accents that suit each critter
                                 static const float density[] = { 0.28f, 0.12f, 0.12f, 0.6f, 0.15f, 0.18f, 0.2f, 0.12f };
                                 Random rng;
                                 uint32_t newOn = 0, newAcc = 0;
                                 for (int s = 0; s < critter::numSteps; ++s)
                                 {
                                     float chance = density[row];
                                     if (row == critter::kick && s % 4 == 0) chance = s == 0 ? 1.0f : 0.7f;
                                     if ((row == critter::snare || row == critter::clap) && s % 8 == 4) chance = 0.9f;
                                     if (row == critter::closedHat && s % 2 == 0) chance = 0.85f;
                                     if (row == critter::openHat && s % 4 == 2) chance = 0.45f;
                                     if (rng.nextFloat() < chance)
                                     {
                                         newOn |= 1u << s;
                                         if (rng.nextFloat() < (s % 4 == 0 ? 0.5f : 0.15f))
                                             newAcc |= 1u << s;
                                     }
                                 }
                                 p.setRowBits (row, newOn | (newAcc << 16));
                                 break;
                             }
                             case 2: p.setRowBits (row, 0x1111u | (0x0001u << 16)); break;
                             case 3: p.setRowBits (row, 0x5555u | (0x1111u << 16)); break;
                             case 4: p.setRowBits (row, 0xffffu | (0x1111u << 16)); break;
                             case 5: p.setRowBits (row, 0x4444u); break;
                             case 6: p.setRowBits (row, rot (on, 15) | (rot (acc, 15) << 16)); break;
                             case 7: p.setRowBits (row, rot (on, 1) | (rot (acc, 1) << 16)); break;
                             case 8: p.setRowBits (row, 0); break;
                             case 9:
                                 for (int r = 0; r < critter::numVoices; ++r)
                                     p.setRowBits (r, 0);
                                 break;
                             default: break;
                         }
                         p.notifyStateChanged();
                         safe->repaint();
                     });
}

//==============================================================================
// Card header
//==============================================================================
CardHeader::CardHeader (CritterEditor& e, CritterProcessor& p) : editor (e), proc (p) {}

Rectangle<float> CardHeader::chipArea() const
{
    return { 0.0f, (float) getHeight() - 24.0f, (float) getWidth(), 24.0f };
}

Rectangle<float> CardHeader::diceArea() const
{
    return { (float) getWidth() - 30.0f, 4.0f, 28.0f, 28.0f };
}

void CardHeader::tick (double dt)
{
    if (diceSpin > 0.0f)
    {
        diceSpin = jmax (0.0f, diceSpin - (float) dt * 2.5f);
        repaint (diceArea().expanded (8.0f).toNearestInt());
    }
}

Rectangle<float> CardHeader::closeArea() const
{
    auto chip = chipArea();
    return chip.removeFromRight (chip.getHeight()).reduced (3.0f);
}

void CardHeader::paint (Graphics& g)
{
    const auto col = cc::critter (voice);
    const auto deep = cc::deep (voice);
    auto b = getLocalBounds().toFloat();

    // name in big outlined letters
    const auto nameFont = aa::Fonts::display (34.0f);
    const String name (critter::critterName (voice));
    drawOutlinedText (g, name, nameFont, { 2.0f, 32.0f }, col, cc::ink, 5.0f);

    // role pill
    const auto pillFont = aa::Fonts::uiBold (10.0f).withExtraKerningFactor (0.12f);
    const String role = String (critter::roleName (voice)).toUpperCase() + "  " + noteNames[voice];
    const float nameW = aa::Fonts::textWidth (nameFont, name);
    auto pill = Rectangle<float> (aa::Fonts::textWidth (pillFont, role) + 18.0f, 20.0f).withPosition (nameW + 14.0f, 10.0f);
    g.setColour (Colours::white);
    g.fillRoundedRectangle (pill, 10.0f);
    g.setColour (deep);
    g.drawRoundedRectangle (pill.reduced (0.75f), 10.0f, 1.5f);
    g.setFont (pillFont);
    g.drawText (role, pill, Justification::centred, false);

    // personality
    g.setColour (cc::ink.withAlpha (0.66f));
    g.setFont (aa::Fonts::ui (12.5f));
    g.drawText (personalities[voice], Rectangle<float> (2.0f, 38.0f, b.getWidth(), 16.0f), Justification::centredLeft, true);

    // dice: roll a new sound for this critter
    {
        const auto d = diceArea().reduced (2.0f);
        const float e = diceSpin * diceSpin;
        const float angle = e * pi * 1.5f;
        const float lift = std::sin (diceSpin * pi) * 5.0f;
        const auto xf = AffineTransform::rotation (angle, d.getCentreX(), d.getCentreY()).translated (0.0f, -lift);
        Path box;
        box.addRoundedRectangle (d, 6.0f);
        g.setColour (cc::ink.withAlpha (0.12f));
        g.fillEllipse (Rectangle<float> (d.getWidth() * (1.0f - lift * 0.04f), 5.0f).withCentre ({ d.getCentreX(), d.getBottom() + 2.0f }));
        g.setColour (diceHover ? col.brighter (0.3f) : Colours::white);
        g.fillPath (box, xf);
        g.setColour (cc::ink);
        g.strokePath (box, PathStrokeType (1.6f), xf);
        const float pr = 2.1f, o = d.getWidth() * 0.27f;
        const auto c = d.getCentre();
        for (auto off : { Point<float> (-o, -o), Point<float> (o, -o), Point<float> (0.0f, 0.0f), Point<float> (-o, o), Point<float> (o, o) })
        {
            const auto pc = (c + off).transformedBy (xf);
            g.setColour (diceHover ? cc::ink : deep);
            g.fillEllipse (Rectangle<float> (pr * 2.0f, pr * 2.0f).withCentre (pc));
        }
    }

    // what it has eaten
    auto chip = chipArea();
    if (auto* s = proc.getSample (voice))
    {
        const auto font = aa::Fonts::uiBold (11.0f);
        const String text = "ATE  " + s->displayName;
        const float w = jmin (chip.getWidth(), aa::Fonts::textWidth (font, text) + 26.0f + chip.getHeight());
        auto c = chip.withWidth (w);
        g.setColour (col.withAlpha (0.28f));
        g.fillRoundedRectangle (c, c.getHeight() * 0.5f);
        g.setColour (deep.withAlpha (0.7f));
        g.drawRoundedRectangle (c.reduced (0.75f), c.getHeight() * 0.5f, 1.2f);

        // fork icon
        auto icon = c.removeFromLeft (c.getHeight()).reduced (6.0f);
        g.setColour (cc::ink);
        for (int i = 0; i < 3; ++i)
            g.fillRect (Rectangle<float> (1.2f, icon.getHeight() * 0.45f).withPosition (icon.getX() + 1.5f + (float) i * 2.6f, icon.getY()));
        g.fillRoundedRectangle (Rectangle<float> (7.4f, 2.2f).withPosition (icon.getX() + 1.5f, icon.getY() + icon.getHeight() * 0.4f), 1.0f);
        g.fillRect (Rectangle<float> (1.8f, icon.getHeight() * 0.55f).withPosition (icon.getX() + 4.3f, icon.getY() + icon.getHeight() * 0.45f));

        auto close = c.removeFromRight (c.getHeight()).reduced (3.0f);
        g.setFont (font);
        g.setColour (cc::ink);
        g.drawFittedText (text, c.toNearestInt(), Justification::centredLeft, 1, 0.8f);

        g.setColour (closeHover ? deep : cc::ink.withAlpha (0.12f));
        g.fillEllipse (close);
        g.setColour (closeHover ? Colours::white : cc::ink.withAlpha (0.7f));
        const auto cc2 = close.getCentre();
        const float d = close.getWidth() * 0.2f;
        g.drawLine (cc2.x - d, cc2.y - d, cc2.x + d, cc2.y + d, 1.6f);
        g.drawLine (cc2.x - d, cc2.y + d, cc2.x + d, cc2.y - d, 1.6f);
    }
    else
    {
        g.setColour (cc::ink.withAlpha (0.42f));
        g.setFont (aa::Fonts::ui (11.5f));
        g.drawText (proc.loading[(size_t) voice].load() ? "Munching..." : "Hungry! Drop an audio file on my pad to feed me.",
                    chip, Justification::centredLeft, true);
    }
}

void CardHeader::mouseDown (const MouseEvent& e)
{
    if (diceArea().contains (e.position))
    {
        editor.rollDice (voice);
        spinDice();
        return;
    }
    if (proc.getSample (voice) != nullptr && closeArea().contains (e.position))
    {
        proc.clearSample (voice);
        proc.notifyStateChanged();
        repaint();
        return;
    }
    if (chipArea().contains (e.position))
        editor.showSampleMenu (voice, this);
}

void CardHeader::mouseMove (const MouseEvent& e)
{
    const bool h = proc.getSample (voice) != nullptr && closeArea().contains (e.position);
    const bool dh = diceArea().contains (e.position);
    if (h != closeHover || dh != diceHover)
    {
        closeHover = h;
        diceHover = dh;
        repaint();
    }
    setMouseCursor (h || dh || chipArea().contains (e.position) ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
    if (dh)
        setTooltip ("Roll the dice: a random new tune, decay, tone and snap for " + String (critter::critterName (voice)));
    else if (proc.getSample (voice) != nullptr)
        setTooltip (closeHover ? "Back to synth" : "Fed with: " + proc.getSample (voice)->filePath);
    else
        setTooltip ("Drag an audio file (wav, aiff, flac, mp3, ogg) onto " + String (critter::critterName (voice))
                    + "'s pad, or click here to choose one.");
}

//==============================================================================
// Editor
//==============================================================================
CritterEditor::CritterEditor (CritterProcessor& p)
    : aa::EditorBase (p, baseWidth, baseHeight, makeTheme()),
      proc (p),
      volumeKnob (p.apvts, "volume", "Volume"),
      driveKnob (p.apvts, "drive", "Drive"),
      roomKnob (p.apvts, "room", "Room"),
      pitchKnob (p.apvts, "pitch", "Kit Pitch"),
      swingKnob (p.apvts, "swing", "Swing"),
      accentKnob (p.apvts, "accent", "Accent"),
      humanizeKnob (p.apvts, "humanize", "Humanize"),
      seqToggle (p.apvts, "seq_on", "Sequencer"),
      presets (p)
{
    useLookAndFeel (std::make_unique<CritterLookAndFeel> (makeTheme()));
    aa::Fonts::setDisplayTypeface (PluginAssets::LilitaOneRegular_ttf, (size_t) PluginAssets::LilitaOneRegular_ttfSize);

    selected = jlimit (0, critter::numVoices - 1, proc.selectedCritter);

    for (int v = 0; v < critter::numVoices; ++v)
    {
        pads.push_back (std::make_unique<CritterPad> (*this, proc, v));
        content.addAndMakeVisible (*pads.back());
        lastSampleVersion[(size_t) v] = proc.sampleVersion[(size_t) v].load();
        lastFailures[(size_t) v] = proc.loadFailures[(size_t) v].load();

        static const char* ids[] = { "tune", "decay", "tone", "snap", "level", "pan" };
        static const char* labels[] = { "Tune", "Decay", "Tone", "Snap", "Level", "Pan" };
        for (int k = 0; k < 6; ++k)
        {
            auto knob = std::make_unique<aa::Knob> (p.apvts, CritterProcessor::voiceParamId (v, ids[k]), labels[k]);
            knob->setAccent (cc::deep (v));
            knob->setBipolar (k == 0 || k == 5);
            content.addChildComponent (*knob);
            voiceKnobs.push_back (std::move (knob));
        }
        updateTooltips (v);
    }

    volumeKnob.setAccent (pink);
    driveKnob.setAccent (tangerine);
    roomKnob.setAccent (grape);
    pitchKnob.setAccent (sky);
    pitchKnob.setBipolar (true);
    swingKnob.setAccent (mint);
    accentKnob.setAccent (pink);
    humanizeKnob.setAccent (grape);
    seqToggle.setAccent (mint);
    presets.setAccent (pink);

    volumeKnob.setTooltip ("Volume: overall output level");
    driveKnob.setTooltip ("Drive: warm saturation on the whole kit");
    roomKnob.setTooltip ("Room: how much of the critters' little room you hear");
    pitchKnob.setTooltip ("Kit Pitch: transpose every critter at once (bigger or smaller critters!)");
    swingKnob.setTooltip ("Swing: delays every second 16th for a shuffled groove");
    accentKnob.setTooltip ("Accent: how much louder accented steps are than normal ones");
    humanizeKnob.setTooltip ("Humanize: tiny random changes in loudness and pitch on sequenced hits");
    seqToggle.setTooltip ("Sequencer: play the pattern in time with the host. MIDI notes C1-G1 always play the critters");

    for (auto* c : std::initializer_list<Component*> { &grid, &card, &volumeKnob, &driveKnob, &roomKnob, &pitchKnob,
                                                       &swingKnob, &accentKnob, &humanizeKnob, &seqToggle, &presets })
        content.addAndMakeVisible (c);

    // drain stale hits from before the editor opened
    CritterProcessor::Hit h;
    while (proc.hits.pop (h)) {}
    lastPatternVersion = proc.patternVersion.load();

    selectCritter (selected);
    finishSetup();
}

CritterEditor::~CritterEditor()
{
    proc.selectedCritter = selected;
}

void CritterEditor::updateTooltips (int v)
{
    const bool fed = proc.getSample (v) != nullptr;
    auto base = [this, v] (int k) { return voiceKnobs[(size_t) (v * 6 + k)].get(); };
    const String name (critter::critterName (v));
    base (0)->setTooltip (fed ? "Tune: repitch the sample (semitones)" : "Tune: " + name + "'s pitch in semitones");
    base (1)->setTooltip (fed ? "Decay: fade the sample out (turn all the way up to play it to the end)"
                             : "Decay: how long " + name + " rings");
    base (2)->setTooltip (fed ? "Tone: below the middle = low-pass filter, above = high-pass filter" : String (toneTips[v]));
    base (3)->setTooltip (fed ? "Snap: below the middle skips into the sample, above boosts its attack" : String (snapTips[v]));
    base (4)->setTooltip ("Level: " + name + "'s volume");
    base (5)->setTooltip ("Pan: place " + name + " left or right");
}

void CritterEditor::selectCritter (int v)
{
    v = jlimit (0, critter::numVoices - 1, v);
    selected = v;
    proc.selectedCritter = v;
    for (int i = 0; i < critter::numVoices; ++i)
    {
        pads[(size_t) i]->setSelected (i == v);
        for (int k = 0; k < 6; ++k)
            voiceKnobs[(size_t) (i * 6 + k)]->setVisible (i == v);
    }
    card.setVoice (v);
    grid.setSelectedRow (v);
}

void CritterEditor::audition (int v)
{
    proc.auditionMask.fetch_or (1u << v);
}

void CritterEditor::showSampleMenu (int v, Component* target)
{
    const bool fed = proc.getSample (v) != nullptr;
    const String name (critter::critterName (v));

    PopupMenu m;
    m.setLookAndFeel (&getLookAndFeel());
    m.addSectionHeader (name + " the " + String (critter::roleName (v)).toLowerCase());
    m.addItem (1, "Play " + name);
    m.addItem (2, fed ? "Load a different sample..." : "Load sample...");
    m.addItem (3, "Back to synth", fed);
    if (fed && File::isAbsolutePath (proc.getSample (v)->filePath) && File (proc.getSample (v)->filePath).existsAsFile())
        m.addItem (4, "Show sample file");

    m.showMenuAsync (PopupMenu::Options().withTargetComponent (target),
                     [safe = Component::SafePointer<CritterEditor> (this), v] (int result)
                     {
                         if (safe == nullptr)
                             return;
                         switch (result)
                         {
                             case 1: safe->audition (v); break;
                             case 2: safe->chooseSampleFile (v); break;
                             case 3:
                                 safe->proc.clearSample (v);
                                 safe->proc.notifyStateChanged();
                                 break;
                             case 4:
                                 if (auto* s = safe->proc.getSample (v))
                                     File (s->filePath).revealToUser();
                                 break;
                             default: break;
                         }
                     });
}

void CritterEditor::chooseSampleFile (int v)
{
    chooser = std::make_unique<FileChooser> ("Feed " + String (critter::critterName (v)) + " a sample",
                                             File::getSpecialLocation (File::userMusicDirectory),
                                             proc.getAudioWildcard());
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                          [safe = Component::SafePointer<CritterEditor> (this), v] (const FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (safe == nullptr || ! file.existsAsFile())
                                  return;
                              safe->proc.loadSampleAsync (v, file);
                              safe->pads[(size_t) v]->startNom();
                          });
}

void CritterEditor::rollDice (int v)
{
    Random rng;
    auto set = [this, v] (const char* id, float value)
    {
        if (auto* p = proc.param (CritterProcessor::voiceParamId (v, id)))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        }
    };

    float decayDefault = 300.0f;
    if (auto* p = proc.param (CritterProcessor::voiceParamId (v, "decay")))
        decayDefault = p->convertFrom0to1 (p->getDefaultValue());

    // musical ranges: whole semitones, decay within a couple of octaves of the critter's nature
    set ("tune", (float) roundToInt ((rng.nextFloat() * 2.0f - 1.0f) * (v == critter::kick ? 4.0f : 7.0f)));
    set ("decay", jlimit (15.0f, 3000.0f, decayDefault * std::pow (2.0f, rng.nextFloat() * 2.6f - 1.3f)));
    set ("tone", 10.0f + rng.nextFloat() * 80.0f);
    set ("snap", 10.0f + rng.nextFloat() * 80.0f);
    audition (v);
}

void CritterEditor::layoutContent()
{
    titleArea = { 14.0f, 2.0f, 420.0f, 58.0f };
    presets.setBounds (Rectangle<float> (250.0f, 34.0f).withCentre ({ 588.0f, 32.0f }).toNearestInt());

    const float padW = (padsArea.getWidth() - 3.0f * 8.0f) / 4.0f;
    const float padH = (padsArea.getHeight() - 8.0f) / 2.0f;
    for (int v = 0; v < critter::numVoices; ++v)
    {
        const int c = v % 4, r = v / 4;
        pads[(size_t) v]->setBounds (Rectangle<float> (padsArea.getX() + (float) c * (padW + 8.0f),
                                                       padsArea.getY() + (float) r * (padH + 8.0f), padW, padH).toNearestInt());
    }

    card.setBounds (Rectangle<float> (cardArea.getX() + 18.0f, cardArea.getY() + 12.0f, cardArea.getWidth() - 36.0f, 80.0f).toNearestInt());
    const auto knobArea = Rectangle<float> (cardArea.getX() + 10.0f, cardArea.getY() + 98.0f, cardArea.getWidth() - 20.0f, 164.0f);
    const float cw = knobArea.getWidth() / 3.0f, ch = knobArea.getHeight() / 2.0f;
    for (int v = 0; v < critter::numVoices; ++v)
        for (int k = 0; k < 6; ++k)
        {
            auto cell = Rectangle<float> (knobArea.getX() + (float) (k % 3) * cw, knobArea.getY() + (float) (k / 3) * ch, cw, ch);
            voiceKnobs[(size_t) (v * 6 + k)]->setBounds (cell.withSizeKeepingCentre (100.0f, 80.0f).toNearestInt());
        }

    seqToggle.setBounds (Rectangle<float> (patternArea.getX() + 106.0f, patternArea.getY() + 11.0f, 130.0f, 24.0f).toNearestInt());
    const float miniW = 112.0f;
    float mx = patternArea.getRight() - 12.0f - 3.0f * miniW - 2.0f * 8.0f;
    for (auto* k : { &swingKnob, &accentKnob, &humanizeKnob })
    {
        k->setBounds (Rectangle<float> (mx, patternArea.getY() + 8.0f, miniW, 32.0f).toNearestInt());
        mx += miniW + 8.0f;
    }
    grid.setBounds (Rectangle<float> (patternArea.getX() + 12.0f, patternArea.getY() + 48.0f, patternArea.getWidth() - 24.0f,
                                      patternArea.getHeight() - 58.0f).toNearestInt());

    const auto mArea = Rectangle<float> (masterArea.getX() + 6.0f, masterArea.getY() + 34.0f, masterArea.getWidth() - 12.0f,
                                         masterArea.getHeight() - 42.0f);
    const float mw = mArea.getWidth() / 2.0f, mh = mArea.getHeight() / 2.0f;
    aa::Knob* masters[] = { &volumeKnob, &driveKnob, &roomKnob, &pitchKnob };
    for (int i = 0; i < 4; ++i)
    {
        auto cell = Rectangle<float> (mArea.getX() + (float) (i % 2) * mw, mArea.getY() + (float) (i / 2) * mh, mw, mh);
        masters[i]->setBounds (cell.withSizeKeepingCentre (86.0f, 100.0f).toNearestInt());
    }

    background = {};
}

void CritterEditor::rebuildBackground (float scale)
{
    background = makeImage (baseWidth, baseHeight, scale, true);
    Graphics g (background);
    g.addTransform (AffineTransform::scale (scale));
    auto b = baseBounds().toFloat();

    g.setGradientFill (ColourGradient (theme().background, 0.0f, 0.0f, theme().backgroundAlt, 0.0f, b.getBottom(), false));
    g.fillAll();

    // soft candy blobs + polka dots
    const Colour blobs[] = { cc::critter (0), cc::critter (6), cc::critter (3), cc::critter (2) };
    const Point<float> blobPos[] = { { 80.0f, 40.0f }, { 900.0f, 70.0f }, { 860.0f, 600.0f }, { 40.0f, 620.0f } };
    for (int i = 0; i < 4; ++i)
    {
        g.setGradientFill (ColourGradient (blobs[i].withAlpha (0.16f), blobPos[i].x, blobPos[i].y,
                                           blobs[i].withAlpha (0.0f), blobPos[i].x + 240.0f, blobPos[i].y, true));
        g.fillEllipse (Rectangle<float> (480.0f, 480.0f).withCentre (blobPos[i]));
    }
    const float spacing = 22.0f;
    g.setColour (Colour (0xff9a82e0).withAlpha (0.13f));
    for (int row = 0; row * spacing * 0.866f < b.getHeight() + spacing; ++row)
        for (int c = 0; c * spacing < b.getWidth() + spacing; ++c)
        {
            const float x = (float) c * spacing + ((row % 2) ? spacing * 0.5f : 0.0f);
            const float y = (float) row * spacing * 0.866f;
            g.fillEllipse (Rectangle<float> (3.4f, 3.4f).withCentre ({ x, y }));
        }

    // pad shadows
    for (auto& pad : pads)
    {
        auto r = pad->getBounds().toFloat().reduced (1.5f);
        g.setColour (cc::ink.withAlpha (0.07f));
        g.fillRoundedRectangle (r.translated (0.0f, 3.5f).expanded (1.0f), 17.0f);
    }

    drawPanel (g, cardArea);
    drawPanel (g, patternArea);
    drawPanel (g, masterArea);

    // card: soft tint band behind the knobs
    {
        auto knobBand = Rectangle<float> (cardArea.getX() + 10.0f, cardArea.getY() + 98.0f, cardArea.getWidth() - 20.0f, 162.0f);
        g.setColour (Colours::white.withAlpha (0.55f));
        g.fillRoundedRectangle (knobBand, 14.0f);
        g.setColour (cc::lilac.withAlpha (0.9f));
        g.drawRoundedRectangle (knobBand.reduced (0.5f), 14.0f, 1.0f);
    }

    drawSectionTitle (g, { patternArea.getX() + 16.0f, patternArea.getY() + 15.0f }, "Pattern", mint);
    drawSectionTitle (g, { masterArea.getX() + 16.0f, masterArea.getY() + 15.0f }, "Master", pink);

    // header text
    g.setColour (cc::ink.withAlpha (0.5f));
    g.setFont (aa::Fonts::uiBold (11.0f).withExtraKerningFactor (0.22f));
    g.drawText ("ADVENTURE AUDIO", Rectangle<float> (b.getRight() - 236.0f, 18.0f, 216.0f, 28.0f), Justification::centredRight, false);
}

void CritterEditor::paintContent (Graphics& g)
{
    const float scale = physicalScale (g);
    if (background.isNull() || background.getWidth() != roundToInt ((float) baseWidth * scale))
        rebuildBackground (scale);
    g.drawImage (background, baseBounds().toFloat());

    if (! g.clipRegionIntersects (titleArea.toNearestInt()))
        return;

    // Title: every letter belongs to a critter and hops when it plays.
    // Letters are pre-rendered into small images and blitted at whole device pixels.
    if (titleLetters.empty())
    {
        const String title = "Critter Kit";
        const auto font = aa::Fonts::display (40.0f);
        float x = titleArea.getX() + 6.0f;
        for (int i = 0; i < title.length(); ++i)
        {
            const String ch = title.substring (i, i + 1);
            if (ch == " ")
            {
                x += aa::Fonts::textWidth (font, ch) * 0.8f;
                continue;
            }
            GlyphArrangement ga;
            ga.addLineOfText (font, ch, x, 46.0f);
            Path p, outline;
            ga.createPath (p);
            PathStrokeType (6.0f, PathStrokeType::curved, PathStrokeType::rounded).createStrokedPath (outline, p);
            titleLetters.push_back (p);
            titleOutlines.push_back (outline);
            x += aa::Fonts::textWidth (font, ch) + 0.5f;
        }
        titleEnd = x;
    }

    if (titleImages.size() != titleLetters.size() || std::abs (titleImageScale - scale) > 0.001f)
    {
        titleImages.clear();
        titleImageScale = scale;
        for (size_t i = 0; i < titleLetters.size(); ++i)
        {
            const auto bounds = titleOutlines[i].getBounds().expanded (1.0f).getSmallestIntegerContainer();
            Image img (Image::ARGB, jmax (1, roundToInt ((float) bounds.getWidth() * scale)),
                       jmax (1, roundToInt ((float) bounds.getHeight() * scale)), true);
            Graphics ig (img);
            ig.addTransform (AffineTransform::translation ((float) -bounds.getX(), (float) -bounds.getY()).scaled (scale));
            ig.setColour (cc::ink);
            ig.fillPath (titleOutlines[i]);
            ig.setColour (cc::critter ((int) i % critter::numVoices));
            ig.fillPath (titleLetters[i]);
            titleImages.push_back ({ img, bounds.toFloat() });
        }
    }

    for (size_t i = 0; i < titleImages.size(); ++i)
    {
        const float bounce = std::round (letterBounce[i % letterBounce.size()] * scale) / scale;
        const auto& [img, area] = titleImages[i];
        g.drawImage (img, area.translated (0.0f, bounce));
    }
    g.setColour (cc::ink.withAlpha (0.55f));
    g.setFont (aa::Fonts::uiBold (10.5f).withExtraKerningFactor (0.16f));
    g.drawText ("CREATURE DRUM MACHINE", Rectangle<float> (titleEnd + 14.0f, 25.0f, 220.0f, 24.0f), Justification::centredLeft, false);
}

void CritterEditor::onFrame (double, double frameDt)
{
    // cap animation work at ~60 fps, even on 120 Hz displays
    pendingDt += frameDt;
    if (pendingDt < 1.0 / 62.0)
        return;
    const double dt = jmin (0.1, pendingDt);
    pendingDt = 0.0;

    // hits from the audio thread
    CritterProcessor::Hit h;
    while (proc.hits.pop (h))
    {
        if (! isPositiveAndBelow (h.voice, critter::numVoices))
            continue;
        pads[(size_t) h.voice]->hit (h.velocity);
        for (int i = h.voice; i < (int) letterVel.size(); i += critter::numVoices)
            letterVel[(size_t) i] -= 40.0f + 50.0f * h.velocity;
    }

    // samples arriving / leaving / failing
    for (int v = 0; v < critter::numVoices; ++v)
    {
        const int version = proc.sampleVersion[(size_t) v].load();
        if (version != lastSampleVersion[(size_t) v])
        {
            lastSampleVersion[(size_t) v] = version;
            pads[(size_t) v]->onSampleChanged (proc.getSample (v) != nullptr);
            updateTooltips (v);
            if (v == selected)
                card.repaint();
        }
        const int fails = proc.loadFailures[(size_t) v].load();
        if (fails != lastFailures[(size_t) v])
        {
            lastFailures[(size_t) v] = fails;
            pads[(size_t) v]->onLoadFailed();
            if (v == selected)
                card.repaint();
        }
    }

    const auto pv = proc.patternVersion.load();
    if (pv != lastPatternVersion)
    {
        lastPatternVersion = pv;
        grid.patternChanged();
    }

    // eyes follow the mouse while it's anywhere near the window
    const auto mouse = content.getMouseXYRelative().toFloat();
    const bool near = content.getLocalBounds().toFloat().expanded (120.0f).contains (mouse);
    for (auto& pad : pads)
        pad->tick (dt, mouse, near);
    grid.tick (dt);
    card.tick (dt);

    // title letters: damped springs
    bool moving = false;
    for (size_t i = 0; i < letterBounce.size(); ++i)
    {
        const float k = 260.0f, c = 11.0f;
        const int steps = jmax (1, (int) std::ceil (dt / 0.005));
        const float hstep = (float) dt / (float) steps;
        for (int s = 0; s < steps; ++s)
        {
            letterVel[i] += (-k * letterBounce[i] - c * letterVel[i]) * hstep;
            letterBounce[i] += letterVel[i] * hstep;
        }
        letterBounce[i] = jlimit (-14.0f, 10.0f, letterBounce[i]);
        if (std::abs (letterBounce[i]) > 0.05f || std::abs (letterVel[i]) > 0.5f)
            moving = true;
        else
            letterBounce[i] = letterVel[i] = 0.0f;
    }
    if (moving)
        content.repaint (titleArea.toNearestInt());

    if (card.isShowing() && proc.loading[(size_t) selected].load())
        card.repaint();

    proc.collectGarbage();
    if (proc.hostNotifyPending.exchange (false))
        proc.notifyStateChanged();
}

