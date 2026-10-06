#pragma once

#include "BabbleWidgets.h"

namespace babble::ui
{
/** The singer: a big cartoon head on a teal sunburst stage. The mouth follows the vowel being
    sung, the eyes blink and wander, eyebrows wiggle with vibrato, cheeks blush with loudness,
    the head bobs on every note and a speech bubble shows the babbled syllables. */
class FaceStage : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit FaceStage (BabbleProcessor& p);

    void tick (double dt);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    std::function<void (float velocity)> onNote; // fired for every note-on (drives the title hop)

    /** 0..1: how much the character is singing right now (for the title wobble). */
    float getSinging() const noexcept { return singing; }

private:
    struct Note
    {
        juce::Point<float> pos, vel;
        float age = 0.0f, life = 2.0f, size = 14.0f, phase = 0.0f, spin = 0.0f;
        int colour = 0;
        bool beamed = false;
    };

    void rebuildBackground (float scale);
    void drawBody (juce::Graphics&, float bob);
    void drawHead (juce::Graphics&);
    void drawEyes (juce::Graphics&, juce::Point<float> headCentre, float rx, float ry);
    void drawHair (juce::Graphics&, juce::Point<float> headCentre, float rx, float ry);
    void drawCostume (juce::Graphics&, juce::Point<float> headCentre, float rx, float ry, bool front);
    void drawBubble (juce::Graphics&);
    void drawNotes (juce::Graphics&);
    void pushSyllable (const juce::String& s);
    void spawnNote();

    BabbleProcessor& proc;
    std::atomic<float>* voiceParam = nullptr;
    std::atomic<float>* babbleParam = nullptr;

    juce::Image background;
    juce::Point<float> headCentre;
    float headRx = 110.0f, headRy = 102.0f;

    double clock = 0.0;
    float bounce = 0.0f, bounceVel = 0.0f, squash = 0.0f, squashVel = 0.0f;
    float tilt = 0.0f, tiltVel = 0.0f, tiltTarget = 0.0f;
    float tuft = 0.0f, tuftVel = 0.0f;
    float blink = 0.0f, blinkTimer = 2.0f, blinkPhase = -1.0f;
    bool doubleBlink = false;
    juce::Point<float> look, lookTarget;
    float lookTimer = 1.0f;

    float vowel = 0.0f, open = 0.0f, lips = 0.0f, level = 0.0f, vibrato = 0.0f;
    float singing = 0.0f, surprise = 0.0f, giggle = 0.0f, poke = 0.0f, sleep = 0.0f;
    int voiceType = 2;

    std::vector<Note> notes;
    float noteTimer = 0.0f;
    int noteSide = 0;

    juce::StringArray syllables;
    float bubblePop = 0.0f, bubblePopVel = 0.0f, bubbleAlpha = 0.0f, silence = 10.0f, hintAlpha = 0.0f;
    juce::String sungText;

    juce::Random random;
};
} // namespace babble::ui
