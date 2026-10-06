#pragma once

#include "StardustProcessor.h"

namespace stardust::ui
{
const juce::Colour navy { 0xff0b0d2b };
const juce::Colour violet { 0xff2a1650 };
const juce::Colour cyan { 0xff6ef2ff };
const juce::Colour magenta { 0xffff6ad5 };
const juce::Colour gold { 0xffffd36e };
const juce::Colour lavender { 0xffb79cff };
const juce::Colour starWhite { 0xfff4f2ff };

/** Draws a four-pointed sparkle (a "twinkle") with a soft halo. */
void drawSparkle (juce::Graphics& g, juce::Point<float> c, float size, juce::Colour colour, float alpha, float rotation = 0.0f);
} // namespace stardust::ui

/** The central viewport: a window into deep space. Parallax starfield, drifting nebula, a glowing
    stereo oscilloscope of the output, supernova bursts on every note and a twinkling star for every
    "Twinkle" glint the engine plays. */
class SpaceView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit SpaceView (StardustProcessor& p);

    void tick (double dt);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Star { float x, y, size, brightness, phase, rate; int layer; juce::Colour colour; };
    struct Particle { juce::Point<float> pos, vel; float life, maxLife, size; juce::Colour colour; };
    struct Ring { juce::Point<float> pos; float age, life, size; juce::Colour colour; };
    struct Sparkle { juce::Point<float> pos; float age, life, size, rotation, flicker, strength; juce::Colour colour; };
    struct Comet { juce::Point<float> pos, vel; float age = 0.0f, life = 0.0f; bool active = false; };

    void rebuildImages (float scale);
    void spawnBurst (int note, float velocity);
    void spawnSparkle (const stardust::GlintEvent& e);
    void updateScope();
    juce::Colour noteColour (int note) const;

    StardustProcessor& processor;
    std::vector<Star> stars;
    std::vector<Particle> particles;
    std::vector<Ring> rings;
    std::vector<Sparkle> sparkles;
    Comet comet;
    float cometTimer = 3.0f;

    static constexpr int scopePoints = 180;
    std::array<float, scopePoints> dispL {}, dispR {};
    float scopeGain = 1.0f, level = 0.0f, warp = 0.0f;

    juce::Image background, overlay;
    float imageScale = 0.0f;
    double clock = 0.0;
    int voices = 0;
    juce::String chordText;
    juce::Random random { 0x57A2 };
};
