#include "Views.h"

using namespace juce;

namespace ers::ui
{
namespace
{
    void placeRow (Rectangle<int> row, std::initializer_list<Component*> comps)
    {
        const int w = row.getWidth() / (int) comps.size();
        for (auto* c : comps)
            c->setBounds (row.removeFromLeft (w).withSizeKeepingCentre (jmin (w, 56), row.getHeight()));
    }

    constexpr float maxEnvMs = 32000.0f;
}

//==============================================================================
// EnvelopeEditor
//==============================================================================
EnvelopeEditor::EnvelopeEditor (SynthProcessor& p, int e) : proc (p), env (-1)
{
    setEnvelope (e);
}

void EnvelopeEditor::setEnvelope (int e)
{
    if (e == env)
        return;
    env = e;
    const String prefix = "e" + String (e + 1);
    attack = std::make_unique<ModKnob> (proc, prefix + "Attack", "Attack", envAccent);
    hold = std::make_unique<ModKnob> (proc, prefix + "Hold", "Hold", envAccent);
    decay = std::make_unique<ModKnob> (proc, prefix + "Decay", "Decay", envAccent);
    sustain = std::make_unique<ModKnob> (proc, prefix + "Sustain", "Sustain", envAccent);
    release = std::make_unique<ModKnob> (proc, prefix + "Release", "Release", envAccent);
    for (auto* k : { attack.get(), hold.get(), decay.get(), sustain.get(), release.get() })
        addAndMakeVisible (k);
    resized();
    repaint();
}

void EnvelopeEditor::collectKnobs (std::vector<ModKnob*>& out)
{
    for (auto* k : { attack.get(), hold.get(), decay.get(), sustain.get(), release.get() })
        out.push_back (k);
}

Rectangle<float> EnvelopeEditor::graphArea() const
{
    return getLocalBounds().toFloat().withTrimmedBottom (66.0f).reduced (2.0f, 4.0f);
}

void EnvelopeEditor::resized()
{
    auto r = getLocalBounds();
    auto row = r.removeFromBottom (62);
    if (attack != nullptr)
        placeRow (row, { attack.get(), hold.get(), decay.get(), sustain.get(), release.get() });
}

float EnvelopeEditor::timeToWidth (float ms) const
{
    const float maxW = graphArea().getWidth() / 4.4f;
    return maxW * std::log1p (jmax (0.0f, ms) / 15.0f) / std::log1p (maxEnvMs / 15.0f);
}

float EnvelopeEditor::widthToTime (float w) const
{
    const float maxW = graphArea().getWidth() / 4.4f;
    return 15.0f * std::expm1 (jlimit (0.0f, 1.0f, w / maxW) * std::log1p (maxEnvMs / 15.0f));
}

float EnvelopeEditor::value (int index) const
{
    auto* p = proc.vpParam (index);
    return p->convertFrom0to1 (p->getValue());
}

void EnvelopeEditor::setValue (int index, float v)
{
    auto* p = proc.vpParam (index);
    p->setValueNotifyingHost (p->convertTo0to1 (p->getNormalisableRange().getRange().clipValue (v)));
}

std::vector<EnvelopeEditor::Handle> EnvelopeEditor::handles() const
{
    const auto g = graphArea().reduced (8.0f, 6.0f);
    const float a = timeToWidth (value (vp::env (env, EnvField::attack)));
    const float h = timeToWidth (value (vp::env (env, EnvField::hold)));
    const float d = timeToWidth (value (vp::env (env, EnvField::decay)));
    const float s = value (vp::env (env, EnvField::sustain)) * 0.01f;
    const float rl = timeToWidth (value (vp::env (env, EnvField::release)));
    const float sustainW = g.getWidth() * 0.12f;

    const float x0 = g.getX(), top = g.getY(), bottom = g.getBottom();
    const float ys = bottom - s * g.getHeight();
    const float xa = x0 + a, xh = xa + h, xd = xh + d, xs = xd + sustainW, xr = xs + rl;

    auto curveY = [&] (float y0, float y1, float curve) { return y0 + (y1 - y0) * LfoShape::curveSegment (0.5f, curve); };

    return {
        { { xa, top }, vp::env (env, EnvField::attack), -1, false },
        { { xh, top }, vp::env (env, EnvField::hold), -1, false },
        { { xd, ys }, vp::env (env, EnvField::decay), vp::env (env, EnvField::sustain), false },
        { { xr, bottom }, vp::env (env, EnvField::release), -1, false },
        { { (x0 + xa) * 0.5f, curveY (bottom, top, value (vp::env (env, EnvField::attackCurve)) * 0.01f) }, vp::env (env, EnvField::attackCurve), -1, true },
        { { (xh + xd) * 0.5f, curveY (top, ys, value (vp::env (env, EnvField::decayCurve)) * 0.01f) }, vp::env (env, EnvField::decayCurve), -1, true },
        { { (xs + xr) * 0.5f, curveY (ys, bottom, value (vp::env (env, EnvField::releaseCurve)) * 0.01f) }, vp::env (env, EnvField::releaseCurve), -1, true },
    };
}

int EnvelopeEditor::hit (Point<float> p) const
{
    const auto hs = handles();
    int best = -1;
    float bestD = 10.0f;
    for (int i = 0; i < (int) hs.size(); ++i)
    {
        const float d = hs[(size_t) i].pos.getDistanceFrom (p);
        if (d < bestD)
        {
            bestD = d;
            best = i;
        }
    }
    return best;
}

void EnvelopeEditor::paint (Graphics& g)
{
    const auto area = graphArea();
    drawScreen (g, area);
    const auto r = area.reduced (8.0f, 6.0f);
    const auto hs = handles();

    const float s = value (vp::env (env, EnvField::sustain)) * 0.01f;
    const float ys = r.getBottom() - s * r.getHeight();
    const float aC = value (vp::env (env, EnvField::attackCurve)) * 0.01f;
    const float dC = value (vp::env (env, EnvField::decayCurve)) * 0.01f;
    const float rC = value (vp::env (env, EnvField::releaseCurve)) * 0.01f;

    Path path;
    path.startNewSubPath (r.getX(), r.getBottom());
    auto segment = [&] (float x0, float y0, float x1, float y1, float curve)
    {
        const int steps = jmax (2, (int) ((x1 - x0) / 3.0f));
        for (int i = 1; i <= steps; ++i)
        {
            const float t = (float) i / (float) steps;
            path.lineTo (x0 + (x1 - x0) * t, y0 + (y1 - y0) * LfoShape::curveSegment (t, curve));
        }
    };
    segment (r.getX(), r.getBottom(), hs[0].pos.x, r.getY(), aC);
    path.lineTo (hs[1].pos.x, r.getY());
    segment (hs[1].pos.x, r.getY(), hs[2].pos.x, ys, dC);
    const float xs = hs[2].pos.x + r.getWidth() * 0.12f;
    path.lineTo (xs, ys);
    segment (xs, ys, hs[3].pos.x, r.getBottom(), rC);

    Path fill (path);
    fill.lineTo (r.getX(), r.getBottom());
    fill.closeSubPath();
    g.setGradientFill (ColourGradient (envAccent.withAlpha (0.25f), 0.0f, r.getY(), envAccent.withAlpha (0.02f), 0.0f, r.getBottom(), false));
    g.fillPath (fill);
    g.setColour (envAccent.withAlpha (0.3f));
    g.strokePath (path, PathStrokeType (4.0f));
    g.setColour (envAccent);
    g.strokePath (path, PathStrokeType (1.8f));

    // sustain region marker
    g.setColour (Colours::white.withAlpha (0.06f));
    g.fillRect (Rectangle<float> (hs[2].pos.x, r.getY(), xs - hs[2].pos.x, r.getHeight()));

    // live level
    if (liveLevel > 0.001f)
    {
        const float y = r.getBottom() - liveLevel * r.getHeight();
        g.setColour (Colours::white.withAlpha (0.3f));
        g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        g.setColour (Colours::white);
        g.fillEllipse (Rectangle<float> (6.0f, 6.0f).withCentre ({ r.getX(), y }));
    }

    for (int i = 0; i < (int) hs.size(); ++i)
    {
        const auto& h = hs[(size_t) i];
        const bool active = i == hover || i == dragging;
        const float size = h.isCurve ? (active ? 8.0f : 6.0f) : (active ? 11.0f : 9.0f);
        g.setColour (h.isCurve ? envAccent.withAlpha (0.7f) : Colours::white);
        if (h.isCurve)
            g.drawEllipse (Rectangle<float> (size, size).withCentre (h.pos), 1.5f);
        else
        {
            g.fillEllipse (Rectangle<float> (size, size).withCentre (h.pos));
            g.setColour (envAccent);
            g.drawEllipse (Rectangle<float> (size, size).withCentre (h.pos), 1.5f);
        }
    }

    g.setFont (aa::Fonts::ui (10.0f));
    g.setColour (textDim);
    g.drawText (env == 0 ? "ENV 1 - AMP" : "ENV " + String (env + 1), area.reduced (8.0f, 4.0f).toNearestInt(), Justification::topRight);
}

void EnvelopeEditor::mouseMove (const MouseEvent& e)
{
    const int h = hit (e.position);
    if (h != hover)
    {
        hover = h;
        repaint();
    }
}

void EnvelopeEditor::mouseDown (const MouseEvent& e)
{
    dragging = hit (e.position);
    if (dragging < 0)
        return;
    const auto h = handles()[(size_t) dragging];
    proc.vpParam (h.param)->beginChangeGesture();
    if (h.paramY >= 0)
        proc.vpParam (h.paramY)->beginChangeGesture();
    dragStartValue = value (h.param);
    dragStartY = e.position.y;
}

void EnvelopeEditor::mouseDrag (const MouseEvent& e)
{
    if (dragging < 0)
        return;
    const auto hs = handles();
    const auto& h = hs[(size_t) dragging];
    const auto r = graphArea().reduced (8.0f, 6.0f);

    if (h.isCurve)
    {
        // up bends towards the start value (faster), down towards the end
        const bool rising = dragging == 4;
        const float delta = (dragStartY - e.position.y) / 60.0f * 100.0f * (rising ? -1.0f : 1.0f);
        setValue (h.param, jlimit (-100.0f, 100.0f, dragStartValue + delta));
        repaint();
        return;
    }

    float segmentStart = r.getX();
    if (dragging == 1) segmentStart = hs[0].pos.x;
    if (dragging == 2) segmentStart = hs[1].pos.x;
    if (dragging == 3) segmentStart = hs[2].pos.x + r.getWidth() * 0.12f;

    setValue (h.param, widthToTime (e.position.x - segmentStart));
    if (h.paramY >= 0)
        setValue (h.paramY, jlimit (0.0f, 100.0f, (r.getBottom() - e.position.y) / r.getHeight() * 100.0f));
    repaint();
}

void EnvelopeEditor::mouseUp (const MouseEvent&)
{
    if (dragging >= 0)
    {
        const auto h = handles()[(size_t) dragging];
        proc.vpParam (h.param)->endChangeGesture();
        if (h.paramY >= 0)
            proc.vpParam (h.paramY)->endChangeGesture();
    }
    dragging = -1;
}

void EnvelopeEditor::refresh()
{
    for (auto* k : { attack.get(), hold.get(), decay.get(), sustain.get(), release.get() })
        k->refreshModulation();

    const float level = proc.engine.ui.envLevel[(size_t) env].load (std::memory_order_relaxed);
    if (std::abs (level - liveLevel) > 0.002f)
    {
        liveLevel = level;
        repaint();
    }
}

//==============================================================================
// LfoEditor
//==============================================================================
LfoEditor::LfoEditor (SynthProcessor& p, int l) : proc (p), lfo (-1)
{
    addAndMakeVisible (snapButton);
    addAndMakeVisible (gridButton);
    snapButton.setClickingTogglesState (true);
    snapButton.setToggleState (true, dontSendNotification);
    snapButton.onClick = [this] { snap = snapButton.getToggleState(); };
    snapButton.setTooltip ("Snap points to the grid (hold Shift to place freely)");
    gridButton.onClick = [this]
    {
        static const int options[] = { 2, 4, 8, 16, 32 };
        int i = 0;
        while (i < 4 && options[i] != gridDivs)
            ++i;
        gridDivs = options[(i + 1) % 5];
        gridButton.setButtonText ("1/" + String (gridDivs));
        repaint();
    };
    gridButton.setTooltip ("Grid size");
    for (auto* b : { &snapButton, &gridButton })
    {
        b->setColour (TextButton::buttonColourId, Colours::black.withAlpha (0.4f));
        b->setColour (TextButton::buttonOnColourId, lfoAccent.withAlpha (0.5f));
        b->setColour (TextButton::textColourOffId, textDim);
        b->setColour (TextButton::textColourOnId, textBright);
    }
    setLfo (l);
}

void LfoEditor::setLfo (int l)
{
    if (l == lfo)
        return;
    lfo = l;
    shape = proc.getLfoShape (l);
    shapeVersion = proc.lfoShapeVersion.load();
    rebuildControls();
    repaint();
}

void LfoEditor::rebuildControls()
{
    const String prefix = "l" + String (lfo + 1);
    rate = std::make_unique<ModKnob> (proc, prefix + "Rate", "Rate", lfoAccent);
    rise = std::make_unique<ModKnob> (proc, prefix + "Rise", "Rise", lfoAccent);
    delay = std::make_unique<ModKnob> (proc, prefix + "Delay", "Delay", lfoAccent);
    smooth = std::make_unique<ModKnob> (proc, prefix + "Smooth", "Smooth", lfoAccent);
    phase = std::make_unique<ModKnob> (proc, prefix + "Phase", "Phase", lfoAccent);
    division = std::make_unique<aa::ChoiceBox> (proc.apvts, prefix + "Div", "Rate", aa::ChoiceBox::Style::stepper);
    mode = std::make_unique<aa::ChoiceBox> (proc.apvts, prefix + "Mode", "Mode", aa::ChoiceBox::Style::segmented);
    sync = std::make_unique<aa::Toggle> (proc.apvts, prefix + "Sync", "Sync");
    division->setAccent (lfoAccent);
    mode->setAccent (lfoAccent);
    mode->setShortNames ({ "TRIG", "FREE", "ENV" });
    mode->setTooltip ("Trigger: restarts on each note. Free: one shared LFO (locked to the song when synced). Envelope: plays once.");
    sync->setAccent (lfoAccent);
    for (Component* c : std::initializer_list<Component*> { rate.get(), rise.get(), delay.get(), smooth.get(), phase.get(),
                                                            division.get(), mode.get(), sync.get() })
        addAndMakeVisible (c);
    resized();
}

void LfoEditor::collectKnobs (std::vector<ModKnob*>& out)
{
    for (auto* k : { rate.get(), rise.get(), delay.get(), smooth.get(), phase.get() })
        out.push_back (k);
}

Rectangle<float> LfoEditor::graphArea() const
{
    return getLocalBounds().toFloat().withTrimmedBottom (66.0f).reduced (2.0f, 4.0f);
}

void LfoEditor::resized()
{
    if (rate == nullptr)
        return;
    auto g = graphArea().toNearestInt();
    gridButton.setBounds (g.getRight() - 46, g.getY() + 4, 40, 16);
    snapButton.setBounds (g.getRight() - 92, g.getY() + 4, 44, 16);

    auto row = getLocalBounds().removeFromBottom (62);
    const int w = row.getWidth() / 7;
    auto rateCell = row.removeFromLeft (w);
    rate->setBounds (rateCell.withSizeKeepingCentre (jmin (w, 56), 62));
    division->setBounds (rateCell.withSizeKeepingCentre (jmin (w + 10, 84), 36));
    sync->setBounds (row.removeFromLeft (w).withSizeKeepingCentre (jmin (w, 70), 20));
    mode->setBounds (row.removeFromLeft (w * 2).reduced (4, 12));
    placeRow (row, { rise.get(), delay.get(), smooth.get(), phase.get() });
}

Point<float> LfoEditor::toScreen (const LfoShape::Point& p) const
{
    const auto r = graphArea().reduced (8.0f, 10.0f);
    return { r.getX() + p.x * r.getWidth(), r.getBottom() - p.y * r.getHeight() };
}

int LfoEditor::hitPoint (Point<float> p) const
{
    for (int i = 0; i < (int) shape.points.size(); ++i)
        if (toScreen (shape.points[(size_t) i]).getDistanceFrom (p) < 8.0f)
            return i;
    return -1;
}

int LfoEditor::hitCurve (Point<float> p) const
{
    for (int i = 0; i + 1 < (int) shape.points.size(); ++i)
    {
        const auto& a = shape.points[(size_t) i];
        const auto& b = shape.points[(size_t) i + 1];
        if (b.x - a.x < 0.02f)
            continue;
        const float mx = (a.x + b.x) * 0.5f;
        const LfoShape::Point mid { mx, shape.evaluate (mx), 0.0f };
        if (toScreen (mid).getDistanceFrom (p) < 7.0f)
            return i;
    }
    return -1;
}

void LfoEditor::paint (Graphics& g)
{
    const auto area = graphArea();
    drawScreen (g, area);
    const auto r = area.reduced (8.0f, 10.0f);

    // grid
    g.setColour (Colours::white.withAlpha (0.05f));
    for (int i = 1; i < gridDivs; ++i)
        g.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) i / (float) gridDivs), r.getY(), r.getBottom());
    for (int i = 1; i < 4; ++i)
        g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) i / 4.0f), r.getX(), r.getRight());

    Path path;
    const int steps = (int) r.getWidth();
    for (int i = 0; i <= steps; ++i)
    {
        const float x = (float) i / (float) steps;
        const auto pt = toScreen ({ x, shape.evaluate (x), 0.0f });
        if (i == 0) path.startNewSubPath (pt); else path.lineTo (pt);
    }
    Path fill (path);
    fill.lineTo (r.getRight(), r.getBottom());
    fill.lineTo (r.getX(), r.getBottom());
    fill.closeSubPath();
    g.setGradientFill (ColourGradient (lfoAccent.withAlpha (0.25f), 0.0f, r.getY(), lfoAccent.withAlpha (0.02f), 0.0f, r.getBottom(), false));
    g.fillPath (fill);
    g.setColour (lfoAccent.withAlpha (0.3f));
    g.strokePath (path, PathStrokeType (4.0f));
    g.setColour (lfoAccent);
    g.strokePath (path, PathStrokeType (1.8f));

    // curve handles
    for (int i = 0; i + 1 < (int) shape.points.size(); ++i)
    {
        const auto& a = shape.points[(size_t) i];
        const auto& b = shape.points[(size_t) i + 1];
        if (b.x - a.x < 0.02f)
            continue;
        const float mx = (a.x + b.x) * 0.5f;
        const auto c = toScreen ({ mx, shape.evaluate (mx), 0.0f });
        g.setColour (lfoAccent.withAlpha (i == dragCurve ? 1.0f : 0.55f));
        g.drawEllipse (Rectangle<float> (6.0f, 6.0f).withCentre (c), 1.3f);
    }

    for (int i = 0; i < (int) shape.points.size(); ++i)
    {
        const auto c = toScreen (shape.points[(size_t) i]);
        const bool active = i == hover || i == dragPoint;
        const float s = active ? 10.0f : 8.0f;
        g.setColour (Colours::white);
        g.fillEllipse (Rectangle<float> (s, s).withCentre (c));
        g.setColour (lfoAccent);
        g.drawEllipse (Rectangle<float> (s, s).withCentre (c), 1.5f);
    }

    // playhead
    const float px = r.getX() + livePhase * r.getWidth();
    g.setColour (Colours::white.withAlpha (0.25f));
    g.drawVerticalLine ((int) px, r.getY(), r.getBottom());
    g.setColour (Colours::white);
    g.fillEllipse (Rectangle<float> (7.0f, 7.0f).withCentre ({ px, r.getBottom() - shape.evaluate (livePhase) * r.getHeight() }));

    g.setFont (aa::Fonts::ui (10.0f));
    g.setColour (textDim);
    g.drawText ("LFO " + String (lfo + 1) + "  -  click to add, double-click to delete, drag the rings to bend, right-click for shapes",
                area.reduced (8.0f, 4.0f).toNearestInt(), Justification::bottomLeft);
}

void LfoEditor::mouseMove (const MouseEvent& e)
{
    const int h = hitPoint (e.position);
    if (h != hover)
    {
        hover = h;
        repaint();
    }
}

void LfoEditor::mouseDown (const MouseEvent& e)
{
    if (! graphArea().contains (e.position))
        return;
    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }

    dragPoint = hitPoint (e.position);
    dragCurve = -1;
    if (dragPoint < 0)
    {
        dragCurve = hitCurve (e.position);
        if (dragCurve >= 0)
        {
            curveStart = shape.points[(size_t) dragCurve].curve;
            dragStartY = e.position.y;
            return;
        }

        // add a point here
        const auto r = graphArea().reduced (8.0f, 10.0f);
        float x = jlimit (0.0f, 1.0f, (e.position.x - r.getX()) / r.getWidth());
        float y = jlimit (0.0f, 1.0f, (r.getBottom() - e.position.y) / r.getHeight());
        if (snap && ! e.mods.isShiftDown())
        {
            x = std::round (x * (float) gridDivs) / (float) gridDivs;
            y = std::round (y * 16.0f) / 16.0f;
        }
        if (x <= 0.0f || x >= 1.0f)
            return;
        shape.points.push_back ({ x, y, 0.0f });
        shape.sanitise();
        dragPoint = hitPoint (toScreen ({ x, y, 0.0f }));
        proc.setLfoShape (lfo, shape);
        shapeVersion = proc.lfoShapeVersion.load();
        repaint();
    }
}

void LfoEditor::mouseDrag (const MouseEvent& e)
{
    if (dragCurve >= 0)
    {
        const auto& a = shape.points[(size_t) dragCurve];
        const auto& b = shape.points[(size_t) dragCurve + 1];
        const float dir = b.y >= a.y ? -1.0f : 1.0f;
        shape.points[(size_t) dragCurve].curve = jlimit (-1.0f, 1.0f, curveStart + dir * (dragStartY - e.position.y) / 70.0f);
    }
    else if (dragPoint >= 0)
    {
        const auto r = graphArea().reduced (8.0f, 10.0f);
        float x = (e.position.x - r.getX()) / r.getWidth();
        float y = jlimit (0.0f, 1.0f, (r.getBottom() - e.position.y) / r.getHeight());
        if (snap && ! e.mods.isShiftDown())
        {
            x = std::round (x * (float) gridDivs) / (float) gridDivs;
            y = std::round (y * 16.0f) / 16.0f;
        }
        auto& pts = shape.points;
        const int n = (int) pts.size();
        if (dragPoint == 0)
            x = 0.0f;
        else if (dragPoint == n - 1)
            x = 1.0f;
        else
            x = jlimit (pts[(size_t) dragPoint - 1].x, pts[(size_t) dragPoint + 1].x, x);
        pts[(size_t) dragPoint].x = x;
        pts[(size_t) dragPoint].y = y;
    }
    else
        return;

    proc.setLfoShape (lfo, shape);
    shapeVersion = proc.lfoShapeVersion.load();
    repaint();
}

void LfoEditor::mouseUp (const MouseEvent&)
{
    dragPoint = dragCurve = -1;
}

void LfoEditor::mouseDoubleClick (const MouseEvent& e)
{
    const int i = hitPoint (e.position);
    if (i > 0 && i < (int) shape.points.size() - 1)
    {
        shape.points.erase (shape.points.begin() + i);
        proc.setLfoShape (lfo, shape);
        shapeVersion = proc.lfoShapeVersion.load();
        hover = -1;
        repaint();
    }
    else if (const int c = hitCurve (e.position); c >= 0)
    {
        shape.points[(size_t) c].curve = 0.0f;
        proc.setLfoShape (lfo, shape);
        shapeVersion = proc.lfoShapeVersion.load();
        repaint();
    }
}

void LfoEditor::showMenu()
{
    PopupMenu menu;
    const auto names = LfoShape::presetNames();
    PopupMenu shapes;
    for (int i = 0; i < names.size(); ++i)
        shapes.addItem (1 + i, names[i]);
    menu.addSubMenu ("Load shape", shapes);
    menu.addItem (100, "Flip horizontally");
    menu.addItem (101, "Flip vertically");
    menu.addItem (102, "Flatten curves");

    SafePointer<LfoEditor> safe (this);
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [safe] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        auto& shape = safe->shape;
        if (result >= 1 && result < 100)
            shape = LfoShape::preset ((LfoShape::Preset) (result - 1), Random::getSystemRandom().nextInt (10000));
        else if (result == 100)
        {
            std::vector<LfoShape::Point> flipped;
            for (int i = (int) shape.points.size() - 1; i >= 0; --i)
            {
                auto p = shape.points[(size_t) i];
                p.x = 1.0f - p.x;
                p.curve = i > 0 ? -shape.points[(size_t) i - 1].curve : 0.0f;
                flipped.push_back (p);
            }
            shape.points = flipped;
        }
        else if (result == 101)
            for (auto& p : shape.points)
            {
                p.y = 1.0f - p.y;
                p.curve = -p.curve;
            }
        else if (result == 102)
            for (auto& p : shape.points)
                p.curve = 0.0f;
        shape.sanitise();
        safe->proc.setLfoShape (safe->lfo, shape);
        safe->shapeVersion = safe->proc.lfoShapeVersion.load();
        safe->repaint();
    });
}

void LfoEditor::refresh()
{
    for (auto* k : { rate.get(), rise.get(), delay.get(), smooth.get(), phase.get() })
        k->refreshModulation();

    if (shapeVersion != proc.lfoShapeVersion.load() && dragPoint < 0 && dragCurve < 0)
    {
        shape = proc.getLfoShape (lfo);
        shapeVersion = proc.lfoShapeVersion.load();
        repaint();
    }

    const bool synced = proc.vpParam (vp::lfo (lfo, LfoField::sync))->getValue() > 0.5f;
    rate->setVisible (! synced);
    division->setVisible (synced);

    const float ph = proc.engine.ui.lfoPhase[(size_t) lfo].load (std::memory_order_relaxed);
    if (std::abs (ph - livePhase) > 0.001f)
    {
        livePhase = ph;
        repaint (graphArea().toNearestInt());
    }
}

//==============================================================================
// ModulationPanel
//==============================================================================
ModulationPanel::ModulationPanel (SynthProcessor& p) : proc (p), envEditor (p, 0), lfoEditor (p, 0)
{
    for (int i = 0; i < 8; ++i)
    {
        auto* h = handles.add (new ModSourceHandle ((ModSource) ((int) ModSource::env1 + i)));
        addAndMakeVisible (h);
    }
    addAndMakeVisible (envEditor);
    addChildComponent (lfoEditor);
}

void ModulationPanel::collectKnobs (std::vector<ModKnob*>&) {}

Rectangle<float> ModulationPanel::tabArea (int i) const
{
    auto r = getLocalBounds().toFloat().reduced (8.0f, 0.0f).removeFromTop (34.0f).withTrimmedTop (6.0f);
    const float w = r.getWidth() / 8.0f;
    return { r.getX() + w * (float) i, r.getY(), w - 4.0f, r.getHeight() };
}

void ModulationPanel::select (int tab)
{
    selected = tab;
    if (tab < 4)
        envEditor.setEnvelope (tab);
    else
        lfoEditor.setLfo (tab - 4);
    envEditor.setVisible (tab < 4);
    lfoEditor.setVisible (tab >= 4);
    repaint();
}

void ModulationPanel::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setGradientFill (ColourGradient (panelFill.brighter (0.06f), r.getX(), r.getY(), panelFill.darker (0.25f), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (panelEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

    for (int i = 0; i < 8; ++i)
    {
        const auto t = tabArea (i);
        const auto colour = i < 4 ? envAccent : lfoAccent;
        const bool sel = i == selected;
        g.setColour (sel ? colour.withAlpha (0.18f) : Colours::white.withAlpha (0.03f));
        g.fillRoundedRectangle (t, 6.0f);
        g.setColour (sel ? colour : panelEdge);
        g.drawRoundedRectangle (t.reduced (0.5f), 6.0f, 1.0f);

        g.setFont (aa::Fonts::display (11.5f));
        g.setColour (sel ? textBright : textDim);
        g.drawText ((i < 4 ? "ENV " : "LFO ") + String ((i % 4) + 1), t.withTrimmedRight (20.0f).withTrimmedLeft (6.0f).toNearestInt(),
                    Justification::centredLeft);

        // live activity bar
        auto bar = t.reduced (6.0f, 0.0f).removeFromBottom (3.0f).translated (0.0f, -2.0f).withTrimmedRight (16.0f);
        g.setColour (Colours::white.withAlpha (0.06f));
        g.fillRect (bar);
        g.setColour (colour);
        g.fillRect (bar.withWidth (bar.getWidth() * jlimit (0.0f, 1.0f, meters[(size_t) i])));
    }
}

void ModulationPanel::resized()
{
    for (int i = 0; i < 8; ++i)
    {
        const auto t = tabArea (i);
        handles[i]->setBounds (Rectangle<float> (t.getRight() - 20.0f, t.getY() + 4.0f, 17.0f, 17.0f).toNearestInt());
    }
    auto content = getLocalBounds().reduced (8, 0).withTrimmedTop (38).withTrimmedBottom (4);
    envEditor.setBounds (content);
    lfoEditor.setBounds (content);
}

void ModulationPanel::mouseDown (const MouseEvent& e)
{
    for (int i = 0; i < 8; ++i)
        if (tabArea (i).contains (e.position))
            select (i);
}

void ModulationPanel::refresh()
{
    bool changed = false;
    for (int i = 0; i < 8; ++i)
    {
        const float v = i < 4 ? proc.engine.ui.envLevel[(size_t) i].load (std::memory_order_relaxed)
                              : proc.engine.ui.lfoValue[(size_t) i - 4].load (std::memory_order_relaxed);
        if (std::abs (v - meters[(size_t) i]) > 0.01f)
        {
            meters[(size_t) i] = v;
            changed = true;
        }
    }
    if (changed)
        repaint (getLocalBounds().removeFromTop (36));

    if (selected < 4)
        envEditor.refresh();
    else
        lfoEditor.refresh();
}
} // namespace ers::ui
