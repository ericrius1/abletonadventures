#include "Modulation.h"
#include <aakit/dsp/Core.h>

using namespace juce;

namespace ers
{
//==============================================================================
StringArray LfoShape::presetNames()
{
    return { "Sine", "Triangle", "Saw Down", "Saw Up", "Square", "Pulse", "Steps", "Exp Decay", "Bounce",
             "Stairs", "Random" };
}

LfoShape LfoShape::preset (Preset p, int seed)
{
    LfoShape s;
    auto& pts = s.points;
    switch (p)
    {
        case Preset::sine:
            // Four bent segments (exponent 2) approximate a sine to within a few percent.
            pts = { { 0.0f, 0.5f, 0.25f }, { 0.25f, 1.0f, -0.25f }, { 0.5f, 0.5f, 0.25f }, { 0.75f, 0.0f, -0.25f }, { 1.0f, 0.5f, 0.0f } };
            break;
        case Preset::triangle:
            pts = { { 0.0f, 0.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
            break;
        case Preset::sawDown:
            pts = { { 0.0f, 1.0f, 0.0f }, { 0.995f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
            break;
        case Preset::sawUp:
            pts = { { 0.0f, 0.0f, 0.0f }, { 0.995f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
            break;
        case Preset::square:
            pts = { { 0.0f, 1.0f, 0.0f }, { 0.495f, 1.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, { 0.995f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
            break;
        case Preset::pulse:
            pts = { { 0.0f, 1.0f, 0.0f }, { 0.12f, 1.0f, 0.0f }, { 0.125f, 0.0f, 0.0f }, { 0.995f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
            break;
        case Preset::steps:
        {
            const float ys[] = { 0.0f, 0.5f, 0.25f, 0.75f, 1.0f, 0.5f, 0.75f, 0.25f };
            for (int i = 0; i < 8; ++i)
            {
                pts.push_back ({ (float) i / 8.0f, ys[i], 0.0f });
                pts.push_back ({ (float) (i + 1) / 8.0f - 0.004f, ys[i], 0.0f });
            }
            pts.push_back ({ 1.0f, 0.0f, 0.0f });
            break;
        }
        case Preset::expDecay:
            pts = { { 0.0f, 1.0f, 0.8f }, { 1.0f, 0.0f, 0.0f } };
            break;
        case Preset::bounce:
            pts = { { 0.0f, 1.0f, 0.6f }, { 0.4f, 0.0f, -0.6f }, { 0.6f, 0.5f, 0.6f }, { 0.8f, 0.0f, -0.6f },
                    { 0.9f, 0.2f, 0.6f }, { 1.0f, 0.0f, 0.0f } };
            break;
        case Preset::stairs:
            for (int i = 0; i < 4; ++i)
            {
                const float y = (float) i / 3.0f;
                pts.push_back ({ (float) i / 4.0f, y, 0.0f });
                pts.push_back ({ (float) (i + 1) / 4.0f - 0.004f, y, 0.0f });
            }
            pts.push_back ({ 1.0f, 0.0f, 0.0f });
            break;
        case Preset::random:
        {
            aa::dsp::Rng rng ((uint32_t) (seed * 2654435761u + 12345u));
            const int n = 6;
            const float first = rng.next01();
            pts.push_back ({ 0.0f, first, rng.nextBipolar() * 0.5f });
            for (int i = 1; i < n; ++i)
                pts.push_back ({ (float) i / (float) n, rng.next01(), rng.nextBipolar() * 0.5f });
            pts.push_back ({ 1.0f, first, 0.0f });
            break;
        }
        case Preset::count:
            break;
    }
    s.sanitise();
    return s;
}

float LfoShape::curveSegment (float t, float curve) noexcept
{
    // Power curve, symmetric for +/- bends. curve > 0 bows the segment towards its start value.
    if (std::abs (curve) < 1.0e-3f)
        return t;
    const float k = std::pow (2.0f, std::abs (curve) * 4.0f);   // exponent 1 .. 16
    return curve > 0.0f ? 1.0f - std::pow (1.0f - t, k) : std::pow (t, k);
}

float LfoShape::evaluate (float x) const noexcept
{
    if (points.empty())
        return 0.5f;
    if (x <= points.front().x)
        return points.front().y;

    for (size_t i = 0; i + 1 < points.size(); ++i)
    {
        const auto& a = points[i];
        const auto& b = points[i + 1];
        if (x <= b.x)
        {
            const float w = b.x - a.x;
            const float t = w <= 1.0e-6f ? 1.0f : (x - a.x) / w;
            return a.y + (b.y - a.y) * curveSegment (t, a.curve);
        }
    }
    return points.back().y;
}

void LfoShape::render (float* table) const
{
    for (int i = 0; i <= lfoTableSize; ++i)
        table[i] = evaluate ((float) i / (float) lfoTableSize);
}

void LfoShape::sanitise()
{
    if (points.size() < 2)
        points = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };

    for (auto& p : points)
    {
        p.x = jlimit (0.0f, 1.0f, p.x);
        p.y = jlimit (0.0f, 1.0f, p.y);
        p.curve = jlimit (-1.0f, 1.0f, p.curve);
    }
    std::stable_sort (points.begin(), points.end(), [] (const Point& a, const Point& b) { return a.x < b.x; });
    points.front().x = 0.0f;
    points.back().x = 1.0f;
    if (points.size() > 64)
        points.resize (64);
}

bool LfoShape::operator== (const LfoShape& other) const noexcept
{
    if (points.size() != other.points.size())
        return false;
    for (size_t i = 0; i < points.size(); ++i)
        if (points[i].x != other.points[i].x || points[i].y != other.points[i].y || points[i].curve != other.points[i].curve)
            return false;
    return true;
}

ValueTree LfoShape::toValueTree (const Identifier& type) const
{
    ValueTree t (type);
    String s;
    for (const auto& p : points)
        s << String (p.x, 4) << "," << String (p.y, 4) << "," << String (p.curve, 3) << ";";
    t.setProperty ("points", s, nullptr);
    return t;
}

LfoShape LfoShape::fromValueTree (const ValueTree& tree)
{
    LfoShape shape;
    const auto items = StringArray::fromTokens (tree.getProperty ("points").toString(), ";", {});
    for (const auto& item : items)
    {
        const auto v = StringArray::fromTokens (item, ",", {});
        if (v.size() >= 2)
            shape.points.push_back ({ v[0].getFloatValue(), v[1].getFloatValue(), v.size() > 2 ? v[2].getFloatValue() : 0.0f });
    }
    if (shape.points.size() < 2)
        return preset (Preset::sine);
    shape.sanitise();
    return shape;
}

//==============================================================================
int ModMatrix::find (ModSource source, int dest) const
{
    for (int i = 0; i < maxModSlots; ++i)
        if (slots[(size_t) i].source.load() == (int) source && slots[(size_t) i].dest.load() == dest)
            return i;
    return -1;
}

int ModMatrix::add (ModSource source, int dest, float amount)
{
    int index = find (source, dest);
    if (index < 0)
    {
        for (int i = 0; i < maxModSlots && index < 0; ++i)
            if (slots[(size_t) i].source.load() == (int) ModSource::none || slots[(size_t) i].dest.load() < 0)
                index = i;
        if (index < 0)
            return -1;

        auto& s = slots[(size_t) index];
        s.aux = (int) ModSource::none;
        s.bipolar = false;
        s.dest = dest;
        s.source = (int) source;
    }
    slots[(size_t) index].amount = jlimit (-1.0f, 1.0f, amount);
    ++version;
    return index;
}

void ModMatrix::remove (int slotIndex)
{
    if (isPositiveAndBelow (slotIndex, maxModSlots))
    {
        slots[(size_t) slotIndex].clear();
        ++version;
    }
}

void ModMatrix::clear()
{
    for (auto& s : slots)
        s.clear();
    ++version;
}

std::vector<int> ModMatrix::slotsForDest (int dest) const
{
    std::vector<int> r;
    for (int i = 0; i < maxModSlots; ++i)
        if (slots[(size_t) i].dest.load() == dest && slots[(size_t) i].source.load() != (int) ModSource::none)
            r.push_back (i);
    return r;
}

int ModMatrix::snapshot (std::array<ModRoute, maxModSlots>& out) const noexcept
{
    int n = 0;
    for (const auto& s : slots)
    {
        if (! s.isActive())
            continue;
        auto& r = out[(size_t) n++];
        r.source = (ModSource) s.source.load (std::memory_order_relaxed);
        r.dest = s.dest.load (std::memory_order_relaxed);
        r.amount = s.amount.load (std::memory_order_relaxed);
        r.aux = (ModSource) s.aux.load (std::memory_order_relaxed);
        r.bipolar = s.bipolar.load (std::memory_order_relaxed);
        if (r.dest >= (int) modDests().size() || (int) r.source <= 0 || (int) r.source >= numModSources)
            --n;
    }
    return n;
}

ValueTree ModMatrix::toValueTree() const
{
    ValueTree t ("ModMatrix");
    for (const auto& s : slots)
    {
        if (s.source.load() == (int) ModSource::none || s.dest.load() < 0)
            continue;
        ValueTree slot ("Slot");
        slot.setProperty ("source", modSourceName ((ModSource) s.source.load()), nullptr);
        slot.setProperty ("dest", modDests()[(size_t) s.dest.load()].paramId, nullptr);
        slot.setProperty ("amount", s.amount.load(), nullptr);
        if (s.aux.load() != (int) ModSource::none)
            slot.setProperty ("aux", modSourceName ((ModSource) s.aux.load()), nullptr);
        if (s.bipolar.load())
            slot.setProperty ("bipolar", true, nullptr);
        t.appendChild (slot, nullptr);
    }
    return t;
}

void ModMatrix::fromValueTree (const ValueTree& tree)
{
    auto sourceFromName = [] (const String& name)
    {
        for (int i = 0; i < numModSources; ++i)
            if (modSourceName ((ModSource) i) == name)
                return (ModSource) i;
        return ModSource::none;
    };

    for (auto& s : slots)
        s.clear();

    int index = 0;
    for (auto slot : tree)
    {
        if (index >= maxModSlots)
            break;
        const auto source = sourceFromName (slot.getProperty ("source").toString());
        const int dest = modDestIndex (slot.getProperty ("dest").toString());
        if (source == ModSource::none || dest < 0)
            continue;
        auto& s = slots[(size_t) index++];
        s.dest = dest;
        s.amount = jlimit (-1.0f, 1.0f, (float) slot.getProperty ("amount", 0.0f));
        s.aux = (int) sourceFromName (slot.getProperty ("aux").toString());
        s.bipolar = (bool) slot.getProperty ("bipolar", false);
        s.source = (int) source;
    }
    ++version;
}
} // namespace ers
