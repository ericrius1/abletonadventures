#pragma once

#include <aakit/AdventureKit.h>
#include "TapeDSP.h"

class TapeDreamsProcessor : public aa::PluginBase
{
public:
    TapeDreamsProcessor();
    ~TapeDreamsProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override { return isMonoOrStereoEffectLayout (layouts); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // ---- UI feed (audio thread writes, UI reads) ---------------------------------
    std::atomic<float> inputLevel { 0.0f };    // mean-square level hitting the tape (incl. drive)
    std::atomic<float> outputLevel { 0.0f };   // mean-square output level
    std::atomic<float> tapeSpeed { 1.0f };     // instantaneous playback speed ratio (wow, flutter, crinkle, stop)
    std::atomic<float> motorSpeed { 1.0f };    // tape-stop motor speed 0..1
    std::atomic<float> dropoutLevel { 0.0f };  // current wear dropout depth
    std::atomic<float> recGlow { 0.0f };       // how hard the tape is being hit (0..1)

private:
    void resetState();
    void processChunk (float* const* io, int numChannels, int n);

    std::atomic<float>* wow = nullptr;
    std::atomic<float>* flutter = nullptr;
    std::atomic<float>* drive = nullptr;
    std::atomic<float>* age = nullptr;
    std::atomic<float>* wear = nullptr;
    std::atomic<float>* hiss = nullptr;
    std::atomic<float>* tapeStop = nullptr;
    std::atomic<float>* mix = nullptr;
    std::atomic<float>* squash = nullptr;
    std::atomic<float>* crinkle = nullptr;
    std::atomic<float>* hum = nullptr;
    std::atomic<float>* mains = nullptr;
    std::atomic<float>* inputGain = nullptr;
    std::atomic<float>* outputGain = nullptr;
    std::atomic<float>* stopTime = nullptr;

    static constexpr int maxChunk = 512;

    float sr = 44100.0f;
    int osOrder = 2;
    float osLatency = 0.0f;
    int latency = 0;
    float wetCentre = 0.0f;       // wet read delay (samples) that lines up with the dry path
    float maxLag = 0.0f;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    juce::AudioBuffer<float> work;
    std::array<float, maxChunk> driveG {}, driveB {}, driveOff {};

    std::array<float, 65> compTable {};

    // per-channel state
    struct Channel
    {
        tape::Biquad preEmph, deEmph, bump;
        aa::dsp::OnePole dcBlock;
        tape::SincDelayLine wetLine, dryLine;
        aa::dsp::Svf ageLow, ageHigh, stopLow;
        aa::dsp::OnePole dropTone, hissHigh, hissLow;
        float lastDelay = 0.0f;
    };
    std::array<Channel, 2> chans;

    tape::WowFlutter wowFlutter;
    tape::Crinkle crinkleGen;
    tape::Dropouts dropouts;
    tape::Squash squashComp;
    aa::dsp::Rng noise { 0x5EEDu };

    aa::dsp::Smoother inGainSm, outGainSm, mixSm, driveSm, hissSm, humSm, ageSm, wearSm;
    aa::dsp::Smoother wowSm, flutterSm, crinkleSm, squashSm;

    // tape stop
    float stopPhase = 0.0f;        // 0 = running, 1 = halted
    float motor = 1.0f;            // smoothed speed
    float lagA = 0.0f, lagB = 0.0f;
    float xfade = -1.0f;           // < 0: no crossfade running
    float xfadeInc = 0.0f;

    float lastChunkSpeed = 1.0f;
    float humPhase = 0.0f;
    float hissEnv = 0.0f, hissEnvA = 0.0f, hissEnvR = 0.0f;
    float inMs = 0.0f, outMs = 0.0f, meterCoeff = 0.0f;
    float glow = 0.0f, glowRelease = 0.0f;
    int filterCountdown = 0;
    float lastAge = -1.0f, lastMotorForFilter = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapeDreamsProcessor)
};
