#pragma once

#include <aakit/AdventureKit.h>
#include "CrystalEngine.h"

class CrystalProcessor : public aa::PluginBase
{
public:
    CrystalProcessor();
    ~CrystalProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override { return isMonoOrStereoEffectLayout (layouts); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    double getTailLengthSeconds() const override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static float intervalRatio (int index);
    static const juce::StringArray& intervalNames();

    // ---- UI feed (written by the audio thread, read by the editor) -------------
    struct Hit { float amplitude; };
    aa::dsp::SpscQueue<Hit, 64> hits;
    std::atomic<float> tailLevel { 0.0f };    // wet RMS
    std::atomic<float> inputLevel { 0.0f };   // dry peak
    std::atomic<float> shimmerLevel { 0.0f }; // shimmer return RMS
    std::atomic<float> freezeLevel { 0.0f };  // 0..1 freeze crossfade position

    // Clicking the cave pings a glassy "tink" through the reverb (pitch 0..1 picks a note).
    std::atomic<bool> pingRequested { false };
    std::atomic<float> pingPitch { 0.5f };

private:
    void updateEngineParams();
    void renderPing (float* left, float* right, int numSamples);

    std::atomic<float>* size = nullptr;
    std::atomic<float>* decay = nullptr;
    std::atomic<float>* shimmer = nullptr;
    std::atomic<float>* interval = nullptr;
    std::atomic<float>* damping = nullptr;
    std::atomic<float>* freeze = nullptr;
    std::atomic<float>* predelay = nullptr;
    std::atomic<float>* mix = nullptr;
    std::atomic<float>* sparkle = nullptr;
    std::atomic<float>* modulation = nullptr;
    std::atomic<float>* lowCut = nullptr;
    std::atomic<float>* highCut = nullptr;
    std::atomic<float>* width = nullptr;
    std::atomic<float>* duck = nullptr;
    std::atomic<float>* output = nullptr;

    static constexpr int scratchSize = 256;
    std::unique_ptr<crystal::CrystalReverb> reverb; // large: keep it off the processor's own footprint
    juce::AudioBuffer<float> scratch;

    float sr = 44100.0f;
    aa::dsp::Smoother dryGain, wetGain, outGain;
    float duckEnv = 0.0f, duckGain = 1.0f, fastEnv = 0.0f, slowEnv = 0.0f;
    float wetPower = 0.0f;
    int refractory = 0;
    float pingEnv = 0.0f, pingFreq = 880.0f;
    float pingPhase[3] {};
    aa::dsp::PeakMeter inMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrystalProcessor)
};
