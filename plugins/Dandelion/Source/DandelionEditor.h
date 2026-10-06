#pragma once

#include "DandelionProcessor.h"

/** One floating seed (= one grain that was played). */
struct Seed
{
    juce::Point<float> pos, vel;
    float life = 1.0f, maxLife = 4.0f, size = 6.0f, spin = 0.0f, spinRate = 0.0f;
    int octave = 0;
};

/** The dusk meadow with the dandelion whose seeds are the grains. */
class MeadowScene : public juce::Component
{
public:
    explicit MeadowScene (DandelionProcessor& p);
    void addSeed (const DandelionProcessor::GrainEvent& e);
    void tick (double dt);
    void paint (juce::Graphics&) override;
    void resized() override { backdrop = {}; head = {}; }

private:
    void renderBackdrop (float scale);
    void renderHead (float scale);
    juce::Point<float> headCentre() const;

    DandelionProcessor& processor;
    std::vector<Seed> seeds;
    juce::Image backdrop, head;
    float headScale = 1.0f;
    float sway = 0.0f, swayVel = 0.0f, time = 0.0f, glow = 0.0f;
    juce::Random random;
    std::vector<juce::Point<float>> stars;
};

/** Waveform of the current source with position / spray / voice playheads and grain sparks. */
class WaveView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit WaveView (DandelionProcessor& p);
    void addSpark (const DandelionProcessor::GrainEvent& e);
    void tick (double dt);
    void paint (juce::Graphics&) override;
    void resized() override { waveImage = {}; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void rebuildWave (float scale);
    void setPositionFromX (float x);

    DandelionProcessor& processor;
    juce::Image waveImage;
    int builtVersion = -1;
    const dandelion::SampleData* builtFor = nullptr;
    struct Spark { float x, y, life, octave; };
    std::vector<Spark> sparks;
    juce::Random random;
    std::unique_ptr<juce::ParameterAttachment> positionAttachment;
    float positionValue = 0.0f;
};

/** Small draggable pill showing the sample's root key. */
class NoteBox : public juce::Component, public juce::SettableTooltipClient
{
public:
    NoteBox (juce::AudioProcessorValueTreeState& state, const juce::String& paramID);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    int value = 60, dragStartValue = 60;
};

/** Rounded text pill button in the kit style. */
class PillButton : public juce::Button
{
public:
    explicit PillButton (const juce::String& text) : juce::Button (text) {}
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

class DandelionEditor : public aa::EditorBase, public juce::FileDragAndDropTarget
{
public:
    explicit DandelionEditor (DandelionProcessor&);
    ~DandelionEditor() override = default;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragHover = true; content.repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragHover = false; content.repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void paintContent (juce::Graphics&) override;
    void paintContentOver (juce::Graphics&) override;
    void layoutContent() override;
    void onFrame (double now, double dt) override;
    void loadFile (const juce::File& f);
    void chooseFile();

    DandelionProcessor& proc;
    MeadowScene scene { proc };
    WaveView wave { proc };

    aa::Knob position, spray, size, density, drift, wind, seeds, reverse, shapeKnob, shimmer, cutoff, width,
        attack, release, reverb, volume;
    aa::ChoiceBox source;
    NoteBox root;
    PillButton loadButton { "Load sample..." };
    aa::PresetSelector presets;
    juce::MidiKeyboardComponent keyboard;
    std::unique_ptr<juce::FileChooser> chooser;

    std::array<juce::Rectangle<float>, 4> panels;
    bool dragHover = false;
    juce::String statusText;
    float statusTimer = 0.0f;
};
