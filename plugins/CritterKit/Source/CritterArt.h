#pragma once

#include <juce_graphics/juce_graphics.h>

/** Vector artwork for the eight critters (juce::Graphics only, no bitmaps). */
namespace critter
{
namespace colours
{
    const juce::Colour ink { 0xff3a2d5c };
    const juce::Colour cream { 0xfffff6ea };
    const juce::Colour lilac { 0xffeadcff };
    const juce::Colour blush { 0xffff7aa8 };

    inline juce::Colour critter (int v)
    {
        static const juce::uint32 c[] = { 0xffffc23d, 0xffff6f61, 0xff4fd49a, 0xff5bc0ff,
                                          0xffa98bff, 0xffff9a4d, 0xffff85c0, 0xff9be15d };
        return juce::Colour (c[juce::jlimit (0, 7, v)]);
    }

    /** A deeper version of the critter colour that reads well as text / arcs on the cream background. */
    inline juce::Colour deep (int v)
    {
        auto c = critter (v);
        return c.withSaturation (juce::jmin (1.0f, c.getSaturation() * 1.1f)).withBrightness (c.getBrightness() * 0.78f);
    }
}

struct Pose
{
    float squash = 0.0f;            // +: squashed flat, -: stretched tall
    float bob = 0.0f;               // lift off the ground (px)
    float mouth = 0.0f;             // 0 closed .. 1 wide open
    float blink = 0.0f;             // 0 open .. 1 closed
    float excited = 0.0f;           // > 0.5: happy ^ ^ eyes
    float action = 0.0f;            // 0..1: the critter's party trick (clap, flap, hiss, ring...)
    float time = 0.0f;              // seconds, for wiggles
    juce::Point<float> look;        // -1..1 where the pupils point
};

/** Draws a critter standing on the bottom-centre of `area`. */
void drawCritter (juce::Graphics& g, int kind, juce::Rectangle<float> area, const Pose& pose);

/** Little 4-point sparkle used for particles and accents. */
juce::Path sparklePath (juce::Point<float> centre, float radius, float angle = 0.0f);
} // namespace critter
