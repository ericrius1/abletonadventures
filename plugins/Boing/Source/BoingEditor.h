#pragma once

#include "BoingProcessor.h"

/** The cartoon stage: shows the predicted bounce trajectory and animates a ball for every hit. */
class BounceStage : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit BounceStage (BoingProcessor& p);

    void tick (double dt);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override { hoverHint = true; }
    void mouseExit (const juce::MouseEvent&) override { hoverHint = false; }

    std::function<void()> onImpact; // fired whenever a ball lands (drives the title wiggle)

private:
    struct Ball
    {
        double age = 0.0;
        float size = 18.0f;
        int nextImpact = 0;
        float squash = 0.0f;
        float hue = 0.0f;
        boing::Schedule schedule;
        std::array<juce::Point<float>, 10> trail {};
        int trailCount = 0;
    };

    struct Ripple { juce::Point<float> pos; float age = 0.0f, strength = 1.0f; juce::Colour colour; };
    struct Dust { juce::Point<float> pos, vel; float life = 1.0f, size = 2.0f; };

    juce::Point<float> positionFor (const boing::Schedule& s, double t, float& heightNorm) const;
    float timeToX (double t, const boing::Schedule& s) const;
    float apexFor (double interval, const boing::Schedule& s) const;
    void spawnBall (float amplitude);
    void drawBall (juce::Graphics& g, juce::Point<float> c, float size, float squash, juce::Colour colour, float lookDir);
    void rebuildBackground (float scale);

    BoingProcessor& processor;
    std::vector<Ball> balls;
    std::vector<Ripple> ripples;
    std::vector<Dust> dust;
    std::array<float, boing::maxBounces> padFlash {};
    boing::Schedule preview;
    juce::Rectangle<float> floorArea, skyArea;
    juce::Image background;
    float cloudOffset = 0.0f;
    double clock = 0.0;
    bool hoverHint = false;
    juce::Random random;
};

class BoingEditor : public aa::EditorBase
{
public:
    explicit BoingEditor (BoingProcessor&);
    ~BoingEditor() override = default;

private:
    void paintContent (juce::Graphics&) override;
    void layoutContent() override;
    void onFrame (double now, double dt) override;
    void updateSyncState();

    BoingProcessor& proc;
    BounceStage stage { proc };

    aa::Knob time, bounciness, bounces, damping, tone, wobble, spread, rethrow, duck, mix;
    aa::Toggle sync;
    aa::ChoiceBox division, mode;
    aa::PresetSelector presets;

    std::array<float, 6> letterBounce {};
    std::array<float, 6> letterVel {};
    juce::Rectangle<float> titleArea;
    std::array<juce::Rectangle<float>, 4> panels;
};
