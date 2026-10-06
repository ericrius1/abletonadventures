#pragma once

#include "OrreryProcessor.h"
#include "OrreryLookAndFeel.h"
#include "OrreryStage.h"

/** Six little planets to pick which orbit the side panel edits. */
class PlanetPicker : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit PlanetPicker (OrreryProcessor& p) : proc (p) { setTooltip ("Pick the planet to edit (or click it in the orrery)"); }

    void setSelected (int i) { selected = i; repaint(); }
    void refreshIfChanged();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    std::function<void (int)> onSelect;

private:
    OrreryProcessor& proc;
    int selected = 0;
    int lastOnMask = -1;
};

/** Shows the selected planet's rhythm as a little timeline: beats vs. pulses, with the planet moving along it. */
class RhythmView : public juce::Component, public juce::SettableTooltipClient
{
public:
    RhythmView (OrreryProcessor& p, OrreryStage& s) : proc (p), stage (s)
    {
        setTooltip ("This planet's rhythm: pulses (dots) spread evenly across its revolution of beats (ticks)");
    }
    void setOrbit (int o) { orbit = o; repaint(); }
    void paint (juce::Graphics&) override;

private:
    OrreryProcessor& proc;
    OrreryStage& stage;
    int orbit = 0;
};

/** Play state, bar/beat and tempo. */
class TransportReadout : public juce::Component, public juce::SettableTooltipClient
{
public:
    TransportReadout (OrreryProcessor& p, OrreryStage& s) : proc (p), stage (s)
    {
        setTooltip ("Green: following the host transport. Gold: free-running at the host tempo. Grey: waiting for play.");
    }
    void tick();
    void paint (juce::Graphics&) override;

private:
    OrreryProcessor& proc;
    OrreryStage& stage;
    juce::String lastText;
    int lastState = -1;
    float glow = 0.0f;
    double lastBeat = -1.0;
};

/** Small brass level gauge. */
class LevelGauge : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit LevelGauge (OrreryProcessor& p) : proc (p) { setTooltip ("Output level"); }
    void tick (double dt);
    void paint (juce::Graphics&) override;

private:
    OrreryProcessor& proc;
    float needle = 0.0f;
};

//==============================================================================
class OrreryEditor : public aa::EditorBase
{
public:
    explicit OrreryEditor (OrreryProcessor&);
    ~OrreryEditor() override;

private:
    void paintContent (juce::Graphics&) override;
    void paintContentOver (juce::Graphics&) override;
    void layoutContent() override;
    void onFrame (double now, double dt) override;

    void selectOrbit (int orbit);
    void layoutPlanetPanel();
    void rebuildBackground (float scale);

    OrreryProcessor& proc;
    OrreryStage stage { proc };
    aa::PresetSelector presets;
    PlanetPicker picker { proc };
    RhythmView rhythm { proc, stage };
    TransportReadout readout { proc, stage };
    LevelGauge gauge { proc };

    orrery::BrassStepper scaleBox, rootBox, speedBox, echoTimeBox;
    orrery::JewelToggle freeRun, follow, sound;
    orrery::OrreryKnob material, brightness, decay, spread, volume, echo, feedback, reverb;

    // selected-planet controls (recreated when the selection changes)
    std::unique_ptr<orrery::OrreryKnob> beatsKnob, pulsesKnob, offsetKnob, noteKnob, octaveKnob, velocityKnob, chanceKnob, gateKnob;
    std::unique_ptr<orrery::JewelToggle> onToggle;
    int lastNoteShown = -1;

    juce::Rectangle<float> planetPanel, cosmosPanel, soundPanel, planetTitleArea;
    float cosmosRuleEnd = 0.0f, soundRuleEnd = 0.0f;
    std::array<juce::Rectangle<int>, 8> planetKnobCells;
    juce::Rectangle<int> onToggleArea;

    juce::Image background;
    float backgroundScale = 0.0f;
};
