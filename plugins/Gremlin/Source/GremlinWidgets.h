#pragma once

#include "GremlinProcessor.h"

namespace gremlin
{
//==============================================================================
namespace pal
{
    const juce::Colour bg       { 0xff120c1c };
    const juce::Colour bgAlt    { 0xff1c1030 };
    const juce::Colour ink      { 0xff07040d };
    const juce::Colour panel    { 0xff1a1228 };
    const juce::Colour green    { 0xff8cff3a };
    const juce::Colour pink     { 0xffff3d8b };
    const juce::Colour purple   { 0xff8a5bff };
    const juce::Colour cyan     { 0xff3df2ff };
    const juce::Colour yellow   { 0xffffd23d };
    const juce::Colour orange   { 0xffff8a3d };
    const juce::Colour lavender { 0xffd9d2ff };
    const juce::Colour text     { 0xffeee8ff };
    const juce::Colour textDim  { 0x99d8ceff };
}

juce::Colour fxColour (Fx fx);

/** Little vector glyph for each effect. */
void drawFxIcon (juce::Graphics& g, juce::Rectangle<float> area, Fx fx, juce::Colour colour, float strokeWidth = 1.8f);

/** Text with an RGB-split shadow (pink left, cyan right) - the CRT look. */
void drawChromaticText (juce::Graphics& g, const juce::String& text, const juce::Font& font,
                        juce::Rectangle<float> area, juce::Justification just, juce::Colour main, float split);

/** Draws horizontal CRT scanlines over an area. */
void drawScanlines (juce::Graphics& g, juce::Rectangle<float> area, float spacing, float alpha);

//==============================================================================
class GremlinLookAndFeel : public aa::LookAndFeel
{
public:
    explicit GremlinLookAndFeel (const aa::Theme& t) : aa::LookAndFeel (t) {}

    void drawKnob (juce::Graphics&, juce::Rectangle<float> bounds, float proportion, bool bipolar, bool hovered,
                   bool dragging, juce::Colour accent, aa::Knob& knob) override;
    void drawKnobCaption (juce::Graphics&, juce::Rectangle<float> area, const juce::String& text, bool showingValue,
                          juce::Colour accent, aa::Knob& knob) override;
    juce::Rectangle<float> drawPanel (juce::Graphics&, juce::Rectangle<float> bounds, const juce::String& title,
                                      juce::Colour tint) override;
};

//==============================================================================
/** Glowing vertical tube fader for one effect's odds. */
class FxFader : public juce::Slider
{
public:
    FxFader (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, Fx fx, const juce::String& label);
    ~FxFader() override;

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent& e) override { hovered = true; repaint(); Slider::mouseEnter (e); }
    void mouseExit (const juce::MouseEvent& e) override { hovered = false; repaint(); Slider::mouseExit (e); }
    void startedDragging() override { dragging = true; repaint(); }
    void stoppedDragging() override { dragging = false; repaint(); }

    void flash (float amount) { flashLevel = juce::jmax (flashLevel, amount); repaint(); }
    void tick (float dt);
    void setLive (bool isLive) { if (live != isLive) { live = isLive; repaint(); } }

private:
    juce::Rectangle<float> tubeArea() const;

    Fx fx;
    juce::String label;
    bool hovered = false, dragging = false, live = false;
    float flashLevel = 0.0f;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
/** Chunky arcade pad for a force toggle. Momentary while held; shift- or right-click to latch. */
class ForcePad : public juce::Component, public juce::SettableTooltipClient
{
public:
    ForcePad (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, Fx fx, const juce::String& label);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    void tick (float dt, bool engineDoingIt);
    bool isOn() const noexcept { return on; }

private:
    Fx fx;
    juce::String label;
    bool on = false, momentary = false, latched = false, active = false;
    float glow = 0.0f, press = 0.0f, phase = 0.0f;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

//==============================================================================
/** VT323 seed readout: drag to change, click the die to re-roll. */
class SeedBox : public juce::Component, public juce::SettableTooltipClient
{
public:
    SeedBox (juce::AudioProcessorValueTreeState& state, const juce::String& paramID);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    void tick (float dt);
    void setDimmed (bool d) { if (dimmed != d) { dimmed = d; repaint(); } }

private:
    juce::Rectangle<float> dieArea() const;
    void setSeed (int v);

    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    int value = 1, dragStartValue = 1;
    bool draggingNumber = false, dimmed = false;
    float roll = 0.0f;
    juce::Random random;
};

//==============================================================================
/** Pattern page with a sweeping scanline playhead. Past steps light up in their effect's
    colour; when Lock is on the steps ahead show what the gremlin is going to do. */
class Timeline : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Timeline (GremlinProcessor& p);

    void addEvent (const StepEvent& e);
    void tick (double dt);
    void paint (juce::Graphics&) override;
    void resized() override { backdrop = {}; }

private:
    struct Slot
    {
        int8_t fx = -1;            // -1 = never seen
        bool forced = false;
        bool continuation = false;
        int8_t slices = 0;
        double ppq = -1.0e9;       // when it was written
    };

    void rebuildBackdrop (float scale);
    void resetSlots();

    GremlinProcessor& processor;
    std::array<Slot, 128> slots {};
    std::array<Decision, 128> predicted {};
    std::array<bool, 128> predictedCont {};
    int numSlots = 16, lastGrid = -1, lastLockBars = -1;
    double pageLenPpq = 8.0, gridBeats = 0.5, ppq = 0.0, lastHostPpq = 0.0, lastPpqPerBar = 0.0;
    float flash = 0.0f;
    bool locked = false;
    juce::Image backdrop;
    juce::Rectangle<float> strip;
};
} // namespace gremlin
