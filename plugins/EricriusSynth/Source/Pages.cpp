#include "Views.h"

using namespace juce;

namespace ers::ui
{
namespace
{
    constexpr int rowsPerColumn = 16;
    constexpr float rowH = 28.0f;

    String destCategory (const ModDest& d)
    {
        if (d.vpIndex >= 0)
        {
            const int i = d.vpIndex;
            if (i < vp::subBase)        return "Osc " + String::charToString ((juce_wchar) ('A' + (i - vp::oscBase) / vp::oscFields));
            if (i < vp::filterBase)     return "Sub / Noise";
            if (i < vp::filterRouting)  return "Filter " + String (1 + (i - vp::filterBase) / vp::filterFields);
            if (i < vp::lfoBase)        return "Envelopes";
            if (i < vp::macroBase)      return "LFOs";
            return "Global";
        }
        return "FX: " + fx::kindName (fx::paramSpecs()[(size_t) d.fxIndex].owner);
    }

    const std::vector<ModSource>& handleSources()
    {
        static const std::vector<ModSource> s { ModSource::env1, ModSource::env2, ModSource::env3, ModSource::env4,
                                                ModSource::lfo1, ModSource::lfo2, ModSource::lfo3, ModSource::lfo4,
                                                ModSource::macro1, ModSource::macro2, ModSource::macro3, ModSource::macro4,
                                                ModSource::velocity, ModSource::note, ModSource::modWheel, ModSource::aftertouch,
                                                ModSource::pitchBend, ModSource::random, ModSource::alternate, ModSource::releaseVelocity };
        return s;
    }
} // namespace

//==============================================================================
// MatrixPage
//==============================================================================
MatrixPage::MatrixPage (SynthProcessor& p) : proc (p)
{
    for (auto s : handleSources())
    {
        auto* h = handles.add (new ModSourceHandle (s));
        addAndMakeVisible (h);
    }
}

std::vector<int> MatrixPage::visibleSlots() const
{
    std::vector<int> v;
    for (int i = 0; i < maxModSlots; ++i)
        if (proc.matrix.slots[(size_t) i].source.load() != (int) ModSource::none && proc.matrix.slots[(size_t) i].dest.load() >= 0)
            v.push_back (i);
    return v;
}

Rectangle<float> MatrixPage::rowArea (int row) const
{
    auto r = getLocalBounds().toFloat().reduced (12.0f, 0.0f).withTrimmedTop (92.0f);
    const float colW = r.getWidth() / 2.0f;
    const int col = row / rowsPerColumn, inCol = row % rowsPerColumn;
    return { r.getX() + colW * (float) col + (col == 1 ? 8.0f : 0.0f), r.getY() + rowH * (float) inCol, colW - 8.0f, rowH - 3.0f };
}

Rectangle<float> MatrixPage::cell (int row, Column c) const
{
    auto r = rowArea (row);
    const float w = r.getWidth();
    auto take = [&] (float fraction) { return r.removeFromLeft (w * fraction); };
    auto source = take (0.2f);
    auto aux = take (0.14f);
    auto amount = take (0.24f);
    auto bip = take (0.07f);
    auto dest = take (0.3f);
    switch (c)
    {
        case Column::source:  return source;
        case Column::aux:     return aux;
        case Column::amount:  return amount;
        case Column::bipolar: return bip;
        case Column::dest:    return dest;
        case Column::remove:  return r;
        case Column::none:    break;
    }
    return {};
}

MatrixPage::Column MatrixPage::hitColumn (int row, Point<float> p) const
{
    for (auto c : { Column::source, Column::aux, Column::amount, Column::bipolar, Column::dest, Column::remove })
        if (cell (row, c).contains (p))
            return c;
    return Column::none;
}

int MatrixPage::rowAt (Point<float> p) const
{
    for (int row = 0; row < maxModSlots; ++row)
        if (rowArea (row).contains (p))
            return row;
    return -1;
}

void MatrixPage::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawSection (g, r, "MODULATION MATRIX", otherAccent);

    g.setFont (aa::Fonts::ui (11.0f));
    g.setColour (textDim);
    g.drawText ("Drag a source onto any knob - or pick sources and destinations below. Alt-drag a knob to change its depth.",
                Rectangle<float> (r.getX() + 14.0f, r.getY() + 30.0f, r.getWidth() - 28.0f, 16.0f).toNearestInt(), Justification::centredLeft);

    // column headers
    for (int col = 0; col < 2; ++col)
    {
        const int row = col * rowsPerColumn;
        auto header = [&] (Column c, const char* text)
        {
            auto a = cell (row, c).translated (0.0f, -18.0f).withHeight (16.0f);
            g.setFont (aa::Fonts::uiBold (10.0f));
            g.setColour (textDim);
            g.drawText (text, a.toNearestInt(), Justification::centred);
        };
        header (Column::source, "SOURCE");
        header (Column::aux, "VIA");
        header (Column::amount, "AMOUNT");
        header (Column::bipolar, "BIP");
        header (Column::dest, "DESTINATION");
    }

    const auto slots = visibleSlots();
    const auto& dests = modDests();
    for (int row = 0; row < maxModSlots; ++row)
    {
        auto area = rowArea (row);
        const bool used = row < (int) slots.size();
        const bool addRow = row == (int) slots.size();
        if (! used && ! addRow)
        {
            g.setColour (Colours::white.withAlpha (0.015f));
            g.fillRoundedRectangle (area, 5.0f);
            continue;
        }

        g.setColour (Colours::white.withAlpha (used ? 0.045f : 0.025f));
        g.fillRoundedRectangle (area, 5.0f);

        if (addRow)
        {
            g.setColour (textDim);
            g.setFont (aa::Fonts::ui (11.0f));
            g.drawText ("+  add a routing", cell (row, Column::source).withRight (area.getRight()).reduced (8.0f, 0.0f).toNearestInt(),
                        Justification::centredLeft);
            continue;
        }

        const auto& slot = proc.matrix.slots[(size_t) slots[(size_t) row]];
        const auto src = (ModSource) slot.source.load();
        const auto aux = (ModSource) slot.aux.load();
        const float amt = slot.amount.load();
        const auto colour = sourceColour (src);

        auto sc = cell (row, Column::source).reduced (4.0f, 3.0f);
        g.setColour (colour.withAlpha (0.2f));
        g.fillRoundedRectangle (sc, 4.0f);
        g.setColour (colour);
        g.setFont (aa::Fonts::uiBold (11.0f));
        g.drawFittedText (modSourceName (src), sc.toNearestInt(), Justification::centred, 1);

        g.setColour (aux == ModSource::none ? textDim.withAlpha (0.5f) : sourceColour (aux));
        g.setFont (aa::Fonts::ui (10.5f));
        g.drawFittedText (aux == ModSource::none ? "-" : modSourceName (aux), cell (row, Column::aux).toNearestInt(), Justification::centred, 1);

        auto bar = cell (row, Column::amount).reduced (6.0f, 9.0f);
        g.setColour (Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (bar, 3.0f);
        const float mid = bar.getCentreX();
        const float end = mid + amt * bar.getWidth() * 0.5f;
        g.setColour (colour);
        g.fillRoundedRectangle (Rectangle<float> (jmin (mid, end), bar.getY(), std::abs (end - mid), bar.getHeight()), 3.0f);
        g.setColour (Colours::white.withAlpha (0.3f));
        g.drawVerticalLine ((int) mid, bar.getY() - 2.0f, bar.getBottom() + 2.0f);
        g.setColour (textBright);
        g.setFont (aa::Fonts::uiBold (10.0f));
        g.drawText ((amt >= 0.0f ? "+" : "") + String (roundToInt (amt * 100.0f)) + "%",
                    cell (row, Column::amount).translated (0.0f, 9.0f).toNearestInt(), Justification::centred);

        auto bc = cell (row, Column::bipolar).withSizeKeepingCentre (14.0f, 14.0f);
        g.setColour (slot.bipolar.load() ? colour : Colours::white.withAlpha (0.12f));
        g.fillRoundedRectangle (bc, 3.0f);

        const int d = slot.dest.load();
        g.setColour (textBright);
        g.setFont (aa::Fonts::ui (11.0f));
        g.drawFittedText (isPositiveAndBelow (d, (int) dests.size()) ? dests[(size_t) d].name : String ("?"),
                          cell (row, Column::dest).reduced (4.0f, 0.0f).toNearestInt(), Justification::centredLeft, 1);

        auto x = cell (row, Column::remove).withSizeKeepingCentre (12.0f, 12.0f);
        g.setColour (textDim);
        g.drawLine (x.getX(), x.getY(), x.getRight(), x.getBottom(), 1.5f);
        g.drawLine (x.getRight(), x.getY(), x.getX(), x.getBottom(), 1.5f);
    }
}

void MatrixPage::resized()
{
    auto r = getLocalBounds().reduced (14, 0).withTrimmedTop (50).removeFromTop (22);
    const int w = jmin (26, r.getWidth() / handles.size());
    for (auto* h : handles)
        h->setBounds (r.removeFromLeft (w).reduced (2));
}

void MatrixPage::showSourceMenu (int slot, bool aux)
{
    PopupMenu menu;
    if (aux)
        menu.addItem (1 + (int) ModSource::none, "None");
    for (int s = 1; s < numModSources; ++s)
        menu.addItem (1 + s, modSourceName ((ModSource) s));

    SafePointer<MatrixPage> safe (this);
    menu.showMenuAsync (PopupMenu::Options(), [safe, slot, aux] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        auto& m = safe->proc.matrix;
        const auto src = (ModSource) (result - 1);
        if (slot < 0)
        {
            // new routing: needs a destination too
            safe->showDestMenu (-1 - (int) src);
            return;
        }
        if (aux)
            m.slots[(size_t) slot].aux = (int) src;
        else
            m.slots[(size_t) slot].source = (int) src;
        ++m.version;
        safe->repaint();
    });
}

void MatrixPage::showDestMenu (int slot)
{
    // slot < 0 encodes "new routing with source (-1 - slot)"
    PopupMenu menu;
    const auto& dests = modDests();
    StringArray categories;
    for (const auto& d : dests)
        categories.addIfNotAlreadyThere (destCategory (d));
    for (const auto& c : categories)
    {
        PopupMenu sub;
        for (int i = 0; i < (int) dests.size(); ++i)
            if (destCategory (dests[(size_t) i]) == c)
                sub.addItem (1 + i, dests[(size_t) i].name);
        menu.addSubMenu (c, sub);
    }

    SafePointer<MatrixPage> safe (this);
    menu.showMenuAsync (PopupMenu::Options(), [safe, slot] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        auto& m = safe->proc.matrix;
        if (slot < 0)
            m.add ((ModSource) (-1 - slot), result - 1, 0.3f);
        else
        {
            m.slots[(size_t) slot].dest = result - 1;
            ++m.version;
        }
        safe->repaint();
    });
}

void MatrixPage::mouseDown (const MouseEvent& e)
{
    const int row = rowAt (e.position);
    if (row < 0)
        return;
    const auto slots = visibleSlots();
    if (row == (int) slots.size())
    {
        showSourceMenu (-1, false);
        return;
    }
    if (row > (int) slots.size())
        return;

    const int slot = slots[(size_t) row];
    auto& s = proc.matrix.slots[(size_t) slot];
    switch (hitColumn (row, e.position))
    {
        case Column::source:  showSourceMenu (slot, false); break;
        case Column::aux:     showSourceMenu (slot, true); break;
        case Column::dest:    showDestMenu (slot); break;
        case Column::bipolar: s.bipolar = ! s.bipolar.load(); ++proc.matrix.version; repaint(); break;
        case Column::remove:  proc.matrix.remove (slot); repaint(); break;
        case Column::amount:
            dragSlot = slot;
            dragStartAmount = s.amount.load();
            dragStartX = e.position.x;
            break;
        case Column::none: break;
    }
}

void MatrixPage::mouseDrag (const MouseEvent& e)
{
    if (dragSlot < 0)
        return;
    const float scale = e.mods.isShiftDown() ? 600.0f : 150.0f;
    proc.matrix.slots[(size_t) dragSlot].amount = jlimit (-1.0f, 1.0f, dragStartAmount + (e.position.x - dragStartX) / scale);
    ++proc.matrix.version;
    repaint();
}

void MatrixPage::mouseUp (const MouseEvent&)
{
    dragSlot = -1;
}

void MatrixPage::mouseDoubleClick (const MouseEvent& e)
{
    const int row = rowAt (e.position);
    const auto slots = visibleSlots();
    if (row >= 0 && row < (int) slots.size() && hitColumn (row, e.position) == Column::amount)
    {
        proc.matrix.slots[(size_t) slots[(size_t) row]].amount = 0.0f;
        ++proc.matrix.version;
        repaint();
    }
}

void MatrixPage::refresh()
{
    const int v = proc.matrix.version.load();
    if (v != version)
    {
        version = v;
        repaint();
    }
}

//==============================================================================
// FxPage
//==============================================================================
namespace
{
    String fxDescription (fx::Kind k)
    {
        switch (k)
        {
            case fx::Kind::hyper:      return "Lush unison-style chorus plus a dimension widener.";
            case fx::Kind::distortion: return "Eleven flavours of saturation, folding and crushing, with a tone filter.";
            case fx::Kind::flanger:    return "Classic swept comb flanging with stereo phase.";
            case fx::Kind::phaser:     return "Swirling all-pass phaser.";
            case fx::Kind::chorus:     return "Warm, wide dual-delay chorus.";
            case fx::Kind::delay:      return "Stereo, ping-pong and tap delays, free or tempo-synced.";
            case fx::Kind::compressor: return "Single-band compressor or punchy three-band up/down compression.";
            case fx::Kind::reverb:     return "Plate, hall and room reverbs.";
            case fx::Kind::eq:         return "Low shelf, peak and high shelf.";
            case fx::Kind::filter:     return "Every synth filter type, as an effect.";
            case fx::Kind::count:      break;
        }
        return {};
    }
}

FxPage::FxPage (SynthProcessor& p) : proc (p)
{
    select (fx::Kind::hyper);
}

bool FxPage::isOn (fx::Kind k) const
{
    if (auto* p = proc.param (fx::kindPrefix (k) + "On"))
        return p->getValue() > 0.5f;
    return false;
}

Rectangle<float> FxPage::slotArea (int index) const
{
    auto r = getLocalBounds().toFloat().reduced (12.0f, 0.0f).withTrimmedTop (34.0f);
    r = r.removeFromLeft (230.0f);
    const float h = jmin (40.0f, (r.getHeight() - 8.0f) / (float) fx::numKinds);
    return { r.getX(), r.getY() + h * (float) index, r.getWidth(), h - 4.0f };
}

Rectangle<float> FxPage::powerArea (int index) const
{
    auto s = slotArea (index);
    return s.removeFromRight (34.0f).withSizeKeepingCentre (20.0f, 20.0f);
}

void FxPage::select (fx::Kind k)
{
    selected = k;
    controls.clear();
    knobs.clear();

    const auto& specs = fx::paramSpecs();
    for (int i : fx::specsFor (k))
    {
        const auto& s = specs[(size_t) i];
        if (s.id == fx::kindPrefix (k) + "On")
        {
            auto* power = new PowerButton (proc.apvts, s.id, fxAccent);
            controls.add (power);
            addAndMakeVisible (power);
            continue;
        }
        Component* c = nullptr;
        if (s.type == fx::ParamSpec::Type::floating)
        {
            auto* knob = new ModKnob (proc, s.id, s.label, fxAccent);
            knobs.push_back (knob);
            c = knob;
        }
        else if (s.type == fx::ParamSpec::Type::choice)
        {
            auto* box = new aa::ChoiceBox (proc.apvts, s.id, s.label, aa::ChoiceBox::Style::stepper);
            box->setAccent (fxAccent);
            c = box;
        }
        else
        {
            auto* t = new aa::Toggle (proc.apvts, s.id, s.label);
            t->setAccent (fxAccent);
            c = t;
        }
        controls.add (c);
        addAndMakeVisible (c);
    }
    resized();
    repaint();
}

void FxPage::collectKnobs (std::vector<ModKnob*>&) {}

void FxPage::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawSection (g, r, "EFFECTS", fxAccent);

    const auto order = proc.getFxOrder();
    for (int i = 0; i < fx::numKinds; ++i)
    {
        auto s = slotArea (i);
        if (dragIndex >= 0 && i == dragTarget)
        {
            g.setColour (fxAccent);
            g.fillRect (s.getX(), (i > dragIndex ? s.getBottom() + 1.0f : s.getY() - 3.0f), s.getWidth(), 2.0f);
        }
        const auto k = order[(size_t) i];
        const bool sel = k == selected;
        const bool on = isOn (k);
        g.setColour (sel ? fxAccent.withAlpha (0.16f) : Colours::white.withAlpha (0.035f));
        g.fillRoundedRectangle (s, 6.0f);
        g.setColour (sel ? fxAccent.withAlpha (0.8f) : panelEdge);
        g.drawRoundedRectangle (s.reduced (0.5f), 6.0f, 1.0f);

        // grip
        g.setColour (textDim.withAlpha (0.6f));
        for (int k2 = 0; k2 < 3; ++k2)
            g.fillRect (s.getX() + 10.0f, s.getCentreY() - 4.0f + (float) k2 * 4.0f, 10.0f, 1.5f);

        g.setFont (aa::Fonts::display (12.5f));
        g.setColour (on ? textBright : textDim);
        g.drawText (fx::kindName (k).toUpperCase(), s.withTrimmedLeft (30.0f).toNearestInt(), Justification::centredLeft);

        auto pw = powerArea (i);
        g.setColour (on ? fxAccent : Colours::white.withAlpha (0.15f));
        g.fillEllipse (pw.reduced (5.0f));
        if (on)
        {
            g.setColour (fxAccent.withAlpha (0.25f));
            g.fillEllipse (pw);
        }
    }

    // right side: selected effect
    auto right = r.withTrimmedLeft (262.0f).withTrimmedTop (34.0f).reduced (12.0f, 6.0f);
    g.setColour (Colours::black.withAlpha (0.2f));
    g.fillRoundedRectangle (right, 8.0f);
    g.setFont (aa::Fonts::display (20.0f));
    g.setColour (isOn (selected) ? fxAccent : textDim);
    g.drawText (fx::kindName (selected).toUpperCase(), right.reduced (48.0f, 10.0f).removeFromTop (26.0f).toNearestInt(), Justification::centredLeft);
    g.setFont (aa::Fonts::ui (11.5f));
    g.setColour (textDim);
    g.drawText (fxDescription (selected), right.reduced (48.0f, 10.0f).withTrimmedTop (26.0f).removeFromTop (18.0f).toNearestInt(),
                Justification::centredLeft);

    if (selected == fx::Kind::compressor)
    {
        const float gr = jlimit (0.0f, 24.0f, proc.getFxRack().compressorGainReductionDb.load());
        auto meter = right.reduced (14.0f).removeFromRight (16.0f).withTrimmedTop (60.0f);
        g.setColour (Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (meter, 3.0f);
        g.setColour (filterAccent);
        g.fillRoundedRectangle (meter.withHeight (meter.getHeight() * gr / 24.0f), 3.0f);
        g.setFont (aa::Fonts::ui (9.5f));
        g.setColour (textDim);
        g.drawText ("GR", meter.translated (0.0f, -14.0f).withHeight (12.0f).expanded (8.0f, 0.0f).toNearestInt(), Justification::centred);
    }
}

void FxPage::resized()
{
    auto right = getLocalBounds().toFloat().withTrimmedLeft (262.0f).withTrimmedTop (34.0f).reduced (12.0f, 6.0f).toNearestInt();
    if (controls.isEmpty())
        return;
    // the first control is the power button
    controls[0]->setBounds (right.getX() + 14, right.getY() + 12, 24, 24);

    auto area = right.reduced (24, 0).withTrimmedTop (66).withTrimmedBottom (10);
    if (selected == fx::Kind::compressor)
        area = area.withTrimmedRight (30);
    const int n = controls.size() - 1;
    const int columns = jmin (6, jmax (1, n));
    const int rows = (n + columns - 1) / columns;
    const int cellW = area.getWidth() / columns;
    const int cellH = jmin (90, area.getHeight() / jmax (1, rows));
    for (int i = 0; i < n; ++i)
    {
        auto cellArea = Rectangle<int> (area.getX() + (i % columns) * cellW, area.getY() + (i / columns) * cellH, cellW, cellH);
        auto* c = controls[i + 1];
        if (dynamic_cast<ModKnob*> (c) != nullptr)
            c->setBounds (cellArea.withSizeKeepingCentre (jmin (cellW, 76), jmin (cellH, 84)));
        else
            c->setBounds (cellArea.withSizeKeepingCentre (jmin (cellW - 8, 130), 40));
    }
}

void FxPage::mouseDown (const MouseEvent& e)
{
    const auto order = proc.getFxOrder();
    for (int i = 0; i < fx::numKinds; ++i)
    {
        if (powerArea (i).contains (e.position))
        {
            if (auto* p = proc.param (fx::kindPrefix (order[(size_t) i]) + "On"))
                p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
            repaint();
            return;
        }
        if (slotArea (i).contains (e.position))
        {
            if (order[(size_t) i] != selected)
                select (order[(size_t) i]);
            dragIndex = i;
            dragTarget = i;
            dragY = e.position.y;
            return;
        }
    }
}

void FxPage::mouseDrag (const MouseEvent& e)
{
    if (dragIndex < 0 || std::abs (e.position.y - dragY) < 4.0f)
        return;
    int target = dragIndex;
    for (int i = 0; i < fx::numKinds; ++i)
        if (e.position.y >= slotArea (i).getY() - 2.0f)
            target = i;
    if (target != dragTarget)
    {
        dragTarget = target;
        repaint();
    }
}

void FxPage::mouseUp (const MouseEvent&)
{
    if (dragIndex >= 0 && dragTarget >= 0 && dragTarget != dragIndex)
    {
        auto order = proc.getFxOrder();
        const auto k = order[(size_t) dragIndex];
        std::vector<fx::Kind> v (order.begin(), order.end());
        v.erase (v.begin() + dragIndex);
        v.insert (v.begin() + dragTarget, k);
        std::copy (v.begin(), v.end(), order.begin());
        proc.setFxOrder (order);
    }
    dragIndex = dragTarget = -1;
    repaint();
}

void FxPage::refresh()
{
    for (auto* k : knobs)
        k->refreshModulation();
    if (selected == fx::Kind::compressor)
        repaint (getLocalBounds().withTrimmedLeft (getWidth() - 80));
    // power states can change from automation
    static int counter = 0;
    if (++counter % 10 == 0)
        repaint (slotArea (0).getUnion (slotArea (fx::numKinds - 1)).toNearestInt().expanded (2));
}
} // namespace ers::ui
