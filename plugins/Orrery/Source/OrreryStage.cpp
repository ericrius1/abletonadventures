#include "OrreryStage.h"

using namespace juce;
namespace pal = orrery::palette;

namespace
{
    constexpr float twoPi = MathConstants<float>::twoPi;
    constexpr float rippleLife = 0.75f;
    constexpr float labelLife = 1.5f;

    float wrapAngle (float a)
    {
        a = std::fmod (a + MathConstants<float>::pi, twoPi);
        if (a < 0.0f)
            a += twoPi;
        return a - MathConstants<float>::pi;
    }

    double frac (double x) { return x - std::floor (x); }

    const char* romanNumeral (int n)
    {
        static const char* r[] = { "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII", "XIII", "XIV", "XV", "XVI" };
        return r[jlimit (0, 15, n - 1)];
    }
} // namespace

OrreryStage::OrreryStage (OrreryProcessor& p) : proc (p)
{
    setOpaque (false);
    selected = jlimit (0, orrery::numOrbits - 1, proc.selectedOrbit);
    pending.reserve (512);
    ripples.reserve (96);
    labels.reserve (96);
    sparks.reserve (200);

    Random r (7);
    for (int i = 0; i < 46; ++i)
        twinkles.push_back ({ { r.nextFloat(), r.nextFloat() }, 0.8f + r.nextFloat() * 1.6f, r.nextFloat() * twoPi,
                              0.6f + r.nextFloat() * 1.8f, 0.35f + r.nextFloat() * 0.65f });

    for (int i = 0; i < orrery::numOrbits; ++i)
    {
        lastBeats[(size_t) i] = -1;
        lastSpeed[(size_t) i] = 1.0;
    }

    // forget anything that happened while the window was closed
    OrreryProcessor::NoteEvent ev;
    while (proc.noteEvents.pop (ev)) {}
}

void OrreryStage::resized()
{
    background = {};
    orbitLayer = {};
}

void OrreryStage::setSelected (int orbit)
{
    selected = jlimit (0, orrery::numOrbits - 1, orbit);
    repaint();
}

Point<float> OrreryStage::polar (float radius, float angle) const noexcept
{
    const auto c = centre();
    return { c.x + radius * std::sin (angle), c.y - radius * std::cos (angle) };
}

float OrreryStage::markerAngle (int orbit, int pulse) const
{
    const auto& op = proc.orbitParams[(size_t) orbit];
    const int pulses = jlimit (1, 16, (int) std::round (op.pulses->load()));
    const float offset = op.offset->load() / 100.0f;
    return twoPi * ((float) pulse + offset) / (float) pulses;
}

float OrreryStage::getPlanetPhase (int orbit) const
{
    return (float) frac ((double) displayAngle[(size_t) jlimit (0, orrery::numOrbits - 1, orbit)] / (double) twoPi);
}

int OrreryStage::orbitAt (Point<float> pos) const
{
    for (int i = orrery::numOrbits - 1; i >= 0; --i)
        if (pos.getDistanceFrom (planetPos[(size_t) i]) < orrery::planets()[(size_t) i].size * unit() + 8.0f)
            return i;

    const float d = pos.getDistanceFrom (centre());
    int best = -1;
    float bestDist = 14.0f * unit();
    for (int i = 0; i < orrery::numOrbits; ++i)
    {
        const float dist = std::abs (d - orbitRadius (i));
        if (dist < bestDist)
        {
            bestDist = dist;
            best = i;
        }
    }
    return best;
}

void OrreryStage::setParam (const String& id, float realValue)
{
    if (auto* p = proc.param (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (realValue));
        p->endChangeGesture();
    }
}

//==============================================================================
void OrreryStage::mouseDown (const MouseEvent& e)
{
    const int i = orbitAt (e.position);
    if (i >= 0 && onSelect)
        onSelect (i);
}

void OrreryStage::mouseDoubleClick (const MouseEvent& e)
{
    const int i = orbitAt (e.position);
    if (i >= 0)
    {
        const bool on = proc.orbitParams[(size_t) i].on->load() > 0.5f;
        setParam (orrery::orbitParamId (i, "on"), on ? 0.0f : 1.0f);
    }
}

void OrreryStage::mouseMove (const MouseEvent& e)
{
    const int h = orbitAt (e.position);
    hoverSun = e.position.getDistanceFrom (centre()) < 30.0f * unit();
    if (h != hovered)
    {
        hovered = h;
        setMouseCursor (h >= 0 ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
    }
}

void OrreryStage::mouseExit (const MouseEvent&)
{
    hovered = -1;
    hoverSun = false;
}

void OrreryStage::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    const int i = orbitAt (e.position);
    if (i < 0)
        return;
    const float d = std::abs (wheel.deltaY) > std::abs (wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;
    if (d == 0.0f)
        return;
    const char* what = e.mods.isShiftDown() ? "beats" : "pulses";
    const auto& op = proc.orbitParams[(size_t) i];
    const int current = (int) std::round ((e.mods.isShiftDown() ? op.beats : op.pulses)->load());
    setParam (orrery::orbitParamId (i, what), (float) jlimit (1, 16, current + (d > 0.0f ? 1 : -1)));
}

String OrreryStage::getTooltip()
{
    if (hovered >= 0)
    {
        const auto& pl = orrery::planets()[(size_t) hovered];
        const auto& op = proc.orbitParams[(size_t) hovered];
        const int pulses = (int) std::round (op.pulses->load()), beats = (int) std::round (op.beats->load());
        return String (pl.name) + ": " + String (pulses) + (pulses == 1 ? " pulse" : " pulses") + " every " + String (beats)
               + (beats == 1 ? " beat" : " beats") + ", playing " + orrery::noteName (proc.currentNoteForOrbit (hovered))
               + ". Click to edit, double-click to mute, scroll for pulses (shift: beats).";
    }
    if (hoverSun)
        return "The sun glows on every beat and sings on the downbeat.";
    return "Click a planet or its orbit to edit it. Each mark on an orbit plays a note when its planet passes.";
}

//==============================================================================
void OrreryStage::activate (const OrreryProcessor::NoteEvent& ev)
{
    const int o = jlimit (0, orrery::numOrbits - 1, ev.orbit);
    const int pulse = jlimit (0, 15, ev.pulse);
    const bool played = ev.note >= 0;
    auto& f = flash[(size_t) o][(size_t) pulse];
    f = jmax (f, played ? 1.0f : 0.35f);

    if (! played)
        return;

    const auto colour = Colour (orrery::planets()[(size_t) o].colour);
    const auto pos = polar (orbitRadius (o), markerAngle (o, pulse));
    bump[(size_t) o] = 1.0f;
    noteGlow = jmin (1.0f, noteGlow + 0.25f);

    if (ripples.size() >= 90)
        ripples.erase (ripples.begin());
    ripples.push_back ({ pos, 0.0f, 0.4f + 0.6f * ev.velocity, colour });

    if (labels.size() >= 90)
        labels.erase (labels.begin());
    labels.push_back ({ pos, 0.0f, orrery::noteName (ev.note), colour, o == selected });

    // a puff of stardust, flung outward from the sun
    const auto out = pos - centre();
    const float len = jmax (1.0f, out.getDistanceFromOrigin());
    const Point<float> dir (out.x / len, out.y / len), tangent (-dir.y, dir.x);
    for (int k = 0; k < 4 && sparks.size() < 190; ++k)
    {
        const float speed = (18.0f + random.nextFloat() * 30.0f) * unit();
        const float side = (random.nextFloat() - 0.5f) * 50.0f * unit();
        sparks.push_back ({ pos, dir * speed + tangent * side, 1.0f, 1.6f + random.nextFloat() * 1.8f,
                            colour.interpolatedWith (Colours::white, random.nextFloat() * 0.6f) });
    }
}

void OrreryStage::tick (double dt)
{
    clock += dt;
    const auto fdt = (float) dt;

    // ---- musical clock (smoothed toward the audio thread's position) --------------
    const int state = proc.uiRunState.load();
    running = state != OrreryProcessor::stopped;
    bpm = proc.uiBpm.load();
    beatsPerBar = jlimit (1, 16, proc.uiBeatsPerBar.load());

    const double now = Time::getMillisecondCounterHiRes();
    double estimate = proc.uiPpq.load();
    if (running)
        estimate += jlimit (0.0, 120.0, now - proc.uiWallMs.load()) / 60000.0 * bpm;

    const int jumps = proc.uiJumpCount.load();
    if (jumps != lastJumpCount || std::abs (estimate - visPpq) > 0.75)
    {
        lastJumpCount = jumps;
        visPpq = estimate;
        pending.clear();
        lastBeatIndex = std::floor (visPpq);
    }
    else if (running)
    {
        visPpq += dt * bpm / 60.0;
        visPpq += (estimate - visPpq) * jmin (1.0, dt * 6.0);
    }
    else
    {
        visPpq += (estimate - visPpq) * jmin (1.0, dt * 10.0);
    }

    // ---- beats and bars light up the sun ---------------------------------------
    const double beatIndex = std::floor (visPpq + 1.0e-6);
    if (beatIndex != lastBeatIndex)
    {
        if (beatIndex > lastBeatIndex && beatIndex - lastBeatIndex < 4.0)
        {
            const int beatInBar = (int) (((int64) beatIndex % beatsPerBar + beatsPerBar) % beatsPerBar);
            beatPulse = 1.0f;
            beatLamp[(size_t) beatInBar] = 1.0f;
            if (beatInBar == 0)
            {
                sunPulse = 1.0f;
                sing = 1.0f;
            }
        }
        lastBeatIndex = beatIndex;
    }

    // ---- note events: shown exactly when the planet reaches the mark -------------
    OrreryProcessor::NoteEvent ev;
    while (proc.noteEvents.pop (ev))
    {
        if (pending.size() >= 500)
        {
            activate (pending.front());
            pending.erase (pending.begin());
        }
        pending.push_back (ev);
    }
    for (size_t i = 0; i < pending.size();)
    {
        const double ahead = pending[i].ppq - visPpq;
        const bool stale = ahead < -1.0 || ahead > 2.0; // from before a jump, or queued while the window was closed
        if (stale || ahead <= 0.003 || ! running)
        {
            if (! stale)
                activate (pending[i]);
            pending.erase (pending.begin() + (long) i);
        }
        else
            ++i;
    }

    // ---- planets ----------------------------------------------------------------
    const double speedMul = orrery::speedValue ((int) proc.speed->load());
    const float lagDecay = std::exp (-fdt * 9.0f);
    for (int i = 0; i < orrery::numOrbits; ++i)
    {
        const auto& op = proc.orbitParams[(size_t) i];
        const int beats = jlimit (1, 16, (int) std::round (op.beats->load()));
        const float target = twoPi * (float) frac (visPpq * speedMul / beats);

        if (lastBeats[(size_t) i] > 0 && (beats != lastBeats[(size_t) i] || speedMul != lastSpeed[(size_t) i]))
        {
            const float old = twoPi * (float) frac (visPpq * lastSpeed[(size_t) i] / lastBeats[(size_t) i]);
            angleLag[(size_t) i] = wrapAngle (angleLag[(size_t) i] + old - target);
        }
        lastBeats[(size_t) i] = beats;
        lastSpeed[(size_t) i] = speedMul;
        angleLag[(size_t) i] *= lagDecay;
        displayAngle[(size_t) i] = target + angleLag[(size_t) i];

        const float omega = running ? (float) (twoPi * speedMul * bpm / 60.0 / beats) : 0.0f;
        const float trailTarget = running ? jlimit (0.18f, 1.05f, omega * 0.32f) : 0.0f;
        trailLength[(size_t) i] += (trailTarget - trailLength[(size_t) i]) * jmin (1.0f, fdt * 3.0f);

        planetPos[(size_t) i] = polar (orbitRadius (i), displayAngle[(size_t) i]);
        bump[(size_t) i] = jmax (0.0f, bump[(size_t) i] - fdt * 4.0f);

        for (auto& f : flash[(size_t) i])
            f = jmax (0.0f, f - fdt * 1.8f);
    }

    for (auto& r : ripples)
        r.age += fdt;
    ripples.erase (std::remove_if (ripples.begin(), ripples.end(), [] (const Ripple& r) { return r.age > rippleLife; }), ripples.end());
    for (auto& sp : sparks)
    {
        sp.pos += sp.vel * fdt;
        sp.vel *= std::exp (-fdt * 2.5f);
        sp.life -= fdt * 1.3f;
    }
    sparks.erase (std::remove_if (sparks.begin(), sparks.end(), [] (const Spark& sp) { return sp.life <= 0.0f; }), sparks.end());
    for (auto& l : labels)
        l.age += fdt;
    labels.erase (std::remove_if (labels.begin(), labels.end(), [] (const FloatingLabel& l) { return l.age > labelLife; }), labels.end());

    // ---- harmony caption --------------------------------------------------------
    {
        std::array<uint32, 4> mask {};
        bool any = false;
        for (int w = 0; w < 4; ++w)
        {
            mask[(size_t) w] = proc.uiChord[w].load (std::memory_order_relaxed);
            any = any || mask[(size_t) w] != 0;
        }
        const int sc = (int) proc.scale->load(), rt = (int) proc.root->load();
        const bool followOn = proc.follow->load() > 0.5f;
        if (mask != lastChordMask || sc != lastScale || rt != lastRoot || followOn != lastFollowOn || harmonyText.isEmpty())
        {
            lastChordMask = mask;
            lastScale = sc;
            lastRoot = rt;
            lastFollowOn = followOn;
            following = any;
            harmonyText = any ? "FOLLOWING  " + proc.followedChordText().replace (" ", "  ")
                              : orrery::rootNames()[rt] + "  " + String (orrery::scales()[(size_t) jlimit (0, 9, sc)].name).toUpperCase();
            if (any)
                followGlow = 1.0f;
        }
        followGlow = jmax (following ? 0.35f : 0.0f, followGlow - fdt * 1.5f);
    }

    sunPulse = jmax (0.0f, sunPulse - fdt * 1.6f);
    beatPulse = jmax (0.0f, beatPulse - fdt * 3.5f);
    sing = jmax (0.0f, sing - fdt * 2.2f);
    noteGlow = jmax (0.0f, noteGlow - fdt * 2.5f);
    for (auto& l : beatLamp)
        l = jmax (0.0f, l - fdt * 2.2f);

    repaint();
}

//==============================================================================
void OrreryStage::rebuildBackground (float scale)
{
    backgroundScale = scale;
    backgroundBeats = beatsPerBar;
    background = Image (Image::ARGB, jmax (1, roundToInt ((float) getWidth() * scale)), jmax (1, roundToInt ((float) getHeight() * scale)), true);
    Graphics g (background);
    g.addTransform (AffineTransform::scale (scale));

    const auto c = centre();
    const float u = unit();
    const float rIn = bezelInner(), rOut = bezelOuter();

    // Plate: a faint glow that draws the eye to the centre
    {
        ColourGradient plate (Colour (0x3a3b4a9a), c.x, c.y, Colour (0x000b1230), c.x + rIn, c.y, true);
        plate.addColour (0.55, Colour (0x1a2a2f6a));
        g.setGradientFill (plate);
        g.fillEllipse (Rectangle<float> (rIn * 2.0f, rIn * 2.0f).withCentre (c));
    }

    // faint engraved guide circles and spokes between the orbits
    g.setColour (pal::brass.withAlpha (0.05f));
    for (int k = 0; k < 24; ++k)
    {
        const float a = twoPi * (float) k / 24.0f;
        g.drawLine ({ polar (40.0f * u, a), polar (rIn - 4.0f * u, a) }, 0.6f);
    }

    // Bezel band
    {
        Path ring;
        ring.addEllipse (Rectangle<float> (rOut * 2.0f, rOut * 2.0f).withCentre (c));
        ring.addEllipse (Rectangle<float> (rIn * 2.0f, rIn * 2.0f).withCentre (c));
        ring.setUsingNonZeroWinding (false);
        // polished brass: bright where the "light" hits the top-left, deep at the bottom-right
        ColourGradient band (Colour (0xffe9cc8c), c.x - rOut * 0.7f, c.y - rOut, Colour (0xff4a3519), c.x + rOut * 0.7f, c.y + rOut, false);
        band.addColour (0.18, Colour (0xffc99f5c));
        band.addColour (0.38, Colour (0xff8e6a34));
        band.addColour (0.5, Colour (0xffb38a4b));
        band.addColour (0.66, Colour (0xff7a5a2a));
        band.addColour (0.82, Colour (0xff9b7640));
        g.setGradientFill (band);
        g.setOpacity (0.82f);
        g.fillPath (ring);
        g.setOpacity (1.0f);

        // soft inner shadow where the band meets the plate
        g.setColour (pal::ink.withAlpha (0.35f));
        g.drawEllipse (Rectangle<float> (rIn * 2.0f, rIn * 2.0f).withCentre (c).expanded (3.0f), 3.0f);

        g.setColour (pal::ink.withAlpha (0.6f));
        g.drawEllipse (Rectangle<float> (rIn * 2.0f, rIn * 2.0f).withCentre (c).expanded (1.2f), 1.4f);
        g.setGradientFill (ColourGradient (pal::brassLight, c.x - rOut, c.y - rOut, pal::brassDark, c.x + rOut, c.y + rOut, false));
        g.drawEllipse (Rectangle<float> (rOut * 2.0f, rOut * 2.0f).withCentre (c), 1.6f);
        g.drawEllipse (Rectangle<float> (rIn * 2.0f, rIn * 2.0f).withCentre (c), 1.0f);
        g.setColour (pal::brass.withAlpha (0.3f));
        g.drawEllipse (Rectangle<float> ((rOut + 5.0f * u) * 2.0f, (rOut + 5.0f * u) * 2.0f).withCentre (c), 0.7f);

        // a fine ring of engraved dots just inside the bezel
        g.setColour (pal::brass.withAlpha (0.35f));
        for (int k = 0; k < 96; ++k)
            g.fillEllipse (Rectangle<float> (1.1f * u, 1.1f * u).withCentre (polar (rIn - 5.0f * u, twoPi * (float) k / 96.0f)));
    }

    // Sixteenth ticks and beat numerals engraved into the band
    const int ticks = beatsPerBar * 4;
    for (int k = 0; k < ticks; ++k)
    {
        const float a = twoPi * (float) k / (float) ticks;
        const bool beat = k % 4 == 0;
        if (beat)
            continue;
        g.setColour (pal::ink.withAlpha (0.7f));
        g.drawLine ({ polar (rIn + 0.5f, a), polar (rIn + 3.5f * u, a) }, 1.1f);
        g.drawLine ({ polar (rOut - 3.5f * u, a), polar (rOut - 0.5f, a) }, 1.1f);
        const float mid = (rIn + rOut) * 0.5f;
        g.setColour (pal::ink.withAlpha (0.55f));
        g.fillEllipse (Rectangle<float> (1.8f * u, 1.8f * u).withCentre (polar (mid, a)));
        g.setColour (pal::brassLight.withAlpha (0.35f));
        g.fillEllipse (Rectangle<float> (1.2f * u, 1.2f * u).withCentre (polar (mid, a).translated (0.0f, 0.8f)));
    }

    numeralPaths.clear();
    const auto numeralFont = aa::Fonts::display (10.0f * u);
    for (int b = 0; b < beatsPerBar; ++b)
    {
        const float a = twoPi * (float) b / (float) beatsPerBar;
        GlyphArrangement ga;
        const String text (romanNumeral (b + 1));
        const float w = aa::Fonts::textWidth (numeralFont, text);
        ga.addLineOfText (numeralFont, text, -w * 0.5f, 3.6f * u);
        Path p;
        ga.createPath (p);
        p.applyTransform (AffineTransform::rotation (a).translated (polar ((rIn + rOut) * 0.5f, a)));
        numeralPaths.push_back (p);
        g.setColour (Colour (0xfffff0c8).withAlpha (0.45f));
        g.fillPath (p, AffineTransform::translation (0.0f, 0.9f));
        g.setColour (pal::ink.withAlpha (0.9f));
        g.fillPath (p);
    }

    // Corner flourishes
    auto b = getLocalBounds().toFloat().reduced (6.0f * u);
    const Point<float> corners[] = { b.getTopLeft(), b.getTopRight(), b.getBottomRight(), b.getBottomLeft() };
    for (int k = 0; k < 4; ++k)
    {
        const auto p = corners[k];
        const float start = twoPi * 0.25f * (float) k + twoPi * 0.25f;
        for (float rr : { 22.0f, 28.0f })
        {
            Path arc;
            arc.addCentredArc (p.x, p.y, rr * u, rr * u, 0.0f, start, start + twoPi * 0.25f, true);
            g.setColour (pal::brass.withAlpha (rr < 25.0f ? 0.45f : 0.22f));
            g.strokePath (arc, PathStrokeType (rr < 25.0f ? 1.0f : 0.7f));
        }
        const float mid = start + twoPi * 0.125f;
        const Point<float> dir (std::sin (mid), -std::cos (mid));
        orrery::drawSparkle (g, p + dir * (11.0f * u), 4.0f * u, pal::brass.withAlpha (0.65f));
        g.setColour (pal::brass.withAlpha (0.5f));
        g.fillEllipse (Rectangle<float> (2.4f * u, 2.4f * u).withCentre (p + dir * (35.0f * u)));
    }
}

int64 OrreryStage::orbitSignature() const
{
    int64 sig = selected * 7 + (hovered + 1) * 131;
    for (int i = 0; i < orrery::numOrbits; ++i)
    {
        const auto& op = proc.orbitParams[(size_t) i];
        sig = sig * 31 + (op.on->load() > 0.5f ? 1 : 0);
        sig = sig * 31 + (int64) std::round (op.pulses->load());
        sig = sig * 31 + (int64) std::round (op.beats->load());
        sig = sig * 131 + (int64) std::round (op.offset->load() * 10.0f);
    }
    return sig;
}

void OrreryStage::rebuildOrbitLayer (float scale)
{
    orbitScale = scale;
    orbitLayerSignature = orbitSignature();
    orbitLayer = Image (Image::ARGB, jmax (1, roundToInt ((float) getWidth() * scale)), jmax (1, roundToInt ((float) getHeight() * scale)), true);
    Graphics g (orbitLayer);
    g.addTransform (AffineTransform::scale (scale));

    const auto c = centre();
    const float u = unit();

    for (int i = 0; i < orrery::numOrbits; ++i)
    {
        const auto& op = proc.orbitParams[(size_t) i];
        const bool on = op.on->load() > 0.5f;
        const bool sel = i == selected;
        const bool hov = i == hovered;
        const auto colour = Colour (orrery::planets()[(size_t) i].colour);
        const float r = orbitRadius (i);
        auto circle = Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c);

        if (sel)
        {
            g.setColour (colour.withAlpha (0.05f));
            g.drawEllipse (circle, 9.0f * u);
            g.setColour (colour.withAlpha (0.1f));
            g.drawEllipse (circle, 3.5f * u);
        }

        if (on)
        {
            g.setColour (pal::ink.withAlpha (0.55f));
            g.drawEllipse (circle.translated (0.0f, 1.0f), 1.4f);
            g.setColour (sel ? colour.interpolatedWith (pal::brassLight, 0.45f).withAlpha (0.75f)
                             : pal::brass.withAlpha (hov ? 0.65f : 0.42f));
            g.drawEllipse (circle, sel ? 1.3f : 1.0f);
        }
        else
        {
            Path ring;
            ring.addEllipse (circle);
            Path dashed;
            const float dashes[] = { 3.0f * u, 5.0f * u };
            PathStrokeType (0.9f).createDashedStroke (dashed, ring, dashes, 2);
            g.setColour (pal::brass.withAlpha (hov || sel ? 0.4f : 0.2f));
            g.fillPath (dashed);
            continue;
        }

        const int pulses = jlimit (1, 16, (int) std::round (op.pulses->load()));
        const int beats = jlimit (1, 16, (int) std::round (op.beats->load()));

        // the beat grid of this orbit (selected orbit only)
        if (sel)
        {
            g.setColour (pal::parchment.withAlpha (0.45f));
            for (int k = 0; k < beats; ++k)
                g.fillEllipse (Rectangle<float> (2.4f * u, 2.4f * u).withCentre (polar (r - 9.0f * u, twoPi * (float) k / (float) beats)));
        }

        for (int k = 0; k < pulses; ++k)
        {
            const float a = markerAngle (i, k);
            const auto inner = polar (r - 5.0f * u, a), outer = polar (r + 5.0f * u, a);
            g.setColour (pal::ink.withAlpha (0.6f));
            g.drawLine ({ inner.translated (0.0f, 1.0f), outer.translated (0.0f, 1.0f) }, 2.0f);
            g.setColour (sel ? pal::brassLight : pal::brass.withAlpha (0.85f));
            g.drawLine ({ inner, outer }, sel ? 1.6f : 1.2f);

            if (k == 0)
            {
                const auto p = polar (r + 9.0f * u, a);
                Path diamond;
                diamond.addRectangle (-2.2f * u, -2.2f * u, 4.4f * u, 4.4f * u);
                g.setColour (sel ? colour : pal::brass.withAlpha (0.8f));
                g.fillPath (diamond, AffineTransform::rotation (MathConstants<float>::pi * 0.25f + a).translated (p));
            }
        }
    }
}

//==============================================================================
void OrreryStage::drawSun (Graphics& g)
{
    const auto c = centre();
    const float u = unit();
    const float pulse = jmax (sunPulse, beatPulse * 0.45f);
    const float sunR = 22.0f * u * (1.0f + 0.06f * pulse);

    // corona
    for (int i = 6; i > 0; --i)
    {
        const float rr = sunR * (1.15f + 0.32f * (float) i) * (1.0f + 0.1f * pulse);
        g.setColour (Colour (0xffffc864).withAlpha ((0.05f + 0.035f * pulse + 0.02f * noteGlow) * (1.0f - (float) i * 0.1f)));
        g.fillEllipse (Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (c));
    }

    // engraved rays, slowly turning
    Path rays;
    const int numRays = 16;
    const float spin = (float) clock * 0.06f;
    for (int k = 0; k < numRays; ++k)
    {
        const float a = spin + twoPi * (float) k / (float) numRays;
        const float len = sunR * ((k % 2 == 0) ? 1.75f : 1.45f) * (1.0f + 0.14f * pulse);
        const float half = (k % 2 == 0 ? 0.075f : 0.06f);
        const Point<float> tip (c.x + len * std::sin (a), c.y - len * std::cos (a));
        const Point<float> l (c.x + sunR * 1.02f * std::sin (a - half), c.y - sunR * 1.02f * std::cos (a - half));
        const Point<float> r (c.x + sunR * 1.02f * std::sin (a + half), c.y - sunR * 1.02f * std::cos (a + half));
        rays.startNewSubPath (l);
        rays.lineTo (tip);
        rays.lineTo (r);
        rays.closeSubPath();
    }
    g.setGradientFill (ColourGradient (pal::brassLight.withAlpha (0.9f), c.x, c.y, pal::brass.withAlpha (0.25f), c.x + sunR * 1.8f, c.y, true));
    g.fillPath (rays);

    // body
    auto body = Rectangle<float> (sunR * 2.0f, sunR * 2.0f).withCentre (c);
    ColourGradient grad (Colour (0xfffff7da), c.x - sunR * 0.3f, c.y - sunR * 0.35f, Colour (0xffe0782e), c.x + sunR * 0.9f, c.y + sunR * 0.9f, true);
    grad.addColour (0.45, Colour (0xffffd56e).interpolatedWith (Colours::white, 0.25f * pulse));
    g.setGradientFill (grad);
    g.fillEllipse (body);
    g.setColour (Colour (0xffa9622a).withAlpha (0.8f));
    g.drawEllipse (body, 1.1f);

    // a sleepy, smiling face that sings on the downbeat
    const Colour faceInk = Colour (0xff8a4a17).withAlpha (0.85f);
    const float eyeY = c.y - sunR * 0.1f, eyeDx = sunR * 0.36f, eyeW = sunR * 0.2f;
    g.setColour (faceInk);
    for (int side = -1; side <= 1; side += 2)
    {
        Path eye;
        eye.addCentredArc (c.x + (float) side * eyeDx, eyeY + eyeW * 0.25f, eyeW, eyeW * 0.75f, 0.0f,
                           -MathConstants<float>::halfPi * 0.95f, MathConstants<float>::halfPi * 0.95f, true);
        g.strokePath (eye, PathStrokeType (1.3f * u, PathStrokeType::curved, PathStrokeType::rounded));
    }
    g.setColour (Colour (0xffff8a7a).withAlpha (0.35f));
    for (int side = -1; side <= 1; side += 2)
        g.fillEllipse (Rectangle<float> (sunR * 0.28f, sunR * 0.17f).withCentre ({ c.x + (float) side * sunR * 0.55f, c.y + sunR * 0.22f }));

    g.setColour (faceInk);
    if (sing > 0.05f)
    {
        const float open = sing;
        g.fillEllipse (Rectangle<float> (sunR * (0.2f + 0.08f * open), sunR * (0.12f + 0.24f * open)).withCentre ({ c.x, c.y + sunR * 0.38f }));
    }
    else
    {
        Path smile;
        smile.addCentredArc (c.x, c.y + sunR * 0.22f, sunR * 0.26f, sunR * 0.2f, 0.0f, MathConstants<float>::pi * 0.62f,
                             MathConstants<float>::pi * 1.38f, true);
        g.strokePath (smile, PathStrokeType (1.3f * u, PathStrokeType::curved, PathStrokeType::rounded));
    }
}

void OrreryStage::drawPlanet (Graphics& g, int i, Point<float> pos, bool enabled)
{
    const auto& pl = orrery::planets()[(size_t) i];
    const float u = unit();
    const float b = bump[(size_t) i];
    const float rad = pl.size * u * (1.0f + 0.22f * b * b);
    auto colour = Colour (pl.colour);
    if (! enabled)
        colour = colour.withSaturation (colour.getSaturation() * 0.25f).withMultipliedBrightness (0.6f);

    const auto c = centre();
    auto toSun = c - pos;
    const float dist = jmax (1.0f, toSun.getDistanceFromOrigin());
    const Point<float> light (toSun.x / dist, toSun.y / dist);

    // halo
    if (enabled)
    {
        const float halo = rad * (1.9f + 0.9f * b);
        g.setGradientFill (ColourGradient (colour.withAlpha (0.3f + 0.25f * b), pos.x, pos.y, colour.withAlpha (0.0f), pos.x + halo, pos.y, true));
        g.fillEllipse (Rectangle<float> (halo * 2.0f, halo * 2.0f).withCentre (pos));
    }

    const float tilt = -0.38f;
    auto drawRing = [&] (bool front)
    {
        Graphics::ScopedSaveState save (g);
        const auto t = AffineTransform::rotation (tilt).translated (pos);
        if (front)
        {
            Path half;
            half.addRectangle (-rad * 3.0f, 0.0f, rad * 6.0f, rad * 3.0f);
            g.reduceClipRegion (half, t);
        }
        Path ring;
        ring.addEllipse (-rad * 1.95f, -rad * 0.55f, rad * 3.9f, rad * 1.1f);
        g.setColour (colour.brighter (0.4f).withAlpha (front ? 0.85f : 0.5f));
        g.strokePath (ring, PathStrokeType (rad * 0.22f), t);
        g.setColour (pal::ink.withAlpha (0.35f));
        g.strokePath (ring, PathStrokeType (0.6f), t.translated (0.0f, 0.0f));
    };

    if (i == 5)
        drawRing (false);

    // body, lit from the sun
    auto body = Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (pos);
    const auto hl = pos + light * (rad * 0.45f);
    ColourGradient grad (colour.brighter (0.65f), hl.x, hl.y, pal::ink, hl.x + rad * 1.75f, hl.y, true);
    grad.addColour (0.3, colour.brighter (0.15f));
    grad.addColour (0.62, colour.darker (0.55f));
    g.setGradientFill (grad);
    g.fillEllipse (body);

    {
        Graphics::ScopedSaveState save (g);
        Path clip;
        clip.addEllipse (body);
        g.reduceClipRegion (clip);

        switch (i)
        {
            case 0: // Mercury: craters
                g.setColour (pal::ink.withAlpha (0.22f));
                g.fillEllipse (Rectangle<float> (rad * 0.5f, rad * 0.42f).withCentre (pos + Point<float> (-rad * 0.3f, -rad * 0.25f)));
                g.fillEllipse (Rectangle<float> (rad * 0.32f, rad * 0.28f).withCentre (pos + Point<float> (rad * 0.35f, rad * 0.3f)));
                g.fillEllipse (Rectangle<float> (rad * 0.22f, rad * 0.2f).withCentre (pos + Point<float> (-rad * 0.1f, rad * 0.5f)));
                break;
            case 1: // Venus: soft cloud swirls
            {
                g.setColour (Colours::white.withAlpha (0.22f));
                Path swirl;
                swirl.addCentredArc (pos.x - rad * 0.2f, pos.y, rad * 0.8f, rad * 0.45f, 0.3f, -1.2f, 1.6f, true);
                swirl.addCentredArc (pos.x + rad * 0.3f, pos.y + rad * 0.3f, rad * 0.6f, rad * 0.3f, 0.3f, 2.0f, 4.4f, true);
                g.strokePath (swirl, PathStrokeType (rad * 0.18f, PathStrokeType::curved, PathStrokeType::rounded));
                break;
            }
            case 2: // Earth: a continent
            {
                g.setColour (Colour (0xff59d68f).withAlpha (enabled ? 0.65f : 0.3f));
                Path land;
                land.startNewSubPath (pos.x - rad * 0.55f, pos.y - rad * 0.2f);
                land.quadraticTo (pos.x - rad * 0.2f, pos.y - rad * 0.75f, pos.x + rad * 0.15f, pos.y - rad * 0.35f);
                land.quadraticTo (pos.x + rad * 0.55f, pos.y - rad * 0.1f, pos.x + rad * 0.1f, pos.y + rad * 0.3f);
                land.quadraticTo (pos.x - rad * 0.2f, pos.y + rad * 0.7f, pos.x - rad * 0.55f, pos.y - rad * 0.2f);
                land.closeSubPath();
                g.fillPath (land);
                break;
            }
            case 3: // Mars: polar cap
                g.setColour (Colours::white.withAlpha (0.65f));
                g.fillEllipse (Rectangle<float> (rad * 0.8f, rad * 0.45f).withCentre (pos + Point<float> (0.0f, -rad * 0.82f)));
                break;
            case 4: // Jupiter: bands and the great red spot
                g.setColour (pal::ink.withAlpha (0.22f));
                g.fillRect (Rectangle<float> (body.getX(), pos.y - rad * 0.42f, body.getWidth(), rad * 0.2f));
                g.fillRect (Rectangle<float> (body.getX(), pos.y + rad * 0.12f, body.getWidth(), rad * 0.28f));
                g.setColour (Colours::white.withAlpha (0.18f));
                g.fillRect (Rectangle<float> (body.getX(), pos.y - rad * 0.12f, body.getWidth(), rad * 0.16f));
                g.setColour (Colour (0xffc4503a).withAlpha (enabled ? 0.7f : 0.3f));
                g.fillEllipse (Rectangle<float> (rad * 0.42f, rad * 0.26f).withCentre (pos + Point<float> (rad * 0.32f, rad * 0.27f)));
                break;
            default: // Saturn: subtle bands
                g.setColour (Colours::white.withAlpha (0.14f));
                g.fillRect (Rectangle<float> (body.getX(), pos.y - rad * 0.3f, body.getWidth(), rad * 0.18f));
                break;
        }

        // night side
        g.setGradientFill (ColourGradient (Colours::transparentBlack, pos.x + light.x * rad * 0.1f, pos.y + light.y * rad * 0.1f,
                                           pal::ink.withAlpha (0.55f), pos.x - light.x * rad, pos.y - light.y * rad, false));
        g.fillEllipse (body);
    }

    // sunlit rim
    {
        Path rim;
        const float a = std::atan2 (light.x, -light.y);
        rim.addCentredArc (pos.x, pos.y, rad - 0.6f, rad - 0.6f, 0.0f, a - 1.1f, a + 1.1f, true);
        g.setColour (colour.brighter (0.8f).withAlpha (enabled ? 0.7f : 0.3f));
        g.strokePath (rim, PathStrokeType (1.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    if (i == 5)
        drawRing (true);

    // Earth's moon orbits once per beat
    if (i == 2)
    {
        const float ma = twoPi * (float) frac (visPpq);
        const auto mp = pos + Point<float> (std::sin (ma), -std::cos (ma)) * (rad * 1.85f);
        g.setColour (Colour (0xffe6e2d6).withAlpha (enabled ? 0.95f : 0.4f));
        g.fillEllipse (Rectangle<float> (rad * 0.62f, rad * 0.62f).withCentre (mp));
        g.setColour (pal::ink.withAlpha (0.35f));
        g.fillEllipse (Rectangle<float> (rad * 0.36f, rad * 0.36f).withCentre (mp + Point<float> (-light.x, -light.y) * (rad * 0.12f)));
    }

    // Jupiter's little moons
    if (i == 4)
    {
        for (int m = 0; m < 2; ++m)
        {
            const float ma = twoPi * (float) frac (visPpq * (m == 0 ? 0.5 : 0.25)) + (float) m * 2.0f;
            const auto mp = pos + Point<float> (std::sin (ma), -std::cos (ma) * 0.45f) * (rad * (1.6f + 0.5f * (float) m));
            g.setColour (pal::parchment.withAlpha (enabled ? 0.85f : 0.35f));
            g.fillEllipse (Rectangle<float> (2.4f * u, 2.4f * u).withCentre (mp));
        }
    }

    // selection ring
    if (i == selected)
    {
        Path ring;
        ring.addEllipse (Rectangle<float> (rad * 3.3f + 6.0f, rad * 3.3f + 6.0f).withCentre (pos));
        Path dashed;
        const float dashes[] = { 2.5f * u, 3.5f * u };
        PathStrokeType (1.1f).createDashedStroke (dashed, ring, dashes, 2,
                                                  AffineTransform::rotation ((float) clock * 0.6f, pos.x, pos.y));
        g.setColour (pal::parchment.withAlpha (0.75f));
        g.fillPath (dashed);
    }
}

void OrreryStage::drawTrail (Graphics& g, int i)
{
    const float len = trailLength[(size_t) i];
    if (len < 0.01f)
        return;

    const bool on = proc.orbitParams[(size_t) i].on->load() > 0.5f;
    const auto colour = Colour (orrery::planets()[(size_t) i].colour);
    const float a = displayAngle[(size_t) i];
    const float r = orbitRadius (i);
    const float rad = jmin (9.5f, orrery::planets()[(size_t) i].size) * unit();
    const float peak = on ? 0.42f : 0.1f;
    auto width = [rad] (float t) { return rad * 1.25f * std::pow (1.0f - t, 0.8f) + 0.4f; };
    auto alphaAt = [peak] (float t) { return peak * std::pow (1.0f - t, 1.5f); };

    // a tapered ribbon along the orbit, in a few chunks so each linear gradient follows the curve
    const int chunks = 5, steps = 6;
    for (int ch = 0; ch < chunks; ++ch)
    {
        const float t0 = (float) ch / (float) chunks, t1 = (float) (ch + 1) / (float) chunks;
        Path ribbon;
        for (int s = 0; s <= steps; ++s)
        {
            const float t = t0 + (t1 - t0) * (float) s / (float) steps;
            const auto p = polar (r + width (t) * 0.5f, a - len * t);
            if (s == 0)
                ribbon.startNewSubPath (p);
            else
                ribbon.lineTo (p);
        }
        for (int s = steps; s >= 0; --s)
        {
            const float t = t0 + (t1 - t0) * (float) s / (float) steps;
            ribbon.lineTo (polar (r - width (t) * 0.5f, a - len * t));
        }
        ribbon.closeSubPath();

        const auto p0 = polar (r, a - len * t0), p1 = polar (r, a - len * t1);
        g.setGradientFill (ColourGradient (colour.withAlpha (alphaAt (t0)), p0.x, p0.y, colour.withAlpha (alphaAt (t1)), p1.x, p1.y, false));
        g.fillPath (ribbon);
    }
}

void OrreryStage::paint (Graphics& g)
{
    const float scale = jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (background.isNull() || std::abs (backgroundScale - scale) > 0.01f || backgroundBeats != beatsPerBar)
        rebuildBackground (scale);
    if (orbitLayer.isNull() || std::abs (orbitScale - scale) > 0.01f || orbitLayerSignature != orbitSignature())
        rebuildOrbitLayer (scale);

    const auto bounds = getLocalBounds().toFloat();
    const auto c = centre();
    const float u = unit();

    // twinkling stars
    for (auto& t : twinkles)
    {
        const auto p = Point<float> (t.pos.x * bounds.getWidth(), t.pos.y * bounds.getHeight());
        const float a = t.brightness * (0.35f + 0.65f * (0.5f + 0.5f * std::sin ((float) clock * t.speed + t.phase)));
        if (t.size > 2.0f)
            orrery::drawSparkle (g, p, t.size * 2.2f * u, pal::parchment.withAlpha (a * 0.8f));
        else
        {
            g.setColour (pal::parchment.withAlpha (a * 0.7f));
            g.fillEllipse (Rectangle<float> (t.size * u, t.size * u).withCentre (p));
        }
    }

    g.drawImage (background, bounds);

    // harmony caption in the top-left corner
    {
        auto area = Rectangle<float> (46.0f * u, 10.0f * u, 200.0f * u, 16.0f * u);
        const Colour lampColour = following ? Colour (0xff8fd3ff) : pal::brass;
        const auto lc = Point<float> (area.getX() + 4.0f * u, area.getCentreY());
        if (followGlow > 0.0f)
        {
            g.setColour (lampColour.withAlpha (0.4f * followGlow));
            g.fillEllipse (Rectangle<float> (12.0f * u, 12.0f * u).withCentre (lc));
        }
        g.setColour (lampColour.withAlpha (following ? 1.0f : 0.6f));
        g.fillEllipse (Rectangle<float> (4.5f * u, 4.5f * u).withCentre (lc));
        g.setFont (aa::Fonts::uiBold (9.0f * u).withExtraKerningFactor (0.12f));
        g.setColour (following ? Colour (0xffcfeaff) : pal::parchment.withAlpha (0.6f));
        g.drawText (harmonyText, area.withTrimmedLeft (12.0f * u), Justification::centredLeft, false);
    }

    // the bar hand rides around the bezel; numerals light up as it passes
    {
        const float rIn = bezelInner(), rOut = bezelOuter();
        for (int b = 0; b < beatsPerBar && b < (int) numeralPaths.size(); ++b)
        {
            const float l = beatLamp[(size_t) b];
            if (l < 0.01f)
                continue;
            const auto p = polar ((rIn + rOut) * 0.5f, twoPi * (float) b / (float) beatsPerBar);
            g.setGradientFill (ColourGradient (Colour (0xffffe6a8).withAlpha (0.55f * l), p.x, p.y, Colour (0x00ffe6a8), p.x + 14.0f * u, p.y, true));
            g.fillEllipse (Rectangle<float> (28.0f * u, 28.0f * u).withCentre (p));
            g.setColour (Colour (0xfffff6dc).withAlpha (l));
            g.fillPath (numeralPaths[(size_t) b]);
        }

        const float barPhase = (float) frac (visPpq / (double) beatsPerBar);
        const float a = twoPi * barPhase;
        const float mid = (rIn + rOut) * 0.5f;
        const auto jewel = polar (mid, a);
        for (int k = 0; k < 8; ++k)
        {
            Path seg;
            seg.addCentredArc (c.x, c.y, mid, mid, 0.0f, a - 0.05f * (float) (k + 1), a - 0.05f * (float) k, true);
            g.setColour (Colour (0xfffff3cf).withAlpha (0.28f * (1.0f - (float) k / 8.0f)));
            g.strokePath (seg, PathStrokeType (3.0f * u, PathStrokeType::curved, PathStrokeType::butt));
        }
        g.setGradientFill (ColourGradient (Colour (0xfffff3cf).withAlpha (0.6f), jewel.x, jewel.y, Colour (0x00fff3cf), jewel.x + 10.0f * u, jewel.y, true));
        g.fillEllipse (Rectangle<float> (20.0f * u, 20.0f * u).withCentre (jewel));
        orrery::drawSparkle (g, jewel, 7.0f * u, Colour (0xfffff3cf));
    }

    g.drawImage (orbitLayer, bounds);

    // marker flashes
    for (int i = 0; i < orrery::numOrbits; ++i)
    {
        const auto colour = Colour (orrery::planets()[(size_t) i].colour);
        const int pulses = jlimit (1, 16, (int) std::round (proc.orbitParams[(size_t) i].pulses->load()));
        for (int k = 0; k < pulses; ++k)
        {
            const float f = flash[(size_t) i][(size_t) k];
            if (f < 0.01f)
                continue;
            const auto p = polar (orbitRadius (i), markerAngle (i, k));
            const float rr = (4.0f + 9.0f * f) * u;
            g.setGradientFill (ColourGradient (colour.withAlpha (0.75f * f), p.x, p.y, colour.withAlpha (0.0f), p.x + rr, p.y, true));
            g.fillEllipse (Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (p));
            g.setColour (Colours::white.withAlpha (0.9f * f));
            g.fillEllipse (Rectangle<float> ((2.0f + 2.0f * f) * u, (2.0f + 2.0f * f) * u).withCentre (p));
        }
    }

    // ripples
    for (auto& r : ripples)
    {
        const float t = r.age / rippleLife;
        const float ease = 1.0f - (1.0f - t) * (1.0f - t);
        const float rr = (3.0f + 15.0f * ease * (0.6f + 0.4f * r.strength)) * u;
        g.setColour (r.colour.withAlpha ((1.0f - t) * (1.0f - t) * 0.7f * r.strength));
        g.drawEllipse (Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (r.pos), 1.3f * (1.0f - t) + 0.4f);
    }

    // stardust
    for (auto& sp : sparks)
    {
        const float l = jlimit (0.0f, 1.0f, sp.life);
        orrery::drawSparkle (g, sp.pos, sp.size * (0.5f + 0.5f * l) * u, sp.colour.withAlpha (l * 0.9f));
    }

    // brass arms from the hub
    for (int i = 0; i < orrery::numOrbits; ++i)
    {
        if (proc.orbitParams[(size_t) i].on->load() <= 0.5f)
            continue;
        const float a = displayAngle[(size_t) i];
        const auto from = polar (30.0f * u, a);
        const auto to = polar (orbitRadius (i) - orrery::planets()[(size_t) i].size * u - 1.5f, a);
        g.setColour (pal::ink.withAlpha (0.4f));
        g.drawLine ({ from.translated (0.0f, 1.0f), to.translated (0.0f, 1.0f) }, 1.6f);
        g.setColour (pal::brass.withAlpha (i == selected ? 0.55f : 0.26f));
        g.drawLine ({ from, to }, 1.0f);
    }

    // comet trails
    for (int i = 0; i < orrery::numOrbits; ++i)
        drawTrail (g, i);

    drawSun (g);

    for (int i = 0; i < orrery::numOrbits; ++i)
        drawPlanet (g, i, planetPos[(size_t) i], proc.orbitParams[(size_t) i].on->load() > 0.5f);

    // floating note names
    const auto labelFont = aa::Fonts::uiBold (11.5f * u);
    for (auto& l : labels)
    {
        const float t = l.age / labelLife;
        const float rise = 26.0f * u * (1.0f - (1.0f - t) * (1.0f - t));
        const float alpha = t < 0.1f ? t / 0.1f : 1.0f - std::pow ((t - 0.1f) / 0.9f, 1.5f);
        auto area = Rectangle<float> (40.0f * u, 14.0f * u).withCentre (l.pos.translated (0.0f, -12.0f * u - rise));
        g.setFont (l.emphasised ? aa::Fonts::uiBold (13.0f * u) : labelFont);
        g.setColour (pal::ink.withAlpha (0.7f * alpha));
        g.drawText (l.text, area.translated (0.0f, 1.0f), Justification::centred, false);
        g.setColour (l.colour.interpolatedWith (Colours::white, 0.35f).withAlpha (alpha * (l.emphasised ? 1.0f : 0.9f)));
        g.drawText (l.text, area, Justification::centred, false);
    }
}
