#pragma once

#include <aakit/AdventureKit.h>
#include "StardustEngine.h"

class StardustProcessor : public aa::PluginBase
{
public:
    StardustProcessor();
    ~StardustProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override { return isInstrumentLayout (layouts); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // ---- UI feed -------------------------------------------------------------
    juce::MidiKeyboardState keyboardState;   // on-screen keys (merged into the MIDI stream in processBlock)
    stardust::Engine engine;                 // note + glint event queues, voice count, envelope levels

    static constexpr int scopeSize = 4096;
    std::array<std::atomic<float>, scopeSize> scopeL {}, scopeR {};
    std::atomic<int> scopeWrite { 0 };
    std::atomic<float> outputLevelL { 0.0f }, outputLevelR { 0.0f };

private:
    stardust::Settings readSettings() const;
    void renderRange (juce::AudioBuffer<float>& buffer, int start, int end);
    void processFx (float* left, float* right, int n);

    // parameters
    std::atomic<float>* pCutoff = nullptr;
    std::atomic<float>* pResonance = nullptr;
    std::atomic<float>* pFilterEnv = nullptr;
    std::atomic<float>* pShapeA = nullptr;
    std::atomic<float>* pDetune = nullptr;
    std::atomic<float>* pTwinkle = nullptr;
    std::atomic<float>* pGravity = nullptr;
    std::atomic<float>* pSpaceMix = nullptr;
    std::atomic<float>* pAAttack = nullptr;
    std::atomic<float>* pADecay = nullptr;
    std::atomic<float>* pASustain = nullptr;
    std::atomic<float>* pARelease = nullptr;
    std::atomic<float>* pDrive = nullptr;
    std::atomic<float>* pKeyTrack = nullptr;
    std::atomic<float>* pFilterType = nullptr;
    std::atomic<float>* pFAttack = nullptr;
    std::atomic<float>* pFDecay = nullptr;
    std::atomic<float>* pFSustain = nullptr;
    std::atomic<float>* pFRelease = nullptr;
    std::atomic<float>* pUnison = nullptr;
    std::atomic<float>* pSpread = nullptr;
    std::atomic<float>* pLevelA = nullptr;
    std::atomic<float>* pOctaveA = nullptr;
    std::atomic<float>* pShapeB = nullptr;
    std::atomic<float>* pOctaveB = nullptr;
    std::atomic<float>* pSemiB = nullptr;
    std::atomic<float>* pFineB = nullptr;
    std::atomic<float>* pLevelB = nullptr;
    std::atomic<float>* pSub = nullptr;
    std::atomic<float>* pNoise = nullptr;
    std::atomic<float>* pLfoShape = nullptr;
    std::atomic<float>* pLfoRate = nullptr;
    std::atomic<float>* pLfoSync = nullptr;
    std::atomic<float>* pLfoDivision = nullptr;
    std::atomic<float>* pLfoPitch = nullptr;
    std::atomic<float>* pLfoCutoff = nullptr;
    std::atomic<float>* pLfoShapeMod = nullptr;
    std::atomic<float>* pDrift = nullptr;
    std::atomic<float>* pVoiceMode = nullptr;
    std::atomic<float>* pGlide = nullptr;
    std::atomic<float>* pVelocity = nullptr;
    std::atomic<float>* pChorus = nullptr;
    std::atomic<float>* pSpaceSize = nullptr;
    std::atomic<float>* pVolume = nullptr;

    aa::dsp::Transport transport;
    aa::dsp::StereoChorus chorus;
    aa::dsp::FdnReverb reverb;

    std::array<float, stardust::maxChunk> synthL {}, synthR {}, glintL {}, glintR {};

    float sr = 44100.0f;
    float chorusAmt = 0.0f, chorusTarget = 0.0f;
    float spaceMix = 0.0f, spaceMixTarget = 0.0f;
    float spaceSize = 0.6f, spaceSizeTarget = 0.6f, appliedSize = -1.0f;
    aa::dsp::Smoother volume;
    float muteGain = 1.0f, muteTarget = 1.0f;
    float dcCoeff = 0.999f, dcInL = 0.0f, dcInR = 0.0f, dcOutL = 0.0f, dcOutR = 0.0f;
    bool firstBlock = true;
    aa::dsp::PeakMeter meterL, meterR;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StardustProcessor)
};
