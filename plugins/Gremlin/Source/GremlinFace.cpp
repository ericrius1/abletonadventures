#include "GremlinFace.h"

using namespace juce;

namespace gremlin
{
namespace
{
    const Colour skinHi { 0xffa9ee62 };
    const Colour skin { 0xff5cb82e };
    const Colour skinLo { 0xff2b6b1c };
    const Colour skinDeep { 0xff17400f };
    const Colour outlineCol { 0xff08040e };
    const Colour earIn { 0xffff6aa6 };
    const Colour earInLo { 0xff9e1149 };
    const Colour mouthTop { 0xff4a0c28 };
    const Colour mouthBottom { 0xff17030d };
    const Colour tongueCol { 0xffff5c8a };
    const Colour toothCol { 0xfffff3d6 };
    const Colour idleEye { 0xffd9ff4f };

    // character design space: x -160..160, y -100..112 (head centre at 0,0)
    constexpr float designW = 320.0f, designH = 212.0f, designBottom = 112.0f;

    Path makeHead()
    {
        Path p;
        p.startNewSubPath (0.0f, -64.0f);
        p.cubicTo (40.0f, -66.0f, 72.0f, -46.0f, 77.0f, -14.0f);
        p.cubicTo (82.0f, 16.0f, 66.0f, 50.0f, 28.0f, 62.0f);
        p.quadraticTo (0.0f, 70.0f, -28.0f, 62.0f);
        p.cubicTo (-66.0f, 50.0f, -82.0f, 16.0f, -77.0f, -14.0f);
        p.cubicTo (-72.0f, -46.0f, -40.0f, -66.0f, 0.0f, -64.0f);
        p.closeSubPath();
        return p;
    }

    Path makeEar (bool notch)
    {
        Path p;
        p.startNewSubPath (-8.0f, -20.0f);
        if (notch)
        {
            p.cubicTo (10.0f, -30.0f, 26.0f, -38.0f, 40.0f, -44.0f);
            p.lineTo (46.0f, -36.0f);
            p.lineTo (53.0f, -50.0f);
            p.cubicTo (64.0f, -56.0f, 78.0f, -62.0f, 92.0f, -66.0f);
        }
        else
        {
            p.cubicTo (22.0f, -34.0f, 60.0f, -55.0f, 92.0f, -66.0f);
        }
        p.cubicTo (80.0f, -44.0f, 54.0f, -6.0f, -6.0f, 18.0f);
        p.closeSubPath();
        return p;
    }

    Path makeInnerEar()
    {
        Path p;
        p.startNewSubPath (2.0f, -12.0f);
        p.cubicTo (24.0f, -25.0f, 54.0f, -43.0f, 76.0f, -54.0f);
        p.cubicTo (64.0f, -36.0f, 40.0f, -8.0f, 2.0f, 8.0f);
        p.closeSubPath();
        return p;
    }

    void approach (float& v, float target, float rate, float dt)
    {
        v += (target - v) * (1.0f - std::exp (-dt * rate));
    }
} // namespace

//==============================================================================
GremlinFace::GremlinFace (GremlinProcessor& p) : processor (p)
{
    setTooltip ("This is the gremlin. It shows what it's doing to your audio - poke it if you like.");
    particles.reserve (64);
}

void GremlinFace::resized()
{
    bezel = getLocalBounds().toFloat().reduced (1.0f);
    screen = bezel.reduced (11.0f).withTrimmedBottom (13.0f);
    statusArea = Rectangle<float> (screen.getX() + 12.0f, screen.getY() + 5.0f, screen.getWidth() - 24.0f, 20.0f);
    faceArea = screen.withTrimmedTop (22.0f);
    backdrop = {};
    faceImage = {};
    pixelImage = {};
}

AffineTransform GremlinFace::characterTransform() const
{
    const float k = jmin (faceArea.getWidth() / designW, faceArea.getHeight() / designH);
    return AffineTransform::scale (k).translated (faceArea.getCentreX(), faceArea.getBottom() - designBottom * k);
}

Colour GremlinFace::eyeColour() const
{
    return shownFx == Fx::none ? idleEye : fxColour (shownFx);
}

void GremlinFace::mouseDown (const MouseEvent&)
{
    pokeTimer = 0.8f;
    hopVel -= 160.0f;
    hairVel += 60.0f;
    earVelL += 14.0f;
    earVelR -= 14.0f;
    const auto c = characterTransform();
    for (int i = 0; i < 8; ++i)
    {
        Particle pt;
        pt.kind = 2;
        pt.pos = Point<float> (0.0f, -60.0f).transformedBy (c);
        const float a = random.nextFloat() * MathConstants<float>::twoPi;
        pt.vel = { std::cos (a) * 90.0f, std::sin (a) * 90.0f - 40.0f };
        pt.life = 0.6f;
        pt.size = 3.0f;
        pt.colour = i % 2 == 0 ? pal::pink : pal::green;
        particles.push_back (pt);
    }
}

//==============================================================================
void GremlinFace::onStep (const StepEvent& e)
{
    const auto fx = (Fx) jlimit (0, numFx - 1, (int) e.fx);
    if (fx != Fx::none)
    {
        hopVel -= 60.0f;
        hairVel += 25.0f;
        aura = 1.0f;
        if (fx == Fx::scatter)
            lastScatter = e.slices;
        else
            lastSlices = e.slices;

        const auto c = characterTransform();
        const int n = 3 + random.nextInt (3);
        for (int i = 0; i < n && particles.size() < 60; ++i)
        {
            Particle pt;
            pt.kind = fx == Fx::crush ? 1 : 2;
            const float side = random.nextBool() ? 1.0f : -1.0f;
            pt.pos = Point<float> (side * (70.0f + random.nextFloat() * 40.0f), -40.0f - random.nextFloat() * 40.0f).transformedBy (c);
            pt.vel = { side * (30.0f + random.nextFloat() * 60.0f), -50.0f - random.nextFloat() * 70.0f };
            pt.life = 0.45f + random.nextFloat() * 0.3f;
            pt.size = fx == Fx::crush ? 5.0f : 3.0f;
            pt.colour = fxColour (fx);
            particles.push_back (pt);
        }
    }
    else if (std::abs (e.ppq - std::round (e.ppq)) < 1.0e-3)
    {
        hopVel -= 16.0f;
        tap = 1.0f;
    }
}

void GremlinFace::tick (double dt)
{
    const float fdt = (float) dt;
    clock += dt;
    auto& eng = processor.engine;
    const auto fx = (Fx) jlimit (0, numFx - 1, eng.activeFx.load());
    const bool forced = eng.activeForced.load();
    const float tapeSpeed = eng.tapeSpeed.load();

    if (fx != Fx::none)
    {
        shownFx = fx;
        shownForced = forced;
        holdTimer = 0.18f;
    }
    else if ((holdTimer -= fdt) <= 0.0f)
    {
        shownFx = Fx::none;
        shownForced = false;
    }

    const float timeScale = shownFx == Fx::halfSpeed ? 0.4f : (shownFx == Fx::tapeStop ? jmax (0.12f, tapeSpeed) : 1.0f);
    animTime += dt * timeScale;
    const float t = (float) animTime;

    // ---- idle life ---------------------------------------------------------------
    blinkTimer -= fdt * timeScale;
    if (blinkTimer <= 0.0f && blinkPhase <= 0.0f)
    {
        blinkPhase = 0.16f;
        blinkTimer = random.nextFloat() < 0.2f ? 0.28f : 1.8f + random.nextFloat() * 3.5f;
    }
    if (blinkPhase > 0.0f)
        blinkPhase = jmax (0.0f, blinkPhase - fdt * timeScale);

    lookTimer -= fdt * timeScale;
    if (lookTimer <= 0.0f)
    {
        lookTarget = { random.nextFloat() * 1.6f - 0.8f, random.nextFloat() * 1.1f - 0.5f };
        lookTimer = 0.7f + random.nextFloat() * 2.2f;
    }

    twitchTimer -= fdt;
    if (twitchTimer <= 0.0f)
    {
        (random.nextBool() ? earVelL : earVelR) += 10.0f + random.nextFloat() * 6.0f;
        twitchTimer = 2.5f + random.nextFloat() * 5.0f;
    }

    auto spring = [fdt] (float& x, float& v, float k, float damp)
    {
        v += (-k * x - damp * v) * fdt;
        x += v * fdt;
    };
    spring (earTwitchL, earVelL, 260.0f, 9.0f);
    spring (earTwitchR, earVelR, 260.0f, 9.0f);
    spring (hair, hairVel, 140.0f, 5.0f);
    spring (hop, hopVel, 300.0f, 13.0f);
    tap = jmax (0.0f, tap - fdt * 5.0f);
    aura = jmax (0.0f, aura - fdt * 2.2f);
    if (pokeTimer > 0.0f)
        pokeTimer -= fdt;

    // ---- expression targets --------------------------------------------------------
    const float chaos = processor.chaosAmount();
    Pose tgt;
    tgt.grin = 0.25f + 0.75f * chaos;
    tgt.mouthOpen = 0.08f + 0.14f * chaos + 0.04f * std::sin (t * 1.7f);
    tgt.lookX = lookTarget.x;
    tgt.lookY = lookTarget.y;
    tgt.browL = 0.35f + 0.12f * std::sin (t * 0.9f);
    tgt.browR = -0.25f - 0.25f * chaos;
    tgt.tilt = 0.035f * std::sin (t * 0.7f);
    tgt.bob = 1.6f * std::sin (t * 2.1f);
    float jitterAmp = 0.0f;

    switch (shownFx)
    {
        case Fx::stutter:
            tgt.mouthOpen = 0.4f + 0.45f * (float) (eng.repeatIndex.load() & 1);
            tgt.pupil = 0.5f;
            tgt.browL = tgt.browR = 0.7f;
            tgt.ghost = 1.0f;
            jitterAmp = 5.0f;
            break;
        case Fx::reverse:
            tgt.flip = -1.0f;
            tgt.lookX = -0.8f;
            tgt.ghost = 0.35f;
            tgt.grin = jmax (tgt.grin, 0.7f);
            break;
        case Fx::tapeStop:
        {
            const float slow = 1.0f - tapeSpeed;
            tgt.spiral = 1.0f;
            tgt.earL = tgt.earR = -0.7f * slow;
            tgt.tilt = 0.2f * slow;
            tgt.bob = 10.0f * slow;
            tgt.mouthOpen = 0.55f;
            tgt.grin = 0.2f;
            tgt.browL = tgt.browR = 0.5f;
            break;
        }
        case Fx::halfSpeed:
            tgt.sleepy = 1.0f;
            tgt.eyeOpen = 0.4f;
            tgt.lookY = 0.6f;
            tgt.lookX *= 0.3f;
            tgt.mouthOpen = 0.3f + 0.3f * (0.5f + 0.5f * std::sin (t * 1.3f));
            tgt.earL = tgt.earR = -0.35f;
            tgt.tilt = 0.12f * std::sin (t * 0.9f);
            tgt.browL = tgt.browR = -0.1f;
            break;
        case Fx::crush:
            tgt.pixel = 1.0f;
            tgt.grin = jmax (tgt.grin, 0.85f);
            tgt.mouthOpen = 0.35f;
            break;
        case Fx::gate:
        {
            const float gl = eng.gateLevel.load();
            tgt.mouthOpen = 0.05f + 0.8f * gl;
            tgt.eyeOpen = 0.5f + 0.5f * gl;
            tgt.browL = tgt.browR = -0.4f;
            break;
        }
        case Fx::scatter:
            tgt.glitch = 1.0f;
            tgt.ghost = 0.25f;
            break;
        case Fx::none:
            break;
    }

    const bool napping = shownFx == Fx::none && chaos < 0.01f && pokeTimer <= 0.0f;
    if (napping)
    {
        tgt.eyeOpen = 0.12f;
        tgt.sleepy = 1.0f;
        tgt.lookY = 0.6f;
        tgt.mouthOpen = 0.12f + 0.08f * std::sin (t * 1.2f);
        tgt.earL = tgt.earR = -0.3f;
        tgt.tilt = 0.08f;
        tgt.bob = 3.0f + 2.0f * std::sin (t * 1.2f);
    }

    if (shownForced)
    {
        tgt.browR = jmin (tgt.browR, -0.6f);
        tgt.pupil *= 0.8f;
    }

    if (pokeTimer > 0.0f)
    {
        tgt.happy = 1.0f;
        tgt.mouthOpen = 0.65f;
        tgt.grin = 1.0f;
        tgt.earL = tgt.earR = 0.4f;
    }

    approach (pose.eyeOpen, tgt.eyeOpen, 16.0f, fdt);
    approach (pose.lookX, tgt.lookX, 14.0f, fdt);
    approach (pose.lookY, tgt.lookY, 14.0f, fdt);
    approach (pose.pupil, tgt.pupil, 12.0f, fdt);
    approach (pose.browL, tgt.browL, 10.0f, fdt);
    approach (pose.browR, tgt.browR, 10.0f, fdt);
    approach (pose.mouthOpen, tgt.mouthOpen, shownFx == Fx::gate || shownFx == Fx::stutter ? 40.0f : 14.0f, fdt);
    approach (pose.grin, tgt.grin, 8.0f, fdt);
    approach (pose.earL, tgt.earL, 9.0f, fdt);
    approach (pose.earR, tgt.earR, 9.0f, fdt);
    approach (pose.tilt, tgt.tilt, 8.0f, fdt);
    approach (pose.bob, tgt.bob, 10.0f, fdt);
    approach (pose.spiral, tgt.spiral, 20.0f, fdt);
    approach (pose.sleepy, tgt.sleepy, 8.0f, fdt);
    approach (pose.flip, tgt.flip, 16.0f, fdt);
    approach (pose.pixel, tgt.pixel, 12.0f, fdt);
    approach (pose.glitch, tgt.glitch, 18.0f, fdt);
    approach (pose.ghost, tgt.ghost, 14.0f, fdt);
    approach (pose.happy, tgt.happy, 20.0f, fdt);

    // jitter (stutter): jump on every repeat
    if (jitterAmp > 0.0f)
    {
        const int rep = eng.repeatIndex.load();
        if (rep != lastRepeat)
        {
            lastRepeat = rep;
            jitter = { (random.nextFloat() * 2.0f - 1.0f) * jitterAmp, (random.nextFloat() * 2.0f - 1.0f) * jitterAmp * 0.6f };
        }
    }
    else
    {
        jitter *= jmax (0.0f, 1.0f - fdt * 14.0f);
        lastRepeat = -1;
    }

    glitchTimer -= fdt;
    if (glitchTimer <= 0.0f)
    {
        glitchTimer = 0.06f + random.nextFloat() * 0.05f;
        for (auto& o : stripOffsets)
            o = random.nextFloat() < 0.45f ? 0.0f : random.nextFloat() * 2.0f - 1.0f;
        if (shownFx == Fx::scatter)
            lookTarget = { random.nextFloat() * 1.8f - 0.9f, random.nextFloat() * 1.2f - 0.6f };
    }

    spiralAngle += fdt * (1.5f + 14.0f * tapeSpeed);
    idleTimer += fdt;
    if (idleTimer > 4.5f)
    {
        idleTimer = 0.0f;
        idleMessage = random.nextInt (1000);
    }

    // ---- particles -----------------------------------------------------------------
    if (shownFx == Fx::halfSpeed || napping)
    {
        zTimer -= fdt;
        if (zTimer <= 0.0f && particles.size() < 60)
        {
            zTimer = 0.75f;
            Particle pt;
            pt.kind = 0;
            pt.pos = Point<float> (pose.flip * 58.0f, -58.0f).transformedBy (characterTransform());
            pt.vel = { 14.0f + random.nextFloat() * 10.0f, -26.0f };
            pt.life = 1.8f;
            pt.size = 14.0f + random.nextFloat() * 6.0f;
            pt.colour = pal::purple.brighter (0.4f);
            particles.push_back (pt);
        }
    }

    for (auto& pt : particles)
    {
        pt.age += fdt;
        pt.pos += pt.vel * fdt;
        if (pt.kind != 0)
            pt.vel.y += 160.0f * fdt;
        else
            pt.pos.x += std::sin (pt.age * 4.0f) * 0.4f;
    }
    particles.erase (std::remove_if (particles.begin(), particles.end(), [] (const Particle& pt) { return pt.age >= pt.life; }),
                     particles.end());

    repaint();
}

//==============================================================================
String GremlinFace::statusText() const
{
    String s;
    switch (shownFx)
    {
        case Fx::stutter:   s = lastSlices == 0 ? "RATCHET ROLL!!" : "ST-ST-STUTTER x" + String (lastSlices); break;
        case Fx::reverse:   s = "<< ESREVER"; break;
        case Fx::tapeStop:  s = "TAAAPE  STOOOP..."; break;
        case Fx::halfSpeed: s = "HAAALF   SPEEEED"; break;
        case Fx::crush:     s = "8-BIT CRUNCH"; break;
        case Fx::gate:      s = "CHOP CHOP CHOP"; break;
        case Fx::scatter:   s = "SCATTER -" + String (lastScatter) + (lastScatter == 1 ? " STEP" : " STEPS"); break;
        case Fx::none:
        {
            const float chaos = processor.chaosAmount();
            if (pokeTimer > 0.0f)
                return "HEE HEE! THAT TICKLES";
            if (chaos < 0.01f)
                return "NAPPING... ZZZ";
            static const char* calm[] = { "LURKING...", "SNIFFING TRANSIENTS", "PLOTTING MISCHIEF", "CHEWING ON CABLES",
                                          "COUNTING BEATS", "WAITING FOR THE GRID" };
            static const char* wild[] = { "ITCHING TO GLITCH!!", "MAYHEM MODE", "FINGERS TWITCHING", "NOTHING IS SAFE" };
            if (chaos > 0.7f)
                return wild[idleMessage % 4];
            return calm[idleMessage % 6];
        }
    }
    return shownForced ? "!! " + s : s;
}

//==============================================================================
void GremlinFace::rebuildStatic (float scale)
{
    cachedScale = scale;
    const int w = jmax (1, roundToInt ((float) getWidth() * scale));
    const int h = jmax (1, roundToInt ((float) getHeight() * scale));

    // ---- backdrop: bezel + screen ------------------------------------------------------
    backdrop = Image (Image::ARGB, w, h, true);
    {
        Graphics g (backdrop);
        g.addTransform (AffineTransform::scale (scale));

        g.setColour (Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (bezel.translated (0.0f, 3.0f), 20.0f);
        g.setGradientFill (ColourGradient (Colour (0xff2f2346), 0.0f, bezel.getY(), Colour (0xff140d20), 0.0f, bezel.getBottom(), false));
        g.fillRoundedRectangle (bezel, 20.0f);
        g.setColour (Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (bezel.reduced (1.0f), 19.0f, 1.0f);
        g.setColour (Colours::black.withAlpha (0.7f));
        g.drawRoundedRectangle (bezel, 20.0f, 1.0f);

        // screws
        for (auto pt : { Point<float> (bezel.getX() + 7.0f, bezel.getY() + 7.0f), Point<float> (bezel.getRight() - 7.0f, bezel.getY() + 7.0f),
                         Point<float> (bezel.getX() + 7.0f, bezel.getBottom() - 7.0f), Point<float> (bezel.getRight() - 7.0f, bezel.getBottom() - 7.0f) })
        {
            g.setColour (Colours::black.withAlpha (0.5f));
            g.fillEllipse (Rectangle<float> (5.0f, 5.0f).withCentre (pt.translated (0.0f, 0.5f)));
            g.setColour (Colour (0xff4a3c66));
            g.fillEllipse (Rectangle<float> (4.0f, 4.0f).withCentre (pt));
        }

        g.setFont (aa::Fonts::accent (14.0f));
        g.setColour (pal::textDim.withAlpha (0.5f));
        g.drawText ("GRML-9000", Rectangle<float> (screen.getX() + 4.0f, screen.getBottom() + 1.0f, 100.0f, 13.0f),
                    Justification::centredLeft, false);

        // screen glass
        Path clip;
        clip.addRoundedRectangle (screen, 12.0f);
        g.setColour (Colours::black);
        g.fillPath (clip);
        g.saveState();
        g.reduceClipRegion (clip);
        g.setGradientFill (ColourGradient (Colour (0xff1d1236), screen.getCentreX(), screen.getY() + screen.getHeight() * 0.42f,
                                           Colour (0xff060309), screen.getRight() + 20.0f, screen.getBottom(), true));
        g.fillRect (screen);

        // stars
        Random r (99);
        for (int i = 0; i < 40; ++i)
        {
            const float x = screen.getX() + r.nextFloat() * screen.getWidth();
            const float y = screen.getY() + 24.0f + r.nextFloat() * screen.getHeight() * 0.45f;
            g.setColour (Colours::white.withAlpha (0.08f + r.nextFloat() * 0.22f));
            const float s = r.nextFloat() < 0.15f ? 2.0f : 1.2f;
            g.fillRect (x, y, s, s);
        }

        // synthwave floor
        const float horizon = screen.getY() + screen.getHeight() * 0.66f;
        g.setGradientFill (ColourGradient (pal::purple.withAlpha (0.0f), 0.0f, horizon - 30.0f, pal::purple.withAlpha (0.16f), 0.0f,
                                           horizon, false));
        g.fillRect (screen.withTop (horizon - 30.0f).withBottom (horizon));
        g.setColour (pal::purple.withAlpha (0.28f));
        g.drawHorizontalLine ((int) horizon, screen.getX(), screen.getRight());
        const float vx = screen.getCentreX();
        for (int i = -10; i <= 10; ++i)
        {
            g.setColour (pal::purple.withAlpha (0.16f));
            g.drawLine (vx + (float) i * 14.0f, horizon, vx + (float) i * 60.0f, screen.getBottom() + 10.0f, 1.0f);
        }
        for (int i = 1; i < 7; ++i)
        {
            const float y = horizon + std::pow ((float) i / 6.0f, 1.8f) * (screen.getBottom() - horizon);
            g.setColour (pal::purple.withAlpha (0.12f + 0.02f * (float) i));
            g.drawHorizontalLine ((int) y, screen.getX(), screen.getRight());
        }
        g.restoreState();
    }

    // ---- overlay: scanlines, vignette, glare --------------------------------------------
    overlay = Image (Image::ARGB, w, h, true);
    {
        Graphics g (overlay);
        g.addTransform (AffineTransform::scale (scale));
        Path clip;
        clip.addRoundedRectangle (screen, 12.0f);
        g.saveState();
        g.reduceClipRegion (clip);

        drawScanlines (g, screen, 3.0f, 0.2f);

        ColourGradient vignette (Colours::transparentBlack, screen.getCentreX(), screen.getCentreY(),
                                 Colours::black.withAlpha (0.6f), screen.getX() - 10.0f, screen.getY() - 10.0f, true);
        vignette.addColour (0.6, Colours::transparentBlack);
        g.setGradientFill (vignette);
        g.fillRect (screen);

        Path glare;
        glare.startNewSubPath (screen.getX(), screen.getY());
        glare.lineTo (screen.getX() + screen.getWidth() * 0.62f, screen.getY());
        glare.quadraticTo (screen.getX() + screen.getWidth() * 0.3f, screen.getY() + screen.getHeight() * 0.18f,
                           screen.getX(), screen.getY() + screen.getHeight() * 0.42f);
        glare.closeSubPath();
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.07f), screen.getX(), screen.getY(), Colours::white.withAlpha (0.0f),
                                           screen.getX() + screen.getWidth() * 0.35f, screen.getY() + screen.getHeight() * 0.3f, false));
        g.fillPath (glare);
        g.restoreState();

        g.setColour (Colours::black.withAlpha (0.85f));
        g.drawRoundedRectangle (screen.reduced (0.5f), 12.0f, 2.5f);
        g.setColour (Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (screen.expanded (1.5f), 13.5f, 1.0f);
    }
}

//==============================================================================
void GremlinFace::drawEye (Graphics& g, const Pose& p, float s, Colour eyeCol) const
{
    const Point<float> inner (s * 11.0f, -6.0f), outer (s * 52.0f, -18.0f);
    const Point<float> ec (s * 31.0f, -13.0f);

    if (p.happy > 0.5f)
    {
        Path arc;
        arc.startNewSubPath (s * 14.0f, -8.0f);
        arc.quadraticTo (s * 31.0f, -32.0f, s * 48.0f, -10.0f);
        g.setColour (outlineCol);
        g.strokePath (arc, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded));
        return;
    }

    Path eye;
    eye.startNewSubPath (inner);
    eye.quadraticTo (s * 30.0f, -42.0f, outer.x, outer.y);
    eye.quadraticTo (s * 32.0f, 10.0f, inner.x, inner.y);
    eye.closeSubPath();

    g.setColour (skinDeep.withAlpha (0.6f));
    g.strokePath (eye, PathStrokeType (8.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (Colour (0xff12071c));
    g.fillPath (eye);

    {
        Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (eye);
        const auto ic = ec + Point<float> (p.lookX * 9.0f, p.lookY * 5.0f);

        ColourGradient glow (eyeCol.withAlpha (0.55f), ic.x, ic.y, eyeCol.withAlpha (0.0f), ic.x + 28.0f, ic.y, true);
        g.setGradientFill (glow);
        g.fillEllipse (Rectangle<float> (56.0f, 56.0f).withCentre (ic));

        if (p.spiral > 0.5f)
        {
            Path sp;
            const int n = 70;
            for (int i = 0; i <= n; ++i)
            {
                const float tt = (float) i / (float) n;
                const float a = spiralAngle * s + tt * MathConstants<float>::twoPi * 3.0f;
                const float rad = 0.8f + 13.0f * tt;
                const Point<float> q (ec.x + std::cos (a) * rad * s, ec.y + std::sin (a) * rad);
                if (i == 0)
                    sp.startNewSubPath (q);
                else
                    sp.lineTo (q);
            }
            g.setColour (eyeCol.withAlpha (0.35f));
            g.strokePath (sp, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (eyeCol.brighter (0.4f));
            g.strokePath (sp, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        else if (p.pixel > 0.5f)
        {
            g.setColour (eyeCol);
            g.fillRect (Rectangle<float> (21.0f, 21.0f).withCentre (ic));
            g.setColour (outlineCol);
            g.fillRect (Rectangle<float> (6.0f, 13.0f).withCentre (ic));
            g.setColour (Colours::white);
            g.fillRect (Rectangle<float> (4.0f, 4.0f).withCentre (ic + Point<float> (-6.0f, -6.0f)));
        }
        else
        {
            const auto iris = Rectangle<float> (23.0f, 23.0f).withCentre (ic);
            ColourGradient ig (eyeCol.brighter (0.6f), ic.x - 2.0f, ic.y - 3.0f, eyeCol.darker (0.45f), ic.x + 11.0f, ic.y + 4.0f, true);
            ig.addColour (0.5, eyeCol);
            g.setGradientFill (ig);
            g.fillEllipse (iris);
            g.setColour (outlineCol);
            g.fillEllipse (Rectangle<float> (1.4f + 3.6f * p.pupil, 16.0f * (0.75f + 0.25f * p.pupil)).withCentre (ic));
            g.setColour (Colours::white.withAlpha (0.95f));
            g.fillEllipse (Rectangle<float> (4.5f, 4.5f).withCentre (ic + Point<float> (-4.5f, -5.0f)));
            g.fillEllipse (Rectangle<float> (2.2f, 2.2f).withCentre (ic + Point<float> (4.0f, 4.5f)));
        }

        // eyelid
        float open = jlimit (0.0f, 1.0f, p.eyeOpen);
        if (blinkPhase > 0.0f && p.spiral < 0.5f)
            open *= 1.0f - std::sin (MathConstants<float>::pi * (1.0f - blinkPhase / 0.16f));
        if (open < 0.995f)
        {
            const float top = -34.0f, bottom = 4.0f;
            const float lidY = top + (bottom - top) * (1.0f - open);
            Path lid;
            lid.startNewSubPath (s * 2.0f, top - 12.0f);
            lid.lineTo (s * 62.0f, top - 12.0f);
            lid.lineTo (s * 62.0f, lidY - 7.0f);
            lid.quadraticTo (s * 31.0f, lidY + 6.0f, s * 2.0f, lidY + 1.0f);
            lid.closeSubPath();
            g.setGradientFill (ColourGradient (skin, 0.0f, top - 10.0f, skinLo, 0.0f, lidY + 4.0f, false));
            g.fillPath (lid);
            Path edge;
            edge.startNewSubPath (s * 62.0f, lidY - 7.0f);
            edge.quadraticTo (s * 31.0f, lidY + 6.0f, s * 2.0f, lidY + 1.0f);
            g.setColour (outlineCol);
            g.strokePath (edge, PathStrokeType (2.6f, PathStrokeType::curved, PathStrokeType::rounded));
        }
    }

    g.setColour (outlineCol);
    g.strokePath (eye, PathStrokeType (2.8f, PathStrokeType::curved, PathStrokeType::rounded));
}

void GremlinFace::drawMouth (Graphics& g, const Pose& p) const
{
    const float cy = 30.0f;
    const float w = 24.0f + 32.0f * p.grin;
    const float curl = 4.0f + 11.0f * p.grin;
    const float open = 2.0f + 26.0f * p.mouthOpen;
    const float y0 = cy - curl;
    const float upC = cy + 7.0f;
    const float loC = cy + 9.0f + open * 2.0f;

    auto curveY = [&] (float x, float ctrl)
    {
        const float tt = jlimit (0.0f, 1.0f, (x + w) / (2.0f * w));
        return y0 + 2.0f * tt * (1.0f - tt) * (ctrl - y0);
    };

    Path m;
    m.startNewSubPath (-w, y0);
    m.quadraticTo (0.0f, upC, w, y0);
    m.quadraticTo (0.0f, loC, -w, y0);
    m.closeSubPath();

    g.setGradientFill (ColourGradient (mouthTop, 0.0f, y0, mouthBottom, 0.0f, curveY (0.0f, loC), false));
    g.fillPath (m);

    {
        Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (m);

        // tongue
        const float tongueY = curveY (0.0f, loC);
        auto tongue = Rectangle<float> (w * 0.95f, 10.0f + open * 0.6f).withCentre ({ w * 0.16f, tongueY - 1.0f });
        g.setColour (tongueCol);
        g.fillEllipse (tongue);
        g.setColour (tongueCol.darker (0.4f));
        g.drawLine (tongue.getCentreX(), tongue.getY() + 3.0f, tongue.getCentreX() + 1.0f, tongue.getCentreY() + 2.0f, 1.4f);

        // upper teeth
        const int n = 8 + roundToInt (p.grin * 5.0f);
        const float tw = 2.0f * w / (float) n;
        for (int i = 0; i < n; ++i)
        {
            const float x0 = -w + (float) i * tw;
            const float xm = x0 + tw * 0.5f;
            const float yt = curveY (xm, upC);
            float len = (i % 2 == 0 ? 7.5f : 6.0f) * (0.8f + 0.4f * p.grin);
            float spread = 0.0f;
            if (i == n - 3)
            {
                len = 13.5f; // the snaggle fang
                spread = 1.2f;
            }
            Path tooth;
            tooth.addTriangle (x0 + 0.5f - spread, yt - 5.0f, x0 + tw - 0.5f + spread, yt - 5.0f, xm, yt + len);
            g.setColour (toothCol);
            g.fillPath (tooth);
            g.setColour (outlineCol.withAlpha (0.35f));
            g.strokePath (tooth, PathStrokeType (1.0f));
        }

        // lower fangs
        for (float xf : { -w * 0.55f, w * 0.32f })
        {
            const float yb = curveY (xf, loC);
            Path fang;
            fang.addTriangle (xf - 4.5f, yb + 5.0f, xf + 4.5f, yb + 5.0f, xf, yb - 8.0f - open * 0.15f);
            g.setColour (toothCol);
            g.fillPath (fang);
            g.setColour (outlineCol.withAlpha (0.35f));
            g.strokePath (fang, PathStrokeType (1.0f));
        }
    }

    g.setColour (outlineCol);
    g.strokePath (m, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));

    // cheek dimples
    for (float s : { -1.0f, 1.0f })
    {
        Path d;
        d.startNewSubPath (s * (w - 3.0f), y0 - 6.0f);
        d.quadraticTo (s * (w + 6.0f), y0 - 2.0f, s * (w + 3.0f), y0 + 5.0f);
        g.setColour (skinDeep);
        g.strokePath (d, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
    }
}

void GremlinFace::drawHands (Graphics& g, const Pose&) const
{
    for (float s : { -1.0f, 1.0f })
    {
        const float lift = (s < 0.0f ? tap : 0.0f) * 6.0f + pokeTimer * 4.0f * (s > 0.0f ? 1.0f : 0.0f);
        const float cx = s * 70.0f;
        const float y = 90.0f - lift;

        for (int f = -1; f <= 1; ++f)
        {
            const float fx = cx + (float) f * 11.0f;
            Path claw;
            claw.addTriangle (fx - 3.5f, y + 18.0f, fx + 3.5f, y + 18.0f, fx + s * 1.5f, y + 26.0f);
            g.setColour (toothCol);
            g.fillPath (claw);
            g.setColour (outlineCol);
            g.strokePath (claw, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));

            auto finger = Rectangle<float> (10.0f, 22.0f).withCentre ({ fx, y + 9.0f });
            g.setColour (skin);
            g.fillRoundedRectangle (finger, 5.0f);
            g.setColour (outlineCol);
            g.drawRoundedRectangle (finger, 5.0f, 2.4f);
        }

        auto palm = Rectangle<float> (42.0f, 20.0f).withCentre ({ cx, y });
        g.setGradientFill (ColourGradient (skinHi, cx - 10.0f, y - 10.0f, skinLo, cx + 10.0f, y + 12.0f, false));
        g.fillEllipse (palm);
        g.setColour (outlineCol);
        g.drawEllipse (palm, 2.6f);
        // knuckle lines
        g.setColour (skinDeep);
        for (int f = -1; f <= 1; ++f)
            g.drawLine (cx + (float) f * 11.0f, y + 4.0f, cx + (float) f * 11.0f, y + 8.0f, 1.6f);
    }
}

void GremlinFace::drawCharacter (Graphics& g, const Pose& p, Colour eyeCol) const
{
    // ---- body peeking up from behind the ledge -----------------------------------------
    {
        Path body;
        body.startNewSubPath (-44.0f, 40.0f);
        body.cubicTo (-70.0f, 58.0f, -96.0f, 90.0f, -104.0f, 130.0f);
        body.lineTo (104.0f, 130.0f);
        body.cubicTo (96.0f, 90.0f, 70.0f, 58.0f, 44.0f, 40.0f);
        body.closeSubPath();
        g.setGradientFill (ColourGradient (skinLo, 0.0f, 50.0f, skinDeep, 0.0f, 120.0f, false));
        g.fillPath (body);
        g.setColour (outlineCol);
        g.strokePath (body, PathStrokeType (3.2f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // ---- head group -------------------------------------------------------------------
    {
        Graphics::ScopedSaveState save (g);
        const float hopSquash = jlimit (-0.06f, 0.06f, hop * 0.004f);
        g.addTransform (AffineTransform::scale (p.flip * (1.0f - hopSquash), 1.0f + hopSquash)
                            .rotated (p.tilt)
                            .translated (jitter.x, p.bob + hop + jitter.y));

        const auto earRT = AffineTransform::rotation (-(p.earR + earTwitchR * 0.12f)).translated (62.0f, -18.0f);
        const auto earLT = AffineTransform::scale (-1.0f, 1.0f).rotated (p.earL + earTwitchL * 0.12f).translated (-62.0f, -18.0f);

        Path earR = makeEar (false), earL = makeEar (true);
        earR.applyTransform (earRT);
        earL.applyTransform (earLT);
        const Path head = makeHead();

        // neon rim glow
        Path silhouette;
        silhouette.addPath (head);
        silhouette.addPath (earR);
        silhouette.addPath (earL);
        g.setColour (eyeCol.withAlpha (0.07f + 0.1f * aura));
        g.strokePath (silhouette, PathStrokeType (18.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (eyeCol.withAlpha (0.14f + 0.16f * aura));
        g.strokePath (silhouette, PathStrokeType (8.0f, PathStrokeType::curved, PathStrokeType::rounded));

        auto drawEar = [&] (const Path& outer, const AffineTransform& t, bool earring)
        {
            auto tp = [&t] (float x, float y) { return Point<float> (x, y).transformedBy (t); };
            const auto base = tp (0.0f, 0.0f), tip = tp (90.0f, -60.0f);
            g.setGradientFill (ColourGradient (skin, base.x, base.y, skinLo, tip.x, tip.y, false));
            g.fillPath (outer);
            Path inner = makeInnerEar();
            inner.applyTransform (t);
            const auto a = tp (6.0f, -2.0f), b = tp (76.0f, -54.0f);
            g.setGradientFill (ColourGradient (earInLo, a.x, a.y, earIn, b.x, b.y, false));
            g.fillPath (inner);
            g.setColour (outlineCol);
            g.strokePath (outer, PathStrokeType (3.2f, PathStrokeType::curved, PathStrokeType::rounded));
            if (earring)
            {
                const auto c = tp (36.0f, 4.0f);
                g.setColour (pal::yellow);
                g.drawEllipse (Rectangle<float> (11.0f, 11.0f).withCentre (c.translated (0.0f, 6.0f)), 2.6f);
                g.setColour (Colours::white.withAlpha (0.8f));
                g.fillEllipse (Rectangle<float> (2.4f, 2.4f).withCentre (c.translated (-3.0f, 2.5f)));
            }
        };
        drawEar (earL, earLT, false);
        drawEar (earR, earRT, true);

        // hair tuft (behind the head outline)
        for (int i = -1; i <= 1; ++i)
        {
            const float bx = (float) i * 10.0f + 2.0f;
            const float tipX = bx + (float) i * 13.0f + hair * 0.25f + 5.0f;
            const float tipY = -98.0f + std::abs ((float) i) * 13.0f;
            Path h;
            h.startNewSubPath (bx - 7.0f, -56.0f);
            h.quadraticTo (bx - 4.0f, -78.0f, tipX, tipY);
            h.quadraticTo (bx + 3.0f, -76.0f, bx + 7.0f, -56.0f);
            h.closeSubPath();
            g.setColour (skinLo);
            g.fillPath (h);
            g.setColour (outlineCol);
            g.strokePath (h, PathStrokeType (2.6f, PathStrokeType::curved, PathStrokeType::rounded));
        }

        // head
        ColourGradient hg (skinHi, -26.0f, -38.0f, skinLo, 62.0f, 72.0f, true);
        hg.addColour (0.42, skin);
        g.setGradientFill (hg);
        g.fillPath (head);
        {
            Graphics::ScopedSaveState s2 (g);
            g.reduceClipRegion (head);
            g.setColour (skinHi.withAlpha (0.22f));
            g.fillEllipse (Rectangle<float> (122.0f, 64.0f).withCentre ({ 0.0f, 30.0f }));
            g.setColour (skinDeep.withAlpha (0.35f));
            g.fillEllipse (Rectangle<float> (170.0f, 40.0f).withCentre ({ 0.0f, 74.0f }));
            // blush
            g.setColour (pal::pink.withAlpha (0.22f));
            g.fillEllipse (Rectangle<float> (24.0f, 12.0f).withCentre ({ -52.0f, 18.0f }));
            g.fillEllipse (Rectangle<float> (24.0f, 12.0f).withCentre ({ 52.0f, 18.0f }));
            // freckles + wart (asymmetric on purpose, so the mirror trick reads)
            g.setColour (skinDeep.withAlpha (0.8f));
            for (auto pt : { Point<float> (50.0f, 4.0f), Point<float> (57.0f, 9.0f), Point<float> (47.0f, 11.0f) })
                g.fillEllipse (Rectangle<float> (3.0f, 3.0f).withCentre (pt));
            g.setColour (skinHi.darker (0.15f));
            g.fillEllipse (Rectangle<float> (7.0f, 7.0f).withCentre ({ -58.0f, 4.0f }));
            g.setColour (skinDeep.withAlpha (0.7f));
            g.drawEllipse (Rectangle<float> (7.0f, 7.0f).withCentre ({ -58.0f, 4.0f }), 1.0f);
        }
        g.setColour (outlineCol);
        g.strokePath (head, PathStrokeType (3.5f, PathStrokeType::curved, PathStrokeType::rounded));

        // brows
        for (float s : { -1.0f, 1.0f })
        {
            const float b = s < 0.0f ? p.browL : p.browR;
            Path brow;
            brow.startNewSubPath (s * 12.0f, -37.0f - b * 6.0f + jmax (0.0f, -b) * 8.0f);
            brow.quadraticTo (s * 30.0f, -51.0f - b * 7.0f, s * 53.0f, -41.0f - b * 5.0f - jmax (0.0f, b) * 3.0f);
            g.setColour (skinDeep);
            g.strokePath (brow, PathStrokeType (7.0f, PathStrokeType::curved, PathStrokeType::rounded));
        }

        drawEye (g, p, -1.0f, eyeCol);
        drawEye (g, p, 1.0f, eyeCol);

        // nose
        g.setColour (skinDeep);
        g.fillEllipse (Rectangle<float> (5.0f, 3.6f).withCentre ({ -6.0f, 9.0f }));
        g.fillEllipse (Rectangle<float> (5.0f, 3.6f).withCentre ({ 6.0f, 9.0f }));
        Path snout;
        snout.startNewSubPath (-10.0f, 3.0f);
        snout.quadraticTo (0.0f, -2.0f, 10.0f, 3.0f);
        g.setColour (skinHi.withAlpha (0.6f));
        g.strokePath (snout, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));

        drawMouth (g, p);
    }

    // ---- the ledge it's hiding behind -------------------------------------------------------
    {
        auto ledge = Rectangle<float> (-200.0f, 100.0f, 400.0f, 40.0f);
        g.setGradientFill (ColourGradient (Colour (0xff2a1d42), 0.0f, 100.0f, Colour (0xff0c0716), 0.0f, 128.0f, false));
        g.fillRect (ledge);
        g.setColour (eyeCol.withAlpha (0.25f));
        g.fillRect (ledge.withHeight (4.0f).translated (0.0f, -1.0f));
        g.setColour (eyeCol.withAlpha (0.8f));
        g.fillRect (ledge.withHeight (1.5f));
    }

    drawHands (g, p);
}

void GremlinFace::renderCharacterImage (Image& img, float pixelsPerUnit, Colour eyeCol) const
{
    img.clear (img.getBounds());
    Graphics g (img);
    g.addTransform (characterTransform().translated (-faceArea.getX(), -faceArea.getY()).scaled (pixelsPerUnit));
    drawCharacter (g, pose, eyeCol);
}

//==============================================================================
void GremlinFace::paint (Graphics& g)
{
    const float phys = jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (backdrop.isNull() || std::abs (cachedScale - phys) > 0.01f)
        rebuildStatic (phys);

    const auto bounds = getLocalBounds().toFloat();
    g.drawImage (backdrop, bounds);

    const auto eyeCol = eyeColour();
    {
        Graphics::ScopedSaveState save (g);
        Path clip;
        clip.addRoundedRectangle (screen, 12.0f);
        g.reduceClipRegion (clip);

        // aura behind the gremlin
        const auto ct = characterTransform();
        const auto headC = Point<float> (0.0f, -6.0f).transformedBy (ct);
        const float auraR = faceArea.getWidth() * (0.5f + 0.06f * aura);
        g.setGradientFill (ColourGradient (eyeCol.withAlpha (0.12f + 0.2f * aura), headC.x, headC.y, eyeCol.withAlpha (0.0f),
                                           headC.x + auraR, headC.y, true));
        g.fillEllipse (Rectangle<float> (auraR * 2.0f, auraR * 2.0f).withCentre (headC));

        // character, rendered off-screen so we can mangle it
        Image* img = nullptr;
        if (pose.pixel > 0.04f)
        {
            const float block = 1.0f + pose.pixel * 6.5f;
            const int w = jmax (1, (int) std::ceil (faceArea.getWidth() / block));
            const int h = jmax (1, (int) std::ceil (faceArea.getHeight() / block));
            if (pixelImage.getWidth() != w || pixelImage.getHeight() != h)
                pixelImage = Image (Image::ARGB, w, h, true);
            renderCharacterImage (pixelImage, (float) w / faceArea.getWidth(), eyeCol);
            img = &pixelImage;
            g.setImageResamplingQuality (Graphics::lowResamplingQuality);
        }
        else
        {
            const int w = jmax (1, roundToInt (faceArea.getWidth() * phys));
            const int h = jmax (1, roundToInt (faceArea.getHeight() * phys));
            if (faceImage.getWidth() != w || faceImage.getHeight() != h)
                faceImage = Image (Image::ARGB, w, h, true);
            renderCharacterImage (faceImage, (float) w / faceArea.getWidth(), eyeCol);
            img = &faceImage;
        }

        if (pose.ghost > 0.02f)
        {
            const float d = 3.0f + 4.0f * pose.ghost;
            g.setColour (pal::pink.withAlpha (0.45f * pose.ghost));
            g.drawImage (*img, faceArea.translated (-d, 0.0f), RectanglePlacement::stretchToFit, true);
            g.setColour (pal::cyan.withAlpha (0.45f * pose.ghost));
            g.drawImage (*img, faceArea.translated (d, 0.0f), RectanglePlacement::stretchToFit, true);
        }

        g.setColour (Colours::black);
        if (pose.glitch > 0.02f)
        {
            const int n = (int) stripOffsets.size();
            const float sh = faceArea.getHeight() / (float) n;
            for (int i = 0; i < n; ++i)
            {
                Graphics::ScopedSaveState s2 (g);
                g.reduceClipRegion (Rectangle<float> (faceArea.getX() - 40.0f, faceArea.getY() + sh * (float) i,
                                                      faceArea.getWidth() + 80.0f, sh + 0.5f).toNearestInt());
                const float off = stripOffsets[(size_t) i] * pose.glitch * 18.0f;
                if (std::abs (off) > 9.0f)
                {
                    g.setColour ((i % 2 == 0 ? pal::cyan : pal::pink).withAlpha (0.5f));
                    g.drawImage (*img, faceArea.translated (off * 1.6f, 0.0f), RectanglePlacement::stretchToFit, true);
                    g.setColour (Colours::black);
                }
                g.drawImage (*img, faceArea.translated (off, 0.0f));
            }
        }
        else
        {
            g.drawImage (*img, faceArea);
        }
        g.setImageResamplingQuality (Graphics::mediumResamplingQuality);

        // particles
        for (const auto& pt : particles)
        {
            const float a = jlimit (0.0f, 1.0f, 1.0f - pt.age / pt.life);
            if (pt.kind == 0)
            {
                g.setColour (pt.colour.withAlpha (a));
                g.setFont (aa::Fonts::accent (pt.size));
                g.drawText ("z", Rectangle<float> (pt.size * 1.5f, pt.size * 1.5f).withCentre (pt.pos), Justification::centred, false);
            }
            else if (pt.kind == 1)
            {
                g.setColour (pt.colour.withAlpha (a));
                g.fillRect (Rectangle<float> (pt.size, pt.size).withCentre (pt.pos));
            }
            else
            {
                g.setColour (pt.colour.withAlpha (a * 0.3f));
                g.fillEllipse (Rectangle<float> (pt.size * 3.0f, pt.size * 3.0f).withCentre (pt.pos));
                g.setColour (pt.colour.withAlpha (a));
                g.fillEllipse (Rectangle<float> (pt.size, pt.size).withCentre (pt.pos));
            }
        }

        // status line
        const bool cursorOn = std::fmod (clock, 1.0) < 0.55;
        const auto status = "> " + statusText();
        auto font = aa::Fonts::accent (21.0f);
        drawChromaticText (g, status, font, statusArea, Justification::centredLeft,
                           shownFx == Fx::none ? pal::text.withAlpha (0.85f) : eyeCol, shownFx == Fx::none ? 0.8f : 1.6f);
        if (cursorOn)
        {
            const float tw = aa::Fonts::textWidth (font, status);
            g.setColour ((shownFx == Fx::none ? pal::green : eyeCol).withAlpha (0.9f));
            g.fillRect (Rectangle<float> (statusArea.getX() + tw + 3.0f, statusArea.getY() + 4.0f, 8.0f, 13.0f));
        }

        const auto bpmText = String (processor.engine.uiBpm.load(), 1) + " BPM";
        const auto bpmFont = aa::Fonts::accent (17.0f);
        if (aa::Fonts::textWidth (font, status) + 14.0f + aa::Fonts::textWidth (bpmFont, bpmText) + 10.0f < statusArea.getWidth())
        {
            g.setFont (bpmFont);
            g.setColour (pal::textDim.withAlpha (0.6f));
            g.drawText (bpmText, statusArea, Justification::centredRight, false);
        }
    }

    g.drawImage (overlay, bounds);

    // power LED
    const auto led = Rectangle<float> (6.0f, 6.0f).withCentre ({ screen.getRight() - 6.0f, screen.getBottom() + 7.5f });
    const auto ledCol = shownForced ? pal::pink : (shownFx != Fx::none ? eyeCol : pal::green.withMultipliedBrightness (0.6f));
    g.setColour (ledCol.withAlpha (0.3f));
    g.fillEllipse (led.expanded (3.0f));
    g.setColour (ledCol);
    g.fillEllipse (led);
}
} // namespace gremlin
