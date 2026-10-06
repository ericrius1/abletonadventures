#pragma once

#include "OrreryProcessor.h"
#include "OrreryLookAndFeel.h"

/** The animated brass orrery: sun, orbits with their trigger marks, planets with comet trails,
    ripples and floating note names whenever a planet strikes a mark. */
class OrreryStage : public juce::Component, public juce::TooltipClient
{
public:
    explicit OrreryStage (OrreryProcessor& p);

    void tick (double dt);
    void setSelected (int orbit);

    std::function<void (int)> onSelect;

    /** Smoothed musical position used for drawing (quarter notes). */
    double getVisualPpq() const noexcept { return visPpq; }
    /** 0..1 position of a planet within its revolution. */
    float getPlanetPhase (int orbit) const;
    bool isRunning() const noexcept { return running; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    juce::String getTooltip() override;

private:
    struct Ripple
    {
        juce::Point<float> pos;
        float age = 0.0f, strength = 1.0f;
        juce::Colour colour;
    };

    struct FloatingLabel
    {
        juce::Point<float> pos;
        float age = 0.0f;
        juce::String text;
        juce::Colour colour;
        bool emphasised = false;
    };

    struct Spark
    {
        juce::Point<float> pos, vel;
        float life = 1.0f, size = 2.0f;
        juce::Colour colour;
    };

    struct Twinkle
    {
        juce::Point<float> pos;
        float size, phase, speed, brightness;
    };

    float unit() const noexcept { return juce::jmin (getWidth(), getHeight()) / 528.0f; }
    juce::Point<float> centre() const noexcept { return getLocalBounds().toFloat().getCentre(); }
    float orbitRadius (int i) const noexcept { return (63.0f + 30.5f * (float) i) * unit(); }
    float bezelInner() const noexcept { return 237.0f * unit(); }
    float bezelOuter() const noexcept { return 252.0f * unit(); }
    juce::Point<float> polar (float radius, float angle) const noexcept;
    float markerAngle (int orbit, int pulse) const;
    int orbitAt (juce::Point<float> pos) const;
    void activate (const OrreryProcessor::NoteEvent& ev);
    void setParam (const juce::String& id, float realValue);

    void rebuildBackground (float scale);
    void rebuildOrbitLayer (float scale);
    juce::int64 orbitSignature() const;
    void drawSun (juce::Graphics& g);
    void drawPlanet (juce::Graphics& g, int i, juce::Point<float> pos, bool enabled);
    void drawTrail (juce::Graphics& g, int i);

    OrreryProcessor& proc;

    // timing
    double visPpq = 0.0;
    int lastJumpCount = -1;
    bool running = false;
    double clock = 0.0;
    float bpm = 120.0f;
    int beatsPerBar = 4;

    // planets
    std::array<float, orrery::numOrbits> displayAngle {}, angleLag {}, trailLength {}, bump {};
    std::array<int, orrery::numOrbits> lastBeats {};
    std::array<double, orrery::numOrbits> lastSpeed {};
    std::array<juce::Point<float>, orrery::numOrbits> planetPos {};
    std::array<std::array<float, 16>, orrery::numOrbits> flash {};
    float sunPulse = 0.0f, beatPulse = 0.0f, sing = 0.0f, noteGlow = 0.0f;
    std::array<float, 16> beatLamp {};
    double lastBeatIndex = 0.0;

    std::vector<OrreryProcessor::NoteEvent> pending;
    std::vector<Ripple> ripples;
    std::vector<FloatingLabel> labels;
    std::vector<Twinkle> twinkles;
    std::vector<Spark> sparks;
    std::vector<juce::Path> numeralPaths;
    juce::Random random;

    int selected = 0, hovered = -1;
    bool hoverSun = false;

    // harmony caption (top-left corner)
    juce::String harmonyText;
    bool following = false;
    float followGlow = 0.0f;
    std::array<juce::uint32, 4> lastChordMask {};
    int lastScale = -1, lastRoot = -1;
    bool lastFollowOn = false;

    juce::Image background, orbitLayer;
    float backgroundScale = 0.0f, orbitScale = 0.0f;
    int backgroundBeats = 0;
    juce::int64 orbitLayerSignature = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrreryStage)
};
