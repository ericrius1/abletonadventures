#include "StardustEngine.h"

using namespace juce;

namespace stardust
{
namespace
{
    // Unison layouts: detune offsets (sorted low -> high, slightly irregular so the beating never
    // lines up) and the stereo position of each of those voices (shuffled so pitch != side).
    const float unisonOffsets[maxUnison][maxUnison] = {
        { 0.0f },
        { -1.0f, 1.0f },
        { -1.0f, 0.0f, 0.97f },
        { -1.0f, -0.31f, 0.33f, 0.96f },
        { -1.0f, -0.48f, 0.0f, 0.5f, 0.95f },
        { -1.0f, -0.58f, -0.19f, 0.2f, 0.6f, 0.97f },
        { -1.0f, -0.62f, -0.29f, 0.0f, 0.3f, 0.64f, 0.98f },
    };

    const float unisonPans[maxUnison][maxUnison] = {
        { 0.0f },
        { -1.0f, 1.0f },
        { -1.0f, 0.0f, 1.0f },
        { -1.0f, 0.33f, -0.33f, 1.0f },
        { -1.0f, 0.5f, 0.0f, -0.5f, 1.0f },
        { -1.0f, 0.6f, -0.2f, 0.2f, -0.6f, 1.0f },
        { -1.0f, 0.67f, -0.33f, 0.0f, 0.33f, -0.67f, 1.0f },
    };

    const float glintHarmonics[] = { 3.0f, 4.0f, 5.0f, 6.0f, 8.0f, 10.0f, 12.0f, 16.0f };

    constexpr float glintLowHz = 1400.0f, glintHighHz = 7500.0f;
    constexpr float glintBaseLevel = 0.26f;
    constexpr float voiceOutputGain = 0.3f; // headroom: a full chord sits around -6 dBFS peak

    inline float nyquistFade (float dt) { return aa::dsp::clamp01 ((0.45f - dt) * 10.0f); }
    inline float smoothstep (float t) { return t * t * (3.0f - 2.0f * t); }
    inline float noteToHz (float note) { return 440.0f * std::exp2 ((note - 69.0f) * (1.0f / 12.0f)); }
} // namespace

//==============================================================================
void Engine::prepare (double sampleRate)
{
    sr = (float) sampleRate;
    uint32_t seed = 0xA341316Cu;
    for (auto& v : voices)
    {
        const Voice fresh;
        v = fresh;
        v.rng.seed (seed);
        seed = seed * 1664525u + 1013904223u;
        v.amp.setSampleRate (sr);
        v.filt.setSampleRate (sr);
    }
    reset();
}

void Engine::reset()
{
    for (auto& v : voices)
    {
        v.active = v.gate = v.sustained = v.fadingOut = false;
        v.amp.kill();
        v.filt.kill();
        v.ic1L = v.ic2L = v.ic1R = v.ic2R = 0.0f;
    }
    for (auto& gl : glints)
        gl.active = false;

    numHeld = 0;
    monoIndex = -1;
    sustainDown = false;
    lastNotePitch = -1.0f;
    bendTarget = modWheelTarget = 0.0f;
    bend.snap (0.0f);
    modWheel.snap (0.0f);
    lfoPhase = vibPhase = 0.0f;
    lfoSmoothed = 0.0f;
    polyGain = 1.0f;
    activeVoiceCount.store (0);
}

//==============================================================================
void Engine::setSettings (const Settings& s)
{
    auto set = [this] (Ctl& c, float v)
    {
        if (settingsInit) c.target = v;
        else c.snap (v);
    };

    set (shapeA, s.shapeA);
    set (shapeB, s.shapeB);
    set (detune, s.detune);
    set (spread, s.spread);
    set (levelA, s.levelA);
    set (levelB, s.levelB);
    set (subLevel, s.subLevel);
    set (noiseLevel, s.noiseLevel);
    set (cutoffOct, std::log2 (jlimit (16.0f, 22000.0f, s.cutoff)));
    set (resonance, s.resonance);
    set (drive, s.drive);
    set (envAmount, s.envAmount);
    set (keyTrack, s.keyTrack);
    set (lfoPitch, s.lfoPitch);
    set (lfoCutoff, s.lfoCutoff);
    set (lfoShapeMod, s.lfoShapeMod);
    set (twinkle, s.twinkle);
    set (drift, s.drift);
    set (gravity, s.gravity);

    if (! settingsInit)
    {
        currentMode = s.voiceMode;
        shapeA0 = shapeA1 = s.shapeA;
        shapeB0 = shapeB1 = s.shapeB;
    }
    settingsInit = true;

    if (s.voiceMode != currentMode)
    {
        allNotesOff();
        monoIndex = -1;
        currentMode = s.voiceMode;
    }

    settings = s;
    octAMul = std::exp2 ((float) s.octaveA);
    bRatio = std::exp2 ((float) s.octaveB + (float) s.semiB / 12.0f + s.fineB / 1200.0f);

    for (auto& v : voices)
        if (v.active)
            applyEnvelopeSettings (v);
}

void Engine::applyEnvelopeSettings (Voice& v) const
{
    v.amp.set (settings.aAttack, settings.aDecay, settings.aSustain, settings.aRelease);
    v.filt.set (settings.fAttack, settings.fDecay, settings.fSustain, settings.fRelease);
}

void Engine::setTransport (double ppqAtBlockStart, double bpm)
{
    blockPpq = ppqAtBlockStart;
    blockBpm = jmax (20.0, bpm);
    samplesIntoBlock = 0;
}

//==============================================================================
void Engine::handleMidi (const MidiMessage& m)
{
    if (m.isNoteOn())
        noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())
        noteOff (m.getNoteNumber());
    else if (m.isPitchWheel())
        bendTarget = (float) (m.getPitchWheelValue() - 8192) / 8192.0f * pitchBendRange;
    else if (m.isAllSoundOff())
        allSoundOff();
    else if (m.isAllNotesOff())
        allNotesOff();
    else if (m.isResetAllControllers())
    {
        bendTarget = 0.0f;
        modWheelTarget = 0.0f;
        sustainPedal (false);
    }
    else if (m.isController())
    {
        const int cc = m.getControllerNumber();
        const int value = m.getControllerValue();
        if (cc == 1)
            modWheelTarget = (float) value / 127.0f;
        else if (cc == 64)
            sustainPedal (value >= 64);
        else if (cc >= 124 && cc <= 127) // omni / mono / poly mode messages imply all-notes-off
            allNotesOff();
    }
}

float Engine::glideStartPitch (int note) const
{
    return (settings.glide > 0.0005f && lastNotePitch >= 0.0f) ? lastNotePitch : (float) note;
}

Engine::Voice* Engine::monoVoice()
{
    if (isPositiveAndBelow (monoIndex, numVoiceSlots))
    {
        auto& v = voices[(size_t) monoIndex];
        if (v.active && ! v.fadingOut)
            return &v;
    }
    return nullptr;
}

void Engine::noteOn (int note, float velocity)
{
    noteEvents.push ({ note, velocity });

    if (currentMode == VoiceMode::poly)
    {
        for (auto& v : voices)
            if (v.active && ! v.fadingOut && v.note == note)
            {
                startVoice (v, note, velocity, v.pitch, true);
                lastNotePitch = (float) note;
                return;
            }

        if (auto* v = allocateVoice())
            startVoice (*v, note, velocity, glideStartPitch (note), false);
        lastNotePitch = (float) note;
        return;
    }

    // ---- mono / legato ----
    const bool wasHeld = numHeld > 0;
    for (int i = 0; i < numHeld; ++i)
        if (heldNotes[(size_t) i] == note)
        {
            for (int j = i; j < numHeld - 1; ++j)
            {
                heldNotes[(size_t) j] = heldNotes[(size_t) j + 1];
                heldVelocity[(size_t) j] = heldVelocity[(size_t) j + 1];
            }
            --numHeld;
            break;
        }
    if (numHeld < (int) heldNotes.size())
    {
        heldNotes[(size_t) numHeld] = note;
        heldVelocity[(size_t) numHeld] = velocity;
        ++numHeld;
    }

    const bool glideOn = settings.glide > 0.0005f;
    if (auto* mv = monoVoice())
    {
        const bool legato = currentMode == VoiceMode::legato && wasHeld;
        const bool glideNow = glideOn && (currentMode == VoiceMode::mono || legato);
        const float startPitch = glideNow ? mv->pitch : (float) note;

        if (legato)
        {
            mv->note = note;
            mv->targetPitch = (float) note;
            mv->pitch = startPitch;
            mv->gate = true;
            mv->sustained = false;
        }
        else
        {
            startVoice (*mv, note, velocity, startPitch, true);
        }
    }
    else if (auto* v = allocateVoice())
    {
        const float startPitch = (currentMode == VoiceMode::mono && glideOn && lastNotePitch >= 0.0f) ? lastNotePitch
                                                                                                      : (float) note;
        startVoice (*v, note, velocity, startPitch, false);
        monoIndex = (int) (v - voices.data());
    }
    lastNotePitch = (float) note;
}

void Engine::noteOff (int note)
{
    if (currentMode == VoiceMode::poly)
    {
        for (auto& v : voices)
            if (v.active && ! v.fadingOut && v.gate && v.note == note)
            {
                if (sustainDown)
                {
                    v.gate = false;
                    v.sustained = true;
                }
                else
                    releaseVoice (v);
            }
        return;
    }

    for (int i = 0; i < numHeld; ++i)
        if (heldNotes[(size_t) i] == note)
        {
            for (int j = i; j < numHeld - 1; ++j)
            {
                heldNotes[(size_t) j] = heldNotes[(size_t) j + 1];
                heldVelocity[(size_t) j] = heldVelocity[(size_t) j + 1];
            }
            --numHeld;
            break;
        }

    auto* mv = monoVoice();
    if (mv == nullptr || mv->note != note || ! mv->gate)
        return;

    if (numHeld > 0)
    {
        // fall back to the previously held key (last-note priority), legato
        const int back = heldNotes[(size_t) numHeld - 1];
        mv->note = back;
        mv->targetPitch = (float) back;
        if (settings.glide <= 0.0005f)
            mv->pitch = (float) back;
        lastNotePitch = (float) back;
    }
    else if (sustainDown)
    {
        mv->gate = false;
        mv->sustained = true;
    }
    else
        releaseVoice (*mv);
}

void Engine::sustainPedal (bool down)
{
    sustainDown = down;
    if (! down)
        for (auto& v : voices)
            if (v.active && v.sustained)
                releaseVoice (v);
}

void Engine::allNotesOff()
{
    for (auto& v : voices)
        if (v.active && ! v.fadingOut)
            releaseVoice (v);
    numHeld = 0;
}

void Engine::allSoundOff()
{
    for (auto& v : voices)
        if (v.active)
            beginFade (v, 0.004f);

    const float fastDecay = std::exp (-6.9f / (0.01f * sr));
    for (auto& gl : glints)
        if (gl.active)
        {
            gl.attacking = false;
            gl.decayMul = jmin (gl.decayMul, fastDecay);
        }

    numHeld = 0;
    monoIndex = -1;
    flushRequested = true;
}

void Engine::releaseVoice (Voice& v)
{
    v.gate = false;
    v.sustained = false;
    v.amp.noteOff();
    v.filt.noteOff();
}

void Engine::beginFade (Voice& v, float seconds)
{
    if (v.fadingOut)
    {
        v.fadeStep = jmax (v.fadeStep, 1.0f / jmax (1.0f, seconds * sr));
        return;
    }
    v.fadingOut = true;
    v.gate = false;
    v.sustained = false;
    v.fadeStep = 1.0f / jmax (1.0f, seconds * sr);
}

Engine::Voice* Engine::allocateVoice()
{
    int live = 0;
    for (auto& v : voices)
        if (v.active && ! v.fadingOut)
            ++live;

    if (live >= maxPolyphony)
    {
        // Steal: the quietest released voice, otherwise the oldest held one. It fades out in a few
        // milliseconds in its own slot while the new note starts in a spare one - no click.
        Voice* victim = nullptr;
        float bestLevel = 1.0e9f;
        for (auto& v : voices)
            if (v.active && ! v.fadingOut && ! v.gate && ! v.sustained && v.amp.value < bestLevel)
            {
                bestLevel = v.amp.value;
                victim = &v;
            }
        if (victim == nullptr)
        {
            uint32_t oldest = 0xffffffffu;
            for (auto& v : voices)
                if (v.active && ! v.fadingOut && v.order < oldest)
                {
                    oldest = v.order;
                    victim = &v;
                }
        }
        if (victim != nullptr)
            beginFade (*victim, 0.006f);
    }

    for (auto& v : voices)
        if (! v.active)
            return &v;

    // Every slot is busy fading: reuse the quietest one (it's nearly silent already).
    Voice* best = nullptr;
    float bestLevel = 1.0e9f;
    for (auto& v : voices)
    {
        const float level = v.fade * v.amp.value;
        if (level < bestLevel)
        {
            bestLevel = level;
            best = &v;
        }
    }
    return best;
}

void Engine::startVoice (Voice& v, int note, float velocity, float startPitch, bool retrigger)
{
    const bool fresh = ! (retrigger && v.active && ! v.fadingOut);

    if (fresh)
    {
        for (int u = 0; u < maxUnison; ++u)
        {
            v.phaseA[(size_t) u] = v.rng.next01();
            v.uniW[(size_t) u] = uniTarget[(size_t) u];
            v.uniDriftPhase[(size_t) u] = v.rng.next01();
            v.uniDriftRate[(size_t) u] = 0.05f + 0.25f * v.rng.next01();
        }
        v.phaseB = v.rng.next01();
        v.phaseSub = 0.0f;
        v.wB = settings.levelB;
        v.wSub = settings.subLevel;
        v.wNoise = settings.noiseLevel * 0.6f;
        v.ic1L = v.ic2L = v.ic1R = v.ic2R = 0.0f;
        v.gPrev = -1.0f; // computed on the first control block
        v.amp.kill();
        v.filt.kill();
        v.driftFrom = v.driftTo = v.rng.nextBipolar();
        v.cutDriftFrom = v.cutDriftTo = v.rng.nextBipolar();
        v.driftPhase = v.rng.next01();
        v.driftRate = 0.15f + 0.35f * v.rng.next01();
        v.noteDetune = drift.value * 0.05f * v.rng.nextBipolar();
        v.noiseLp = 0.0f;
        v.pitch = startPitch;
    }
    else
    {
        v.pitch = startPitch;
    }

    v.active = true;
    v.gate = true;
    v.sustained = false;
    v.fadingOut = false;
    v.fade = 1.0f;
    v.note = note;
    v.targetPitch = (float) note;
    v.velocity = velocity;

    const float va = settings.velocity;
    v.velGain = (1.0f - va) + va * std::pow (jlimit (0.0f, 1.0f, velocity), 1.5f);
    v.envDepth = (1.0f - va) + va * (0.4f + 0.6f * velocity);
    v.order = ++orderCounter;
    v.timeOn = 0.0f;

    applyEnvelopeSettings (v);
    v.amp.noteOn();
    v.filt.noteOn();
}

//==============================================================================
void Engine::updateControls (int len)
{
    const float lenF = (float) len;
    ctlCoeff = 1.0f - std::exp (-lenF / (0.015f * sr));
    uniCoeff = 1.0f - std::exp (-lenF / (0.006f * sr));

    for (auto* c : { &shapeA, &shapeB, &detune, &spread, &levelA, &levelB, &subLevel, &noiseLevel, &cutoffOct,
                     &resonance, &drive, &envAmount, &keyTrack, &lfoPitch, &lfoCutoff, &lfoShapeMod, &twinkle,
                     &drift, &gravity })
        c->step (ctlCoeff);

    bend.target = bendTarget;
    modWheel.target = modWheelTarget;
    bend.step (ctlCoeff);
    modWheel.step (ctlCoeff);

    // ---- LFO ----------------------------------------------------------------
    float newPhase;
    bool wrapped = false;
    if (settings.lfoSync)
    {
        const double ppq = blockPpq + (double) samplesIntoBlock * blockBpm / (60.0 * (double) sr);
        const double beats = lfoDivisionBeats (settings.lfoDivision);
        const double cycles = ppq / beats;
        newPhase = (float) (cycles - std::floor (cycles));
        wrapped = newPhase < lfoPhase;
    }
    else
    {
        newPhase = lfoPhase + settings.lfoRate * lenF / sr;
        if (newPhase >= 1.0f)
        {
            newPhase -= std::floor (newPhase);
            wrapped = true;
        }
    }
    if (wrapped)
    {
        lfoPrevHeld = lfoHeld;
        lfoHeld = rng.nextBipolar();
    }
    lfoPhase = newPhase;

    float lfo = 0.0f;
    switch (settings.lfoShape)
    {
        case 0: lfo = osc::sin2pi (lfoPhase); break;
        case 1: lfo = lfoPhase < 0.25f ? 4.0f * lfoPhase : (lfoPhase < 0.75f ? 2.0f - 4.0f * lfoPhase : 4.0f * lfoPhase - 4.0f); break;
        case 2: lfo = 1.0f - 2.0f * lfoPhase; break;
        case 3: lfo = lfoPhase < 0.5f ? 1.0f : -1.0f; break;
        case 4: lfo = lfoHeld; break;
        default: lfo = lfoPrevHeld + (lfoHeld - lfoPrevHeld) * smoothstep (lfoPhase); break;
    }
    lfoSmoothed += (1.0f - std::exp (-lenF / (0.004f * sr))) * (lfo - lfoSmoothed);

    vibPhase += 5.3f * lenF / sr;
    if (vibPhase >= 1.0f)
        vibPhase -= 1.0f;
    const float vib = osc::sin2pi (vibPhase);

    const float lfoPitchSemis = 12.0f * std::pow (lfoPitch.value, 2.5f);
    pitchMod = lfoSmoothed * lfoPitchSemis + vib * modWheel.value * 0.4f;
    cutMod = lfoSmoothed * lfoCutoff.value * 4.0f;
    const float shapeMod = lfoSmoothed * lfoShapeMod.value * 1.5f;

    shapeA0 = shapeA1;
    shapeB0 = shapeB1;
    shapeA1 = jlimit (0.0f, 3.0f, shapeA.value + shapeMod);
    shapeB1 = jlimit (0.0f, 3.0f, shapeB.value + shapeMod);

    // ---- unison layout ------------------------------------------------------
    const int n = jlimit (1, maxUnison, settings.unison);
    const float norm = 1.0f / std::sqrt ((float) n);
    for (int u = 0; u < maxUnison; ++u)
    {
        if (u < n)
        {
            const float off = unisonOffsets[n - 1][u];
            uniOffset[(size_t) u] = off;
            const float side = std::abs (off) < 0.01f ? 1.0f : 0.85f;
            uniTarget[(size_t) u] = levelA.value * side * norm;
            const float pan = unisonPans[n - 1][u] * spread.value;
            const float angle = (pan + 1.0f) * aa::dsp::pi * 0.25f;
            uniPanL[(size_t) u] = std::cos (angle) * 1.41421356f;
            uniPanR[(size_t) u] = std::sin (angle) * 1.41421356f;
        }
        else
        {
            uniTarget[(size_t) u] = 0.0f;
        }
    }

    // ---- filter -------------------------------------------------------------
    const float res = aa::dsp::clamp01 (resonance.value);
    const float q = 0.55f * std::pow (36.0f, res);
    filterK = 1.0f / q;
    bpNorm = 2.0f * std::sqrt (filterK); // band-pass removes a lot of energy: make up for it
    inputComp = 1.0f / std::sqrt (jmax (1.0f, q * 0.5f));

    const float d = aa::dsp::clamp01 (drive.value);
    drivePre = 0.35f * (1.0f + 15.0f * d * d) * inputComp;
    drivePost = 1.0f / (0.35f * (1.0f + 3.0f * d));

    glideCoeff = settings.glide > 0.0005f ? 1.0f - std::exp (-lenF / (settings.glide * 0.33f * sr)) : 1.0f;
}

//==============================================================================
void Engine::render (float* synthL, float* synthR, float* glintL, float* glintR, int n)
{
    jassert (n <= maxChunk);
    std::fill (synthL, synthL + n, 0.0f);
    std::fill (synthR, synthR + n, 0.0f);
    std::fill (glintL, glintL + n, 0.0f);
    std::fill (glintR, glintR + n, 0.0f);

    int pos = 0;
    while (pos < n)
    {
        const int len = jmin (controlInterval, n - pos);
        updateControls (len);
        spawnGlints (len);

        float energy = 0.0f;
        for (auto& v : voices)
            if (v.active)
            {
                renderVoice (v, synthL + pos, synthR + pos, len);
                energy += v.amp.value * v.velGain * v.fade;
            }

        // Gentle polyphony gain: big chords get a little quieter instead of clipping.
        const float targetGain = 1.0f / std::sqrt (jmax (1.0f, energy / 2.5f));
        const float g0 = polyGain;
        polyGain += (1.0f - std::exp (-(float) len / (0.06f * sr))) * (targetGain - polyGain);
        const float dg = (polyGain - g0) / (float) len;
        for (int j = 0; j < len; ++j)
        {
            const float g = (g0 + dg * (float) (j + 1)) * voiceOutputGain;
            synthL[pos + j] *= g;
            synthR[pos + j] *= g;
        }

        renderGlints (glintL + pos, glintR + pos, len);

        samplesIntoBlock += len;
        pos += len;
    }

    // UI feed
    int live = 0;
    const Voice* newest = nullptr;
    for (auto& v : voices)
        if (v.active && ! v.fadingOut && (v.gate || v.sustained || v.amp.value > 0.01f))
        {
            ++live;
            if (newest == nullptr || v.order > newest->order)
                newest = &v;
        }
    activeVoiceCount.store (live, std::memory_order_relaxed);
    ampEnvLevel.store (newest != nullptr ? newest->amp.value : 0.0f, std::memory_order_relaxed);
    filterEnvLevel.store (newest != nullptr ? newest->filt.value : 0.0f, std::memory_order_relaxed);
    lfoValue.store (lfoSmoothed, std::memory_order_relaxed);
    lfoPhaseOut.store (lfoPhase, std::memory_order_relaxed);
}

void Engine::renderVoice (Voice& v, float* outL, float* outR, int len)
{
    const float lenF = (float) len;
    const float lenSec = lenF / sr;
    const float invSr = 1.0f / sr;
    const float invLen = 1.0f / lenF;

    // ---- pitch: glide, drift, gravity ---------------------------------------
    if (! v.fadingOut)
        v.pitch += glideCoeff * (v.targetPitch - v.pitch);
    v.timeOn += lenSec;

    v.driftPhase += v.driftRate * lenSec;
    if (v.driftPhase >= 1.0f)
    {
        v.driftPhase -= 1.0f;
        v.driftFrom = v.driftTo;
        v.driftTo = v.rng.nextBipolar();
        v.cutDriftFrom = v.cutDriftTo;
        v.cutDriftTo = v.rng.nextBipolar();
    }
    const float ds = smoothstep (v.driftPhase);
    const float dr = drift.value;
    const float driftSemis = dr * 0.14f * (v.driftFrom + (v.driftTo - v.driftFrom) * ds);
    const float cutDrift = dr * 0.4f * (v.cutDriftFrom + (v.cutDriftTo - v.cutDriftFrom) * ds);

    const float g = gravity.value;
    float gravMul = 1.0f, gravExtra = 0.0f, gravPitch = 0.0f;
    if (g > 0.0f)
    {
        // Positive gravity: the unison cloud starts scattered and falls together, the pitch settles in.
        const float e = std::exp (-v.timeOn * 1.25f);
        gravMul = 1.0f + 2.5f * g * e;
        gravExtra = 0.2f * g * e;
        gravPitch = 0.25f * g * std::exp (-v.timeOn * 6.0f);
    }
    else if (g < 0.0f)
    {
        // Negative gravity: voices start as one and slowly bloom apart.
        gravMul = 1.0f + g * std::exp (-v.timeOn * 0.6f);
    }

    const float p = v.pitch + bend.value + pitchMod + driftSemis + v.noteDetune + gravPitch;
    const float baseHz = noteToHz (p);
    v.baseHz = baseHz;

    const float detuneSemis = detune.value * detune.value + 0.08f * detune.value;
    const float detSemis = detuneSemis * gravMul + gravExtra;
    const float dtCentre = baseHz * octAMul * invSr;

    float dtA[maxUnison], w0[maxUnison], w1[maxUnison];
    for (int u = 0; u < maxUnison; ++u)
    {
        auto& ph = v.uniDriftPhase[(size_t) u];
        ph += v.uniDriftRate[(size_t) u] * lenSec;
        if (ph >= 1.0f) ph -= 1.0f;

        float target = 0.0f;
        float dt = dtCentre;
        if (uniTarget[(size_t) u] > 0.0f || v.uniW[(size_t) u] > 1.0e-6f)
        {
            const float semis = uniOffset[(size_t) u] * detSemis + dr * 0.05f * osc::sin2pi (ph);
            dt = jmin (0.49f, dtCentre * std::exp2 (semis * (1.0f / 12.0f)));
            target = uniTarget[(size_t) u] * nyquistFade (dt);
        }
        dtA[u] = dt;
        w0[u] = v.uniW[(size_t) u];
        w1[u] = w0[u] + uniCoeff * (target - w0[u]);
        v.uniW[(size_t) u] = w1[u];
    }

    const float dtB = jmin (0.49f, baseHz * bRatio * invSr);
    const float dtSub = jmin (0.49f, baseHz * 0.5f * invSr);
    const float wB0 = v.wB, wSub0 = v.wSub, wN0 = v.wNoise;
    v.wB += uniCoeff * (levelB.value * nyquistFade (dtB) - v.wB);
    v.wSub += uniCoeff * (subLevel.value * nyquistFade (dtSub) - v.wSub);
    v.wNoise += uniCoeff * (noiseLevel.value * 0.6f - v.wNoise);

    // ---- filter envelope + cutoff (control rate, g interpolated per sample) ----
    for (int j = 0; j < len; ++j)
        v.filt.process();

    const float cutOct = cutoffOct.value + envAmount.value * 6.0f * v.envDepth * v.filt.value
                         + keyTrack.value * (v.pitch - 60.0f) * (1.0f / 12.0f) + cutMod + cutDrift;
    const float cutHz = jlimit (16.0f, sr * 0.45f, std::exp2 (cutOct));
    const float g1 = std::tan (aa::dsp::pi * cutHz * invSr);
    if (v.gPrev < 0.0f)
        v.gPrev = g1;
    const float gStart = v.gPrev;
    const float dgF = g1 - gStart;
    v.gPrev = g1;

    // ---- oscillators ----------------------------------------------------------
    float tmpL[controlInterval] = {}, tmpR[controlInterval] = {};
    const float shA0 = shapeA0, dShA = shapeA1 - shapeA0;
    const float shB0 = shapeB0, dShB = shapeB1 - shapeB0;

    for (int u = 0; u < maxUnison; ++u)
    {
        float ph = v.phaseA[(size_t) u];
        const float dt = dtA[u];
        if (w0[u] < 1.0e-6f && w1[u] < 1.0e-6f)
        {
            ph += dt * lenF;
            v.phaseA[(size_t) u] = ph - std::floor (ph);
            continue;
        }
        const float pl = uniPanL[(size_t) u], pr = uniPanR[(size_t) u];
        const float dw = w1[u] - w0[u];
        for (int j = 0; j < len; ++j)
        {
            const float t = (float) (j + 1) * invLen;
            const float s = osc::morph (shA0 + dShA * t, ph, dt) * (w0[u] + dw * t);
            ph += dt;
            if (ph >= 1.0f) ph -= 1.0f;
            tmpL[j] += s * pl;
            tmpR[j] += s * pr;
        }
        v.phaseA[(size_t) u] = ph;
    }

    {
        const bool doB = wB0 > 1.0e-6f || v.wB > 1.0e-6f;
        const bool doSub = wSub0 > 1.0e-6f || v.wSub > 1.0e-6f;
        const bool doNoise = wN0 > 1.0e-6f || v.wNoise > 1.0e-6f;
        const float dwB = v.wB - wB0, dwSub = v.wSub - wSub0, dwN = v.wNoise - wN0;
        float phB = v.phaseB, phS = v.phaseSub;
        for (int j = 0; j < len; ++j)
        {
            const float t = (float) (j + 1) * invLen;
            float m = 0.0f;
            if (doB)
                m += osc::morph (shB0 + dShB * t, phB, dtB) * (wB0 + dwB * t);
            if (doSub)
                m += osc::sin2pi (phS) * (wSub0 + dwSub * t);
            if (doNoise)
            {
                const float white = v.rng.nextBipolar();
                v.noiseLp += 0.5f * (white - v.noiseLp);
                m += (white + v.noiseLp) * 0.5f * (wN0 + dwN * t);
            }
            phB += dtB;
            if (phB >= 1.0f) phB -= 1.0f;
            phS += dtSub;
            if (phS >= 1.0f) phS -= 1.0f;
            tmpL[j] += m;
            tmpR[j] += m;
        }
        v.phaseB = phB;
        v.phaseSub = phS;
    }

    // ---- drive -> TPT state-variable filter -> amp --------------------------------
    const float k = filterK, pre = drivePre, post = drivePost, bpn = bpNorm;
    const auto type = settings.filterType;
    for (int j = 0; j < len; ++j)
    {
        const float t = (float) (j + 1) * invLen;
        const float gg = gStart + dgF * t;
        const float a1 = 1.0f / (1.0f + gg * (gg + k));
        const float a2 = gg * a1;
        const float a3 = gg * a2;

        const float xl = aa::dsp::softClip (tmpL[j] * pre) * post;
        const float xr = aa::dsp::softClip (tmpR[j] * pre) * post;

        float v3 = xl - v.ic2L;
        float v1 = a1 * v.ic1L + a2 * v3;
        float v2 = v.ic2L + a2 * v.ic1L + a3 * v3;
        v.ic1L = 2.0f * v1 - v.ic1L;
        v.ic2L = 2.0f * v2 - v.ic2L;
        const float yl = type == FilterType::lowpass ? v2 : (type == FilterType::bandpass ? v1 * bpn : xl - k * v1 - v2);

        v3 = xr - v.ic2R;
        v1 = a1 * v.ic1R + a2 * v3;
        v2 = v.ic2R + a2 * v.ic1R + a3 * v3;
        v.ic1R = 2.0f * v1 - v.ic1R;
        v.ic2R = 2.0f * v2 - v.ic2R;
        const float yr = type == FilterType::lowpass ? v2 : (type == FilterType::bandpass ? v1 * bpn : xr - k * v1 - v2);

        float gain = v.amp.process() * v.velGain;
        if (v.fadingOut)
        {
            v.fade = jmax (0.0f, v.fade - v.fadeStep);
            gain *= v.fade;
        }
        outL[j] += yl * gain;
        outR[j] += yr * gain;
    }

    if (! v.amp.isActive() || (v.fadingOut && v.fade <= 0.0f))
    {
        v.active = false;
        v.gate = v.sustained = v.fadingOut = false;
        v.amp.kill();
        v.filt.kill();
    }
}

//==============================================================================
void Engine::spawnGlints (int len)
{
    const float tw = twinkle.value;
    if (tw < 0.002f)
        return;

    int sounding = 0;
    for (auto& v : voices)
        if (v.active && ! v.fadingOut && v.amp.value > 0.05f)
            ++sounding;
    if (sounding == 0)
        return;

    const float rate = std::pow (tw, 1.2f) * 9.0f * std::sqrt ((float) jmin (sounding, 6));
    if (! rng.chance (rate * (float) len / sr))
        return;

    int pick = rng.nextInt (sounding);
    const Voice* source = nullptr;
    for (auto& v : voices)
        if (v.active && ! v.fadingOut && v.amp.value > 0.05f && pick-- == 0)
        {
            source = &v;
            break;
        }
    if (source == nullptr)
        return;

    float f = source->baseHz * glintHarmonics[rng.nextInt ((int) std::size (glintHarmonics))];
    if (f <= 0.0f)
        return;
    while (f > glintHighHz) f *= 0.5f;
    while (f < glintLowHz) f *= 2.0f;
    if (f > sr * 0.4f)
        return;

    Glint* slot = nullptr;
    float quietest = 1.0e9f;
    for (auto& gl : glints)
    {
        if (! gl.active)
        {
            slot = &gl;
            break;
        }
        const float level = gl.env * gl.level;
        if (level < quietest)
        {
            quietest = level;
            slot = &gl;
        }
    }
    if (slot == nullptr)
        return;

    const float strength = 0.3f + 0.7f * rng.next01();
    const float level = (0.4f + 0.6f * tw) * strength * glintBaseLevel * jmin (1.0f, source->amp.value * 1.2f) * source->velGain;
    const float decay = 0.4f + 1.6f * rng.next01() * rng.next01();
    const float attack = 0.002f + 0.018f * rng.next01();
    const float pan = rng.nextBipolar() * 0.9f;
    const float angle = (pan + 1.0f) * aa::dsp::pi * 0.25f;

    auto& gl = *slot;
    // A stolen glint that is still audible restarts from its current level to avoid a click.
    gl.env = gl.active ? gl.env : 0.0f;
    gl.active = true;
    gl.attacking = true;
    gl.phase = rng.next01();
    gl.dt = f / sr;
    gl.attackCoeff = 1.0f - std::exp (-1.0f / (attack * sr));
    gl.decayMul = std::exp (-6.9f / (decay * sr));
    gl.level = level;
    gl.panL = std::cos (angle);
    gl.panR = std::sin (angle);
    gl.tremPhase = rng.next01();
    gl.tremDt = (3.0f + 6.0f * rng.next01()) / sr;
    gl.tremDepth = 0.2f + 0.5f * rng.next01();

    glintEvents.push ({ pan, std::log (f / glintLowHz) / std::log (glintHighHz / glintLowHz), strength * tw, decay });
}

void Engine::renderGlints (float* outL, float* outR, int len)
{
    for (auto& gl : glints)
    {
        if (! gl.active)
            continue;

        gl.tremPhase += gl.tremDt * (float) len;
        if (gl.tremPhase >= 1.0f) gl.tremPhase -= 1.0f;
        const float trem = 1.0f - gl.tremDepth * 0.5f * (1.0f + osc::sin2pi (gl.tremPhase));
        const float amp = gl.level * trem;

        for (int j = 0; j < len; ++j)
        {
            if (gl.attacking)
            {
                gl.env += gl.attackCoeff * (1.02f - gl.env);
                if (gl.env >= 1.0f)
                {
                    gl.env = 1.0f;
                    gl.attacking = false;
                }
            }
            else
                gl.env *= gl.decayMul;

            const float s = osc::sin2pi (gl.phase) * gl.env * amp;
            gl.phase += gl.dt;
            if (gl.phase >= 1.0f) gl.phase -= 1.0f;
            outL[j] += s * gl.panL;
            outR[j] += s * gl.panR;
        }

        if (! gl.attacking && gl.env < 1.0e-4f)
            gl.active = false;
    }
}
} // namespace stardust
