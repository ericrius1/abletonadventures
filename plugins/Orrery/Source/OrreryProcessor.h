#pragma once

#include <aakit/AdventureKit.h>
#include "OrreryMusic.h"
#include "BellVoice.h"

/** Orrery: six planets orbit a sun; every time a planet crosses one of its orbit's trigger points it
    plays a note. Planet positions are derived from the host's musical position, so the
    polyrhythms lock to the grid. Notes are played by the built-in bell and also sent out as MIDI. */
class OrreryProcessor : public aa::PluginBase
{
public:
    OrreryProcessor();
    ~OrreryProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override { return isInstrumentLayout (layouts); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    //==============================================================================
    // UI feed (audio thread writes, message thread reads)
    struct NoteEvent
    {
        int orbit = 0, pulse = 0, note = 60;
        float velocity = 1.0f;
        double ppq = 0.0;
    };
    aa::dsp::SpscQueue<NoteEvent, 512> noteEvents;

    enum RunState { stopped = 0, hostPlaying = 1, freeRunning = 2 };
    std::atomic<double> uiPpq { 0.0 };        // musical position at the end of the last block
    std::atomic<double> uiWallMs { 0.0 };     // when that block was processed
    std::atomic<float> uiBpm { 120.0f };
    std::atomic<int> uiRunState { stopped };
    std::atomic<int> uiBeatsPerBar { 4 };
    std::atomic<int> uiJumpCount { 0 };
    std::atomic<float> uiLevel { 0.0f };
    std::atomic<juce::uint32> uiChord[4] {};  // 128-bit mask of the chord currently steering the notes

    /** Pitch that orbit `orbit` would play right now (message thread). */
    int currentNoteForOrbit (int orbit) const;
    /** Note names of the chord being followed, or empty when using the scale. */
    juce::String followedChordText() const;

    int selectedOrbit = 0; // editor state, kept here so it survives closing the window

    // Parameter pointers (public for the editor's read-only use)
    struct OrbitParams
    {
        std::atomic<float>* on = nullptr;
        std::atomic<float>* beats = nullptr;
        std::atomic<float>* pulses = nullptr;
        std::atomic<float>* offset = nullptr;
        std::atomic<float>* note = nullptr;
        std::atomic<float>* octave = nullptr;
        std::atomic<float>* velocity = nullptr;
        std::atomic<float>* chance = nullptr;
        std::atomic<float>* gate = nullptr;
    };
    std::array<OrbitParams, orrery::numOrbits> orbitParams;
    std::atomic<float>* scale = nullptr;
    std::atomic<float>* root = nullptr;
    std::atomic<float>* speed = nullptr;
    std::atomic<float>* freeRun = nullptr;
    std::atomic<float>* follow = nullptr;

private:
    void renderVoices (float* left, float* right, int start, int num);
    void startVoice (int note, float velocity, float pan, int source, int& voiceIndexOut, juce::uint32& idOut);
    void releaseVoice (int voiceIndex, juce::uint32 id);
    void releaseKeyboardVoices (int note);
    void noteOffAll (juce::MidiBuffer& out, int sample);
    void endGeneratedNote (int pitch, juce::MidiBuffer& out, int sample);
    void handleIncoming (const juce::MidiMessage& m, int sample, juce::MidiBuffer& out);
    void rebuildChord();
    void publishChord();
    void processFx (float* left, float* right, int numSamples);

    std::atomic<float>* sound = nullptr;
    std::atomic<float>* material = nullptr;
    std::atomic<float>* brightness = nullptr;
    std::atomic<float>* decay = nullptr;
    std::atomic<float>* spread = nullptr;
    std::atomic<float>* echo = nullptr;
    std::atomic<float>* echoTime = nullptr;
    std::atomic<float>* feedback = nullptr;
    std::atomic<float>* reverb = nullptr;
    std::atomic<float>* volume = nullptr;

    aa::dsp::Transport transport;
    double sr = 44100.0;

    // ---- sequencer --------------------------------------------------------------
    bool wasRunning = false;
    double lastEndPpq = 0.0;
    std::array<double, orrery::numOrbits> lastFirePpq {};
    std::array<bool, orrery::numOrbits> orbitWasOn {};
    std::atomic<bool> flushRequested { true }, resetRequested { false };

    struct ActiveNote
    {
        bool on = false;
        juce::int64 endSample = 0; // relative to the current block start
        int orbit = -1;
        int voice = -1;
        juce::uint32 voiceId = 0;
    };
    std::array<ActiveNote, 128> activeNotes {};
    int numActiveNotes = 0;

    struct Trigger
    {
        int sample = 0, orbit = 0, pulse = 0;
        double ppq = 0.0, intervalPpq = 1.0;
    };
    static constexpr int maxTriggers = 256;
    std::array<Trigger, maxTriggers> triggers {};
    int numTriggers = 0;

    // ---- incoming MIDI / Follow ------------------------------------------------
    std::array<bool, 128> held {};
    int numHeld = 0;
    std::array<int, orrery::maxChord> chord {};
    int chordSize = 0;
    juce::int64 chordGraceSamples = 0;
    bool chordDirty = true;

    // ---- sound -----------------------------------------------------------------
    static constexpr int numVoices = 24, maxSounding = 16;
    std::array<orrery::BellVoice, numVoices> voices;
    juce::uint32 nextVoiceId = 1;
    orrery::BellSettings bellSettings;
    aa::dsp::Rng rng { 0x0BB17u };

    juce::MidiBuffer midiOut;
    juce::AudioBuffer<float> monoScratch;

    aa::dsp::DelayLine echoL, echoR;
    float echoDelaySamples = 1000.0f;
    aa::dsp::OnePole echoLowL, echoLowR, echoHighL, echoHighR, dcBlockL, dcBlockR;
    aa::dsp::FdnReverb reverbFx;
    float lastReverbAmount = -1.0f;
    aa::dsp::Smoother echoMix, reverbMix, outGain, fbAmount;
    float flushFade = 1.0f;     // all-sound-off fade
    int flushState = 0;         // 0 idle, 1 fading down, 2 fading up
    aa::dsp::PeakMeter meter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrreryProcessor)
};
