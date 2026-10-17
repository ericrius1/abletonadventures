#pragma once

#include "Views.h"

class SynthEditor : public aa::EditorBase, public juce::DragAndDropContainer
{
public:
    explicit SynthEditor (SynthProcessor&);
    ~SynthEditor() override;

protected:
    void paintContent (juce::Graphics&) override;
    void layoutContent() override;
    void onFrame (double time, double delta) override;

private:
    enum class Page { synth, matrix, fx };
    void showPage (Page p);

    class TabStrip;

    SynthProcessor& proc;
    Page page = Page::synth;

    std::array<std::unique_ptr<ers::ui::OscPanel>, ers::numOscs> oscPanels;
    ers::ui::SubNoisePanel subNoise;
    ers::ui::FilterPanel filter1, filter2;
    aa::ChoiceBox filterRouting;
    ers::ui::ModulationPanel modulation;
    ers::ui::MacroPanel macros;
    ers::ui::VoicePanel voicing;
    ers::ui::MatrixPage matrixPage;
    ers::ui::FxPage fxPage;
    ers::ui::ScopeView scope;
    aa::PresetSelector presets;
    ers::ui::ModKnob volume;
    std::unique_ptr<TabStrip> tabs;

    std::vector<ers::ui::ModKnob*> knobs;   // static knobs that need live modulation refresh
    int voicesShown = -1;
};
