#pragma once

#include <aakit/AdventureKit.h>

namespace tapeui
{
/** The warm 70s palette. */
namespace palette
{
    const juce::Colour cream      { 0xfff3e3c3 };
    const juce::Colour creamLight { 0xfffbf2de };
    const juce::Colour paper      { 0xfff8eed6 };
    const juce::Colour orange     { 0xffe8743b };
    const juce::Colour mustard    { 0xffe0a526 };
    const juce::Colour brown      { 0xff4a3426 };
    const juce::Colour brownDark  { 0xff2c1f17 };
    const juce::Colour teal       { 0xff2c7a7b };
    const juce::Colour tealDark   { 0xff1c5354 };
    const juce::Colour ink        { 0xff35261d };
    const juce::Colour red        { 0xffd9432b };
    const juce::Colour lampGreen  { 0xff59d9a0 };
}

/** Renders a component-sized ARGB image at the given physical scale (for cached artwork). */
juce::Image makeLayer (int width, int height, float scale);

/** Text as a filled path, centred on a point, optionally rotated (handwriting). */
void drawTextCentred (juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Point<float> centre,
                      float rotation, juce::Colour colour);

void drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius, float angle);

//==============================================================================
/** Chrome-skirted bakelite knobs with tick marks that light up in the knob's accent colour. */
class TapeLookAndFeel : public aa::LookAndFeel
{
public:
    explicit TapeLookAndFeel (const aa::Theme& t) : aa::LookAndFeel (t) {}

    void drawKnob (juce::Graphics&, juce::Rectangle<float> bounds, float proportion, bool bipolar, bool hovered,
                   bool dragging, juce::Colour accent, aa::Knob&) override;
    void drawKnobCaption (juce::Graphics&, juce::Rectangle<float> area, const juce::String& text, bool showingValue,
                          juce::Colour accent, aa::Knob&) override;
};

//==============================================================================
/** Classic moving-coil VU meter with needle ballistics (spring/damper, overshoot, peg bounce). */
class VuMeter : public juce::Component
{
public:
    explicit VuMeter (const juce::String& label);

    /** Feed a mean-square level (0 VU = -14 dBFS RMS). */
    void setLevel (float meanSquare);
    void tick (double dt);

    void paint (juce::Graphics&) override;
    void resized() override { face = {}; glass = {}; }

private:
    struct Geometry { juce::Rectangle<float> face; juce::Point<float> pivot; float radius, halfAngle; };
    Geometry geometry() const;
    juce::Point<float> pointOnArc (const Geometry& geo, float pos, float radius) const;
    void rebuild (float scale);

    juce::String label;
    juce::Image face, glass;
    float layerScale = 0.0f;
    float target = 0.0f, pos = 0.0f, vel = 0.0f, drawnPos = -1.0f;
};

//==============================================================================
/** A jewel indicator lamp with a soft halo. */
class Lamp : public juce::Component
{
public:
    Lamp (const juce::String& label, juce::Colour colour);
    void setLevel (float newLevel);
    void paint (juce::Graphics&) override;

private:
    juce::String label;
    juce::Colour colour;
    float level = 0.0f;
};

//==============================================================================
/** Chunky cassette-deck piano key bound to the Tape Stop parameter. */
class TransportKey : public juce::ToggleButton
{
public:
    TransportKey (juce::AudioProcessorValueTreeState& state, const juce::String& paramID);
    ~TransportKey() override;
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

//==============================================================================
/** The hero: an illustrated cassette whose reels spin with the (wobbly) tape speed. */
class CassetteView : public juce::Component, public juce::SettableTooltipClient
{
public:
    CassetteView();

    void setTrackName (const juce::String& name);
    /** The cassette shows its character: Age yellows it, Wear scuffs it, Hiss adds grain, dropouts dim the light. */
    void setCharacter (float age, float wear, float hiss, float dropout);
    /** speed: playback speed ratio incl. wow/flutter; motor: 0..1 tape-stop motor speed. */
    void tick (double dt, float speed, float motor);

    void paint (juce::Graphics&) override;
    void resized() override { under = {}; over = {}; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; }
    void mouseExit (const juce::MouseEvent&) override { hover = false; }

    std::function<void (bool)> onHold; // press-and-hold brakes the tape

    static constexpr float designW = 400.0f, designH = 256.0f;

private:
    juce::AffineTransform designTransform() const;
    void rebuild (float scale);
    void drawShell (juce::Graphics&);
    void drawInterior (juce::Graphics&);
    void drawReel (juce::Graphics&, juce::Point<float> centre, float packRadius, float angle, bool leftSide);
    float packRadius (bool left) const;

    juce::String trackName;
    juce::Image under, over;
    float layerScale = 0.0f;

    float ageAmt = 0.0f, wearAmt = 0.0f, hissAmt = 0.0f, dropAmt = 0.0f;
    float angleL = 0.0f, angleR = 0.8f;
    float progress = 0.34f, direction = 1.0f, visualSpeed = 1.0f;
    double clock = 0.0;
    bool hover = false;

    struct Mote { juce::Point<float> p; float vx, vy, size, phase; };
    std::array<Mote, 16> motes;
    juce::Random rng { 7 };
};
} // namespace tapeui
