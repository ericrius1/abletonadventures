#pragma once

#include <aakit/AdventureKit.h>
#include "BounceMath.h"

class BoingProcessor : public aa::PluginBase
{
public:
    BoingProcessor();
    ~BoingProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override { return isMonoOrStereoEffectLayout (layouts); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /** Echo schedule for the current parameter values (callable from any thread). */
    boing::Schedule currentSchedule() const;
    float toneAmount() const { return tone->load() / 100.0f; }

    // ---- UI feed -------------------------------------------------------------
    struct Hit { float amplitude; };
    aa::dsp::SpscQueue<Hit, 64> hits;
    std::atomic<float> inputLevel { 0.0f }, wetLevel { 0.0f };
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<bool> testDropRequested { false };

private:
    double firstIntervalSeconds (double bpm) const;

    std::atomic<float>* time = nullptr;
    std::atomic<float>* sync = nullptr;
    std::atomic<float>* division = nullptr;
    std::atomic<float>* bounciness = nullptr;
    std::atomic<float>* bounces = nullptr;
    std::atomic<float>* damping = nullptr;
    std::atomic<float>* mode = nullptr;
    std::atomic<float>* tone = nullptr;
    std::atomic<float>* wobble = nullptr;
    std::atomic<float>* spread = nullptr;
    std::atomic<float>* rethrow = nullptr;
    std::atomic<float>* duck = nullptr;
    std::atomic<float>* mix = nullptr;

    aa::dsp::Transport transport;
    aa::dsp::DelayLine lineL, lineR;

    struct Tap
    {
        float delay = 1.0f, targetDelay = 1.0f;
        float gain = 0.0f, targetGain = 0.0f;
        float lpL = 0.0f, lpR = 0.0f, hpL = 0.0f, hpR = 0.0f;
        float lpCoeff = 1.0f, hpCoeff = 0.0f;
        float pan = 0.0f;
        float wobblePhase = 0.0f, wobbleRate = 1.0f;
    };
    std::array<Tap, boing::maxBounces> taps;
    int lastActiveTap = 0;

    float sr = 44100.0f;
    float fastEnv = 0.0f, slowEnv = 0.0f, duckEnv = 0.0f;
    int refractory = 0;
    float fbL = 0.0f, fbR = 0.0f;
    float delaySmooth = 0.001f, gainSmooth = 0.001f;
    bool firstBlock = true;

    // test "tok" injected when the stage is clicked
    float testPhase = 0.0f, testEnv = 0.0f, testFreq = 0.0f;

    aa::dsp::PeakMeter inMeter, wetMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoingProcessor)
};
