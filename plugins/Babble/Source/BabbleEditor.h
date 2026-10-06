#pragma once

#include "BabbleProcessor.h"
#include "BabbleWidgets.h"
#include "BabbleFace.h"

class BabbleEditor : public aa::EditorBase
{
public:
    explicit BabbleEditor (BabbleProcessor&);
    ~BabbleEditor() override = default;

private:
    void paintContent (juce::Graphics&) override;
    void layoutContent() override;
    void onFrame (double now, double dt) override;
    void updateSyncState();
    void rebuildBackground (float scale);
    void drawTitle (juce::Graphics&);

    BabbleProcessor& proc;

    babble::ui::FaceStage stage { proc };
    babble::ui::MouthPad mouthPad { proc };
    babble::ui::VoicePicker voicePicker { proc };
    babble::ui::ChoirPicker choirPicker { proc };

    aa::Knob babbleKnob, babbleRate, vibRate, vibDepth, vibDelay, breath, bright, detune, attack, release, glide,
        chorus, reverb, volume;
    aa::Toggle babbleSync;
    aa::ChoiceBox babbleDiv;
    aa::PresetSelector presets;
    aa::Caption mouthReadout;

    std::atomic<float>* vowelValue = nullptr;
    std::atomic<float>* shiftValue = nullptr;

    struct PanelInfo
    {
        juce::Rectangle<float> bounds;
        juce::String title;
    };
    std::vector<PanelInfo> panels;

    juce::Image background;
    juce::Rectangle<float> titleArea;
    std::array<float, 6> letterY {}, letterVel {};
    float titleClock = 0.0f, titleEnergy = 0.0f;
    int lastActiveNotes = 0;
    juce::String lastReadout;
};
