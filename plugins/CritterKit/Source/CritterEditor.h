#pragma once

#include "CritterProcessor.h"
#include "CritterArt.h"

class CritterEditor;

//==============================================================================
/** Candy-coloured knobs with cartoon outlines (matches the critters). */
class CritterLookAndFeel : public aa::LookAndFeel
{
public:
    explicit CritterLookAndFeel (const aa::Theme& t) : aa::LookAndFeel (t) {}

    void drawKnob (juce::Graphics&, juce::Rectangle<float> bounds, float proportion, bool bipolar, bool hovered,
                   bool dragging, juce::Colour accent, aa::Knob&) override;
    void drawKnobCaption (juce::Graphics&, juce::Rectangle<float> area, const juce::String& text, bool showingValue,
                          juce::Colour accent, aa::Knob&) override;
};

//==============================================================================
/** Compact knob: dial on the left, label + live value on the right (sequencer groove controls). */
class MiniKnob : public aa::Knob
{
public:
    MiniKnob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, const juce::String& label);
    void paint (juce::Graphics&) override;

private:
    juce::String caption;
};

//==============================================================================
/** One critter: a pad you can click (audition + select), drop samples on, and watch dance. */
class CritterPad : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient
{
public:
    CritterPad (CritterEditor& editor, CritterProcessor& p, int voice);

    void hit (float velocity);
    void tick (double dt, juce::Point<float> mouseInEditor, bool mouseInside);
    void setSelected (bool s);
    void showBubble (const juce::String& text, float seconds);
    void startNom();
    void onSampleChanged (bool nowHasSample);
    void onLoadFailed();

    void paint (juce::Graphics&) override;
    void resized() override { cachedBackground = {}; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hovered = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hovered = false; repaint(); }

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

    juce::Rectangle<float> critterArea() const;

private:
    struct Particle { juce::Point<float> pos, vel; float life = 0.0f, size = 3.0f, spin = 0.0f; int shape = 0; };

    void rebuildBackground (float scale);
    void spawnParticles (int count, float strength);
    void drawBadge (juce::Graphics&, const critter::SampleData&);
    void drawBubble (juce::Graphics&);

    CritterEditor& editor;
    CritterProcessor& proc;
    const int voice;
    juce::Image cachedBackground;
    juce::Random random;

    bool selected = false, hovered = false, dragHover = false;
    float squash = 0.0f, squashVel = 0.0f;
    float mouth = 0.0f, excited = 0.0f, action = 0.0f, flash = 0.0f;
    float blink = 0.0f, blinkTimer = 2.0f, blinkPhase = -1.0f;
    float hungry = 0.0f, nomTime = -1.0f, badgePop = 0.0f, shake = 0.0f;
    float fidgetTimer = 6.0f, idleLookTimer = 0.0f;
    juce::Point<float> look, lookTarget, idleLook;
    float time = 0.0f;
    juce::String bubbleText;
    float bubbleTime = 0.0f, bubbleDuration = 1.0f;
    std::array<Particle, 18> particles {};
    bool lastHasSample = false;
};

//==============================================================================
/** 8 x 16 step grid, rows coloured by critter, with a bouncing playhead ball. */
class StepGrid : public juce::Component, public juce::SettableTooltipClient
{
public:
    StepGrid (CritterEditor& editor, CritterProcessor& p);

    void tick (double dt);
    void setSelectedRow (int row) { selectedRow = row; background = {}; repaint(); }
    void patternChanged() { repaint(); }

    void paint (juce::Graphics&) override;
    void resized() override { background = {}; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    static constexpr float labelWidth = 104.0f, laneHeight = 20.0f, groupGap = 8.0f;

    float rowHeight() const;
    float cellPitch() const;
    float stepX (int step) const;
    juce::Rectangle<float> cellRect (int row, int step) const;
    juce::Rectangle<float> columnRect (int step) const;
    juce::Rectangle<float> laneRect() const;
    bool cellAt (juce::Point<float> p, int& row, int& step) const;
    int labelRowAt (juce::Point<float> p) const;
    void rebuildBackground (float scale);
    void showRowMenu (int row);

    CritterEditor& editor;
    CritterProcessor& proc;
    juce::Image background;
    int selectedRow = 0;
    int hoverRow = -1, hoverStep = -1;
    int dragRow = -1, lastDragStep = -1;
    bool dragValue = false, dragging = false;
    int playStep = -1;
    bool running = false;
    double phase = 0.0;
    std::array<float, critter::numSteps> pop {};
    float ballSquash = 0.0f;
};

//==============================================================================
/** Name card for the selected critter (name, personality, what it has eaten). */
class CardHeader : public juce::Component, public juce::SettableTooltipClient
{
public:
    CardHeader (CritterEditor& editor, CritterProcessor& p);

    void setVoice (int v) { voice = v; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { closeHover = false; repaint(); }

private:
    juce::Rectangle<float> chipArea() const;
    juce::Rectangle<float> closeArea() const;

    CritterEditor& editor;
    CritterProcessor& proc;
    int voice = 0;
    bool closeHover = false;
};

//==============================================================================
class CritterEditor : public aa::EditorBase
{
public:
    explicit CritterEditor (CritterProcessor&);
    ~CritterEditor() override;

    void selectCritter (int voice);
    int getSelected() const noexcept { return selected; }
    void audition (int voice);
    void showSampleMenu (int voice, juce::Component* target);
    void chooseSampleFile (int voice);

private:
    void paintContent (juce::Graphics&) override;
    void layoutContent() override;
    void onFrame (double now, double dt) override;
    void rebuildBackground (float scale);
    void updateTooltips (int voice);

    CritterProcessor& proc;
    int selected = 0;

    std::vector<std::unique_ptr<CritterPad>> pads;
    StepGrid grid { *this, proc };
    CardHeader card { *this, proc };
    std::vector<std::unique_ptr<aa::Knob>> voiceKnobs; // [voice * 6 + param]
    aa::Knob volumeKnob, driveKnob, roomKnob, pitchKnob;
    MiniKnob swingKnob, accentKnob, humanizeKnob;
    aa::Toggle seqToggle;
    aa::PresetSelector presets;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Image background;
    juce::Rectangle<float> titleArea;
    std::array<float, 10> letterBounce {}, letterVel {};
    uint32_t lastPatternVersion = 0;
    std::array<int, critter::numVoices> lastSampleVersion {}, lastFailures {};
};
