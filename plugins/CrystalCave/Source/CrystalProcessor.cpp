#include "CrystalProcessor.h"
#include "CrystalEditor.h"

using namespace juce;
namespace P = aa::params;

namespace
{
    /** Like aa::params::floatParam, but the text parser understands "kHz", "ms" and "s" suffixes so that
        values such as "11.0 kHz" or "300 ms" round-trip correctly. */
    float parseValue (const String& text, P::Unit unit)
    {
        const auto t = text.trim().toLowerCase();
        float v = t.retainCharacters ("0123456789.-").getFloatValue();
        if (unit == P::Unit::hz && t.containsChar ('k'))
            v *= 1000.0f;
        else if (unit == P::Unit::seconds && t.endsWith ("ms"))
            v *= 0.001f;
        else if (unit == P::Unit::ms && t.endsWith ("s") && ! t.endsWith ("ms"))
            v *= 1000.0f;
        return v;
    }

    std::unique_ptr<AudioParameterFloat> unitParam (const String& id, const String& name, float min, float max,
                                                    float def, P::Unit unit, float skewCentre = -1.0f)
    {
        NormalisableRange<float> range (min, max);
        if (skewCentre > min && skewCentre < max)
            range.setSkewForCentre (skewCentre);

        return std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, range, def,
            AudioParameterFloatAttributes()
                .withStringFromValueFunction ([unit] (float v, int) { return P::formatValue (v, unit); })
                .withValueFromStringFunction ([unit] (const String& s) { return parseValue (s, unit); }));
    }
} // namespace

const StringArray& CrystalProcessor::intervalNames()
{
    static const StringArray names { "+12 Octave", "+7 Fifth", "+19 Octave + Fifth", "+24 Two Octaves", "-12 Octave Down" };
    return names;
}

float CrystalProcessor::intervalRatio (int index)
{
    switch (index)
    {
        case 1:  return 1.4983071f; // +7 semitones
        case 2:  return 2.9966142f; // +19
        case 3:  return 4.0f;       // +24
        case 4:  return 0.5f;       // -12
        default: return 2.0f;       // +12
    }
}

AudioProcessorValueTreeState::ParameterLayout CrystalProcessor::createLayout()
{
    // The first eight are the most playable: Push shows parameters in banks of eight, in this order.
    P::Layout layout;
    layout.add (P::percent ("size", "Size", 65.0f));
    layout.add (unitParam ("decay", "Decay", 0.3f, 30.0f, 4.5f, P::Unit::seconds, 4.0f));
    layout.add (P::percent ("shimmer", "Shimmer", 30.0f));
    layout.add (P::choice ("interval", "Interval", intervalNames(), 0));
    layout.add (P::percent ("damping", "Damping", 35.0f));
    layout.add (P::toggle ("freeze", "Freeze", false));
    layout.add (unitParam ("predelay", "Pre-Delay", 0.0f, 250.0f, 18.0f, P::Unit::ms, 60.0f));
    layout.add (P::percent ("mix", "Mix", 35.0f));
    layout.add (P::percent ("sparkle", "Sparkle", 40.0f));
    layout.add (P::percent ("modulation", "Modulation", 40.0f));
    layout.add (unitParam ("lowcut", "Low Cut", 20.0f, 1000.0f, 140.0f, P::Unit::hz, 150.0f));
    layout.add (unitParam ("highcut", "High Cut", 1000.0f, 20000.0f, 11000.0f, P::Unit::hz, 5000.0f));
    layout.add (P::percent ("width", "Width", 100.0f));
    layout.add (P::percent ("duck", "Ducking", 0.0f));
    layout.add (unitParam ("output", "Output", -24.0f, 12.0f, 0.0f, P::Unit::db));
    return layout;
}

CrystalProcessor::CrystalProcessor()
    : aa::PluginBase (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true),
                      createLayout()),
      reverb (std::make_unique<crystal::CrystalReverb>())
{
    size = raw ("size");
    decay = raw ("decay");
    shimmer = raw ("shimmer");
    interval = raw ("interval");
    damping = raw ("damping");
    freeze = raw ("freeze");
    predelay = raw ("predelay");
    mix = raw ("mix");
    sparkle = raw ("sparkle");
    modulation = raw ("modulation");
    lowCut = raw ("lowcut");
    highCut = raw ("highcut");
    width = raw ("width");
    duck = raw ("duck");
    output = raw ("output");

    tailSeconds = 10.0;

    setFactoryPresets ({
        { "Crystal Cave", {} },
        { "Angel Shimmer", { { "size", 85.0f }, { "decay", 9.5f }, { "shimmer", 72.0f }, { "damping", 30.0f },
                             { "sparkle", 55.0f }, { "modulation", 50.0f }, { "predelay", 30.0f },
                             { "lowcut", 220.0f }, { "highcut", 13000.0f }, { "mix", 40.0f } } },
        { "Frozen Lake", { { "size", 92.0f }, { "decay", 14.0f }, { "shimmer", 22.0f }, { "interval", 1.0f },
                           { "damping", 55.0f }, { "sparkle", 20.0f }, { "modulation", 28.0f }, { "predelay", 45.0f },
                           { "lowcut", 150.0f }, { "highcut", 7500.0f }, { "mix", 38.0f } } },
        { "Glass Cathedral", { { "size", 100.0f }, { "decay", 12.0f }, { "shimmer", 38.0f }, { "interval", 2.0f },
                               { "damping", 18.0f }, { "sparkle", 65.0f }, { "modulation", 35.0f }, { "predelay", 70.0f },
                               { "lowcut", 180.0f }, { "highcut", 15000.0f }, { "mix", 35.0f } } },
        { "Deep Ice", { { "size", 96.0f }, { "decay", 22.0f }, { "shimmer", 8.0f }, { "damping", 72.0f },
                        { "sparkle", 10.0f }, { "modulation", 62.0f }, { "predelay", 25.0f },
                        { "lowcut", 60.0f }, { "highcut", 5200.0f }, { "mix", 40.0f } } },
        { "Octave Down Abyss", { { "size", 95.0f }, { "decay", 16.0f }, { "shimmer", 58.0f }, { "interval", 4.0f },
                                 { "damping", 50.0f }, { "sparkle", 15.0f }, { "modulation", 45.0f },
                                 { "lowcut", 35.0f }, { "highcut", 6500.0f }, { "mix", 40.0f } } },
        { "Fairy Dust", { { "size", 55.0f }, { "decay", 5.5f }, { "shimmer", 52.0f }, { "interval", 3.0f },
                          { "damping", 15.0f }, { "sparkle", 92.0f }, { "modulation", 40.0f }, { "predelay", 35.0f },
                          { "lowcut", 320.0f }, { "highcut", 16000.0f }, { "mix", 30.0f } } },
        { "Infinite Freeze", { { "size", 100.0f }, { "decay", 30.0f }, { "shimmer", 40.0f }, { "damping", 25.0f },
                               { "sparkle", 40.0f }, { "modulation", 30.0f }, { "lowcut", 120.0f },
                               { "highcut", 12000.0f }, { "mix", 50.0f } } },
        { "Small Geode", { { "size", 18.0f }, { "decay", 0.9f }, { "shimmer", 12.0f }, { "damping", 40.0f },
                           { "sparkle", 30.0f }, { "modulation", 20.0f }, { "predelay", 6.0f },
                           { "lowcut", 200.0f }, { "highcut", 12000.0f }, { "mix", 25.0f } } },
        { "Aurora Wash", { { "size", 80.0f }, { "decay", 8.0f }, { "shimmer", 45.0f }, { "interval", 1.0f },
                           { "damping", 35.0f }, { "sparkle", 45.0f }, { "modulation", 85.0f }, { "duck", 55.0f },
                           { "mix", 45.0f } } },
        { "Ducked Diamond", { { "size", 62.0f }, { "decay", 3.5f }, { "shimmer", 30.0f }, { "damping", 30.0f },
                              { "sparkle", 50.0f }, { "predelay", 50.0f }, { "duck", 75.0f }, { "mix", 32.0f } } },
    });
}

double CrystalProcessor::getTailLengthSeconds() const
{
    const double d = decay->load();
    const double sh = shimmer->load() / 100.0;
    return jmin (60.0, d * (1.5 + 1.5 * sh) + predelay->load() * 0.001 + 0.5);
}

void CrystalProcessor::updateEngineParams()
{
    crystal::CrystalReverb::Params p;
    p.size = size->load() / 100.0f;
    p.decaySeconds = decay->load();
    p.shimmer = shimmer->load() / 100.0f;
    p.shimmerRatio = intervalRatio (jlimit (0, 4, (int) interval->load()));
    p.damping = damping->load() / 100.0f;
    p.freeze = freeze->load() > 0.5f;
    p.predelayMs = predelay->load();
    p.sparkle = sparkle->load() / 100.0f;
    p.modulation = modulation->load() / 100.0f;
    p.lowCutHz = lowCut->load();
    p.highCutHz = highCut->load();
    p.width = width->load() / 100.0f;
    reverb->setParams (p);
}

void CrystalProcessor::prepareToPlay (double sampleRate, int)
{
    sr = (float) sampleRate;
    reverb->prepare (sampleRate);
    updateEngineParams();
    reverb->snapToTargets();

    scratch.setSize (2, scratchSize, false, true, false);

    const float mixAmt = mix->load() / 100.0f;
    dryGain.reset (sampleRate, 0.03, jmin (1.0f, 2.0f * (1.0f - mixAmt)));
    wetGain.reset (sampleRate, 0.03, jmin (1.0f, 2.0f * mixAmt));
    outGain.reset (sampleRate, 0.03, aa::dsp::dbToGain (output->load()));

    duckEnv = fastEnv = slowEnv = wetPower = 0.0f;
    duckGain = 1.0f;
    refractory = 0;
    pingEnv = 0.0f;
    inMeter.prepare (sr);
}

void CrystalProcessor::reset()
{
    reverb->reset();
    duckEnv = fastEnv = slowEnv = wetPower = 0.0f;
    duckGain = 1.0f;
}

void CrystalProcessor::renderPing (float* left, float* right, int numSamples)
{
    if (pingRequested.exchange (false))
    {
        static const int notes[] = { 76, 79, 81, 83, 86, 88, 91, 93 }; // E major pentatonic-ish, bright register
        const int idx = jlimit (0, 7, (int) (pingPitch.load() * 8.0f));
        pingFreq = aa::dsp::midiToHz ((float) notes[idx]);
        pingEnv = 0.3f;
        for (auto& ph : pingPhase)
            ph = 0.0f;
    }

    if (pingEnv < 0.0005f)
    {
        pingEnv = 0.0f;
        return;
    }

    // a glassy bell: three inharmonic partials, the upper ones dying faster
    const float ratios[] = { 1.0f, 2.756f, 5.404f };
    const float decay = std::exp (-1.0f / (0.18f * sr));
    for (int i = 0; i < numSamples; ++i)
    {
        float s = 0.0f;
        for (int k = 0; k < 3; ++k)
        {
            const float f = pingFreq * ratios[k];
            if (f < sr * 0.45f)
                s += std::sin (aa::dsp::twoPi * pingPhase[k]) * (k == 0 ? 1.0f : (k == 1 ? 0.45f * pingEnv * 3.0f : 0.25f * pingEnv * 3.0f));
            pingPhase[k] += f / sr;
            pingPhase[k] -= std::floor (pingPhase[k]);
        }
        s *= pingEnv;
        pingEnv *= decay;
        left[i] += s;
        if (right != nullptr)
            right[i] += s;
    }
}

void CrystalProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = jmin (buffer.getNumChannels(), getTotalNumOutputChannels());

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numChannels == 0 || numSamples == 0)
        return;

    updateEngineParams();

    const float mixAmt = mix->load() / 100.0f;
    dryGain.setTarget (jmin (1.0f, 2.0f * (1.0f - mixAmt)));
    wetGain.setTarget (jmin (1.0f, 2.0f * mixAmt));
    outGain.setTarget (aa::dsp::dbToGain (output->load()));
    const float duckAmt = duck->load() / 100.0f;

    const float envFastA = aa::dsp::onePoleCoeff (0.001f, sr), envFastR = aa::dsp::onePoleCoeff (0.03f, sr);
    const float envSlowA = aa::dsp::onePoleCoeff (0.015f, sr), envSlowR = aa::dsp::onePoleCoeff (0.35f, sr);
    const float duckA = aa::dsp::onePoleCoeff (0.004f, sr), duckR = aa::dsp::onePoleCoeff (0.22f, sr);
    const float duckSmooth = aa::dsp::onePoleCoeff (0.012f, sr);
    const float powerA = aa::dsp::onePoleCoeff (0.12f, sr);

    float* left = buffer.getWritePointer (0);
    float* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;
    renderPing (left, right, numSamples);

    float* wetL = scratch.getWritePointer (0);
    float* wetR = scratch.getWritePointer (1);

    for (int start = 0; start < numSamples; start += scratchSize)
    {
        const int n = jmin (scratchSize, numSamples - start);
        float* l = left + start;
        float* r = right != nullptr ? right + start : nullptr;

        reverb->process (l, r != nullptr ? r : l, wetL, wetR, n);

        for (int i = 0; i < n; ++i)
        {
            const float inL = l[i];
            const float inR = r != nullptr ? r[i] : inL;

            // Transients -> crystal flashes in the UI; envelope -> ducking.
            const float mono = 0.5f * (std::abs (inL) + std::abs (inR));
            fastEnv += (mono > fastEnv ? envFastA : envFastR) * (mono - fastEnv);
            slowEnv += (mono > slowEnv ? envSlowA : envSlowR) * (mono - slowEnv);
            duckEnv += (mono > duckEnv ? duckA : duckR) * (mono - duckEnv);
            if (refractory > 0)
                --refractory;
            else if (fastEnv > 0.012f && fastEnv > slowEnv * 1.7f)
            {
                hits.push ({ jmin (1.0f, fastEnv * 2.5f) });
                refractory = (int) (0.12f * sr);
            }
            inMeter.process (mono);

            const float duckTarget = 1.0f / (1.0f + duckAmt * 14.0f * duckEnv);
            duckGain += duckSmooth * (duckTarget - duckGain);

            const float wg = wetGain.next() * duckGain;
            const float dg = dryGain.next();
            const float og = outGain.next();
            const float wl = wetL[i] * wg, wr = wetR[i] * wg;
            wetPower += powerA * (0.5f * (wetL[i] * wetL[i] + wetR[i] * wetR[i]) - wetPower);

            if (r != nullptr)
            {
                l[i] = (inL * dg + wl) * og;
                r[i] = (inR * dg + wr) * og;
            }
            else
            {
                l[i] = (inL * dg + 0.7071f * (wl + wr)) * og;
            }
        }
    }

    wetPower = aa::dsp::undenormalise (wetPower);
    inMeter.publish();
    inputLevel.store (inMeter.level.load());
    tailLevel.store (std::sqrt (wetPower));
    shimmerLevel.store (reverb->takeShimmerLevel());
    freezeLevel.store (reverb->getFreezeAmount());
}

AudioProcessorEditor* CrystalProcessor::createEditor()
{
    return new CrystalEditor (*this);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CrystalProcessor();
}
