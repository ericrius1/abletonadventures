#include "TapeDreamsProcessor.h"
#include "TapeDreamsEditor.h"

using namespace juce;
namespace P = aa::params;

namespace
{
    using aa::dsp::pi;
    using aa::dsp::twoPi;

    constexpr float centreDelayMs = 12.5f;   // wow/flutter swing around this point; dry is delayed to match
    constexpr float wowMaxMs = 8.5f;
    constexpr float flutterMaxMs = 0.2f;
    constexpr float crinkleMaxMs = 3.4f;
    constexpr float maxLagSeconds = 3.0f;    // how far the tape-stop read head may fall behind
    constexpr float xfadeSeconds = 0.06f;

    inline float smoothstep (float e0, float e1, float x)
    {
        const float t = aa::dsp::clamp01 ((x - e0) / (e1 - e0));
        return t * t * (3.0f - 2.0f * t);
    }
} // namespace

AudioProcessorValueTreeState::ParameterLayout TapeDreamsProcessor::createLayout()
{
    P::Layout layout;
    // Ordered so the first eight (Push's first bank) are the most playable.
    layout.add (P::percent ("wow", "Wow", 30.0f));
    layout.add (P::percent ("flutter", "Flutter", 25.0f));
    layout.add (P::percent ("drive", "Drive", 35.0f));
    layout.add (P::percent ("age", "Age", 35.0f));
    layout.add (P::percent ("wear", "Wear", 15.0f));
    layout.add (P::percent ("hiss", "Hiss", 20.0f));
    layout.add (P::toggle ("tapeStop", "Tape Stop", false));
    layout.add (P::percent ("mix", "Mix", 100.0f));
    layout.add (P::percent ("squash", "Squash", 20.0f));
    layout.add (P::percent ("crinkle", "Crinkle", 6.0f));
    layout.add (P::percent ("hum", "Hum", 0.0f));
    layout.add (P::choice ("mains", "Mains", { "50 Hz", "60 Hz" }, 0));
    layout.add (P::floatParam ("input", "Input", -18.0f, 18.0f, 0.0f, P::Unit::db));
    layout.add (P::floatParam ("output", "Output", -18.0f, 12.0f, 0.0f, P::Unit::db));
    layout.add (P::floatParam ("stopTime", "Stop Time", 0.3f, 2.0f, 0.8f, P::Unit::seconds, 0.8f));
    return layout;
}

TapeDreamsProcessor::TapeDreamsProcessor()
    : aa::PluginBase (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true),
                      createLayout())
{
    wow = raw ("wow");
    flutter = raw ("flutter");
    drive = raw ("drive");
    age = raw ("age");
    wear = raw ("wear");
    hiss = raw ("hiss");
    tapeStop = raw ("tapeStop");
    mix = raw ("mix");
    squash = raw ("squash");
    crinkle = raw ("crinkle");
    hum = raw ("hum");
    mains = raw ("mains");
    inputGain = raw ("input");
    outputGain = raw ("output");
    stopTime = raw ("stopTime");

    compTable = tape::Saturator::makeCompensationTable();
    tailSeconds = 0.1;
    setLatencySamples ((int) std::ceil (centreDelayMs * 0.001f * 44100.0f) + 8); // refined in prepareToPlay

    setFactoryPresets ({
        { "Dusty Cassette", {} },
        { "Warm Reel-to-Reel", { { "drive", 48.0f }, { "squash", 32.0f }, { "wow", 7.0f }, { "flutter", 6.0f },
                                 { "crinkle", 0.0f }, { "age", 6.0f }, { "wear", 0.0f }, { "hiss", 7.0f } } },
        { "VHS Memories", { { "drive", 40.0f }, { "squash", 35.0f }, { "wow", 48.0f }, { "flutter", 38.0f },
                            { "crinkle", 12.0f }, { "age", 62.0f }, { "wear", 28.0f }, { "hiss", 34.0f },
                            { "hum", 12.0f }, { "mains", 1.0f } } },
        { "Broken Walkman", { { "drive", 58.0f }, { "squash", 42.0f }, { "wow", 82.0f }, { "flutter", 72.0f },
                              { "crinkle", 48.0f }, { "age", 64.0f }, { "wear", 55.0f }, { "hiss", 42.0f },
                              { "output", 1.5f } } },
        { "Sun-Bleached Summer", { { "drive", 30.0f }, { "squash", 18.0f }, { "wow", 55.0f }, { "flutter", 18.0f },
                                   { "crinkle", 14.0f }, { "age", 52.0f }, { "wear", 18.0f }, { "hiss", 28.0f },
                                   { "mix", 88.0f } } },
        { "Lo-Fi Study Beats", { { "drive", 52.0f }, { "squash", 48.0f }, { "wow", 40.0f }, { "flutter", 22.0f },
                                 { "crinkle", 10.0f }, { "age", 72.0f }, { "wear", 22.0f }, { "hiss", 40.0f } } },
        { "Underwater Tape", { { "drive", 25.0f }, { "squash", 25.0f }, { "wow", 100.0f }, { "flutter", 12.0f },
                               { "crinkle", 22.0f }, { "age", 88.0f }, { "wear", 10.0f }, { "hiss", 14.0f } } },
        { "Pristine Glue", { { "drive", 32.0f }, { "squash", 38.0f }, { "wow", 0.0f }, { "flutter", 3.0f },
                             { "crinkle", 0.0f }, { "age", 0.0f }, { "wear", 0.0f }, { "hiss", 0.0f } } },
        { "Answering Machine", { { "drive", 78.0f }, { "squash", 72.0f }, { "wow", 35.0f }, { "flutter", 55.0f },
                                 { "crinkle", 20.0f }, { "age", 100.0f }, { "wear", 38.0f }, { "hiss", 55.0f },
                                 { "hum", 32.0f }, { "mains", 1.0f }, { "output", 0.5f } } },
        { "Grandpa's Attic", { { "drive", 42.0f }, { "squash", 30.0f }, { "wow", 52.0f }, { "flutter", 42.0f },
                               { "crinkle", 36.0f }, { "age", 76.0f }, { "wear", 62.0f }, { "hiss", 50.0f },
                               { "hum", 24.0f }, { "stopTime", 1.6f } } },
    });
}

//==============================================================================
void TapeDreamsProcessor::prepareToPlay (double sampleRate, int)
{
    sr = (float) sampleRate;

    // 4x oversampling for the saturator at 44.1/48k, 2x at 88.2/96k, none above.
    const int newOrder = sampleRate < 60000.0 ? 2 : (sampleRate < 120000.0 ? 1 : 0);
    if (oversampler == nullptr || newOrder != osOrder)
    {
        osOrder = newOrder;
        oversampler = std::make_unique<dsp::Oversampling<float>> (2, (size_t) osOrder,
                                                                  dsp::Oversampling<float>::filterHalfBandFIREquiripple,
                                                                  true, false);
    }
    oversampler->initProcessing ((size_t) maxChunk);
    osLatency = oversampler->getLatencyInSamples();

    // The wet path sits in the middle of the wow/flutter delay; the dry path is delayed to match exactly.
    latency = (int) std::ceil (osLatency + centreDelayMs * 0.001f * sr);
    wetCentre = (float) latency - osLatency;
    setLatencySamples (latency);

    work.setSize (2, maxChunk, false, true, true);
    maxLag = maxLagSeconds * sr;
    const int modSamples = (int) ((wowMaxMs * 1.3f + flutterMaxMs * 1.6f + crinkleMaxMs * 1.2f) * 0.001f * sr);
    const int maxDelay = latency + modSamples + (int) maxLag + 64;
    for (auto& c : chans)
    {
        c.wetLine.prepare (maxDelay);
        c.dryLine.prepare (maxDelay);
    }

    resetState();
}

void TapeDreamsProcessor::reset()
{
    resetState();
}

void TapeDreamsProcessor::resetState()
{
    if (oversampler != nullptr)
        oversampler->reset();

    for (auto& c : chans)
    {
        c.preEmph.reset();
        c.deEmph.reset();
        c.bump.reset();
        c.preEmph.setHighShelf (sr, 3200.0f, 6.0f);
        c.deEmph.setHighShelf (sr, 3200.0f, -6.0f);
        c.bump.setPeak (sr, 72.0f, 0.9f, 2.2f);
        c.dcBlock.setCutoff (7.0f, sr);
        c.dcBlock.reset();
        c.wetLine.clear();
        c.dryLine.clear();
        c.ageLow.reset();
        c.ageHigh.reset();
        c.stopLow.reset();
        c.dropTone.setCutoff (2200.0f, sr);
        c.dropTone.reset();
        c.hissHigh.setCutoff (420.0f, sr);
        c.hissHigh.reset();
        c.hissLow.reset();
        c.lastDelay = wetCentre;
    }

    wowFlutter.prepare (sr);
    crinkleGen.prepare (sr);
    dropouts.prepare (sr);
    squashComp.prepare (sr);
    noise.seed (0x5EEDu);

    const double sd = sr;
    inGainSm.reset (sd, 0.03, aa::dsp::dbToGain (inputGain->load()));
    outGainSm.reset (sd, 0.03, aa::dsp::dbToGain (outputGain->load()));
    mixSm.reset (sd, 0.03, mix->load() * 0.01f);
    driveSm.reset (sd, 0.05, drive->load() * 0.01f);
    hissSm.reset (sd, 0.05, 0.0f);
    humSm.reset (sd, 0.05, 0.0f);
    ageSm.reset (sd, 0.05, age->load() * 0.01f);
    wearSm.reset (sd, 0.05, wear->load() * 0.01f);
    wowSm.reset (sd, 0.4, 0.0f);
    flutterSm.reset (sd, 0.4, 0.0f);
    crinkleSm.reset (sd, 0.2, crinkle->load() * 0.01f);
    squashSm.reset (sd, 0.05, squash->load() * 0.01f);

    stopPhase = tapeStop->load() > 0.5f ? 1.0f : 0.0f;
    motor = 1.0f - stopPhase;
    lagA = lagB = 0.0f;
    xfade = -1.0f;
    xfadeInc = 1.0f / (xfadeSeconds * sr);

    humPhase = 0.0f;
    hissEnv = 0.0f;
    hissEnvA = aa::dsp::onePoleCoeff (0.03f, sr);
    hissEnvR = aa::dsp::onePoleCoeff (0.3f, sr);
    inMs = outMs = 0.0f;
    meterCoeff = aa::dsp::onePoleCoeff (0.05f, sr);
    glow = 0.0f;
    glowRelease = std::exp (-1.0f / (0.12f * sr));
    filterCountdown = 0;
    lastAge = -1.0f;
    lastMotorForFilter = -1.0f;
    ageMakeup = 1.0f;
}

//==============================================================================
void TapeDreamsProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    const int numChannels = jmin (2, buffer.getNumChannels(), getTotalNumOutputChannels());
    if (numChannels == 0 || numSamples == 0)
        return;

    // ---- parameter targets (smoothed per sample) ------------------------------
    const float msToSamples = 0.001f * sr;
    const float wowAmt = wow->load() * 0.01f;
    const float flutterAmt = flutter->load() * 0.01f;
    inGainSm.setTarget (aa::dsp::dbToGain (inputGain->load()));
    outGainSm.setTarget (aa::dsp::dbToGain (outputGain->load()));
    mixSm.setTarget (mix->load() * 0.01f);
    driveSm.setTarget (drive->load() * 0.01f);
    ageSm.setTarget (age->load() * 0.01f);
    wearSm.setTarget (wear->load() * 0.01f);
    crinkleSm.setTarget (crinkle->load() * 0.01f);
    squashSm.setTarget (squash->load() * 0.01f);
    wowSm.setTarget (std::pow (wowAmt, 1.4f) * wowMaxMs * msToSamples);
    flutterSm.setTarget (std::pow (flutterAmt, 1.2f) * flutterMaxMs * msToSamples);

    const float h = hiss->load() * 0.01f;
    // ~ -57 dBFS RMS at 25 %, -34 dBFS at 100 % (0.45 is the filtered noise RMS)
    hissSm.setTarget (h <= 0.0f ? 0.0f : aa::dsp::dbToGain (-80.0f + 46.0f * std::sqrt (h)) * jmin (1.0f, h * 50.0f) / 0.45f);
    const float hu = hum->load() * 0.01f;
    humSm.setTarget (hu <= 0.0f ? 0.0f : aa::dsp::dbToGain (-86.0f + 46.0f * std::sqrt (hu)) * jmin (1.0f, hu * 40.0f) / 1.6f);

    float speedAcc = 0.0f;
    for (int start = 0; start < numSamples; start += maxChunk)
    {
        const int n = jmin (maxChunk, numSamples - start);
        float* io[2] = { buffer.getWritePointer (0, start), numChannels > 1 ? buffer.getWritePointer (1, start) : nullptr };
        processChunk (io, numChannels, n);
        speedAcc += lastChunkSpeed * (float) n;
    }

    inputLevel.store (inMs);
    outputLevel.store (outMs);
    tapeSpeed.store (speedAcc / (float) numSamples);
    motorSpeed.store (motor);
    dropoutLevel.store (dropouts.level());
    recGlow.store (glow);
}

void TapeDreamsProcessor::processChunk (float* const* io, int numCh, int n)
{
    const float compScale = (float) (compTable.size() - 1);
    std::array<float, maxChunk> driveComp;

    // ---- 1. input gain, pre-emphasis and the per-sample drive curve (base rate)
    for (int i = 0; i < n; ++i)
    {
        const float gIn = inGainSm.next();
        const float d = driveSm.next();
        const float g = tape::Saturator::driveGain (d);
        driveG[(size_t) i] = g;
        driveB[(size_t) i] = tape::Saturator::bias (d);
        driveOff[(size_t) i] = aa::dsp::softClip (driveB[(size_t) i]);

        const float pos = aa::dsp::clamp01 (d) * compScale;
        const int k = jmin ((int) compTable.size() - 2, (int) pos);
        driveComp[(size_t) i] = compTable[(size_t) k] + (compTable[(size_t) k + 1] - compTable[(size_t) k]) * (pos - (float) k);

        // Squash: program-dependent tape compression ahead of the saturator, so anything that
        // leaks past its attack gets rounded off by the tape rather than overshooting.
        if ((i & 15) == 0)
            squashComp.setAmount (squashSm.current);
        squashSm.next();
        float x[2] = { 0.0f, 0.0f };
        float pk = 0.0f;
        for (int c = 0; c < numCh; ++c)
        {
            x[c] = io[c][i] * gIn;
            pk = jmax (pk, std::abs (x[c]));
        }
        const float sqGain = squashComp.process (pk);

        const float meterGain = std::sqrt (g); // the IN meter shows the record level (input + half the drive)
        float ms = 0.0f;
        for (int c = 0; c < numCh; ++c)
        {
            ms += x[c] * x[c];
            work.getWritePointer (c)[i] = chans[(size_t) c].preEmph.process (x[c] * sqGain);
        }
        inMs += meterCoeff * (ms * meterGain * meterGain / (float) numCh - inMs);
        const float hit = aa::dsp::clamp01 ((pk * sqGain * g - 0.55f) / 1.3f);
        glow = jmax (hit, glow * glowRelease);
    }

    // ---- 2. oversampled asymmetric saturation
    {
        dsp::AudioBlock<float> block (work.getArrayOfWritePointers(), (size_t) numCh, (size_t) n);
        auto up = oversampler->processSamplesUp (block);
        const int upN = (int) up.getNumSamples();
        for (size_t c = 0; c < up.getNumChannels(); ++c)
        {
            float* p = up.getChannelPointer (c);
            for (int j = 0; j < upN; ++j)
            {
                const size_t bi = (size_t) (j >> osOrder);
                p[j] = tape::Saturator::shape (p[j], driveG[bi], driveB[bi], driveOff[bi]);
            }
        }
        oversampler->processSamplesDown (block);
    }

    // ---- 3. transport, ageing, noise, mix (base rate)
    const bool stopOn = tapeStop->load() > 0.5f;
    const float downTime = stopTime->load();
    const float downInc = 1.0f / (jmax (0.05f, downTime) * sr);
    const float upInc = 1.0f / (jmax (0.15f, downTime * 0.5f) * sr);
    const float motorCoeff = aa::dsp::onePoleCoeff (0.012f, sr);
    const float crinkleDepth = crinkleMaxMs * 0.001f * sr;
    const float humInc = (mains->load() > 0.5f ? 60.0f : 50.0f) / sr;
    const float noiseNorm = std::sqrt (sr / 44100.0f);

    float speedSum = 0.0f;
    const float* workL = work.getReadPointer (0);
    const float* workR = numCh > 1 ? work.getReadPointer (1) : workL;

    for (int i = 0; i < n; ++i)
    {
        // -- per-block-ish coefficient updates
        if (--filterCountdown <= 0)
        {
            filterCountdown = 16;
            const float a = ageSm.current;
            if (std::abs (a - lastAge) > 0.0005f)
            {
                lastAge = a;
                const float lpHz = jmin (21000.0f * std::pow (3000.0f / 21000.0f, std::pow (a, 0.85f)), sr * 0.45f);
                const float hpHz = 16.0f * std::pow (200.0f / 16.0f, std::pow (a, 1.4f));
                ageMakeup = aa::dsp::dbToGain (4.0f * a * std::sqrt (a)); // band-limiting loses energy: make some back
                for (auto& c : chans)
                {
                    c.ageLow.setCutoffQ (lpHz, 0.72f, sr);
                    c.ageHigh.setCutoffQ (hpHz, 0.7f, sr);
                    c.hissLow.setCutoff (jmin (lpHz * 1.1f, 11000.0f), sr);
                }
            }
            if (std::abs (motor - lastMotorForFilter) > 0.0005f)
            {
                lastMotorForFilter = motor;
                for (auto& c : chans)
                    c.stopLow.setCutoffQ (140.0f + 17000.0f * motor * motor, 0.6f, sr);
            }
        }

        const float comp = driveComp[(size_t) i] * ageMakeup;
        const float ageAmt = ageSm.next();
        const float wearAmt = wearSm.next();

        // -- tape colour after the saturator: de-emphasis, head bump, DC block
        float sat[2] = { 0.0f, 0.0f };
        for (int c = 0; c < numCh; ++c)
        {
            auto& ch = chans[(size_t) c];
            float y = (c == 0 ? workL[i] : workR[i]) * comp;
            y = ch.deEmph.process (y);
            y = ch.bump.process (y);
            sat[c] = ch.dcBlock.highpass (y);
        }
        if (numCh == 1)
            sat[1] = sat[0];

        for (int c = 0; c < numCh; ++c)
        {
            chans[(size_t) c].wetLine.push (sat[c]);
            chans[(size_t) c].dryLine.push (io[c][i]);
        }

        // -- transport: wow, flutter, crinkle
        float modL = 0.0f, modR = 0.0f;
        wowFlutter.process (wowSm.next(), flutterSm.next(), modL, modR);
        const float crk = crinkleSm.next();
        const float crkOffset = crinkleGen.process (crk, crinkleDepth);

        // -- tape stop motor
        stopPhase = stopOn ? jmin (1.0f, stopPhase + downInc) : jmax (0.0f, stopPhase - upInc);
        const float targetSpeed = 1.0f - stopPhase;
        motor += motorCoeff * (targetSpeed - motor);
        if (targetSpeed <= 0.0f && motor < 1.0e-4f)
            motor = 0.0f;
        if (targetSpeed >= 1.0f && motor > 0.9999f)
            motor = 1.0f;

        if (motor <= 0.0f)
        {
            // Halted (and silent): re-thread the head at the live point so spin-up starts fresh.
            lagA = lagB = 0.0f;
            xfade = -1.0f;
        }
        else
        {
            const float adv = 1.0f - motor;
            lagA = jmin (maxLag, lagA + adv);
            if (xfade >= 0.0f)
                lagB = jmin (maxLag, lagB + adv);
            else if ((motor >= 1.0f && lagA > 0.25f) || lagA >= maxLag)
            {
                xfade = 0.0f; // back up to speed: glide back onto the live point
                lagB = 0.0f;
            }
        }

        // -- read the tape
        float wet[2] = { 0.0f, 0.0f }, dry[2] = { 0.0f, 0.0f };
        const float gA = xfade >= 0.0f ? std::cos (xfade * pi * 0.5f) : 1.0f;
        const float gB = xfade >= 0.0f ? std::sin (xfade * pi * 0.5f) : 0.0f;
        for (int c = 0; c < numCh; ++c)
        {
            auto& ch = chans[(size_t) c];
            const float base = jmax (10.0f, wetCentre + (c == 0 ? modL : modR) + crkOffset);
            float w = ch.wetLine.read (base + lagA);
            float dr = lagA == 0.0f ? ch.dryLine.readInt (latency) : ch.dryLine.read ((float) latency + lagA);
            if (xfade >= 0.0f)
            {
                w = w * gA + ch.wetLine.read (base + lagB) * gB;
                dr = dr * gA + ch.dryLine.read ((float) latency + lagB) * gB;
            }
            if (c == 0)
            {
                speedSum += motor * (1.0f - (base - ch.lastDelay));
                ch.lastDelay = base;
            }
            wet[c] = w;
            dry[c] = dr;
        }
        if (xfade >= 0.0f)
        {
            xfade += xfadeInc;
            if (xfade >= 1.0f)
            {
                xfade = -1.0f;
                lagA = lagB;
                lagB = 0.0f;
            }
        }

        // -- age: bandwidth closes in, stereo image narrows a touch
        for (int c = 0; c < numCh; ++c)
        {
            auto& ch = chans[(size_t) c];
            wet[c] = ch.ageLow.lowpass (ch.ageHigh.highpass (wet[c]));
        }
        if (numCh > 1)
        {
            const float width = 1.0f - 0.35f * ageAmt;
            const float m = 0.5f * (wet[0] + wet[1]);
            const float s = 0.5f * (wet[0] - wet[1]) * width;
            wet[0] = m + s;
            wet[1] = m - s;
        }

        // -- wear: dropouts lose level and top end (crinkles do a little too)
        const float dip = jmin (0.95f, dropouts.process (wearAmt) + 0.3f * crk * crinkleGen.envelope());
        const float dipTone = jmin (1.0f, dip * 1.6f);
        for (int c = 0; c < numCh; ++c)
        {
            const float dull = chans[(size_t) c].dropTone.lowpass (wet[c]);
            wet[c] = (wet[c] + (dull - wet[c]) * dipTone) * (1.0f - dip);
        }

        // -- hiss breathes with the signal; it fades with the motor
        const float level = numCh > 1 ? 0.5f * (std::abs (wet[0]) + std::abs (wet[1])) : std::abs (wet[0]);
        hissEnv += (level > hissEnv ? hissEnvA : hissEnvR) * (level - hissEnv);
        const float stopGain = smoothstep (0.0f, 0.35f, motor);
        const float hissGain = hissSm.next() * (0.5f + 0.5f * jmin (1.0f, hissEnv * 6.0f)) * stopGain * noiseNorm;

        // -- mains hum (electrical, so it keeps going when the tape stops)
        const float humGain = humSm.next();
        float humSample = 0.0f;
        if (humGain > 1.0e-7f)
        {
            humPhase += humInc;
            if (humPhase >= 1.0f)
                humPhase -= 1.0f;
            const float th = twoPi * humPhase;
            humSample = humGain * (std::sin (th) + 0.5f * std::sin (2.0f * th) + 0.28f * std::sin (3.0f * th)
                                   + 0.12f * std::sin (5.0f * th));
        }

        const float mixAmt = mixSm.next();
        const float outG = outGainSm.next();
        const float stopLp = aa::dsp::clamp01 ((1.0f - motor) * 3.0f);
        float ms = 0.0f;

        for (int c = 0; c < numCh; ++c)
        {
            auto& ch = chans[(size_t) c];
            float hn = 0.0f;
            if (hissGain > 0.0f)
                hn = ch.hissLow.lowpass (ch.hissHigh.highpass (noise.nextBipolar())) * hissGain;

            float out = dry[c] * (1.0f - mixAmt) + (wet[c] + hn) * mixAmt;
            const float lp = ch.stopLow.lowpass (out);
            out = (out + (lp - out) * stopLp) * stopGain;
            out = (out + humSample * mixAmt) * outG;
            io[c][i] = out;
            ms += out * out;
        }
        outMs += meterCoeff * (ms / (float) numCh - outMs);
    }

    lastChunkSpeed = speedSum / (float) jmax (1, n);
}

AudioProcessorEditor* TapeDreamsProcessor::createEditor()
{
    return new TapeDreamsEditor (*this);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TapeDreamsProcessor();
}
