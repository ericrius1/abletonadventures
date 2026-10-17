#pragma once

#include "SynthProcessor.h"

namespace ers::ui
{
//==============================================================================
// Palette
const juce::Colour bgTop       { 0xff0b0e1c };
const juce::Colour bgBottom    { 0xff06070f };
const juce::Colour panelFill   { 0xff121628 };
const juce::Colour panelEdge   { 0xff252b47 };
const juce::Colour screenFill  { 0xff070913 };
const juce::Colour textBright  { 0xffeef1ff };
const juce::Colour textDim     { 0xff8a91b4 };
const juce::Colour oscAccent   { 0xff38e8cf };
const juce::Colour filterAccent{ 0xffffb547 };
const juce::Colour envAccent   { 0xffff5c9f };
const juce::Colour lfoAccent   { 0xff9f82ff };
const juce::Colour macroAccent { 0xffd6f45e };
const juce::Colour fxAccent    { 0xff5cc6ff };
const juce::Colour otherAccent { 0xff7fe3ff };

juce::Colour sourceColour (ModSource s);

/** Draws a section panel (rounded, subtle gradient, thin edge). Returns the area below the title strip. */
juce::Rectangle<float> drawSection (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title,
                                    juce::Colour accent, bool enabled = true);

/** Draws a recessed "screen" for graphs. */
void drawScreen (juce::Graphics& g, juce::Rectangle<float> r);

//==============================================================================
/** A rotary knob attached to a parameter that also shows - and accepts - modulation.

    - Drop a modulation source handle on it to create a routing.
    - Coloured rings show each routing's depth; a bright dot shows the live modulated value.
    - Alt-drag (or Ctrl-drag) adjusts the depth of the most recent routing.
    - Right-click for a menu to invert, make bipolar or remove routings. */
class ModKnob : public juce::Slider, public juce::DragAndDropTarget
{
public:
    ModKnob (SynthProcessor& p, const juce::String& paramId, const juce::String& label, juce::Colour accent);
    ~ModKnob() override;

    void setLabel (const juce::String& l) { label = l; repaint(); }
    void setAccent (juce::Colour c)       { accent = c; repaint(); }
    void setBipolarDisplay (bool b)       { bipolarDisplay = b; repaint(); }
    int getDestIndex() const noexcept     { return destIndex; }

    /** Called by the editor every frame; repaints if the live modulation moved. */
    void refreshModulation();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override  { hovered = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { hovered = false; repaint(); }
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void valueChanged() override { repaint(); }

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override  { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails&) override;

private:
    void showModMenu();
    juce::Rectangle<float> dialArea() const;

    SynthProcessor& proc;
    juce::RangedAudioParameter* parameter = nullptr;
    juce::String label;
    juce::Colour accent;
    int destIndex = -1;
    bool hovered = false, dropHover = false, bipolarDisplay = false;
    bool adjustingMod = false;
    int adjustSlot = -1;
    float adjustStart = 0.0f;
    float lastLive = 0.0f;
    int lastVersion = -1;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
/** A little grab handle you drag onto knobs to modulate them. */
class ModSourceHandle : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ModSourceHandle (ModSource s);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
    ModSource source;
private:
    bool dragStarted = false;
};

//==============================================================================
/** A small power button attached to a bool parameter. */
class PowerButton : public juce::ToggleButton
{
public:
    PowerButton (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, juce::Colour accent);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
private:
    juce::Colour accent;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

//==============================================================================
/** Pseudo-3D view of an oscillator's wavetable: all frames stacked in depth, the current
    (modulated) position glowing. Drop a WAV here to load it as a custom wavetable. */
class WavetableView : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient
{
public:
    WavetableView (SynthProcessor& p, int osc, juce::Colour accent);

    void refresh();          // per frame: picks up table changes and the live position
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

    std::function<void()> onClick;

private:
    void rebuild();

    SynthProcessor& proc;
    int osc;
    juce::Colour accent;
    int builtVersion = -1;
    const Wavetable* builtTable = nullptr;
    std::vector<std::vector<float>> frames;     // display frames
    ers::WavetablePtr table;
    float position = 0.0f, warp = 0.0f;
    bool dropHover = false, flat = false;
    juce::String message;
    double messageTime = 0.0;
};

//==============================================================================
/** "< Table name >" selector with a categorised menu, WAV import and export. */
class TableSelector : public juce::Component
{
public:
    TableSelector (SynthProcessor& p, int osc, juce::Colour accent);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void refresh();
private:
    void showMenu();
    void step (int delta);
    SynthProcessor& proc;
    int osc;
    juce::Colour accent;
    juce::String name;
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
class OscPanel : public juce::Component
{
public:
    OscPanel (SynthProcessor& p, int osc);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
    void collectKnobs (std::vector<ModKnob*>& out);
private:
    SynthProcessor& proc;
    int osc;
    juce::Colour accent;
    PowerButton power;
    TableSelector selector;
    WavetableView view;
    ModKnob position, warpAmount, unison, detune, blend, width, octave, semi, fine, phase, random, pan, level;
    aa::ChoiceBox warp, warpSource, route, stack;
    bool lastOn = true;
};

//==============================================================================
class SubNoisePanel : public juce::Component
{
public:
    explicit SubNoisePanel (SynthProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void collectKnobs (std::vector<ModKnob*>& out);
private:
    SynthProcessor& proc;
    PowerButton subPower, noisePower;
    aa::ChoiceBox subShape, subRoute, noiseType, noiseRoute;
    ModKnob subOctave, subLevel, subPan, noisePitch, noiseLevel, noisePan;
    aa::Toggle noiseKey;
};

//==============================================================================
/** Approximate magnitude-response drawing of a filter (for the UI only). */
class FilterCurve : public juce::Component
{
public:
    FilterCurve (SynthProcessor& p, int filter, juce::Colour accent);
    void paint (juce::Graphics&) override;
    void refresh();
private:
    SynthProcessor& proc;
    int filter;
    juce::Colour accent;
    float lastKey = -1.0f;
};

class FilterPanel : public juce::Component
{
public:
    FilterPanel (SynthProcessor& p, int filter);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh() { curve.refresh(); }
    void collectKnobs (std::vector<ModKnob*>& out);
private:
    SynthProcessor& proc;
    int filter;
    juce::Colour accent;
    PowerButton power;
    aa::ChoiceBox type;
    FilterCurve curve;
    ModKnob cutoff, resonance, drive, var, mix, keyTrack;
};

//==============================================================================
/** AHDSR editor: drag the corners, or the small dots in the middle of segments to bend them. */
class EnvelopeEditor : public juce::Component
{
public:
    EnvelopeEditor (SynthProcessor& p, int env);
    void setEnvelope (int env);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void refresh();
    void collectKnobs (std::vector<ModKnob*>& out);
private:
    struct Handle { juce::Point<float> pos; int param; int paramY; bool isCurve; };
    std::vector<Handle> handles() const;
    juce::Rectangle<float> graphArea() const;
    float timeToWidth (float ms) const;
    float widthToTime (float w) const;
    float value (int vpIndex) const;
    void setValue (int vpIndex, float v);
    int hit (juce::Point<float> p) const;

    SynthProcessor& proc;
    int env;
    int dragging = -1, hover = -1;
    float dragStartValue = 0.0f, dragStartY = 0.0f;
    std::unique_ptr<ModKnob> attack, hold, decay, sustain, release;
    float liveLevel = 0.0f;
};

/** Drawable LFO shape editor with a live playhead. */
class LfoEditor : public juce::Component
{
public:
    LfoEditor (SynthProcessor& p, int lfo);
    void setLfo (int lfo);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void refresh();
    void collectKnobs (std::vector<ModKnob*>& out);
private:
    juce::Rectangle<float> graphArea() const;
    juce::Point<float> toScreen (const LfoShape::Point& p) const;
    int hitPoint (juce::Point<float> p) const;
    int hitCurve (juce::Point<float> p) const;
    void showMenu();
    void rebuildControls();

    SynthProcessor& proc;
    int lfo;
    LfoShape shape;
    int shapeVersion = -1;
    int dragPoint = -1, dragCurve = -1, hover = -1;
    float curveStart = 0.0f, dragStartY = 0.0f;
    bool snap = true;
    int gridDivs = 8;
    float livePhase = 0.0f, liveValue = 0.0f;
    std::unique_ptr<ModKnob> rate, rise, delay, smooth, phase;
    std::unique_ptr<aa::ChoiceBox> division, mode;
    std::unique_ptr<aa::Toggle> sync;
    juce::TextButton snapButton { "SNAP" }, gridButton { "1/8" };
};

//==============================================================================
/** The modulation area: tabs for the 4 envelopes and 4 LFOs (each with a drag handle). */
class ModulationPanel : public juce::Component
{
public:
    explicit ModulationPanel (SynthProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void refresh();
    void collectKnobs (std::vector<ModKnob*>& out);
private:
    juce::Rectangle<float> tabArea (int i) const;
    void select (int tab);
    SynthProcessor& proc;
    int selected = 0;   // 0..3 env, 4..7 lfo
    juce::OwnedArray<ModSourceHandle> handles;
    EnvelopeEditor envEditor;
    LfoEditor lfoEditor;
    std::array<float, 8> meters {};
};

class MacroPanel : public juce::Component
{
public:
    explicit MacroPanel (SynthProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void collectKnobs (std::vector<ModKnob*>& out);
private:
    SynthProcessor& proc;
    juce::OwnedArray<ModKnob> knobs;
    juce::OwnedArray<ModSourceHandle> handles;
    juce::OwnedArray<juce::Label> names;
};

class VoicePanel : public juce::Component
{
public:
    explicit VoicePanel (SynthProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void collectKnobs (std::vector<ModKnob*>& out);
private:
    SynthProcessor& proc;
    aa::ChoiceBox mode;
    ModKnob polyphony, glide, bend, velocity, tune;
    aa::Toggle legato;
};

//==============================================================================
/** Full list of modulation routings. */
class MatrixPage : public juce::Component
{
public:
    explicit MatrixPage (SynthProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void refresh();
private:
    enum class Column { source, aux, amount, bipolar, dest, remove, none };
    juce::Rectangle<float> rowArea (int row) const;
    juce::Rectangle<float> cell (int row, Column c) const;
    Column hitColumn (int row, juce::Point<float> p) const;
    int rowAt (juce::Point<float> p) const;
    std::vector<int> visibleSlots() const;
    void showSourceMenu (int slot, bool aux);
    void showDestMenu (int slot);

    SynthProcessor& proc;
    int version = -1;
    int dragSlot = -1;
    float dragStartAmount = 0.0f, dragStartX = 0.0f;
    juce::OwnedArray<ModSourceHandle> handles;
};

//==============================================================================
/** The effect rack: reorderable list on the left, the selected effect's controls on the right. */
class FxPage : public juce::Component
{
public:
    explicit FxPage (SynthProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void refresh();
    void collectKnobs (std::vector<ModKnob*>& out);
private:
    juce::Rectangle<float> slotArea (int index) const;
    juce::Rectangle<float> powerArea (int index) const;
    void select (fx::Kind k);
    bool isOn (fx::Kind k) const;

    SynthProcessor& proc;
    fx::Kind selected = fx::Kind::hyper;
    int dragIndex = -1, dragTarget = -1;
    float dragY = 0.0f;
    juce::OwnedArray<juce::Component> controls;
    std::vector<ModKnob*> knobs;
};

//==============================================================================
/** Header oscilloscope + output meter. */
class ScopeView : public juce::Component
{
public:
    explicit ScopeView (SynthProcessor& p);
    void paint (juce::Graphics&) override;
    void refresh();
private:
    SynthProcessor& proc;
    std::array<float, 256> trace {};
    float levelL = 0.0f, levelR = 0.0f;
};
} // namespace ers::ui
