#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace aa::dsp
{
/** Reads the host transport each block. When the host isn't playing, it keeps a free-running
    musical clock at the host tempo so tempo-synced plugins still do something sensible. */
struct Transport
{
    double bpm = 120.0;
    double ppqAtBlockStart = 0.0;   // quarter notes
    bool hostPlaying = false;
    bool justStarted = false;
    double sampleRate = 44100.0;
    int timeSigNumerator = 4, timeSigDenominator = 4;

    void prepare (double sr) { sampleRate = sr; freePpq = 0.0; wasPlaying = false; }

    void update (juce::AudioPlayHead* playHead, int numSamples)
    {
        bool playing = false;
        double ppq = freePpq;

        if (playHead != nullptr)
        {
            if (auto pos = playHead->getPosition())
            {
                if (auto b = pos->getBpm())
                    bpm = juce::jlimit (20.0, 999.0, *b);
                if (auto sig = pos->getTimeSignature())
                {
                    timeSigNumerator = juce::jmax (1, sig->numerator);
                    timeSigDenominator = juce::jmax (1, sig->denominator);
                }
                playing = pos->getIsPlaying();
                if (playing)
                    if (auto p = pos->getPpqPosition())
                        ppq = *p;
            }
        }

        justStarted = playing && ! wasPlaying;
        wasPlaying = playing;
        hostPlaying = playing;
        ppqAtBlockStart = ppq;

        // Keep the free-running clock moving (and aligned with the host when playing).
        freePpq = ppq + samplesToPpq (numSamples);
    }

    double samplesToPpq (double samples) const { return samples * bpm / (60.0 * sampleRate); }
    double ppqToSamples (double ppq) const { return ppq * 60.0 * sampleRate / bpm; }
    double samplesPerBeat() const { return 60.0 * sampleRate / bpm; }

private:
    double freePpq = 0.0;
    bool wasPlaying = false;
};

/** Common tempo-sync divisions, in quarter notes. */
inline const juce::StringArray& syncDivisionNames()
{
    static const juce::StringArray names { "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D",
                                           "1/4T", "1/4", "1/4D", "1/2", "1/1" };
    return names;
}

inline double syncDivisionBeats (int index)
{
    static const double beats[] = { 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75,
                                    2.0 / 3.0, 1.0, 1.5, 2.0, 4.0 };
    return beats[juce::jlimit (0, 11, index)];
}
} // namespace aa::dsp
