#pragma once

#include <aakit/AdventureKit.h>
#include "BuiltinSources.h"

class DandelionProcessor : public aa::PluginBase
{
public:
    static constexpr int numVoices = 8;
    static constexpr int maxGrains = 256;
    static constexpr int userSourceIndex = dandelion::BuiltinBank::count; // "Your Sample"

    DandelionProcessor();
    ~DandelionProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override { return isInstrumentLayout (layouts); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void saveExtraState (juce::ValueTree&) override;
    void loadExtraState (const juce::ValueTree&) override;

    // ---- sample management (message thread) ------------------------------------
    bool loadSampleFile (const juce::File& file, juce::String& error);
    void clearUserSample();
    /** The source currently selected (for the waveform display). Message thread only. */
    const dandelion::SampleData* getDisplaySource() const;
    bool hasUserSample() const { return userSample.load() != nullptr; }
    juce::String getUserSampleName() const;
    std::atomic<int> sourceVersion { 0 };   // bumps whenever the displayed source may have changed
    juce::AudioFormatManager formatManager;

    // ---- UI feed -------------------------------------------------------------
    struct GrainEvent { float position, pan, amp; int octave; };
    aa::dsp::SpscQueue<GrainEvent, 1024> grainEvents;
    std::array<std::atomic<float>, numVoices> voicePosition {};   // < 0 when idle
    std::atomic<float> windNow { 0.0f }, outputLevel { 0.0f };
    juce::MidiKeyboardState keyboardState;

private:
    struct Grain
    {
        bool active = false;
        int voice = 0;
        double pos = 0.0, inc = 1.0;
        int length = 1, age = 0;
        float gainL = 0.0f, gainR = 0.0f, shape = 0.0f;
        int fadeLeft = -1;          // >= 0 while fading out after its voice was stolen
    };

    struct Voice
    {
        int note = -1;
        float velocity = 0.0f;
        bool held = false, sustained = false;
        aa::dsp::Adsr env;
        float envValue = 0.0f, prevEnvValue = 0.0f;
        double scan = 0.0;          // 0..1 position in the source
        double grainCountdown = 0.0;
        uint32_t startOrder = 0;
        bool isFree() const { return ! env.isActive(); }
    };

    const dandelion::SampleData* activeSource() const;
    void handleMidi (const juce::MidiMessage& m);
    void noteOn (int note, float velocity);
    void noteOff (int note);
    void spawnGrain (int voiceIndex, const dandelion::SampleData& src, float windAmt, int rootNote);
    void renderGrains (const dandelion::SampleData& src, float* outL, float* outR, int numSamples);
    void retireOldSamples();
    void setUserSample (std::unique_ptr<dandelion::SampleData> sample, const juce::File& file, juce::MemoryBlock flac);

    juce::SharedResourcePointer<dandelion::BuiltinBank> bank;

    // user sample: swapped atomically, old ones retired after a grace period (never freed on the audio thread)
    std::atomic<dandelion::SampleData*> userSample { nullptr };
    std::vector<std::pair<std::unique_ptr<dandelion::SampleData>, juce::uint32>> retired;
    juce::CriticalSection sampleLock; // non-audio threads only
    juce::File userSampleFile;
    juce::MemoryBlock userSampleFlac;

    std::array<Voice, numVoices> voices;
    std::array<Grain, maxGrains> grains;
    uint32_t noteCounter = 0;
    bool sustainPedal = false;
    float pitchBend = 0.0f, modWheel = 0.0f;

    std::array<float, 1025> hannTable {}, percTable {};
    aa::dsp::Rng rng { 0xDA4DE110u };
    aa::dsp::Lfo windLfo;
    aa::dsp::Svf filterL, filterR;
    aa::dsp::FdnReverb reverb;
    aa::dsp::Smoother cutoffSmooth, volumeSmooth;
    juce::AudioBuffer<float> wetBuffer;
    float sr = 44100.0f;
    double scanTime = 0.0;
    aa::dsp::PeakMeter meter;

    std::atomic<float>* position = nullptr;
    std::atomic<float>* spray = nullptr;
    std::atomic<float>* grainSize = nullptr;
    std::atomic<float>* density = nullptr;
    std::atomic<float>* drift = nullptr;
    std::atomic<float>* seeds = nullptr;
    std::atomic<float>* wind = nullptr;
    std::atomic<float>* reverbMix = nullptr;
    std::atomic<float>* shimmer = nullptr;
    std::atomic<float>* reverse = nullptr;
    std::atomic<float>* shape = nullptr;
    std::atomic<float>* width = nullptr;
    std::atomic<float>* cutoff = nullptr;
    std::atomic<float>* attack = nullptr;
    std::atomic<float>* release = nullptr;
    std::atomic<float>* volume = nullptr;
    std::atomic<float>* source = nullptr;
    std::atomic<float>* rootKey = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DandelionProcessor)
};
