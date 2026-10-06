#pragma once

#include "StardustProcessor.h"
#include "SpaceView.h"

namespace stardust::ui
{
//==============================================================================
/** Glass-orb knobs with a glowing value arc and a little star at its tip; glowing envelope faders. */
class StardustLook : public aa::LookAndFeel
{
public:
    explicit StardustLook (const aa::Theme& t) : aa::LookAndFeel (t) {}

    void drawKnob (juce::Graphics&, juce::Rectangle<float> bounds, float proportion, bool bipolar, bool hovered,
                   bool dragging, juce::Colour accent, aa::Knob&) override;
    void drawKnobCaption (juce::Graphics&, juce::Rectangle<float> area, const juce::String& text, bool showingValue,
                          juce::Colour accent, aa::Knob&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                           float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override { return 6; }
};

//==============================================================================
/** A slim vertical fader for one envelope stage. */
class EnvSlider : public juce::Slider
{
public:
    EnvSlider (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, juce::Colour accent,
               const juce::String& tooltip);

    juce::String getStageName() const { return stageName; }

private:
    juce::String stageName;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** Four envelope faders under a live drawing of the envelope shape. */
class EnvGroup : public juce::Component
{
public:
    EnvGroup (StardustProcessor& p, const juce::String& title, const juce::StringArray& paramIDs,
              const juce::StringArray& tooltips, std::atomic<float>& liveLevel, juce::Colour accent);

    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    StardustProcessor& processor;
    juce::String title;
    juce::Colour accent;
    std::atomic<float>& live;
    juce::OwnedArray<EnvSlider> sliders;
    std::array<juce::RangedAudioParameter*, 4> params {};
    juce::Rectangle<float> labelArea, graphArea, letterArea;
    float shownLevel = 0.0f;
    float lastSignature = -1.0f;
};

//==============================================================================
/** Draws the current morph waveform of an oscillator. */
class WavePreview : public juce::Component
{
public:
    WavePreview (std::atomic<float>& shapeValue, juce::Colour accent);
    void tick();
    void paint (juce::Graphics&) override;

private:
    std::atomic<float>& shape;
    juce::Colour accent;
    float shown = -1.0f;
};

/** The LFO's shape with a dot riding its current phase. */
class LfoGlyph : public juce::Component
{
public:
    LfoGlyph (StardustProcessor& p, juce::Colour accent);
    void tick();
    void paint (juce::Graphics&) override;

private:
    StardustProcessor& processor;
    std::atomic<float>* shapeParam = nullptr;
    juce::Colour accent;
    float phase = 0.0f;
};

/** Stereo output meter. */
class StereoMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit StereoMeter (StardustProcessor& p);
    void tick (float dt);
    void paint (juce::Graphics&) override;

private:
    StardustProcessor& processor;
    float levelL = 0.0f, levelR = 0.0f, holdL = 0.0f, holdR = 0.0f, holdTimeL = 0.0f, holdTimeR = 0.0f;
};

//==============================================================================
/** On-screen keys that glow for clicked notes and incoming MIDI alike. */
class CosmicKeyboard : public juce::MidiKeyboardComponent, public juce::SettableTooltipClient
{
public:
    explicit CosmicKeyboard (juce::MidiKeyboardState& state);

    void drawWhiteNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour lineColour, juce::Colour textColour) override;
    void drawBlackNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour noteFillColour) override;
    juce::String getWhiteNoteText (int note) override;
};

/** Small chevron button that shifts the on-screen keyboard by an octave. */
class OctaveButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit OctaveButton (bool pointsUp);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { pressed = false; repaint(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    std::function<void()> onClick;

private:
    bool up, pressed = false;
};
} // namespace stardust::ui

//==============================================================================
class StardustEditor : public aa::EditorBase
{
public:
    explicit StardustEditor (StardustProcessor&);
    ~StardustEditor() override = default;

private:
    void paintContent (juce::Graphics&) override;
    void paintContentOver (juce::Graphics&) override;
    void layoutContent() override;
    void onFrame (double now, double dt) override;

    void rebuildBackground (float scale);
    void updateSyncState();
    void shiftKeyboard (int semitones);
    void drawGlassPanel (juce::Graphics&, juce::Rectangle<float> r, const juce::String& title, juce::Colour accent,
                         float titleLineEnd) const;

    StardustProcessor& proc;
    SpaceView space { proc };

    // oscillators
    aa::Knob shapeA, unison, detune, spread, levelA, octaveA;
    aa::Knob shapeB, octaveB, semiB, fineB, levelB, sub, noise;
    stardust::ui::WavePreview waveA, waveB;

    // filter
    aa::ChoiceBox filterType;
    aa::Knob cutoff, resonance, drive, filterEnv, keyTrack;

    // envelopes
    stardust::ui::EnvGroup ampEnv, filterEnvGroup;

    // lfo
    aa::ChoiceBox lfoShape, lfoDivision;
    aa::Toggle lfoSync;
    aa::Knob lfoRate, lfoPitch, lfoCutoff, lfoShapeMod;
    stardust::ui::LfoGlyph lfoGlyph;

    // fx
    aa::Knob chorus, spaceSize, spaceMix, volume;
    stardust::ui::StereoMeter meter;

    // cosmos + voice
    aa::Knob twinkle, drift, gravity;
    aa::ChoiceBox voiceMode;
    aa::Knob glide, velocity;

    stardust::ui::CosmicKeyboard keyboard;
    stardust::ui::OctaveButton octaveDown { false }, octaveUp { true };
    aa::PresetSelector presets;

    juce::Rectangle<float> oscAPanel, oscBPanel, filterPanel, envPanel, lfoPanel, fxPanel, cosmosPanel, keysPanel,
        voicePanel, spaceArea, titleArea;

    juce::Image background;
    float backgroundScale = 0.0f;
    double clock = 0.0;
};
