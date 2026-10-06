#include "BoingProcessor.h"
#include "BoingEditor.h"

using namespace juce;
namespace P = aa::params;

namespace
{
    inline float fastSin01 (float phase)
    {
        // parabolic approximation of sin(2*pi*phase), phase in [0,1)
        const float x = phase < 0.5f ? phase * 4.0f - 1.0f : 3.0f - phase * 4.0f; // triangle -1..1
        return x * (1.5f - 0.5f * x * x);                                          // soft-shaped
    }
}

AudioProcessorValueTreeState::ParameterLayout BoingProcessor::createLayout()
{
    P::Layout layout;
    layout.add (P::floatParam ("time", "Drop Time", 40.0f, 1500.0f, 420.0f, P::Unit::ms, 300.0f));
    layout.add (P::toggle ("sync", "Tempo Sync", false));
    layout.add (P::choice ("division", "Division", aa::dsp::syncDivisionNames(), 8));
    layout.add (P::floatParam ("bounciness", "Bounciness", 30.0f, 95.0f, 74.0f, P::Unit::percent));
    layout.add (P::integer ("bounces", "Bounces", 2, boing::maxBounces, 12));
    layout.add (P::percent ("damping", "Damping", 30.0f));
    layout.add (P::choice ("mode", "Mode", { "Bounce", "Rise", "Steady" }, 0));
    layout.add (P::floatParam ("tone", "Tone", -100.0f, 100.0f, -25.0f, P::Unit::percent));
    layout.add (P::percent ("wobble", "Wobble", 10.0f));
    layout.add (P::percent ("spread", "Spread", 55.0f));
    layout.add (P::floatParam ("rethrow", "Re-throw", 0.0f, 85.0f, 0.0f, P::Unit::percent));
    layout.add (P::percent ("duck", "Duck", 0.0f));
    layout.add (P::percent ("mix", "Mix", 35.0f));
    return layout;
}

BoingProcessor::BoingProcessor()
    : aa::PluginBase (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true),
                      createLayout())
{
    time = raw ("time");
    sync = raw ("sync");
    division = raw ("division");
    bounciness = raw ("bounciness");
    bounces = raw ("bounces");
    damping = raw ("damping");
    mode = raw ("mode");
    tone = raw ("tone");
    wobble = raw ("wobble");
    spread = raw ("spread");
    rethrow = raw ("rethrow");
    duck = raw ("duck");
    mix = raw ("mix");

    tailSeconds = boing::maxDelaySeconds;

    setFactoryPresets ({
        { "Rubber Ball", {} },
        { "Ping Pong Table", { { "time", 260.0f }, { "bounciness", 82.0f }, { "bounces", 18.0f }, { "damping", 18.0f },
                               { "spread", 100.0f }, { "tone", 10.0f }, { "mix", 38.0f } } },
        { "Basketball Court", { { "time", 700.0f }, { "bounciness", 70.0f }, { "bounces", 10.0f }, { "damping", 28.0f },
                                { "tone", -45.0f }, { "spread", 40.0f }, { "wobble", 5.0f }, { "mix", 30.0f } } },
        { "Marble Staircase", { { "time", 180.0f }, { "bounciness", 90.0f }, { "bounces", 24.0f }, { "damping", 8.0f },
                                { "tone", 55.0f }, { "spread", 70.0f }, { "mix", 32.0f } } },
        { "Moon Gravity", { { "time", 1300.0f }, { "bounciness", 84.0f }, { "bounces", 9.0f }, { "damping", 20.0f },
                            { "tone", -60.0f }, { "wobble", 35.0f }, { "spread", 80.0f }, { "mix", 40.0f } } },
        { "Rising Bubbles", { { "mode", 1.0f }, { "time", 600.0f }, { "bounciness", 78.0f }, { "bounces", 14.0f },
                              { "damping", 35.0f }, { "tone", 40.0f }, { "spread", 90.0f }, { "wobble", 25.0f }, { "mix", 35.0f } } },
        { "Quarter Note Hop", { { "sync", 1.0f }, { "division", 8.0f }, { "mode", 2.0f }, { "bounces", 6.0f },
                                { "damping", 35.0f }, { "spread", 100.0f }, { "tone", -20.0f }, { "mix", 30.0f } } },
        { "Dotted Eighth Bounce", { { "sync", 1.0f }, { "division", 6.0f }, { "bounciness", 80.0f }, { "bounces", 14.0f },
                                    { "damping", 22.0f }, { "spread", 75.0f }, { "tone", -30.0f }, { "mix", 33.0f } } },
        { "Springy Dub", { { "time", 480.0f }, { "bounciness", 88.0f }, { "bounces", 16.0f }, { "damping", 15.0f },
                           { "tone", -70.0f }, { "wobble", 60.0f }, { "rethrow", 55.0f }, { "spread", 65.0f },
                           { "duck", 40.0f }, { "mix", 40.0f } } },
        { "Juggler", { { "time", 330.0f }, { "bounciness", 65.0f }, { "bounces", 8.0f }, { "damping", 25.0f },
                       { "rethrow", 75.0f }, { "spread", 100.0f }, { "tone", 15.0f }, { "mix", 34.0f } } },
        { "Ducked Vocal Bounce", { { "time", 380.0f }, { "bounciness", 76.0f }, { "bounces", 12.0f }, { "damping", 30.0f },
                                   { "duck", 70.0f }, { "tone", -35.0f }, { "spread", 60.0f }, { "mix", 45.0f } } },
    });
}

double BoingProcessor::firstIntervalSeconds (double bpm) const
{
    if (sync->load() > 0.5f)
        return aa::dsp::syncDivisionBeats ((int) division->load()) * 60.0 / jmax (20.0, bpm);
    return time->load() * 0.001;
}

boing::Schedule BoingProcessor::currentSchedule() const
{
    return boing::computeSchedule (firstIntervalSeconds (hostBpm.load()), bounciness->load() / 100.0,
                                   (int) bounces->load(), damping->load() / 100.0,
                                   (boing::Mode) jlimit (0, 2, (int) mode->load()));
}

void BoingProcessor::prepareToPlay (double sampleRate, int)
{
    sr = (float) sampleRate;
    transport.prepare (sampleRate);
    const int maxSamples = (int) (boing::maxDelaySeconds * sampleRate) + 4096;
    lineL.prepare (maxSamples);
    lineR.prepare (maxSamples);

    for (size_t k = 0; k < taps.size(); ++k)
    {
        taps[k] = Tap {};
        taps[k].wobbleRate = 0.55f + 0.29f * (float) k + 0.11f * (float) (k % 3);
        taps[k].wobblePhase = (float) k * 0.137f;
    }

    delaySmooth = aa::dsp::onePoleCoeff (0.09f, sr);
    gainSmooth = aa::dsp::onePoleCoeff (0.02f, sr);
    fastEnv = slowEnv = duckEnv = 0.0f;
    fbL = fbR = 0.0f;
    refractory = 0;
    firstBlock = true;
    inMeter.prepare (sr);
    wetMeter.prepare (sr);
}

void BoingProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = jmin (buffer.getNumChannels(), getTotalNumOutputChannels());
    if (numChannels == 0 || numSamples == 0)
        return;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    transport.update (getPlayHead(), numSamples);
    hostBpm.store (transport.bpm);

    // ---- schedule -> tap targets ---------------------------------------------
    const auto schedule = currentSchedule();
    const float toneAmt = tone->load() / 100.0f;
    const float spreadAmt = spread->load() / 100.0f;
    const float wobbleAmt = wobble->load() / 100.0f;
    const float rethrowAmt = rethrow->load() / 100.0f;
    const float duckAmt = duck->load() / 100.0f;
    const float mixAmt = mix->load() / 100.0f;
    const float dryGain = jmin (1.0f, 2.0f * (1.0f - mixAmt));
    const float wetGain = jmin (1.0f, 2.0f * mixAmt);

    lastActiveTap = jmax (0, schedule.count - 1);
    for (int k = 0; k < boing::maxBounces; ++k)
    {
        auto& tap = taps[(size_t) k];
        if (k < schedule.count)
        {
            tap.targetDelay = (float) (schedule.time[(size_t) k] * sr);
            tap.targetGain = schedule.gain[(size_t) k];
        }
        else
        {
            tap.targetGain = 0.0f;
        }

        // Tone: each successive bounce gets darker (tone < 0) or thinner/brighter (tone > 0).
        const float kk = (float) (k + 1);
        if (toneAmt < 0.0f)
        {
            const float hz = jmax (250.0f, 18000.0f * std::exp (-kk * -toneAmt * 0.32f));
            tap.lpCoeff = 1.0f - std::exp (-aa::dsp::twoPi * jmin (hz, sr * 0.45f) / sr);
            tap.hpCoeff = 0.0f;
        }
        else
        {
            const float hz = jmin (5000.0f, 25.0f * std::exp (kk * toneAmt * 0.38f));
            tap.lpCoeff = 1.0f;
            tap.hpCoeff = toneAmt > 0.001f ? 1.0f - std::exp (-aa::dsp::twoPi * hz / sr) : 0.0f;
        }

        tap.pan = spreadAmt * ((k % 2 == 0) ? -1.0f : 1.0f);

        if (firstBlock)
        {
            tap.delay = tap.targetDelay;
            tap.gain = tap.targetGain;
        }
    }
    firstBlock = false;

    const float maxWobbleSamples = wobbleAmt * 0.0045f * sr;
    const float envFastA = aa::dsp::onePoleCoeff (0.001f, sr), envFastR = aa::dsp::onePoleCoeff (0.03f, sr);
    const float envSlowA = aa::dsp::onePoleCoeff (0.015f, sr), envSlowR = aa::dsp::onePoleCoeff (0.35f, sr);
    const float duckA = aa::dsp::onePoleCoeff (0.005f, sr), duckR = aa::dsp::onePoleCoeff (0.25f, sr);

    float* left = buffer.getWritePointer (0);
    float* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    if (testDropRequested.exchange (false))
    {
        testEnv = 0.9f;
        testPhase = 0.0f;
        testFreq = 880.0f;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        float inL = left[i];
        float inR = right != nullptr ? right[i] : inL;

        // Test "tok" from clicking the stage: a short pitched pluck.
        if (testEnv > 0.0001f)
        {
            const float s = std::sin (aa::dsp::twoPi * testPhase) * testEnv;
            testPhase += testFreq / sr;
            if (testPhase >= 1.0f) testPhase -= 1.0f;
            testFreq = jmax (220.0f, testFreq * 0.99985f);
            testEnv *= 0.9993f;
            inL += s;
            inR += s;
        }
        else
            testEnv = 0.0f;

        // Transient detection -> spawn a ball in the UI
        const float mono = 0.5f * (std::abs (inL) + std::abs (inR));
        fastEnv += (mono > fastEnv ? envFastA : envFastR) * (mono - fastEnv);
        slowEnv += (mono > slowEnv ? envSlowA : envSlowR) * (mono - slowEnv);
        duckEnv += (mono > duckEnv ? duckA : duckR) * (mono - duckEnv);
        if (refractory > 0)
            --refractory;
        else if (fastEnv > 0.012f && fastEnv > slowEnv * 1.7f)
        {
            hits.push ({ jmin (1.0f, fastEnv * 2.5f) });
            refractory = (int) (0.14f * sr);
        }

        inMeter.process (mono);

        lineL.push (inL + fbL);
        lineR.push (inR + fbR);

        float wetL = 0.0f, wetR = 0.0f, lastL = 0.0f, lastR = 0.0f;
        for (int k = 0; k < boing::maxBounces; ++k)
        {
            auto& tap = taps[(size_t) k];
            tap.delay += delaySmooth * (tap.targetDelay - tap.delay);
            tap.gain += gainSmooth * (tap.targetGain - tap.gain);
            if (tap.gain < 0.00001f && tap.targetGain == 0.0f)
                continue;

            float d = tap.delay;
            if (maxWobbleSamples > 0.0f)
            {
                tap.wobblePhase += tap.wobbleRate / sr;
                if (tap.wobblePhase >= 1.0f) tap.wobblePhase -= 1.0f;
                const float depth = maxWobbleSamples * (0.25f + 0.75f * (float) (k + 1) / (float) boing::maxBounces);
                d += depth * (1.0f + fastSin01 (tap.wobblePhase));
            }

            float tl = lineL.read (d);
            float tr = lineR.read (d);

            // per-bounce tone
            tap.lpL += tap.lpCoeff * (tl - tap.lpL);
            tap.lpR += tap.lpCoeff * (tr - tap.lpR);
            tl = tap.lpL;
            tr = tap.lpR;
            if (tap.hpCoeff > 0.0f)
            {
                tap.hpL += tap.hpCoeff * (tl - tap.hpL);
                tap.hpR += tap.hpCoeff * (tr - tap.hpR);
                tl -= tap.hpL;
                tr -= tap.hpR;
            }

            // ping-pong placement
            const float m = 0.5f * (tl + tr);
            const float gl = std::sqrt (1.0f - tap.pan) , gr = std::sqrt (1.0f + tap.pan);
            const float s = std::abs (tap.pan);
            const float ol = tl + (m * gl - tl) * s;
            const float orr = tr + (m * gr - tr) * s;

            wetL += ol * tap.gain;
            wetR += orr * tap.gain;

            if (k == lastActiveTap)
            {
                lastL = ol * tap.gain;
                lastR = orr * tap.gain;
            }
        }

        // Re-throw: feed the final bounce back in to start a new bounce train.
        fbL = aa::dsp::softClip (lastL * rethrowAmt);
        fbR = aa::dsp::softClip (lastR * rethrowAmt);

        const float duckGain = 1.0f - duckAmt * jmin (1.0f, duckEnv * 5.0f);
        wetL *= duckGain * wetGain;
        wetR *= duckGain * wetGain;

        wetMeter.process (0.5f * (std::abs (wetL) + std::abs (wetR)));

        if (right != nullptr)
        {
            left[i] = inL * dryGain + wetL;
            right[i] = inR * dryGain + wetR;
        }
        else
        {
            left[i] = inL * dryGain + 0.5f * (wetL + wetR);
        }
    }

    inMeter.publish();
    wetMeter.publish();
    inputLevel.store (inMeter.level.load());
    wetLevel.store (wetMeter.level.load());
}

AudioProcessorEditor* BoingProcessor::createEditor()
{
    return new BoingEditor (*this);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BoingProcessor();
}
