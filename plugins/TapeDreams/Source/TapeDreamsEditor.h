#pragma once

#include "TapeDreamsProcessor.h"
#include "TapeWidgets.h"

class TapeDreamsEditor : public aa::EditorBase, private juce::ChangeListener
{
public:
    explicit TapeDreamsEditor (TapeDreamsProcessor&);
    ~TapeDreamsEditor() override;

private:
    void paintContent (juce::Graphics&) override;
    void layoutContent() override;
    void onFrame (double now, double dt) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void rebuildBackground (float scale);
    void paintTitle (juce::Graphics&);
    void setMomentaryStop (bool held);

    TapeDreamsProcessor& proc;

    tapeui::CassetteView cassette;
    tapeui::VuMeter inMeter { "INPUT" }, outMeter { "OUTPUT" };
    tapeui::Lamp recLamp { "REC", tapeui::palette::red }, playLamp { "PLAY", tapeui::palette::lampGreen };
    tapeui::TransportKey stopKey;

    aa::Knob drive, squash, wow, flutter, crinkle, age, wear, hiss, hum, mix, input, output, stopTime;
    aa::ChoiceBox mains;
    aa::PresetSelector presets;

    juce::Image background;
    float backgroundScale = 0.0f;
    std::array<juce::Rectangle<float>, 5> panels;
    juce::Rectangle<float> titleArea, deckArea, cassetteWell;

    float titleClock = 0.0f, sag = 0.0f, wobble = 0.0f;
    float blinkClock = 0.0f;
    bool holdActive = false, holdPrevious = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapeDreamsEditor)
};
