#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** Shared palette and vector drawing helpers for Crystal Cave's artwork. */
namespace cave
{
namespace colours
{
    const juce::Colour night     { 0xff0a1430 };
    const juce::Colour nightAlt  { 0xff160f33 };
    const juce::Colour ice       { 0xff8fe3ff };
    const juce::Colour violet    { 0xffb28cff };
    const juce::Colour frost     { 0xffeafcff };
    const juce::Colour deep      { 0xff131d47 };
    const juce::Colour rock      { 0xff0d1636 };
    const juce::Colour lilac     { 0xffd6b8ff };
    const juce::Colour text      { 0xffe6f6ff };
} // namespace colours

/** One crystal shard: a hexagonal prism seen from the side, growing from `base` towards `angle`
    (0 = straight up, clockwise radians). */
struct Crystal
{
    juce::Point<float> base;
    float height = 60.0f, width = 18.0f, angle = 0.0f;
    float hue = 0.0f;      // 0 = ice blue .. 1 = violet
    float tipSkew = 0.05f; // horizontal offset of the tip (fraction of width)

    juce::Point<float> tip() const;
    juce::Point<float> centre() const;
    juce::Colour colour() const;
};

/** Faceted crystal. brightness ~1 = normal; glow > 0 adds a halo and stronger highlights. */
void drawCrystal (juce::Graphics& g, const Crystal& c, float brightness, float glow, float alpha = 1.0f);

/** Four-pointed star glint with a soft halo. */
void drawGlint (juce::Graphics& g, juce::Point<float> centre, float size, juce::Colour colour, float rotation = 0.0f);

/** Six-armed snowflake outline (stroked). */
juce::Path snowflakePath (juce::Point<float> centre, float radius);

/** A small faceted octagonal gem (header icon). */
void drawGemIcon (juce::Graphics& g, juce::Point<float> centre, float radius);
} // namespace cave
