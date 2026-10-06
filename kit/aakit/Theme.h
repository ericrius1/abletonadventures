#pragma once

#include <juce_graphics/juce_graphics.h>

namespace aa
{
/** The colour story for one plugin. Every widget in the kit draws itself from these. */
struct Theme
{
    juce::Colour background   { 0xff15131f };   // main backdrop
    juce::Colour backgroundAlt{ 0xff241f36 };   // second gradient stop
    juce::Colour panel        { 0x33ffffff };   // translucent section fill
    juce::Colour panelOutline { 0x22ffffff };
    juce::Colour text         { 0xfff3f0ff };
    juce::Colour textDim      { 0x99f3f0ff };
    juce::Colour accent       { 0xff7cf2ff };   // primary highlight (value arcs, active states)
    juce::Colour accent2      { 0xffff7ce0 };   // secondary highlight (gradients, alt states)
    juce::Colour knobBody     { 0xff2c2742 };
    juce::Colour knobTrack    { 0x55000000 };
    juce::Colour shadow       { 0x88000000 };
    juce::Colour popupBackground { 0xf01c1a28 };
    juce::Colour pill         { 0x47000000 };   // preset selector / small pill backgrounds

    float cornerRadius = 12.0f;
    bool glow = true;          // soft glow around value arcs
};

/** Small colour helpers used throughout the kit. */
inline juce::Colour mix (juce::Colour a, juce::Colour b, float t) { return a.interpolatedWith (b, juce::jlimit (0.0f, 1.0f, t)); }

inline juce::Colour withMul (juce::Colour c, float brightnessMul)
{
    return c.withBrightness (juce::jlimit (0.0f, 1.0f, c.getBrightness() * brightnessMul));
}
} // namespace aa
