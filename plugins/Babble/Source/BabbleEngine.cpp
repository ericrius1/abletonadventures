#include "BabbleEngine.h"

using namespace juce;

namespace babble
{
namespace
{
    constexpr float halfPi = 1.5707963f;
    constexpr float masterGain = 0.135f;

    /** Parabolic sine approximation of sin(2*pi*p), p in [0,1). */
    inline float sin2pi (float p)
    {
        float x = (p < 0.5f ? p : p - 1.0f) * 2.0f;
        const float y = 4.0f * x * (1.0f - std::abs (x));
        return y * (0.775f + 0.225f * std::abs (y));
    }

    inline float raisedCos (float x) { return 0.5f - 0.5f * std::cos (aa::dsp::pi * aa::dsp::clamp01 (x)); }

    inline float smoothstep (float x)
    {
        x = aa::dsp::clamp01 (x);
        return x * x * (3.0f - 2.0f * x);
    }

    inline float coeff (float seconds, float rate) { return aa::dsp::onePoleCoeff (seconds, rate); }

    inline float talkAmount (float babble) { return aa::dsp::clamp01 ((babble - 0.55f) / 0.45f); }

    /** Smooth maximum (soft knee) used for formant tracking. */
    inline float softMax4 (float a, float b)
    {
        const float a2 = a * a, b2 = b * b;
        return std::sqrt (std::sqrt (a2 * a2 + b2 * b2));
    }

    /** Sum over the harmonics of f0 of the power response |H|^2 of a unity-peak resonator at F
        with bandwidth bw. Used to normalise loudness across pitch, vowel and voice type. */
    float harmonicPowerSum (float F, float bw, float f0)
    {
        if (f0 < bw * 0.2f)
            return halfPi * bw / f0; // many harmonics in the band: the integral is exact enough

        const float q = F / bw;
        const int n0 = jmax (1, (int) (F / f0));
        const int lo = jmax (1, n0 - 4), hi = n0 + 5;
        float s = 0.0f;
        for (int n = lo; n <= hi; ++n)
        {
            const float r = (float) n * f0 / F;
            const float x = q * (r - 1.0f / r);
            s += 1.0f / (1.0f + x * x);
        }

        // Lorentzian tails beyond the explicit window
        const float scale = 0.5f * bw / f0;
        s += scale * (halfPi - std::atan (2.0f * (((float) hi + 0.5f) * f0 - F) / bw));
        if (lo > 1)
            s += scale * (halfPi - std::atan (2.0f * (F - ((float) lo - 0.5f) * f0) / bw));
        return s;
    }

    // Where each choir singer sits (detune and pan), by number of singers.
    constexpr float choirPos[maxChoir + 1][maxChoir] = {
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { -0.5f, 0.5f, 0.0f, 0.0f },
        { 0.0f, -1.0f, 1.0f, 0.0f },
        { -0.33f, 0.33f, -1.0f, 1.0f },
    };
} // namespace

//==============================================================================
void Engine::prepare (double sampleRate)
{
    sr = sampleRate;
    tickLen = jlimit (8, 32, (int) std::round (8.0 * sr / 48000.0));
    tickSeconds = (float) tickLen / (float) sr;
    tickRate = (float) sr / (float) tickLen;

    aa::dsp::Rng seeder (0xB4BB1Eu);
    for (auto& v : voices)
    {
        v.env.setSampleRate ((float) sr);
        v.rng.seed (seeder.nextU32());
        for (auto& sub : v.subs)
        {
            sub.drift.rng.seed (seeder.nextU32());
            sub.jitter.rng.seed (seeder.nextU32());
            sub.driftRate = 0.5f + seeder.next01() * 1.1f;
            sub.jitterRate = 7.0f + seeder.next01() * 6.0f;
            sub.vibRateMul = 0.94f + seeder.next01() * 0.12f;
        }
    }

    chorus.prepare (sr);
    reverb.prepare (sr);
    lastReverbAmount = -1.0f;
    volume.reset (sr, 0.03, aa::dsp::dbToGain (settings.volumeDb));
    dcCoeff = 1.0f - std::exp (-aa::dsp::twoPi * 12.0f / (float) sr);
    reset();
}

void Engine::reset()
{
    for (auto& v : voices)
    {
        v.active = v.held = v.sustained = v.pendingOff = false;
        v.gateCountdown = 0;
        v.note = -1;
        v.env.kill();
        v.kill = v.killTarget = 1.0f;
        for (auto& f : v.bankL) f.reset();
        for (auto& f : v.bankR) f.reset();
        v.tiltL = v.tiltR = 0.0f;
    }

    sustainDown = false;
    bend = bendTarget = 0.0f;
    modWheel = modWheelTarget = 0.0f;
    lastNotePitch = glideSource = -1.0f;
    lastNoteOnClock = -1000000;
    firstTick = true;
    samplesUntilTick = 0;

    sylPhase = 0.0f;
    sylTime = 1.0f;
    depthEff = 0.0f;
    lastSyncIndex = -1.0e9;
    forceSyllable = wasBabbling = false;
    gap = gapTarget = 1.0f;
    inflect = inflectTarget = 0.0f;
    accent = 1.0f;
    locusEnv = murmurEnv = noiseEnv = aspEnv = lipsEnv = 0.0f;
    activity = gateActivity = 0.0f;

    consonantFilterL.reset();
    consonantFilterR.reset();
    consonantNoiseLevel = consonantNoiseStep = 0.0f;
    murmurCoeff = 1.0f;
    murmurL = murmurR = 0.0f;
    crushPhase = crushL = crushR = 0.0f;
    dcL = dcR = 0.0f;
    peak = 0.0f;

    chorus.reset();
    reverb.reset();
}

//==============================================================================
void Engine::handleMidi (const MidiMessage& m)
{
    if (m.isNoteOn())
        noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())
        noteOff (m.getNoteNumber());
    else if (m.isPitchWheel())
        bendTarget = (float) (m.getPitchWheelValue() - 8192) / 8192.0f * 2.0f;
    else if (m.isAllSoundOff())
        killAll();
    else if (m.isAllNotesOff())
        releaseAll();
    else if (m.isSustainPedalOn())
        setSustain (true);
    else if (m.isSustainPedalOff())
        setSustain (false);
    else if (m.isController())
    {
        if (m.getControllerNumber() == 1)
            modWheelTarget = (float) m.getControllerValue() / 127.0f;
        else if (m.getControllerNumber() == 121) // reset all controllers
        {
            bendTarget = 0.0f;
            modWheelTarget = 0.0f;
            setSustain (false);
        }
    }
}

int Engine::pickVoice (int note)
{
    for (int i = 0; i < maxVoices; ++i)
        if (voices[(size_t) i].active && voices[(size_t) i].note == note)
            return i;

    for (int i = 0; i < maxVoices; ++i)
        if (! voices[(size_t) i].active)
            return i;

    // Steal the quietest released voice, else the oldest held one.
    int best = -1;
    float bestLevel = 10.0f;
    for (int i = 0; i < maxVoices; ++i)
    {
        const auto& v = voices[(size_t) i];
        if (! v.held && ! v.sustained && v.env.value < bestLevel)
        {
            bestLevel = v.env.value;
            best = i;
        }
    }
    if (best >= 0)
        return best;

    uint32_t oldest = 0xffffffffu;
    for (int i = 0; i < maxVoices; ++i)
        if (voices[(size_t) i].order < oldest)
        {
            oldest = voices[(size_t) i].order;
            best = i;
        }
    return jmax (0, best);
}

void Engine::noteOn (int note, float velocity)
{
    // Notes struck together (a chord) all glide from the note played before the chord,
    // instead of sliding from one another.
    if (sampleClock - lastNoteOnClock > (int64_t) (0.03 * sr))
        glideSource = lastNotePitch;
    lastNoteOnClock = sampleClock;

    bool anyHeld = false;
    for (auto& v : voices)
        anyHeld = anyHeld || (v.active && (v.held || v.sustained || v.pendingOff));

    auto& v = voices[(size_t) pickVoice (note)];
    const bool wasActive = v.active;

    v.note = note;
    v.velocity = jlimit (0.0f, 1.0f, velocity);
    v.velGain = 0.2f + 0.8f * v.velocity;
    v.held = true;
    v.sustained = false;
    v.pendingOff = false;
    v.gateCountdown = (int) (0.035 * sr);
    v.order = ++orderCounter;
    v.target = (float) note;
    v.age = 0.0f;
    v.killTarget = 1.0f;

    if (! wasActive)
    {
        v.active = true;
        v.kill = 1.0f;
        v.env.value = 0.0f;
        for (auto& f : v.bankL) f.reset();
        for (auto& f : v.bankR) f.reset();
        v.tiltL = v.tiltR = 0.0f;

        const int n = jlimit (1, maxChoir, settings.choir);
        for (int i = 0; i < maxChoir; ++i)
        {
            auto& sub = v.subs[(size_t) i];
            sub.phase = v.rng.next01();
            sub.vibPhase = v.rng.next01();
            sub.gain = i < n ? 1.0f : 0.0f;
            const float theta = (choirPos[n][i] * (0.3f + 0.7f * aa::dsp::clamp01 (settings.detune * 1.6f)) + 1.0f) * 0.25f * aa::dsp::pi;
            sub.panL = std::cos (theta);
            sub.panR = std::sin (theta);
        }

        v.pitch = (settings.glide > 0.002f && glideSource >= 0.0f) ? glideSource : (float) note;
    }
    else if (settings.glide <= 0.002f)
    {
        v.pitch = (float) note;
    }

    v.env.set (settings.attack, 0.1f, 1.0f, settings.release);
    v.env.noteOn();
    lastNotePitch = (float) note;

    if (! anyHeld)
        forceSyllable = true;

    uiEvents.push ({ UiEvent::noteOn, note, 0, v.velocity });
}

void Engine::noteOff (int note)
{
    for (auto& v : voices)
        if (v.active && v.held && v.note == note)
        {
            v.held = false;
            if (sustainDown)
                v.sustained = true;
            else if (v.gateCountdown > 0)
                v.pendingOff = true;
            else
                v.env.noteOff();
        }
}

void Engine::setSustain (bool down)
{
    sustainDown = down;
    if (! down)
        for (auto& v : voices)
            if (v.sustained)
            {
                v.sustained = false;
                if (! v.held)
                {
                    if (v.gateCountdown > 0)
                        v.pendingOff = true;
                    else
                        v.env.noteOff();
                }
            }
}

void Engine::releaseAll()
{
    for (auto& v : voices)
    {
        v.held = v.sustained = v.pendingOff = false;
        v.env.noteOff();
    }
}

void Engine::killAll()
{
    for (auto& v : voices)
    {
        v.held = v.sustained = v.pendingOff = false;
        v.killTarget = 0.0f;
        v.env.noteOff();
    }
    sustainDown = false;
}

//==============================================================================
void Engine::startSyllable (bool fresh)
{
    const auto& list = consonants();
    const float amount = settings.babble;
    const float hardScale = aa::dsp::clamp01 ((amount - 0.25f) / 0.35f);

    std::array<float, numConsonants> weights {};
    float total = 0.0f;
    for (size_t i = 0; i < list.size(); ++i)
    {
        float w = list[i].weight * (list[i].soft ? 1.0f : hardScale);
        if ((int) i == lastConsonant)
            w *= 0.2f;
        weights[i] = w;
        total += w;
    }

    float r = rng.next01() * total;
    consonant = 0;
    for (size_t i = 0; i < list.size(); ++i)
    {
        r -= weights[i];
        if (r <= 0.0f && weights[i] > 0.0f)
        {
            consonant = (int) i;
            break;
        }
    }
    lastConsonant = consonant;

    prevRandomVowel = randomVowel;
    int rv = rng.nextInt (numVowels);
    if (rv == randomVowel)
        rv = (rv + 1 + rng.nextInt (numVowels - 1)) % numVowels;
    randomVowel = rv;

    if (! settings.babbleSync)
        sylLength = (1.0f / jmax (0.1f, settings.babbleRate)) * (1.0f + 0.3f * amount * rng.nextBipolar());

    // talking prosody: at high babble amounts each syllable gets its own little pitch inflection
    // and loudness accent
    const float talk = talkAmount (amount);
    inflectTarget = rng.nextBipolar() * 0.9f * talk;
    accent = 1.0f - 0.28f * amount * rng.next01();

    const auto& c = list[(size_t) consonant];
    closeLen = jmin (c.closeMs * 0.001f, 0.5f * sylLength);
    depthEff = c.depth * aa::dsp::clamp01 (amount * 2.5f);
    sylTime = (fresh && activity < 0.02f) ? 0.35f * closeLen : 0.0f;

    if (fresh || gateActivity > 0.001f)
    {
        const float sung = settings.vowel + amount * ((float) randomVowel - settings.vowel);
        uiEvents.push ({ UiEvent::syllable, consonant, jlimit (0, numVowels - 1, roundToInt (sung)), amount });
    }
}

void Engine::babbleTick (const aa::dsp::Transport& transport, int samplePos)
{
    const auto& s = settings;
    const float amount = s.babble;

    if (amount < 0.005f)
    {
        inflect += (0.0f - inflect) * coeff (0.04f, tickRate);
        accent = 1.0f;
        wasBabbling = false;
        forceSyllable = false;
        vowelTarget = s.vowel;
        gapTarget = 1.0f;
        depthEff = 0.0f;
        locusEnv = murmurEnv = noiseEnv = aspEnv = lipsEnv = 0.0f;
        return;
    }

    bool newSyllable = false, fresh = false;
    if (s.babbleSync)
    {
        const double beats = aa::dsp::syncDivisionBeats (s.babbleDivision);
        const double ppq = transport.ppqAtBlockStart + transport.samplesToPpq ((double) samplePos);
        const double index = std::floor (ppq / beats);
        if (index != lastSyncIndex)
        {
            lastSyncIndex = index;
            newSyllable = true;
        }
        sylLength = (float) (beats * 60.0 / jmax (20.0, transport.bpm));
    }
    else
    {
        sylPhase += tickSeconds / jmax (0.02f, sylLength);
        if (sylPhase >= 1.0f)
        {
            sylPhase = 0.0f;
            newSyllable = true;
        }
    }

    if (forceSyllable || ! wasBabbling)
    {
        newSyllable = true;
        fresh = forceSyllable;
        sylPhase = 0.0f;
    }
    forceSyllable = false;
    wasBabbling = true;

    if (newSyllable)
        startSyllable (fresh);
    else
        sylTime += tickSeconds;

    const auto& c = consonants()[(size_t) consonant];
    const float d = jmax (0.004f, closeLen), t = sylTime;

    float closure = 0.0f;
    if (t < 0.35f * d)      closure = raisedCos (t / (0.35f * d));
    else if (t < 0.6f * d)  closure = 1.0f;
    else if (t < d)         closure = 1.0f - raisedCos ((t - 0.6f * d) / (0.4f * d));

    gapTarget = (1.0f - depthEff * closure) * (t >= 0.4f * d ? accent : 1.0f);
    // the inflection drifts down a touch through each syllable, like speech
    inflect += (inflectTarget - 0.15f * talkAmount (amount) * (t / jmax (0.05f, sylLength)) - inflect) * coeff (0.04f, tickRate);

    const float locus = t < 0.6f * d ? raisedCos (t / (0.25f * d))
                                     : 1.0f - raisedCos ((t - 0.6f * d) / jmax (0.03f, 0.9f * d));
    locusEnv = locus * depthEff;
    murmurEnv = closure * c.murmur * depthEff;
    lipsEnv = closure * c.lips * depthEff;

    noiseEnv = 0.0f;
    if (c.noiseLevel > 0.0f)
    {
        if (c.burst)
        {
            const float t0 = 0.6f * d, tau = c.noiseMs * 0.001f * 0.4f;
            if (t >= t0 && t - t0 < tau * 6.0f)
                noiseEnv = std::exp (-(t - t0) / tau) * aa::dsp::clamp01 ((t - t0) / 0.0015f);
        }
        else
        {
            const float dur = jmin (c.noiseMs * 0.001f, 0.85f * d);
            const float u = (t - 0.05f * d) / jmax (0.005f, dur);
            if (u > 0.0f && u < 1.0f)
                noiseEnv = 0.5f - 0.5f * std::cos (aa::dsp::twoPi * u);
        }
    }

    aspEnv = 0.0f;
    if (c.aspiration > 0.0f)
    {
        if (c.burst)
        {
            const float t0 = 0.6f * d;
            if (t >= t0)
                aspEnv = c.aspiration * std::exp (-(t - t0) / 0.03f) * aa::dsp::clamp01 ((t - t0) / 0.003f);
        }
        else
        {
            aspEnv = c.aspiration * closure;
        }
        aspEnv *= aa::dsp::clamp01 (amount * 2.5f);
    }

    const float rv = (float) (t >= 0.4f * d ? randomVowel : prevRandomVowel);
    vowelTarget = s.vowel + amount * (rv - s.vowel);
}

//==============================================================================
void Engine::controlTick (const aa::dsp::Transport& transport, int samplePos)
{
    const auto& s = settings;

    bend += (bendTarget - bend) * coeff (0.008f, tickRate);
    modWheel += (modWheelTarget - modWheel) * coeff (0.03f, tickRate);
    robotS += ((s.voiceType == robot ? 1.0f : 0.0f) - robotS) * coeff (0.03f, tickRate);
    shiftS += (s.shift - shiftS) * coeff (0.05f, tickRate);
    brightS += (s.brightness - brightS) * coeff (0.03f, tickRate);
    breathS += (s.breath - breathS) * coeff (0.03f, tickRate);
    chorusMix += (s.chorus - chorusMix) * coeff (0.05f, tickRate);
    reverbMix += (s.reverb - reverbMix) * coeff (0.05f, tickRate);

    babbleTick (transport, samplePos);

    if (firstTick)
    {
        vowelS = vowelTarget;
        shiftS = s.shift;
        brightS = s.brightness;
        breathS = s.breath;
        robotS = s.voiceType == robot ? 1.0f : 0.0f;
        chorusMix = s.chorus;
        reverbMix = s.reverb;
    }
    else
    {
        vowelS += (vowelTarget - vowelS) * coeff (s.babble > 0.005f ? 0.03f : 0.04f, tickRate);
    }

    // ---- formant frame -----------------------------------------------------------
    const float v = jlimit (0.0f, (float) (numVowels - 1), vowelS);
    const int i0 = jmin (numVowels - 2, (int) v);
    const float f = v - (float) i0;
    const auto& a = vowelShape (s.voiceType, i0);
    const auto& b = vowelShape (s.voiceType, i0 + 1);
    const float setCoeff = coeff (0.018f, tickRate);

    for (int k = 0; k < numFormants; ++k)
    {
        const float lf = aa::dsp::lerp (std::log (a.freq[(size_t) k]), std::log (b.freq[(size_t) k]), f);
        const float bw = aa::dsp::lerp (a.bw[(size_t) k], b.bw[(size_t) k], f);
        const float db = aa::dsp::lerp (a.db[(size_t) k], b.db[(size_t) k], f);
        if (firstTick)
        {
            logFreqS[(size_t) k] = lf;
            bwS[(size_t) k] = bw;
            dbS[(size_t) k] = db;
        }
        else
        {
            logFreqS[(size_t) k] += (lf - logFreqS[(size_t) k]) * setCoeff;
            bwS[(size_t) k] += (bw - bwS[(size_t) k]) * setCoeff;
            dbS[(size_t) k] += (db - dbS[(size_t) k]) * setCoeff;
        }
    }
    firstTick = false;

    const auto& c = consonants()[(size_t) consonant];
    const float shiftMul = aa::dsp::semitonesToRatio (shiftS);
    const float bwMul = std::sqrt (shiftMul);
    const float presence = jmax (0.0f, brightS - 0.55f), dullness = jmax (0.0f, 0.3f - brightS);
    for (int k = 0; k < numFormants; ++k)
    {
        const float locus = k < 3 ? 1.0f + (c.locus[k] - 1.0f) * locusEnv : 1.0f;
        const float boost = k == 0 ? 0.0f : (k == 1 ? 6.0f * presence - 8.0f * dullness : 14.0f * presence - 20.0f * dullness);
        frame.freq[(size_t) k] = jmin (std::exp (logFreqS[(size_t) k]) * shiftMul * locus, 0.45f * (float) sr);
        frame.bw[(size_t) k] = bwS[(size_t) k] * bwMul;
        frame.gain[(size_t) k] = aa::dsp::dbToGain (dbS[(size_t) k] + boost);
    }

    // ---- voices --------------------------------------------------------------------
    float act = 0.0f, gate = 0.0f;
    for (auto& voice : voices)
    {
        if (! voice.active)
            continue;
        voice.env.set (s.attack, 0.1f, 1.0f, s.release);
        voiceTick (voice);
        act += voice.env.value * voice.velGain * voice.kill;
        gate += (voice.held || voice.sustained || voice.pendingOff) ? voice.velGain : voice.env.value * voice.velGain;
    }
    activity = jmin (1.0f, act);
    gateActivity += (jmin (1.0f, gate) - gateActivity) * coeff (0.004f, tickRate);

    // ---- bus ---------------------------------------------------------------------
    gapTarget = jlimit (0.0f, 1.0f, gapTarget);

    const float murmurHz = 18000.0f * std::pow (350.0f / 18000.0f, aa::dsp::clamp01 (murmurEnv));
    murmurCoeff = murmurEnv < 0.001f ? 1.0f : 1.0f - std::exp (-aa::dsp::twoPi * jmin (murmurHz, 0.45f * (float) sr) / (float) sr);

    float noiseTarget = 0.0f;
    if (c.noiseLevel > 0.0f && c.noiseHz > 0.0f)
    {
        const float hz = jmin (c.noiseHz * std::sqrt (shiftMul), 0.42f * (float) sr);
        consonantFilterL.setCutoffQ (hz, c.noiseQ, (float) sr);
        consonantFilterR.setCutoffQ (hz * 1.04f, c.noiseQ, (float) sr);
        consonantNoiseNorm = 1.0f / std::sqrt ((1.0f / 3.0f) * aa::dsp::pi * (hz / c.noiseQ) / (float) sr);
        noiseTarget = noiseEnv * c.noiseLevel * depthEff * gateActivity;
    }
    consonantNoiseStep = (noiseTarget - consonantNoiseLevel) / (float) tickLen;

    if (std::abs (s.reverb - lastReverbAmount) > 0.01f)
    {
        lastReverbAmount = s.reverb;
        aa::dsp::FdnReverb::Params p;
        p.size = 0.72f;
        p.decaySeconds = 1.8f + 2.8f * s.reverb;
        p.damping = 0.45f;
        p.predelayMs = 22.0f;
        p.modDepth = 0.4f;
        p.modRate = 0.35f;
        p.width = 1.0f;
        p.lowCutHz = 140.0f;
        reverb.setParams (p);
    }

    volume.setTarget (aa::dsp::dbToGain (s.volumeDb));
}

void Engine::voiceTick (Voice& v)
{
    const auto& s = settings;

    const float glideCoeff = s.glide < 0.002f ? 1.0f : coeff (s.glide * 0.4f, tickRate);
    v.pitch += (v.target - v.pitch) * glideCoeff;
    v.age += tickSeconds;

    if (v.gateCountdown > 0)
    {
        v.gateCountdown -= tickLen;
        if (v.gateCountdown <= 0 && v.pendingOff)
        {
            v.pendingOff = false;
            v.env.noteOff();
        }
    }

    float base = v.pitch;
    if (robotS > 0.5f)
        base = std::round (base); // the robot can't slide: it steps
    base += bend + inflect;

    const float delay = s.vibDelay;
    const float fade = delay < 0.01f ? 1.0f : smoothstep ((v.age - 0.25f * delay) / (0.75f * delay));
    const float depthSt = s.vibDepth * 0.55f * fade + modWheel * 0.6f;
    const float human = 1.0f - robotS;
    const int n = jlimit (1, maxChoir, s.choir);
    const float panWidth = 0.3f + 0.7f * aa::dsp::clamp01 (s.detune * 1.6f);
    const float subCoeff = coeff (0.03f, tickRate), panCoeff = coeff (0.02f, tickRate);

    float gainSq = 0.0f, vib0 = 0.0f;
    for (int i = 0; i < maxChoir; ++i)
    {
        auto& sub = v.subs[(size_t) i];
        const float target = i < n ? 1.0f : 0.0f;
        sub.gain += (target - sub.gain) * subCoeff;
        if (target == 0.0f && sub.gain < 1.0e-4f)
            sub.gain = 0.0f;

        const float theta = (choirPos[n][i] * panWidth + 1.0f) * 0.25f * aa::dsp::pi;
        sub.panL += (std::cos (theta) - sub.panL) * panCoeff;
        sub.panR += (std::sin (theta) - sub.panR) * panCoeff;
        gainSq += sub.gain * sub.gain;

        if (sub.gain <= 0.0f)
            continue;

        sub.vibPhase += s.vibRate * sub.vibRateMul * tickSeconds;
        if (sub.vibPhase >= 1.0f)
            sub.vibPhase -= 1.0f;
        float vib = sin2pi (sub.vibPhase);
        if (robotS > 0.5f)
            vib = std::round (vib * 2.0f) * 0.5f; // stepped robo-warble
        if (i == 0)
            vib0 = vib;

        const float drift = sub.drift.process (sub.driftRate, tickRate, aa::dsp::Lfo::smoothRandom) * (0.04f + 0.1f * s.detune) * human;
        const float jitter = sub.jitter.process (sub.jitterRate, tickRate, aa::dsp::Lfo::smoothRandom) * 0.025f * human;
        const float st = base + choirPos[n][i] * s.detune * 0.35f + vib * depthSt + drift + jitter;
        sub.dt = jmin (0.45f, aa::dsp::midiToHz (st) / (float) sr);
    }

    v.uniNorm = std::sqrt (2.0f / jmax (0.25f, gainSq));
    v.vibValue = vib0 * depthSt;

    // ---- this voice's formants -------------------------------------------------------
    const float f0 = aa::dsp::midiToHz (base + vib0 * depthSt);
    std::array<float, numFormants> fr, bw;
    fr[0] = softMax4 (frame.freq[0], f0 * 1.05f);          // sopranos raise F1 to follow the pitch
    fr[1] = softMax4 (frame.freq[1], fr[0] * 1.3f);
    for (int k = 2; k < numFormants; ++k)
        fr[(size_t) k] = frame.freq[(size_t) k];

    const float bv = aa::dsp::clamp01 (brightS + (v.velocity - 0.65f) * 0.25f);
    const float fc = jmin (250.0f * std::pow (2.0f, bv * 6.5f), 0.45f * (float) sr);
    v.tiltCoeff = 1.0f - std::exp (-aa::dsp::twoPi * fc / (float) sr);

    float pSrc = 0.0f, pNoise = 0.0f;
    std::array<float, numFormants> noiseW;
    for (int k = 0; k < numFormants; ++k)
    {
        const size_t kk = (size_t) k;
        fr[kk] = jmin (fr[kk], 0.45f * (float) sr);
        bw[kk] = std::sqrt (frame.bw[kk] * frame.bw[kk] + (0.15f * f0) * (0.15f * f0));
        const float g = frame.gain[kk];
        const float tilt2 = 1.0f / (1.0f + (fr[kk] / fc) * (fr[kk] / fc));
        pSrc += g * g * tilt2 * harmonicPowerSum (fr[kk], bw[kk], f0);
        noiseW[kk] = g * std::sqrt (fr[kk] / fr[0]);
        pNoise += noiseW[kk] * noiseW[kk] * bw[kk];
    }

    const float srcScale = 2.0f * f0 / (aa::dsp::pi * fr[0]);
    const float ns = jmin (40.0f, 1.0f / std::sqrt (jmax (1.0e-9f, pSrc * srcScale * srcScale)));
    const float nn = 1.0f / std::sqrt (jmax (1.0e-9f, (1.0f / 3.0f) * aa::dsp::pi * pNoise / (float) sr));

    const float spread = n > 1 ? 0.012f : 0.0f;
    for (int k = 0; k < numFormants; ++k)
    {
        const size_t kk = (size_t) k;
        v.srcGain[kk] = frame.gain[kk] * fr[kk] / fr[0] * ns;
        v.noiseGain[kk] = noiseW[kk] * nn;
        const float q = fr[kk] / bw[kk];
        v.bankL[kk].setCutoffQ (fr[kk] * (1.0f - spread), q, (float) sr);
        v.bankR[kk].setCutoffQ (fr[kk] * (1.0f + spread), q, (float) sr);
    }
}

//==============================================================================
void Engine::renderVoice (Voice& v, float* outL, float* outR, int n, float gapStart, float gapStep)
{
    const float pw = 0.3f;
    const float pulseMix = jlimit (0.0f, 0.9f, jmax (0.0f, brightS - 0.65f) * 1.2f + robotS * 0.75f);
    const float ring = robotS * 0.5f;
    const float voiced = 1.0f - breathS * breathS;
    const float breathAmt = 0.85f * std::pow (breathS, 0.8f) + 0.9f * aspEnv;
    const float voicedMul = voiced * (1.0f - 0.7f * aspEnv);
    const float glottalDepth = 0.6f * voiced;
    const float killStep = 1.0f / (0.005f * (float) sr);
    const float makeup = 1.0f + 0.5f * robotS; // ring mod + narrow resonances lose some energy

    float g = gapStart;
    for (int i = 0; i < n; ++i)
    {
        const float e = v.env.process();
        if (v.kill != v.killTarget)
            v.kill = v.killTarget > v.kill ? jmin (v.killTarget, v.kill + killStep) : jmax (v.killTarget, v.kill - killStep);
        const float amp = e * v.velGain * v.kill;

        float sL = 0.0f, sR = 0.0f;
        for (auto& sub : v.subs)
        {
            if (sub.gain <= 0.0f)
                continue;

            const float ph = sub.phase, dt = sub.dt;
            float x = 2.0f * ph - 1.0f - aa::dsp::BlepOsc::polyBlep (ph, dt);
            if (pulseMix > 0.0f)
            {
                float t2 = ph - pw;
                if (t2 < 0.0f)
                    t2 += 1.0f;
                const float pulse = (ph < pw ? 1.0f : -1.0f) + aa::dsp::BlepOsc::polyBlep (ph, dt)
                                    - aa::dsp::BlepOsc::polyBlep (t2, dt);
                // inverted so its edge lines up with the saw's reset (blending in phase keeps the level)
                x += pulseMix * (-0.7f * pulse - x);
            }
            if (ring > 0.0f)
            {
                float carrier = ph * 2.0f;
                if (carrier >= 1.0f)
                    carrier -= 1.0f;
                x *= (1.0f - ring) + ring * 1.41f * sin2pi (carrier);
            }

            sub.phase += dt;
            if (sub.phase >= 1.0f)
                sub.phase -= 1.0f;

            sL += x * sub.gain * sub.panL;
            sR += x * sub.gain * sub.panR;
        }

        v.tiltL += v.tiltCoeff * (sL * v.uniNorm - v.tiltL);
        v.tiltR += v.tiltCoeff * (sR * v.uniNorm - v.tiltR);

        const float glottal = 1.0f + glottalDepth * sin2pi (v.subs[0].phase);
        const float nL = v.rng.nextBipolar() * glottal * breathAmt;
        const float nR = v.rng.nextBipolar() * glottal * breathAmt;
        const float vg = voicedMul * g;
        g += gapStep;
        const float xL = v.tiltL * vg, xR = v.tiltR * vg;

        float oL = 0.0f, oR = 0.0f;
        for (int k = 0; k < numFormants; ++k)
        {
            auto& fl = v.bankL[(size_t) k];
            auto& frr = v.bankR[(size_t) k];
            oL += fl.k * fl.process (v.srcGain[(size_t) k] * xL + v.noiseGain[(size_t) k] * nL).bp;
            oR += frr.k * frr.process (v.srcGain[(size_t) k] * xR + v.noiseGain[(size_t) k] * nR).bp;
        }

        outL[i] += oL * amp * makeup;
        outR[i] += oR * amp * makeup;
    }

    if (! v.env.isActive() || (v.killTarget <= 0.0f && v.kill <= 0.0f))
    {
        v.active = v.held = v.sustained = v.pendingOff = false;
        v.gateCountdown = 0;
        v.note = -1;
        v.env.kill();
        v.kill = v.killTarget = 1.0f;
    }
}

void Engine::renderChunk (float* left, float* right, int n)
{
    std::fill (chunkL.begin(), chunkL.begin() + n, 0.0f);
    std::fill (chunkR.begin(), chunkR.begin() + n, 0.0f);

    // gap ramps toward its target, but never faster than a full swing in 4 ms
    const float maxStep = 1.0f / (0.004f * (float) sr);
    const float gapStep = jlimit (-maxStep, maxStep, (gapTarget - gap) / (float) jmax (1, samplesUntilTick));

    for (auto& v : voices)
        if (v.active)
            renderVoice (v, chunkL.data(), chunkR.data(), n, gap, gapStep);
    gap = jlimit (0.0f, 1.0f, gap + gapStep * (float) n);

    const float crushInc = 7200.0f / (float) sr;
    const float crushMix = robotS * 0.3f;

    for (int i = 0; i < n; ++i)
    {
        float l = chunkL[(size_t) i], r = chunkR[(size_t) i];

        // nasal / voiced-stop murmur: the voice goes muffled while the mouth is closed
        murmurL += murmurCoeff * (l - murmurL);
        murmurR += murmurCoeff * (r - murmurR);
        l = murmurL;
        r = murmurR;

        if (consonantNoiseLevel > 0.0f || consonantNoiseStep > 0.0f)
        {
            const float lev = consonantNoiseLevel * consonantNoiseNorm;
            l += consonantFilterL.k * consonantFilterL.process (rng.nextBipolar()).bp * lev;
            r += consonantFilterR.k * consonantFilterR.process (rng.nextBipolar()).bp * lev;
        }
        consonantNoiseLevel = jmax (0.0f, consonantNoiseLevel + consonantNoiseStep);

        l *= masterGain;
        r *= masterGain;

        if (robotS > 0.001f)
        {
            // gentle saturation tames the spiky pulse and adds a bit of growl
            l += robotS * (aa::dsp::softClip (l * 1.6f) * 0.625f - l);
            r += robotS * (aa::dsp::softClip (r * 1.6f) * 0.625f - r);
        }

        if (crushMix > 0.001f)
        {
            crushPhase += crushInc;
            if (crushPhase >= 1.0f)
            {
                crushPhase -= 1.0f;
                crushL = std::round (l * 40.0f) / 40.0f;
                crushR = std::round (r * 40.0f) / 40.0f;
            }
            l += crushMix * (crushL - l);
            r += crushMix * (crushR - r);
        }

        chunkL[(size_t) i] = l;
        chunkR[(size_t) i] = r;
    }

    if (chorusMix > 0.001f)
        chorus.process (chunkL.data(), chunkR.data(), n, 0.45f, 0.55f, chorusMix);

    const float dryGain = 1.0f - 0.35f * reverbMix;
    const float wetGain = reverbMix * 0.9f;

    for (int i = 0; i < n; ++i)
    {
        float l = chunkL[(size_t) i], r = chunkR[(size_t) i];
        float wl = 0.0f, wr = 0.0f;
        reverb.processSample (l, r, wl, wr);
        const float vol = volume.next();
        l = (l * dryGain + wl * wetGain) * vol;
        r = (r * dryGain + wr * wetGain) * vol;

        dcL += dcCoeff * (l - dcL);
        dcR += dcCoeff * (r - dcR);
        l -= dcL;
        r -= dcR;

        auto clip = [] (float x)
        {
            const float a = std::abs (x);
            if (a < 0.85f)
                return x;
            const float y = 0.85f + 0.15f * std::tanh ((a - 0.85f) / 0.15f);
            return x < 0.0f ? -y : y;
        };
        l = clip (l);
        r = clip (r);

        peak = jmax (peak, std::abs (l), std::abs (r));

        if (right != nullptr)
        {
            left[i] = l;
            right[i] = r;
        }
        else
        {
            left[i] = 0.5f * (l + r);
        }
    }
}

void Engine::render (float* left, float* right, int numSamples, const aa::dsp::Transport& transport, int blockOffset)
{
    int pos = 0;
    while (pos < numSamples)
    {
        if (samplesUntilTick <= 0)
        {
            controlTick (transport, blockOffset + pos);
            samplesUntilTick = tickLen;
        }

        const int n = jmin (samplesUntilTick, numSamples - pos);
        renderChunk (left + pos, right != nullptr ? right + pos : nullptr, n);
        sampleClock += n;
        pos += n;
        samplesUntilTick -= n;
    }
}

void Engine::publishUi()
{
    int held = 0;
    float vib = 0.0f;
    uint32_t newest = 0;
    for (const auto& v : voices)
        if (v.active)
        {
            if (v.held || v.sustained)
                ++held;
            if (v.order >= newest)
            {
                newest = v.order;
                vib = v.vibValue;
            }
        }

    uiVowel.store (jlimit (0.0f, 4.0f, vowelS));
    uiOpen.store (activity * gap);
    uiLevel.store (peak);
    uiVibrato.store (jlimit (-1.5f, 1.5f, vib / 0.55f));
    uiLips.store (lipsEnv);
    uiActiveNotes.store (held);
    peak = 0.0f;
}
} // namespace babble
