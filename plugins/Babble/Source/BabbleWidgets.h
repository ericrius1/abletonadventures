#pragma once

#include "BabbleProcessor.h"

namespace babble::ui
{
//==============================================================================
/** Warm sunset palette + a deep plum "ink" for the cartoon outlines. */
namespace palette
{
    const juce::Colour coral { 0xffff7a6b };
    const juce::Colour peach { 0xffffc9a3 };
    const juce::Colour teal { 0xff2ec4b6 };
    const juce::Colour cream { 0xfffff4e6 };
    const juce::Colour plum { 0xff5b2a86 };
    const juce::Colour ink { 0xff3a1a57 };
    const juce::Colour sunshine { 0xffffcf6b };
    const juce::Colour mouth { 0xff4a1f45 };
    const juce::Colour tongue { 0xffff8e8e };
} // namespace palette

//==============================================================================
/** Cartoon mouth description, interpolated from per-vowel keyframes. */
struct MouthShape
{
    float width = 0.6f;   // fraction of the mouth "size"
    float height = 0.6f;  // opening when fully open
    float smile = 0.1f;   // corner lift (-1..1)
    float round = 0.5f;   // 0 = lens-shaped corners, 1 = round
    float teeth = 0.5f;   // upper teeth visibility
    float pucker = 0.0f;  // lip thickness (O/U)
};

MouthShape mouthForVowel (float vowel);

/** Draws a mouth centred at c. open: 0 = closed smile line, 1 = fully open. robot > 0.5 swaps the
    tongue for a glowing speaker grille. */
void drawMouth (juce::Graphics& g, juce::Point<float> c, float size, const MouthShape& m, float open,
                float inkWidth, float robot = 0.0f, float glow = 0.0f);

/** Text drawn as a path with an outline (sticker style). */
void drawOutlinedText (juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Point<float> baseline,
                       juce::Colour fill, juce::Colour outline, float outlineWidth);

//==============================================================================
/** Chunky cream knobs with plum ink outlines and hard "sticker" shadows. */
class BabbleLookAndFeel : public aa::LookAndFeel
{
public:
    explicit BabbleLookAndFeel (const aa::Theme& t) : aa::LookAndFeel (t) {}

    void drawKnob (juce::Graphics&, juce::Rectangle<float> bounds, float proportion, bool bipolar, bool hovered,
                   bool dragging, juce::Colour accent, aa::Knob&) override;
    void drawKnobCaption (juce::Graphics&, juce::Rectangle<float> area, const juce::String& text, bool showingValue,
                          juce::Colour accent, aa::Knob&) override;
    juce::Rectangle<float> drawPanel (juce::Graphics&, juce::Rectangle<float> bounds, const juce::String& title,
                                      juce::Colour tint) override;
};

//==============================================================================
/** XY "mouth pad": x = vowel (A E I O U), y = formant shift (throat size). The puck is a little
    mouth that changes shape; a teal ghost shows the vowel actually being sung (babble included). */
class MouthPad : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit MouthPad (BabbleProcessor& p);
    ~MouthPad() override;

    void tick (double dt);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }

private:
    juce::Rectangle<float> padArea() const;
    juce::Point<float> toScreen (float vowelPos, float shiftSt) const;
    void setFromPosition (juce::Point<float> pos);

    BabbleProcessor& proc;
    juce::RangedAudioParameter* vowelParam = nullptr;
    juce::RangedAudioParameter* shiftParam = nullptr;
    std::unique_ptr<juce::ParameterAttachment> vowelAttachment, shiftAttachment;
    float vowel = 0.0f, shift = 0.0f;
    float ghostVowel = 0.0f, ghostAlpha = 0.0f;
    std::array<juce::Point<float>, 18> trail {};
    int trailCount = 0;
    float trailTimer = 0.0f;
    bool hover = false, dragging = false;
};

//==============================================================================
/** 3 x 2 grid of pills for the voice type. */
class VoicePicker : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit VoicePicker (BabbleProcessor& p);
    ~VoicePicker() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverIndex = -1; repaint(); }

private:
    juce::Rectangle<float> cell (int index) const;
    int indexAt (juce::Point<float> p) const;

    std::unique_ptr<juce::ParameterAttachment> attachment;
    int index = 0, hoverIndex = -1;
};

//==============================================================================
/** Four little singers: click one to set how many choir voices sing each note. */
class ChoirPicker : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ChoirPicker (BabbleProcessor& p);
    ~ChoirPicker() override;

    void tick (double dt);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverIndex = -1; repaint(); }

private:
    juce::Rectangle<float> faceArea (int index) const;
    int indexAt (juce::Point<float> p) const;

    BabbleProcessor& proc;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    int count = 3, hoverIndex = -1;
    float clock = 0.0f, open = 0.0f;
    std::array<float, babble::maxChoir> pop {};
};
} // namespace babble::ui
