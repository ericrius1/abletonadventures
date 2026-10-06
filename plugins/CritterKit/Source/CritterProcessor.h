#pragma once

#include <aakit/AdventureKit.h>
#include "CritterDsp.h"
#include "CritterSamples.h"

class CritterProcessor : public aa::PluginBase
{
public:
    CritterProcessor();
    ~CritterProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override { return isInstrumentLayout (layouts); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static juce::String voiceParamId (int voice, const char* what) { return "k" + juce::String (voice + 1) + "_" + what; }

    //==============================================================================
    // Extra (non-parameter) state: the pattern and the eaten samples.
    void saveExtraState (juce::ValueTree&) override;
    void loadExtraState (const juce::ValueTree&) override;
    void resetExtraState() override;

    //==============================================================================
    // Pattern: one atomic word per row, low 16 bits = step on, high 16 bits = accent.
    bool isStepOn (int row, int step) const noexcept;
    bool isStepAccent (int row, int step) const noexcept;
    void setStep (int row, int step, bool on, bool accent);
    uint32_t getRowBits (int row) const noexcept { return rows[(size_t) row].load (std::memory_order_relaxed); }
    void setRowBits (int row, uint32_t bits);
    void setRowPattern (int row, const juce::String& text); // "X..x" ('x' on, 'X' accent, '.' off)
    juce::String getRowPattern (int row) const;
    std::atomic<uint32_t> patternVersion { 0 };

    //==============================================================================
    // Samples ("feeding" a critter). Message thread / background threads only.
    bool canLoadFile (const juce::String& path) { return critter::SampleCodec::canLoad (formats, path); }
    juce::String getAudioWildcard() const { return formats.getWildcardForAllFormats(); }
    void loadSampleAsync (int voice, const juce::File& file);
    void clearSample (int voice);
    /** Message thread only (the pointer stays valid until the next collectGarbage() call). */
    const critter::SampleData* getSample (int voice) const noexcept { return sampleSlots[(size_t) voice].load(); }
    void collectGarbage();
    std::array<std::atomic<int>, critter::numVoices> sampleVersion {};
    std::array<std::atomic<int>, critter::numVoices> loadFailures {}; // bumps when a file couldn't be eaten
    std::array<std::atomic<bool>, critter::numVoices> loading {};
    std::atomic<bool> hostNotifyPending { false }; // a background load finished: editor tells the host

    /** Tell the host that pattern/sample state changed (so the set gets marked as modified). */
    void notifyStateChanged();

    //==============================================================================
    // UI feed
    struct Hit { int voice = 0; float velocity = 1.0f; };
    aa::dsp::SpscQueue<Hit, 256> hits;
    std::atomic<uint32_t> auditionMask { 0 };
    std::atomic<int> uiStep { -1 };
    std::atomic<double> uiStepPhase { 0.0 }; // 0..1 progress through the current step
    std::atomic<bool> uiRunning { false };
    int selectedCritter = 0; // editor state, message thread only

private:
    void installSample (int voice, std::unique_ptr<critter::SampleData> data);
    void retire (critter::SampleData* old);
    void addEvent (int offset, int voice, float velocity, float detune, int type);

    std::atomic<float>* volume = nullptr;
    std::atomic<float>* drive = nullptr;
    std::atomic<float>* swing = nullptr;
    std::atomic<float>* room = nullptr;
    std::atomic<float>* accent = nullptr;
    std::atomic<float>* humanize = nullptr;
    std::atomic<float>* pitch = nullptr;
    std::atomic<float>* seqOn = nullptr;

    struct VoiceParams
    {
        std::atomic<float>* tune = nullptr;
        std::atomic<float>* decay = nullptr;
        std::atomic<float>* tone = nullptr;
        std::atomic<float>* snap = nullptr;
        std::atomic<float>* level = nullptr;
        std::atomic<float>* pan = nullptr;
        juce::RangedAudioParameter* decayParam = nullptr;
    };
    std::array<VoiceParams, critter::numVoices> voiceParams;

    std::array<std::atomic<uint32_t>, critter::numVoices> rows {};

    // ---- samples ---------------------------------------------------------------
    juce::AudioFormatManager formats;
    std::array<std::atomic<critter::SampleData*>, critter::numVoices> sampleSlots {};
    std::atomic<uint32_t> audioEpoch { 0 };  // odd while processBlock runs
    struct Retired { std::unique_ptr<critter::SampleData> data; uint32_t epoch; };
    std::vector<Retired> graveyard;
    juce::CriticalSection graveyardLock;     // never taken by the audio thread
    std::atomic<int> graveyardSize { 0 };
    std::unique_ptr<juce::ThreadPool> loaderPool; // created on the first file load

    // ---- audio -----------------------------------------------------------------
    struct Event { int offset; int voice; float velocity; float detune; int type; };
    static constexpr int maxEvents = 512;
    std::array<Event, maxEvents> events {};
    int numEvents = 0;

    aa::dsp::Transport transport;
    std::array<critter::Voice, critter::numVoices> voices;
    aa::dsp::FdnReverb reverb;
    float lastRoom = -1.0f;
    aa::dsp::Smoother driveSm, roomSm, volumeSm;
    aa::dsp::Rng humanRng { 0xC0C0A5u };
    float scratchL[critter::maxChunk] {}, scratchR[critter::maxChunk] {};
    float sr = 44100.0f;
    int64_t lastStepIndex = std::numeric_limits<int64_t>::min();
    double expectedPpq = 0.0;
    float dcL = 0.0f, dcR = 0.0f, dcInL = 0.0f, dcInR = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CritterProcessor)
};
