#pragma once

#include "GremlinWidgets.h"

namespace gremlin
{
/** The gremlin itself, peeking up from the bottom of a little CRT monitor. It reacts to
    whatever it is doing to the audio: jittery for stutter, mirrored for reverse, spiral eyes
    for tape stop, sleepy for half speed, pixelated for bitcrush, chomping for gate... */
class GremlinFace : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit GremlinFace (GremlinProcessor& p);

    void tick (double dt);
    void onStep (const StepEvent& e);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    struct Pose
    {
        float eyeOpen = 1.0f, lookX = 0.0f, lookY = 0.0f, pupil = 1.0f;
        float browL = 0.35f, browR = -0.25f;
        float mouthOpen = 0.15f, grin = 0.5f;
        float earL = 0.0f, earR = 0.0f;
        float tilt = 0.0f, bob = 0.0f;
        float spiral = 0.0f, sleepy = 0.0f, flip = 1.0f;
        float pixel = 0.0f, glitch = 0.0f, ghost = 0.0f, happy = 0.0f;
    };

    struct Particle
    {
        juce::Point<float> pos, vel;
        float age = 0.0f, life = 1.0f, size = 8.0f;
        int kind = 0; // 0 = Z, 1 = pixel, 2 = spark
        juce::Colour colour;
    };

    void drawCharacter (juce::Graphics& g, const Pose& p, juce::Colour eyeCol) const;
    void drawEye (juce::Graphics& g, const Pose& p, float side, juce::Colour eyeCol) const;
    void drawMouth (juce::Graphics& g, const Pose& p) const;
    void drawHands (juce::Graphics& g, const Pose& p) const;
    void renderCharacterImage (juce::Image& img, float pixelsPerUnit, juce::Colour eyeCol) const;
    void rebuildStatic (float scale);
    juce::AffineTransform characterTransform() const;
    juce::String statusText() const;
    juce::Colour eyeColour() const;

    GremlinProcessor& processor;
    Pose pose;
    Fx shownFx = Fx::none;
    bool shownForced = false;
    float holdTimer = 0.0f;

    double clock = 0.0, animTime = 0.0;
    float blinkTimer = 2.0f, blinkPhase = 0.0f;
    float lookTimer = 1.0f;
    juce::Point<float> lookTarget;
    float earTwitchL = 0.0f, earVelL = 0.0f, earTwitchR = 0.0f, earVelR = 0.0f, twitchTimer = 3.0f;
    float hair = 0.0f, hairVel = 0.0f;
    float hop = 0.0f, hopVel = 0.0f;
    float tap = 0.0f;
    float aura = 0.0f;
    float spiralAngle = 0.0f;
    juce::Point<float> jitter;
    int lastRepeat = -1;
    float glitchTimer = 0.0f;
    std::array<float, 12> stripOffsets {};
    float pokeTimer = 0.0f;
    int idleMessage = 0;
    float idleTimer = 0.0f;
    float zTimer = 0.0f;
    int lastSlices = 4, lastScatter = 1;
    std::vector<Particle> particles;
    juce::Random random;

    juce::Image backdrop, overlay, faceImage, pixelImage;
    juce::Rectangle<float> bezel, screen, faceArea, statusArea;
    float cachedScale = 0.0f;
};
} // namespace gremlin
