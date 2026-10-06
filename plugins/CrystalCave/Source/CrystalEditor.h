#pragma once

#include "CrystalProcessor.h"
#include "CaveArt.h"

//==============================================================================
/** The glowing cave: crystals that brighten with the reverb tail, sparkles that rise with Shimmer,
    a breathing light at the cave mouth and the decay envelope drawn inside it. Frosts over on Freeze. */
class CaveStage : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit CaveStage (CrystalProcessor& p);

    void tick (double dt);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; }
    void mouseExit (const juce::MouseEvent&) override { hover = false; }

private:
    struct Sparkle
    {
        juce::Point<float> pos, vel;
        float age = 0.0f, life = 2.0f, size = 2.0f, twinkle = 3.0f, hue = 0.0f, swayPhase = 0.0f, spin = 0.0f;
    };
    struct Glint { juce::Point<float> pos; float age = 0.0f, life = 0.6f, size = 6.0f, rot = 0.0f; float hue = 0.0f; };
    struct TailDot { float age = 0.0f, amp = 1.0f; };

    struct CurveKey
    {
        float size = -1.0f, decay = -1.0f, damping = -1.0f, predelay = -1.0f, shimmer = -1.0f;
        int interval = -1;
        bool freeze = false;
        bool operator== (const CurveKey& o) const
        {
            return size == o.size && decay == o.decay && damping == o.damping && predelay == o.predelay
                   && shimmer == o.shimmer && interval == o.interval && freeze == o.freeze;
        }
    };

    void buildScene();
    void rebuildImages (float scale);
    void rebuildCurve (float scale);
    CurveKey currentKey() const;

    float timeToX (float seconds) const;
    float dbToY (float db) const;
    float envelopeDb (float t, float rt, float buildUp) const;

    void spawnSparkle (bool fromMouth);
    void spawnGlint (float strength);
    /** hue 0..1 = ice..violet, < 0 = frost white. Uses pre-tinted sprites (fast image blits). */
    void drawSpriteGlint (juce::Graphics& g, juce::Point<float> centre, float size, float hue, float alpha, float rotation) const;

    CrystalProcessor& processor;
    std::vector<cave::Crystal> crystals;
    std::vector<juce::Point<float>> stars;
    std::vector<Sparkle> sparkles;
    std::vector<Glint> glints;
    std::vector<TailDot> dots;

    juce::Image background, glowLayer, frostLayer, curveLayer, lightIce, lightFrost;
    std::array<juce::Image, 7> glintSprites; // 6 hues from ice to violet + frost white
    juce::Rectangle<float> lightRect;
    std::array<juce::Rectangle<int>, 3> glowRegions;
    float imageScale = 0.0f;
    CurveKey curveKey;
    float curveScale = 0.0f;

    juce::Path mouthPath;
    juce::Rectangle<float> plotArea;
    juce::Point<float> mouthLight;

    float glow = 0.0f, flash = 0.0f, freezeVis = 0.0f, breathPhase = 0.0f, shimmerAmt = 0.0f;
    float spawnAccumulator = 0.0f, glintAccumulator = 0.0f, repaintClock = 0.0f;
    double clock = 0.0;
    bool hover = false;
    juce::Random random { 7 };
};

//==============================================================================
/** Pill toggle with a snowflake for the Freeze parameter. */
class FreezeButton : public juce::ToggleButton
{
public:
    explicit FreezeButton (juce::AudioProcessorValueTreeState& state);
    ~FreezeButton() override;
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

//==============================================================================
class CrystalEditor : public aa::EditorBase
{
public:
    explicit CrystalEditor (CrystalProcessor&);
    ~CrystalEditor() override = default;

private:
    void paintContent (juce::Graphics&) override;
    void layoutContent() override;
    void onFrame (double now, double dt) override;
    void rebuildBackdrop (float scale);

    CrystalProcessor& proc;
    CaveStage stage { proc };

    aa::Knob size, decay, damping, predelay, shimmer, sparkle, lowCut, highCut, modulation, width, mix, duck, output;
    aa::ChoiceBox interval;
    FreezeButton freeze;
    aa::PresetSelector presets;

    std::array<juce::Rectangle<float>, 4> panels;
    juce::Rectangle<float> titleArea;
    juce::Path titlePath;
    juce::Image backdrop;
    float backdropScale = 0.0f;
    double glintClock = 0.0;
    bool glintActive = false;
};
