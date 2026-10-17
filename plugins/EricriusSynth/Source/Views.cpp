#include "Views.h"

using namespace juce;

namespace ers::ui
{
namespace
{
    constexpr float startAngle = -MathConstants<float>::pi * 0.75f;
    constexpr float endAngle = MathConstants<float>::pi * 0.75f;

    float angleFor (float proportion) { return startAngle + jlimit (0.0f, 1.0f, proportion) * (endAngle - startAngle); }

    void strokeArc (Graphics& g, Point<float> c, float radius, float from, float to, float width, Colour colour)
    {
        if (std::abs (to - from) < 1.0e-4f)
            return;
        Path p;
        p.addCentredArc (c.x, c.y, radius, radius, 0.0f, jmin (from, to), jmax (from, to), true);
        g.setColour (colour);
        g.strokePath (p, PathStrokeType (width, PathStrokeType::curved, PathStrokeType::rounded));
    }

    ModSource sourceFromDescription (const var& description)
    {
        const auto s = description.toString();
        if (! s.startsWith ("modsrc:"))
            return ModSource::none;
        const int i = s.fromFirstOccurrenceOf (":", false, false).getIntValue();
        return isPositiveAndBelow (i, numModSources) ? (ModSource) i : ModSource::none;
    }

    /** The phase mapping of the display-friendly warp modes (mirrors the engine's). */
    float displayWarp (Warp w, float amt, float p, bool& invert)
    {
        invert = false;
        switch (w)
        {
            case Warp::sync:
            case Warp::windowSync: { const float q = p * (1.0f + 15.0f * amt); return q - std::floor (q); }
            case Warp::bendPlus:   return 1.0f - std::pow (1.0f - p, 1.0f + 6.0f * amt);
            case Warp::bendMinus:  return std::pow (p, 1.0f + 6.0f * amt);
            case Warp::bendBoth:
            {
                const float k = 1.0f + 6.0f * amt;
                return p < 0.5f ? 0.5f * std::pow (2.0f * p, k) : 1.0f - 0.5f * std::pow (2.0f - 2.0f * p, k);
            }
            case Warp::pwm:        { const float wd = 1.0f - 0.97f * amt; return p < wd ? p / wd : 0.0f; }
            case Warp::asymPlus:
            case Warp::asymMinus:
            {
                const float m = w == Warp::asymPlus ? 0.5f + 0.48f * amt : 0.5f - 0.48f * amt;
                return p < m ? 0.5f * p / m : 0.5f + 0.5f * (p - m) / (1.0f - m);
            }
            case Warp::flip:       invert = p >= 1.0f - 0.5f * amt; return p;
            case Warp::quantize:   { const float s = std::exp2 (1.0f + (1.0f - amt) * 10.0f); return std::floor (p * s) / s; }
            case Warp::off:
            case Warp::mirror:
            case Warp::fm:
            case Warp::am:
            case Warp::rm:
            case Warp::count:      break;
        }
        return p;
    }

    float sampleFrame (const std::vector<float>& f, float p)
    {
        const float x = jlimit (0.0f, 0.9999f, p) * (float) f.size();
        const int i = (int) x;
        const float t = x - (float) i;
        return f[(size_t) i] + t * (f[(size_t) ((i + 1) % (int) f.size())] - f[(size_t) i]);
    }

    void placeKnobs (Rectangle<int> row, std::initializer_list<Component*> comps)
    {
        const int w = row.getWidth() / (int) comps.size();
        for (auto* c : comps)
            c->setBounds (row.removeFromLeft (w).withSizeKeepingCentre (jmin (w, 54), row.getHeight()));
    }
} // namespace

//==============================================================================
Colour sourceColour (ModSource s)
{
    if (s >= ModSource::env1 && s <= ModSource::env4)     return envAccent;
    if (s >= ModSource::lfo1 && s <= ModSource::lfo4)     return lfoAccent;
    if (s >= ModSource::macro1 && s <= ModSource::macro4) return macroAccent;
    return otherAccent;
}

Rectangle<float> drawSection (Graphics& g, Rectangle<float> r, const String& title, Colour accent, bool enabled)
{
    g.setGradientFill (ColourGradient (panelFill.brighter (0.06f), r.getX(), r.getY(), panelFill.darker (0.25f),
                                       r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (panelEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

    auto strip = r.removeFromTop (26.0f);
    if (title.isNotEmpty())
    {
        g.setColour ((enabled ? accent : textDim).withAlpha (enabled ? 1.0f : 0.6f));
        g.setFont (aa::Fonts::display (13.0f));
        g.drawText (title, strip.reduced (34.0f, 0.0f).toNearestInt(), Justification::centredLeft);
        g.setColour (accent.withAlpha (enabled ? 0.55f : 0.15f));
        g.fillRect (strip.getX() + 10.0f, strip.getBottom() - 1.0f, strip.getWidth() - 20.0f, 1.0f);
    }
    return r;
}

void drawScreen (Graphics& g, Rectangle<float> r)
{
    g.setColour (screenFill);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (Colours::white.withAlpha (0.04f));
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX() + 4.0f, r.getRight() - 4.0f);
}

//==============================================================================
// ModKnob
//==============================================================================
ModKnob::ModKnob (SynthProcessor& p, const String& paramId, const String& l, Colour c)
    : proc (p), label (l), accent (c)
{
    parameter = p.param (paramId);
    destIndex = modDestIndex (paramId);
    setSliderStyle (RotaryHorizontalVerticalDrag);
    setTextBoxStyle (NoTextBox, true, 0, 0);
    setMouseDragSensitivity (180);
    setVelocityBasedMode (false);
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.apvts, paramId, *this);
    if (parameter != nullptr)
    {
        setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
        bipolarDisplay = parameter->getNormalisableRange().start < 0.0f && parameter->getNormalisableRange().end > 0.0f;
    }
    setPopupMenuEnabled (false);
}

ModKnob::~ModKnob() = default;

Rectangle<float> ModKnob::dialArea() const
{
    auto b = getLocalBounds().toFloat();
    b.removeFromBottom (14.0f);
    const float s = jmin (b.getWidth(), b.getHeight());
    return b.withSizeKeepingCentre (s, s);
}

void ModKnob::refreshModulation()
{
    if (destIndex < 0)
        return;
    const int v = proc.matrix.version.load();
    const float live = proc.engine.ui.destMod[(size_t) destIndex].load (std::memory_order_relaxed);
    if (v != lastVersion || std::abs (live - lastLive) > 0.002f)
    {
        lastVersion = v;
        lastLive = live;
        repaint();
    }
}

void ModKnob::paint (Graphics& g)
{
    if (parameter == nullptr)
        return;

    const auto dial = dialArea().reduced (2.0f);
    const auto c = dial.getCentre();
    const float r = dial.getWidth() * 0.5f;
    const float norm = parameter->getValue();
    const float valueA = angleFor (norm);
    const bool active = hovered || isMouseButtonDown();

    // routings (outer ring)
    std::vector<int> slots;
    if (destIndex >= 0)
        slots = proc.matrix.slotsForDest (destIndex);

    const float ringR = r - 1.5f;
    const float arcR = r * 0.78f;

    if (dropHover)
    {
        g.setColour (Colours::white.withAlpha (0.18f));
        g.fillEllipse (dial.expanded (2.0f));
    }

    strokeArc (g, c, arcR, startAngle, endAngle, jmax (2.5f, r * 0.12f), Colours::white.withAlpha (0.07f));

    const float fromA = bipolarDisplay ? angleFor (0.5f) : startAngle;
    const auto arcColour = active ? accent.brighter (0.3f) : accent;
    strokeArc (g, c, arcR, fromA, valueA, jmax (4.0f, r * 0.2f), arcColour.withAlpha (0.18f));   // glow
    strokeArc (g, c, arcR, fromA, valueA, jmax (2.5f, r * 0.12f), arcColour);

    for (int s : slots)
    {
        const auto& slot = proc.matrix.slots[(size_t) s];
        const float amt = slot.amount.load();
        const auto colour = sourceColour ((ModSource) slot.source.load());
        const bool bip = slot.bipolar.load() != isBipolarSource ((ModSource) slot.source.load());
        const float lo = bip ? norm - std::abs (amt) : jmin (norm, norm + amt);
        const float hi = bip ? norm + std::abs (amt) : jmax (norm, norm + amt);
        strokeArc (g, c, ringR, angleFor (lo), angleFor (hi), 2.2f, colour.withAlpha (s == adjustSlot ? 1.0f : 0.85f));
    }

    if (! slots.empty())
    {
        const float live = jlimit (0.0f, 1.0f, norm + lastLive);
        const float a = angleFor (live);
        const Point<float> p (c.x + std::sin (a) * ringR, c.y - std::cos (a) * ringR);
        g.setColour (Colours::white);
        g.fillEllipse (Rectangle<float> (4.5f, 4.5f).withCentre (p));
    }

    // body
    const float bodyR = r * 0.58f;
    const auto body = Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c);
    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillEllipse (body.translated (0.0f, 1.5f).expanded (1.5f));
    g.setGradientFill (ColourGradient (Colour (active ? 0xff343b5e : 0xff2a3050), c.x, body.getY(),
                                       Colour (0xff12152a), c.x, body.getBottom(), false));
    g.fillEllipse (body);
    g.setColour (Colours::white.withAlpha (0.12f));
    g.drawEllipse (body.reduced (0.5f), 1.0f);

    const Point<float> dir (std::sin (valueA), -std::cos (valueA));
    g.setColour (Colours::white.withAlpha (0.92f));
    g.drawLine ({ c + dir * (bodyR * 0.25f), c + dir * (bodyR * 0.85f) }, jmax (1.5f, bodyR * 0.13f));

    // caption
    auto caption = getLocalBounds().toFloat().removeFromBottom (14.0f);
    const bool showValue = active || adjustingMod;
    String text = label;
    if (adjustingMod && isPositiveAndBelow (adjustSlot, maxModSlots))
        text = String (roundToInt (proc.matrix.slots[(size_t) adjustSlot].amount.load() * 100.0f)) + "%";
    else if (showValue)
        text = parameter->getCurrentValueAsText();

    g.setFont (showValue ? aa::Fonts::uiBold (10.5f) : aa::Fonts::ui (10.0f).withExtraKerningFactor (0.05f));
    g.setColour (showValue ? accent.interpolatedWith (Colours::white, 0.5f) : textDim);
    g.drawFittedText (showValue ? text : text.toUpperCase(), caption.toNearestInt(), Justification::centred, 1, 0.7f);
}

void ModKnob::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showModMenu();
        return;
    }

    if ((e.mods.isAltDown() || e.mods.isCtrlDown()) && destIndex >= 0)
    {
        const auto slots = proc.matrix.slotsForDest (destIndex);
        if (! slots.empty())
        {
            adjustingMod = true;
            adjustSlot = slots.back();
            adjustStart = proc.matrix.slots[(size_t) adjustSlot].amount.load();
            repaint();
            return;
        }
    }
    Slider::mouseDown (e);
}

void ModKnob::mouseDrag (const MouseEvent& e)
{
    if (adjustingMod)
    {
        const float delta = (float) (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) / 180.0f;
        proc.matrix.slots[(size_t) adjustSlot].amount = jlimit (-1.0f, 1.0f, adjustStart + delta);
        ++proc.matrix.version;
        repaint();
        return;
    }
    if (! e.mods.isPopupMenu())
        Slider::mouseDrag (e);
}

void ModKnob::mouseUp (const MouseEvent& e)
{
    if (adjustingMod)
    {
        adjustingMod = false;
        adjustSlot = -1;
        repaint();
        return;
    }
    if (! e.mods.isPopupMenu())
        Slider::mouseUp (e);
}

void ModKnob::mouseDoubleClick (const MouseEvent& e)
{
    if (! e.mods.isAltDown())
        Slider::mouseDoubleClick (e);
}

void ModKnob::showModMenu()
{
    PopupMenu menu;
    menu.addSectionHeader (parameter != nullptr ? parameter->getName (64) : label);
    menu.addItem (1, "Reset to default");

    const auto slots = destIndex >= 0 ? proc.matrix.slotsForDest (destIndex) : std::vector<int>();
    if (destIndex < 0)
        menu.addItem (-1, "(not modulatable)", false);
    else if (slots.empty())
        menu.addItem (-1, "Drag a modulation source here to modulate", false);

    for (int s : slots)
    {
        auto& slot = proc.matrix.slots[(size_t) s];
        PopupMenu sub;
        sub.addItem (1000 + s, "Invert");
        sub.addItem (2000 + s, "Bipolar", true, slot.bipolar.load());
        sub.addItem (3000 + s, "Set depth to 0");
        sub.addSeparator();
        sub.addItem (4000 + s, "Remove");
        const String name = modSourceName ((ModSource) slot.source.load()) + "  "
                            + (slot.amount.load() >= 0.0f ? "+" : "") + String (roundToInt (slot.amount.load() * 100.0f)) + "%";
        menu.addSubMenu (name, sub);
    }
    if (! slots.empty())
        menu.addItem (5000, "Remove all modulation");

    SafePointer<ModKnob> safe (this);
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [safe] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        auto& m = safe->proc.matrix;
        if (result == 1 && safe->parameter != nullptr)
            safe->parameter->setValueNotifyingHost (safe->parameter->getDefaultValue());
        else if (result >= 1000 && result < 2000)
            m.slots[(size_t) result - 1000].amount = -m.slots[(size_t) result - 1000].amount.load();
        else if (result >= 2000 && result < 3000)
            m.slots[(size_t) result - 2000].bipolar = ! m.slots[(size_t) result - 2000].bipolar.load();
        else if (result >= 3000 && result < 4000)
            m.slots[(size_t) result - 3000].amount = 0.0f;
        else if (result >= 4000 && result < 5000)
            m.remove (result - 4000);
        else if (result == 5000)
            for (int s : m.slotsForDest (safe->destIndex))
                m.remove (s);
        ++m.version;
        safe->repaint();
    });
}

bool ModKnob::isInterestedInDragSource (const SourceDetails& d)
{
    return destIndex >= 0 && sourceFromDescription (d.description) != ModSource::none;
}

void ModKnob::itemDropped (const SourceDetails& d)
{
    dropHover = false;
    const auto src = sourceFromDescription (d.description);
    if (src != ModSource::none && destIndex >= 0)
    {
        if (proc.matrix.find (src, destIndex) < 0)
            proc.matrix.add (src, destIndex, isBipolarSource (src) ? 0.25f : 0.35f);
    }
    repaint();
}

//==============================================================================
// ModSourceHandle
//==============================================================================
ModSourceHandle::ModSourceHandle (ModSource s) : source (s)
{
    setTooltip ("Drag onto any knob to modulate it with " + modSourceName (s));
}

void ModSourceHandle::paint (Graphics& g)
{
    const auto colour = sourceColour (source);
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const float s = jmin (b.getWidth(), b.getHeight());
    auto circle = b.withSizeKeepingCentre (s, s);
    const bool over = isMouseOver();

    g.setColour (colour.withAlpha (over ? 0.35f : 0.18f));
    g.fillEllipse (circle);
    g.setColour (colour.withAlpha (over ? 1.0f : 0.8f));
    g.drawEllipse (circle.reduced (0.5f), 1.2f);

    // four little arrows
    const auto c = circle.getCentre();
    const float a = s * 0.28f, w = s * 0.11f;
    for (int i = 0; i < 4; ++i)
    {
        Path p;
        p.addTriangle (0.0f, -a - w, -w, -a + w * 0.4f, w, -a + w * 0.4f);
        p.applyTransform (AffineTransform::rotation ((float) i * MathConstants<float>::halfPi).translated (c));
        g.fillPath (p);
    }
    g.fillEllipse (Rectangle<float> (w * 1.6f, w * 1.6f).withCentre (c));
}

void ModSourceHandle::mouseDown (const MouseEvent&)
{
    dragStarted = false;
}

void ModSourceHandle::mouseDrag (const MouseEvent& e)
{
    if (dragStarted || e.getDistanceFromDragStart() < 3)
        return;
    if (auto* container = DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragStarted = true;
        Image img (Image::ARGB, 22, 22, true);
        {
            Graphics g (img);
            g.setColour (sourceColour (source).withAlpha (0.85f));
            g.fillEllipse (2.0f, 2.0f, 18.0f, 18.0f);
            g.setColour (Colours::white);
            g.setFont (aa::Fonts::uiBold (8.0f));
            g.drawText (modSourceShortName (source), 0, 0, 22, 22, Justification::centred);
        }
        container->startDragging ("modsrc:" + String ((int) source), this, ScaledImage (img), true);
    }
}

//==============================================================================
// PowerButton
//==============================================================================
PowerButton::PowerButton (AudioProcessorValueTreeState& state, const String& paramId, Colour c) : accent (c)
{
    attachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (state, paramId, *this);
    setTooltip ("On / off");
}

void PowerButton::paintButton (Graphics& g, bool highlighted, bool)
{
    const bool on = getToggleState();
    auto b = getLocalBounds().toFloat().reduced (2.0f);
    const float s = jmin (b.getWidth(), b.getHeight());
    b = b.withSizeKeepingCentre (s, s);

    if (on)
    {
        g.setColour (accent.withAlpha (0.25f));
        g.fillEllipse (b.expanded (2.0f));
    }
    g.setColour (on ? accent : textDim.withAlpha (highlighted ? 0.9f : 0.6f));
    Path p;
    const auto c = b.getCentre();
    const float r = s * 0.36f;
    p.addCentredArc (c.x, c.y, r, r, 0.0f, 0.65f, MathConstants<float>::twoPi - 0.65f, true);
    g.strokePath (p, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
    g.drawLine (c.x, c.y - r - 1.0f, c.x, c.y - r * 0.2f, 1.8f);
}

//==============================================================================
// WavetableView
//==============================================================================
WavetableView::WavetableView (SynthProcessor& p, int o, Colour c) : proc (p), osc (o), accent (c)
{
    setTooltip ("Drop a WAV here to load it as a wavetable. Click to switch between 3D and 2D.");
}

void WavetableView::rebuild()
{
    table = proc.getOscTable (osc);
    builtTable = table.get();
    builtVersion = proc.tableVersion.load();
    frames.clear();
    if (table == nullptr)
        return;

    const int numFrames = table->getNumFrames();
    const int shown = jmin (numFrames, 40);
    for (int i = 0; i < shown; ++i)
    {
        const int f = shown == 1 ? 0 : roundToInt ((float) i * (float) (numFrames - 1) / (float) (shown - 1));
        std::vector<float> pts (160);
        table->getDisplayFrame (f, pts.data(), (int) pts.size());
        frames.push_back (std::move (pts));
    }
}

void WavetableView::refresh()
{
    if (builtVersion != proc.tableVersion.load() || proc.getOscTable (osc).get() != builtTable)
    {
        rebuild();
        repaint();
    }
    const float pos = proc.engine.ui.oscPosition[(size_t) osc].load (std::memory_order_relaxed);
    const float w = proc.engine.ui.oscWarp[(size_t) osc].load (std::memory_order_relaxed);
    if (std::abs (pos - position) > 0.001f || std::abs (w - warp) > 0.001f)
    {
        position = pos;
        warp = w;
        repaint();
    }
    if (message.isNotEmpty() && Time::getMillisecondCounterHiRes() - messageTime > 3500.0)
    {
        message.clear();
        repaint();
    }
}

void WavetableView::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawScreen (g, r);
    if (frames.empty())
        return;

    const bool enabled = proc.vpParam (vp::osc (osc, OscField::on))->getValue() > 0.5f;
    const auto warpMode = (Warp) roundToInt (proc.vpParam (vp::osc (osc, OscField::warp))->convertFrom0to1 (
        proc.vpParam (vp::osc (osc, OscField::warp))->getValue()));
    const auto colour = enabled ? accent : textDim;
    const int n = (int) frames.size();

    // the current frame, interpolated, with the display-friendly warps applied
    std::vector<float> current (frames.front().size());
    {
        const float x = position * (float) (n - 1);
        const int i = jlimit (0, n - 1, (int) x);
        const int j = jmin (n - 1, i + 1);
        const float t = x - (float) i;
        for (size_t k = 0; k < current.size(); ++k)
        {
            const float p = (float) k / (float) current.size();
            bool invert = false;
            const float q = displayWarp (warpMode, warp, p, invert);
            const float a = sampleFrame (frames[(size_t) i], q), b = sampleFrame (frames[(size_t) j], q);
            float s = a + t * (b - a);
            if (warpMode == Warp::mirror)
            {
                const float m = p < 0.5f ? 2.0f * p : 2.0f - 2.0f * p;
                s += warp * ((sampleFrame (frames[(size_t) i], m) * (1.0f - t) + sampleFrame (frames[(size_t) j], m) * t) - s);
            }
            if (warpMode == Warp::windowSync)
                s *= std::sin (MathConstants<float>::pi * p);
            current[k] = invert ? -s : s;
        }
    }

    auto plotFrame = [] (const std::vector<float>& f, Rectangle<float> area, float amp)
    {
        Path p;
        for (size_t k = 0; k <= f.size(); ++k)
        {
            const float x = area.getX() + area.getWidth() * (float) k / (float) f.size();
            const float y = area.getCentreY() - f[k % f.size()] * amp;
            if (k == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    };

    if (flat || n == 1)
    {
        auto area = r.reduced (10.0f, 12.0f);
        auto path = plotFrame (current, area, area.getHeight() * 0.46f);
        Path fill (path);
        fill.lineTo (area.getRight(), area.getCentreY());
        fill.lineTo (area.getX(), area.getCentreY());
        fill.closeSubPath();
        g.setGradientFill (ColourGradient (colour.withAlpha (0.28f), 0.0f, area.getY(), colour.withAlpha (0.02f), 0.0f, area.getCentreY(), false));
        g.fillPath (fill);
        g.setColour (colour.withAlpha (0.25f));
        g.strokePath (path, PathStrokeType (5.0f));
        g.setColour (colour);
        g.strokePath (path, PathStrokeType (1.8f));
    }
    else
    {
        // stacked frames in depth: frame 0 at the front-left, the last frame at the back-right
        const float depthX = r.getWidth() * 0.2f, depthY = r.getHeight() * 0.36f;
        const float frameW = r.getWidth() * 0.74f;
        const float amp = r.getHeight() * 0.17f;
        auto frameArea = [&] (float d)
        {
            return Rectangle<float> (r.getX() + 10.0f + d * depthX, r.getBottom() - amp - 10.0f - d * depthY - amp,
                                     frameW, amp * 2.0f);
        };

        for (int i = n - 1; i >= 0; --i)
        {
            const float d = (float) i / (float) (n - 1);
            const float near = std::abs (d - position);
            g.setColour (colour.withAlpha (jmax (0.08f, 0.32f - near * 0.6f)));
            g.strokePath (plotFrame (frames[(size_t) i], frameArea (d), amp), PathStrokeType (1.0f));
        }

        auto area = frameArea (position);
        auto path = plotFrame (current, area, amp);
        Path fill (path);
        fill.lineTo (area.getRight(), area.getCentreY());
        fill.lineTo (area.getX(), area.getCentreY());
        fill.closeSubPath();
        g.setColour (colour.withAlpha (0.16f));
        g.fillPath (fill);
        g.setColour (colour.withAlpha (0.3f));
        g.strokePath (path, PathStrokeType (5.0f));
        g.setColour (colour.brighter (0.2f));
        g.strokePath (path, PathStrokeType (2.0f));
    }

    // frame readout
    g.setFont (aa::Fonts::ui (10.0f));
    g.setColour (textDim);
    const int numFrames = table != nullptr ? table->getNumFrames() : 1;
    g.drawText ("FRAME " + String (1 + roundToInt (position * (float) (numFrames - 1))) + " / " + String (numFrames),
                r.reduced (8.0f, 5.0f).toNearestInt(), Justification::topRight);

    if (dropHover || message.isNotEmpty())
    {
        g.setColour (Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (dropHover ? accent : textBright);
        g.setFont (aa::Fonts::uiBold (13.0f));
        g.drawFittedText (dropHover ? "Drop to load wavetable" : message, r.reduced (10.0f).toNearestInt(), Justification::centred, 3);
    }
}

void WavetableView::mouseDown (const MouseEvent&)
{
    flat = ! flat;
    repaint();
    if (onClick)
        onClick();
}

bool WavetableView::isInterestedInFileDrag (const StringArray& files)
{
    for (const auto& f : files)
        if (File (f).hasFileExtension ("wav;aif;aiff;flac"))
            return true;
    return false;
}

void WavetableView::filesDropped (const StringArray& files, int, int)
{
    dropHover = false;
    for (const auto& f : files)
    {
        String error;
        if (proc.loadCustomTable (osc, File (f), error))
        {
            message = "Loaded " + File (f).getFileNameWithoutExtension();
            messageTime = Time::getMillisecondCounterHiRes();
            break;
        }
        message = error.isNotEmpty() ? error : "Couldn't read that file";
        messageTime = Time::getMillisecondCounterHiRes();
    }
    repaint();
}

//==============================================================================
// TableSelector
//==============================================================================
TableSelector::TableSelector (SynthProcessor& p, int o, Colour c) : proc (p), osc (o), accent (c) {}

void TableSelector::refresh()
{
    const auto n = proc.getOscTableName (osc);
    if (n != name)
    {
        name = n;
        repaint();
    }
}

void TableSelector::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
    g.setColour (isMouseOver() ? accent.withAlpha (0.6f) : panelEdge);
    g.drawRoundedRectangle (b, b.getHeight() * 0.5f, 1.0f);

    g.setColour (textDim);
    auto arrows = b.reduced (6.0f, 0.0f);
    Path left, right;
    const float cy = b.getCentreY(), s = 3.5f;
    left.addTriangle (arrows.getX(), cy, arrows.getX() + s, cy - s, arrows.getX() + s, cy + s);
    right.addTriangle (arrows.getRight(), cy, arrows.getRight() - s, cy - s, arrows.getRight() - s, cy + s);
    g.fillPath (left);
    g.fillPath (right);

    g.setColour (textBright);
    g.setFont (aa::Fonts::uiBold (11.5f));
    g.drawFittedText (name, b.reduced (16.0f, 0.0f).toNearestInt(), Justification::centred, 1, 0.8f);
}

void TableSelector::step (int delta)
{
    auto* p = proc.vpParam (vp::osc (osc, OscField::table));
    const int current = roundToInt (p->convertFrom0to1 (p->getValue()));
    const int n = getNumFactoryTables();
    const int next = current >= n ? (delta > 0 ? 0 : n - 1) : (current + delta + n) % n;
    proc.selectFactoryTable (osc, next);
    refresh();
}

void TableSelector::mouseDown (const MouseEvent& e)
{
    if (e.x < 18)
        step (-1);
    else if (e.x > getWidth() - 18)
        step (1);
    else
        showMenu();
}

void TableSelector::showMenu()
{
    auto* p = proc.vpParam (vp::osc (osc, OscField::table));
    const int current = roundToInt (p->convertFrom0to1 (p->getValue()));

    PopupMenu menu;
    StringArray categories;
    for (int i = 0; i < getNumFactoryTables(); ++i)
        categories.addIfNotAlreadyThere (getFactoryTableInfo (i).category);

    for (const auto& cat : categories)
    {
        PopupMenu sub;
        bool tickedHere = false;
        for (int i = 0; i < getNumFactoryTables(); ++i)
            if (getFactoryTableInfo (i).category == cat)
            {
                sub.addItem (1 + i, getFactoryTableInfo (i).name, true, i == current);
                tickedHere = tickedHere || i == current;
            }
        menu.addSubMenu (cat, sub, true, nullptr, tickedHere);
    }
    menu.addSeparator();
    if (current >= getNumFactoryTables())
        menu.addItem (-1, "Custom: " + name, false, true);
    menu.addItem (10001, "Load wavetable (WAV)...");
    menu.addItem (10002, "Export this wavetable (WAV)...");

    SafePointer<TableSelector> safe (this);
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [safe] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        auto& proc = safe->proc;
        const int osc = safe->osc;
        if (result > 0 && result <= getNumFactoryTables())
        {
            proc.selectFactoryTable (osc, result - 1);
            safe->refresh();
        }
        else if (result == 10001)
        {
            safe->chooser = std::make_unique<FileChooser> ("Load a wavetable", File(), "*.wav;*.aif;*.aiff;*.flac");
            safe->chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                                        [safe] (const FileChooser& fc)
            {
                if (safe == nullptr || fc.getResult() == File())
                    return;
                String error;
                if (! safe->proc.loadCustomTable (safe->osc, fc.getResult(), error))
                    AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "ericriusSynth",
                                                      "Couldn't load that wavetable.\n" + error);
                safe->refresh();
            });
        }
        else if (result == 10002)
        {
            auto table = proc.getOscTable (osc);
            if (table == nullptr)
                return;
            safe->chooser = std::make_unique<FileChooser> ("Export wavetable",
                                                           File::getSpecialLocation (File::userDocumentsDirectory)
                                                               .getChildFile (table->getName() + ".wav"), "*.wav");
            safe->chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting,
                                        [table] (const FileChooser& fc)
            {
                if (fc.getResult() != File())
                    saveWavetableFile (*table, fc.getResult().withFileExtension ("wav"));
            });
        }
    });
}

//==============================================================================
// OscPanel
//==============================================================================
namespace
{
    String oscParam (int o, const char* field) { return String (o == 0 ? "a" : (o == 1 ? "b" : "c")) + field; }
}

OscPanel::OscPanel (SynthProcessor& p, int o)
    : proc (p), osc (o), accent (o == 0 ? oscAccent : (o == 1 ? Colour (0xff4fb8ff) : Colour (0xff7ef08a))),
      power (p.apvts, oscParam (o, "On"), accent),
      selector (p, o, accent),
      view (p, o, accent),
      position (p, oscParam (o, "Pos"), "WT Pos", accent),
      warpAmount (p, oscParam (o, "WarpAmt"), "Warp", accent),
      unison (p, oscParam (o, "Unison"), "Unison", accent),
      detune (p, oscParam (o, "Detune"), "Detune", accent),
      blend (p, oscParam (o, "Blend"), "Blend", accent),
      width (p, oscParam (o, "Width"), "Width", accent),
      octave (p, oscParam (o, "Oct"), "Oct", accent),
      semi (p, oscParam (o, "Semi"), "Semi", accent),
      fine (p, oscParam (o, "Fine"), "Fine", accent),
      phase (p, oscParam (o, "Phase"), "Phase", accent),
      random (p, oscParam (o, "Rand"), "Rand", accent),
      pan (p, oscParam (o, "Pan"), "Pan", accent),
      level (p, oscParam (o, "Level"), "Level", accent),
      warp (p.apvts, oscParam (o, "Warp"), {}, aa::ChoiceBox::Style::stepper),
      warpSource (p.apvts, oscParam (o, "WarpSrc"), {}, aa::ChoiceBox::Style::stepper),
      route (p.apvts, oscParam (o, "Route"), {}, aa::ChoiceBox::Style::stepper),
      stack (p.apvts, oscParam (o, "Stack"), {}, aa::ChoiceBox::Style::stepper)
{
    for (Component* c : std::initializer_list<Component*> { &power, &selector, &view, &position, &warpAmount, &unison, &detune,
                                                            &blend, &width, &octave, &semi, &fine, &phase, &random, &pan,
                                                            &level, &warp, &warpSource, &route, &stack })
        addAndMakeVisible (c);

    for (auto* b : { &warp, &warpSource, &route, &stack })
        b->setAccent (accent);
    warp.setTooltip ("Warp mode: bends, syncs or modulates the wavetable's phase");
    warpSource.setTooltip ("FM / AM / RM source for the warp");
    route.setTooltip ("Where this oscillator goes: Filter 1, Filter 2, both, or straight to the output");
    stack.setTooltip ("Unison stack: spread unison voices across octaves, fifths or a chord");
}

void OscPanel::collectKnobs (std::vector<ModKnob*>& out)
{
    for (auto* k : { &position, &warpAmount, &unison, &detune, &blend, &width, &octave, &semi, &fine, &phase, &random, &pan, &level })
        out.push_back (k);
}

void OscPanel::refresh()
{
    selector.refresh();
    view.refresh();
    const bool on = power.getToggleState();
    if (on != lastOn)
    {
        lastOn = on;
        repaint();
    }
}

void OscPanel::paint (Graphics& g)
{
    const bool on = power.getToggleState();
    drawSection (g, getLocalBounds().toFloat(), "OSC " + String::charToString ((juce_wchar) ('A' + osc)), accent, on);

    g.setFont (aa::Fonts::ui (9.5f));
    g.setColour (textDim);
    g.drawText ("WARP", warp.getBounds().translated (0, -12).withHeight (12), Justification::centred);
    g.drawText ("SOURCE", warpSource.getBounds().translated (0, -12).withHeight (12), Justification::centred);
    g.drawText ("ROUTE", route.getBounds().translated (0, -12).withHeight (12), Justification::centred);
    g.drawText ("STACK", stack.getBounds().translated (0, -12).withHeight (12), Justification::centred);
}

void OscPanel::resized()
{
    auto r = getLocalBounds().reduced (8, 0);
    auto header = r.removeFromTop (26);
    power.setBounds (header.removeFromLeft (22).withSizeKeepingCentre (20, 20));
    selector.setBounds (header.removeFromRight (190).withSizeKeepingCentre (190, 20));

    r.removeFromTop (6);
    view.setBounds (r.removeFromTop (118));
    r.removeFromTop (4);

    auto row1 = r.removeFromTop (60);
    placeKnobs (row1, { &position, &warpAmount, &unison, &detune, &blend, &width, &level });
    auto row2 = r.removeFromTop (60);
    placeKnobs (row2, { &octave, &semi, &fine, &phase, &random, &pan });

    r.removeFromTop (14);
    auto row3 = r.removeFromTop (22);
    const int w = row3.getWidth() / 4;
    warp.setBounds (row3.removeFromLeft (w).reduced (3, 0));
    warpSource.setBounds (row3.removeFromLeft (w).reduced (3, 0));
    route.setBounds (row3.removeFromLeft (w).reduced (3, 0));
    stack.setBounds (row3.reduced (3, 0));
}

//==============================================================================
// SubNoisePanel
//==============================================================================
SubNoisePanel::SubNoisePanel (SynthProcessor& p)
    : proc (p),
      subPower (p.apvts, "subOn", oscAccent), noisePower (p.apvts, "noiseOn", oscAccent),
      subShape (p.apvts, "subShape", {}, aa::ChoiceBox::Style::stepper),
      subRoute (p.apvts, "subRoute", {}, aa::ChoiceBox::Style::stepper),
      noiseType (p.apvts, "noiseType", {}, aa::ChoiceBox::Style::stepper),
      noiseRoute (p.apvts, "noiseRoute", {}, aa::ChoiceBox::Style::stepper),
      subOctave (p, "subOct", "Oct", oscAccent), subLevel (p, "subLevel", "Level", oscAccent),
      subPan (p, "subPan", "Pan", oscAccent), noisePitch (p, "noisePitch", "Pitch", oscAccent),
      noiseLevel (p, "noiseLevel", "Level", oscAccent), noisePan (p, "noisePan", "Pan", oscAccent),
      noiseKey (p.apvts, "noiseKey", "Key")
{
    for (Component* c : std::initializer_list<Component*> { &subPower, &noisePower, &subShape, &subRoute, &noiseType, &noiseRoute,
                                                            &subOctave, &subLevel, &subPan, &noisePitch, &noiseLevel, &noisePan, &noiseKey })
        addAndMakeVisible (c);
    for (auto* b : { &subShape, &subRoute, &noiseType, &noiseRoute })
        b->setAccent (oscAccent);
    noiseKey.setAccent (oscAccent);
}

void SubNoisePanel::collectKnobs (std::vector<ModKnob*>& out)
{
    for (auto* k : { &subOctave, &subLevel, &subPan, &noisePitch, &noiseLevel, &noisePan })
        out.push_back (k);
}

void SubNoisePanel::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto left = r.removeFromLeft (r.getWidth() * 0.42f).withTrimmedRight (4.0f);
    auto right = r.withTrimmedLeft (4.0f);
    drawSection (g, left, "SUB", oscAccent, subPower.getToggleState());
    drawSection (g, right, "NOISE", oscAccent, noisePower.getToggleState());
}

void SubNoisePanel::resized()
{
    auto r = getLocalBounds();
    auto left = r.removeFromLeft (roundToInt ((float) r.getWidth() * 0.42f)).withTrimmedRight (4).reduced (8, 0);
    auto right = r.withTrimmedLeft (4).reduced (8, 0);

    auto lh = left.removeFromTop (26);
    subPower.setBounds (lh.removeFromLeft (22).withSizeKeepingCentre (20, 20));
    subShape.setBounds (lh.removeFromRight (82).withSizeKeepingCentre (82, 20));
    left.removeFromTop (8);
    placeKnobs (left.removeFromTop (60), { &subOctave, &subLevel, &subPan });
    left.removeFromTop (6);
    subRoute.setBounds (left.removeFromTop (22).withSizeKeepingCentre (96, 20));

    auto rh = right.removeFromTop (26);
    noisePower.setBounds (rh.removeFromLeft (22).withSizeKeepingCentre (20, 20));
    noiseType.setBounds (rh.removeFromRight (118).withSizeKeepingCentre (118, 20));
    right.removeFromTop (8);
    placeKnobs (right.removeFromTop (60), { &noisePitch, &noiseLevel, &noisePan });
    right.removeFromTop (6);
    auto bottom = right.removeFromTop (22);
    noiseKey.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2).withSizeKeepingCentre (70, 20));
    noiseRoute.setBounds (bottom.withSizeKeepingCentre (96, 20));
}

//==============================================================================
// FilterCurve
//==============================================================================
FilterCurve::FilterCurve (SynthProcessor& p, int f, Colour c) : proc (p), filter (f), accent (c) {}

void FilterCurve::refresh()
{
    const int d = modDestForVp (vp::filter (filter, FilterField::cutoff));
    float key = 0.0f;
    for (int field : { (int) FilterField::cutoff, (int) FilterField::resonance, (int) FilterField::type,
                       (int) FilterField::var, (int) FilterField::mix, (int) FilterField::on })
        key += proc.vpParam (vp::filter (filter, (FilterField) field))->getValue() * (float) (field + 1) * 13.7f;
    if (d >= 0)
        key += proc.engine.ui.destMod[(size_t) d].load (std::memory_order_relaxed) * 1000.0f;
    if (std::abs (key - lastKey) > 1.0e-4f)
    {
        lastKey = key;
        repaint();
    }
}

void FilterCurve::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawScreen (g, r);
    auto area = r.reduced (4.0f, 6.0f);

    auto value = [&] (FilterField f)
    {
        auto* p = proc.vpParam (vp::filter (filter, f));
        return p->convertFrom0to1 (p->getValue());
    };
    const bool on = value (FilterField::on) > 0.5f;
    const auto type = (FilterType) roundToInt (value (FilterField::type));
    float cutoffNorm = proc.vpParam (vp::filter (filter, FilterField::cutoff))->getValue();
    if (const int d = modDestForVp (vp::filter (filter, FilterField::cutoff)); d >= 0)
        cutoffNorm = jlimit (0.0f, 1.0f, cutoffNorm + proc.engine.ui.destMod[(size_t) d].load());
    const float fc = proc.vpParam (vp::filter (filter, FilterField::cutoff))->convertFrom0to1 (cutoffNorm);
    const float reso = value (FilterField::resonance) * 0.01f;
    const float var = value (FilterField::var) * 0.01f;
    const float mix = value (FilterField::mix) * 0.01f;
    const float q = 0.6f + reso * reso * 14.0f;

    // grid
    g.setColour (Colours::white.withAlpha (0.05f));
    for (float f : { 100.0f, 1000.0f, 10000.0f })
    {
        const float x = area.getX() + area.getWidth() * std::log (f / 20.0f) / std::log (1000.0f);
        g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
    }

    auto magnitude = [&] (float f) -> float
    {
        const float w = f / fc;
        const float w2 = w * w;
        const float den = std::sqrt ((1.0f - w2) * (1.0f - w2) + (w / q) * (w / q));
        const float lp = 1.0f / den, hp = w2 / den, bp = (w / q) / den, notch = std::abs (1.0f - w2) / den;
        switch (type)
        {
            case FilterType::lp12:     return lp;
            case FilterType::lp24:
            case FilterType::ladder:
            case FilterType::diode:    return lp * (1.0f / std::sqrt ((1.0f - w2) * (1.0f - w2) + w2 * 2.0f));
            case FilterType::hp12:     return hp;
            case FilterType::hp24:     return hp * (w2 / std::sqrt ((1.0f - w2) * (1.0f - w2) + w2 * 2.0f));
            case FilterType::bp12:     return bp;
            case FilterType::bp24:     return bp * bp;
            case FilterType::notch:    return notch;
            case FilterType::peak:     return 1.0f + bp * reso * 6.0f;
            case FilterType::combPlus:
            case FilterType::combMinus:
            case FilterType::flanger:
            {
                const float g0 = (type == FilterType::combMinus ? -0.9f : 0.9f) * (0.3f + 0.7f * reso);
                const float ph = MathConstants<float>::twoPi * w;
                return 1.0f / std::sqrt (1.0f + g0 * g0 - 2.0f * g0 * std::cos (ph)) * (1.0f - std::abs (g0) * 0.5f);
            }
            case FilterType::phaser:
            {
                const float d = std::log2 (w);
                return 1.0f - 0.8f * (0.4f + 0.6f * reso) * std::exp (-d * d * 6.0f) * std::abs (std::cos (d * 3.0f * (1.0f + var)));
            }
            case FilterType::formant:
            {
                static const float vowels[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 350, 1700, 2700 },
                                                    { 450, 800, 2830 }, { 325, 700, 2530 } };
                const float vx = var * 4.0f;
                const int vi = jmin (3, (int) vx);
                const float vt = vx - (float) vi;
                float m = 0.05f;
                for (int k = 0; k < 3; ++k)
                {
                    const float ff = (vowels[vi][k] + vt * (vowels[vi + 1][k] - vowels[vi][k])) * fc / 1000.0f;
                    const float d = std::log2 (f / ff);
                    m += (k == 0 ? 1.0f : 0.6f) * std::exp (-d * d * (6.0f + reso * 30.0f));
                }
                return m * 1.6f;
            }
            case FilterType::wasp:     return var < 0.5f ? lp + (bp - lp) * var * 2.0f : bp + (hp - bp) * (var - 0.5f) * 2.0f;
            case FilterType::ring:
            case FilterType::downsample:
            case FilterType::count:    return 1.0f;
        }
        return 1.0f;
    };

    Path p;
    const int steps = (int) area.getWidth();
    for (int i = 0; i <= steps; ++i)
    {
        const float t = (float) i / (float) steps;
        const float f = 20.0f * std::pow (1000.0f, t);
        const float m = mix * magnitude (f) + (1.0f - mix);
        const float db = jlimit (-30.0f, 24.0f, 20.0f * std::log10 (jmax (1.0e-4f, m)));
        const float y = area.getY() + area.getHeight() * (1.0f - (db + 30.0f) / 54.0f);
        const float x = area.getX() + area.getWidth() * t;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    const auto colour = on ? accent : textDim;
    Path fill (p);
    fill.lineTo (area.getRight(), area.getBottom());
    fill.lineTo (area.getX(), area.getBottom());
    fill.closeSubPath();
    g.setGradientFill (ColourGradient (colour.withAlpha (0.22f), 0.0f, area.getY(), colour.withAlpha (0.0f), 0.0f, area.getBottom(), false));
    g.fillPath (fill);
    g.setColour (colour);
    g.strokePath (p, PathStrokeType (1.6f));

    if (type == FilterType::ring || type == FilterType::downsample)
    {
        g.setFont (aa::Fonts::ui (10.0f));
        g.setColour (textDim);
        g.drawText (type == FilterType::ring ? "RING MOD" : "DOWNSAMPLE", area.toNearestInt(), Justification::centredTop);
    }
}

//==============================================================================
// FilterPanel
//==============================================================================
FilterPanel::FilterPanel (SynthProcessor& p, int f)
    : proc (p), filter (f), accent (f == 0 ? filterAccent : Colour (0xffff8a5c)),
      power (p.apvts, "f" + String (f + 1) + "On", accent),
      type (p.apvts, "f" + String (f + 1) + "Type", {}, aa::ChoiceBox::Style::stepper),
      curve (p, f, accent),
      cutoff (p, "f" + String (f + 1) + "Cutoff", "Cutoff", accent),
      resonance (p, "f" + String (f + 1) + "Reso", "Res", accent),
      drive (p, "f" + String (f + 1) + "Drive", "Drive", accent),
      var (p, "f" + String (f + 1) + "Var", "Var", accent),
      mix (p, "f" + String (f + 1) + "Mix", "Mix", accent),
      keyTrack (p, "f" + String (f + 1) + "Key", "Key", accent)
{
    for (Component* c : std::initializer_list<Component*> { &power, &type, &curve, &cutoff, &resonance, &drive, &var, &mix, &keyTrack })
        addAndMakeVisible (c);
    type.setAccent (accent);
    var.setTooltip ("Type-specific: fatness, comb damping, phaser spread, vowel, ring shape, bit depth, wasp mode");
}

void FilterPanel::collectKnobs (std::vector<ModKnob*>& out)
{
    for (auto* k : { &cutoff, &resonance, &drive, &var, &mix, &keyTrack })
        out.push_back (k);
}

void FilterPanel::paint (Graphics& g)
{
    drawSection (g, getLocalBounds().toFloat(), "FILTER " + String (filter + 1), accent, power.getToggleState());
}

void FilterPanel::resized()
{
    auto r = getLocalBounds().reduced (8, 0);
    auto header = r.removeFromTop (26);
    power.setBounds (header.removeFromLeft (22).withSizeKeepingCentre (20, 20));
    type.setBounds (header.removeFromRight (120).withSizeKeepingCentre (120, 20));
    r.removeFromTop (6);
    r.removeFromBottom (6);
    curve.setBounds (r.removeFromLeft (jmin (170, r.getWidth() / 3 + 20)));
    r.removeFromLeft (4);
    auto top = r.removeFromTop (r.getHeight() / 2);
    placeKnobs (top, { &cutoff, &resonance, &drive });
    placeKnobs (r, { &var, &mix, &keyTrack });
}

//==============================================================================
// MacroPanel
//==============================================================================
MacroPanel::MacroPanel (SynthProcessor& p) : proc (p)
{
    for (int m = 0; m < numMacros; ++m)
    {
        auto* k = knobs.add (new ModKnob (p, "m" + String (m + 1), {}, macroAccent));
        addAndMakeVisible (k);
        auto* h = handles.add (new ModSourceHandle ((ModSource) ((int) ModSource::macro1 + m)));
        addAndMakeVisible (h);
        auto* l = names.add (new Label ({}, p.getMacroName (m)));
        l->setEditable (false, true, false);
        l->setJustificationType (Justification::centred);
        l->setFont (aa::Fonts::uiBold (11.0f));
        l->setColour (Label::textColourId, textBright);
        l->setColour (Label::textWhenEditingColourId, textBright);
        l->setColour (Label::backgroundWhenEditingColourId, Colours::black);
        l->setTooltip ("Double-click to rename");
        l->onTextChange = [this, m, l] { proc.setMacroName (m, l->getText()); };
        addAndMakeVisible (l);
    }
}

void MacroPanel::collectKnobs (std::vector<ModKnob*>& out)
{
    for (auto* k : knobs)
        out.push_back (k);
}

void MacroPanel::paint (Graphics& g)
{
    drawSection (g, getLocalBounds().toFloat(), "MACROS", macroAccent);
    for (int m = 0; m < numMacros; ++m)
        if (names[m]->getText() != proc.getMacroName (m) && ! names[m]->isBeingEdited())
            names[m]->setText (proc.getMacroName (m), dontSendNotification);
}

void MacroPanel::resized()
{
    auto r = getLocalBounds().reduced (8, 0);
    r.removeFromTop (30);
    const int cellW = r.getWidth() / 2, cellH = r.getHeight() / 2;
    for (int m = 0; m < numMacros; ++m)
    {
        auto cell = Rectangle<int> (r.getX() + (m % 2) * cellW, r.getY() + (m / 2) * cellH, cellW, cellH).reduced (4, 2);
        auto nameRow = cell.removeFromBottom (18);
        handles[m]->setBounds (nameRow.removeFromRight (18).reduced (1));
        names[m]->setBounds (nameRow);
        knobs[m]->setBounds (cell.withSizeKeepingCentre (jmin (cell.getWidth(), cell.getHeight() + 14), cell.getHeight()));
    }
}

//==============================================================================
// VoicePanel
//==============================================================================
VoicePanel::VoicePanel (SynthProcessor& p)
    : proc (p),
      mode (p.apvts, "voiceMode", {}, aa::ChoiceBox::Style::segmented),
      polyphony (p, "polyphony", "Voices", otherAccent),
      glide (p, "glide", "Glide", otherAccent),
      bend (p, "bendRange", "Bend", otherAccent),
      velocity (p, "velocity", "Velocity", otherAccent),
      tune (p, "masterTune", "Tune", otherAccent),
      legato (p.apvts, "legatoGlide", "Legato glide")
{
    for (Component* c : std::initializer_list<Component*> { &mode, &polyphony, &glide, &bend, &velocity, &tune, &legato })
        addAndMakeVisible (c);
    mode.setAccent (otherAccent);
    legato.setAccent (otherAccent);
}

void VoicePanel::collectKnobs (std::vector<ModKnob*>& out)
{
    for (auto* k : { &polyphony, &glide, &bend, &velocity, &tune })
        out.push_back (k);
}

void VoicePanel::paint (Graphics& g)
{
    drawSection (g, getLocalBounds().toFloat(), "VOICING", otherAccent);
}

void VoicePanel::resized()
{
    auto r = getLocalBounds().reduced (10, 0);
    r.removeFromTop (32);
    mode.setBounds (r.removeFromTop (22));
    r.removeFromTop (8);
    const int rowH = (r.getHeight() - 26) / 2;
    placeKnobs (r.removeFromTop (rowH), { &polyphony, &glide, &bend });
    placeKnobs (r.removeFromTop (rowH), { &velocity, &tune });
    legato.setBounds (r.removeFromTop (24).withSizeKeepingCentre (130, 20));
}

//==============================================================================
// ScopeView
//==============================================================================
ScopeView::ScopeView (SynthProcessor& p) : proc (p) {}

void ScopeView::refresh()
{
    const int w = proc.scopeWrite.load (std::memory_order_acquire);
    constexpr int size = SynthProcessor::scopeSize;
    // find a rising zero crossing in the older half so the trace stands still
    int start = (w - (int) trace.size() * 2 - 1 + size * 2) % size;
    for (int k = 0; k < size / 2; ++k)
    {
        const int i = (start - k + size) % size;
        const int j = (i - 1 + size) % size;
        if (proc.scope[(size_t) j].load() < 0.0f && proc.scope[(size_t) i].load() >= 0.0f)
        {
            start = i;
            break;
        }
    }
    for (size_t i = 0; i < trace.size(); ++i)
        trace[i] = proc.scope[(size_t) ((start + (int) i * 2) % size)].load (std::memory_order_relaxed);
    levelL = jmax (proc.outputLevelL.load(), levelL * 0.85f);
    levelR = jmax (proc.outputLevelR.load(), levelR * 0.85f);
    repaint();
}

void ScopeView::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawScreen (g, r);
    auto meter = r.removeFromRight (14.0f).reduced (3.0f, 4.0f);
    auto area = r.reduced (4.0f, 3.0f);

    Path p;
    for (size_t i = 0; i < trace.size(); ++i)
    {
        const float x = area.getX() + area.getWidth() * (float) i / (float) (trace.size() - 1);
        const float y = area.getCentreY() - jlimit (-1.0f, 1.0f, trace[i]) * area.getHeight() * 0.48f;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (oscAccent.withAlpha (0.25f));
    g.strokePath (p, PathStrokeType (3.0f));
    g.setColour (oscAccent);
    g.strokePath (p, PathStrokeType (1.2f));

    auto bar = [&] (Rectangle<float> b, float level)
    {
        g.setColour (Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (b, 1.5f);
        const float db = jlimit (-60.0f, 0.0f, 20.0f * std::log10 (jmax (1.0e-5f, level)));
        auto fill = b.withTrimmedTop (b.getHeight() * (-db / 60.0f));
        g.setColour (level > 0.89f ? Colour (0xffff5c5c) : oscAccent.interpolatedWith (macroAccent, (db + 60.0f) / 60.0f));
        g.fillRoundedRectangle (fill, 1.5f);
    };
    bar (meter.removeFromLeft (meter.getWidth() * 0.5f).reduced (0.5f, 0.0f), levelL);
    bar (meter.reduced (0.5f, 0.0f), levelR);
}
} // namespace ers::ui
