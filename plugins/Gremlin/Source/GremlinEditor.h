#pragma once

#include "GremlinProcessor.h"
#include "GremlinWidgets.h"
#include "GremlinFace.h"

class GremlinEditor : public aa::EditorBase
{
public:
    explicit GremlinEditor (GremlinProcessor&);
    ~GremlinEditor() override = default;

private:
    void paintContent (juce::Graphics&) override;
    void layoutContent() override;
    void onFrame (double now, double dt) override;
    void rebuildBackground (float scale);
    void paintTitle (juce::Graphics&);

    GremlinProcessor& proc;

    gremlin::GremlinFace face { proc };
    gremlin::Timeline timeline { proc };

    aa::Knob chaos, mix, ratchet, crush, smooth;
    aa::ChoiceBox grid, lock, slice;
    gremlin::SeedBox seed;
    aa::PresetSelector presets;
    std::vector<std::unique_ptr<gremlin::FxFader>> faders;
    std::vector<std::unique_ptr<gremlin::ForcePad>> pads;

    juce::Image background;
    float backgroundScale = 0.0f;
    juce::Rectangle<float> titleArea;
    juce::Path titlePath;
    juce::Rectangle<float> mischiefPanel, forcePanel, dicePanel, tweakPanel;

    float titleGlitch = 0.0f, titleTimer = 2.0f;
    std::array<float, 6> titleSlices {};
    juce::Random random;
};
