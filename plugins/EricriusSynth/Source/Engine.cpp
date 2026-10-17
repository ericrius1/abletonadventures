#include "Engine.h"

using namespace juce;

namespace ers
{
namespace
{
    inline float frac (float x) noexcept { return x - std::floor (x); }
    inline float midiToHz (float note) noexcept { return 440.0f * std::exp2 ((note - 69.0f) * (1.0f / 12.0f)); }

    inline float polyBlep (float t, float dt) noexcept
    {
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0f;
        }
        if (t > 1.0f - dt)
        {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }
        return 0.0f;
    }

    float stackOffset (Stack s, int u) noexcept
    {
        switch (s)
        {
            case Stack::octave:      return (u & 1) ? 12.0f : 0.0f;
            case Stack::octaveDown:  return (u & 1) ? -12.0f : 0.0f;
            case Stack::octaveBoth:  return u % 3 == 1 ? 12.0f : (u % 3 == 2 ? -12.0f : 0.0f);
            case Stack::fifth:       return (u & 1) ? 7.0f : 0.0f;
            case Stack::octaveFifth: return u % 3 == 1 ? 12.0f : (u % 3 == 2 ? 19.0f : 0.0f);
            case Stack::chord:
            {
                static const float steps[] = { 0.0f, 4.0f, 7.0f, 12.0f };
                return steps[u & 3];
            }
            case Stack::off:
            case Stack::count: break;
        }
        return 0.0f;
    }

    /** How much a warp mode compresses the waveform in time (so we can pick a darker mip level). */
    float warpBandwidth (Warp w, float amt) noexcept
    {
        switch (w)
        {
            case Warp::sync:
            case Warp::windowSync: return 1.0f + 15.0f * amt;
            case Warp::bendPlus:
            case Warp::bendMinus:
            case Warp::bendBoth:   return 1.0f + 3.0f * amt;
            case Warp::pwm:        return 1.0f / (1.0f - 0.97f * amt);
            case Warp::asymPlus:
            case Warp::asymMinus:  return 0.5f / (0.5f - 0.48f * amt);
            case Warp::mirror:     return amt > 0.0f ? 2.0f : 1.0f;
            case Warp::fm:         return 1.0f + 6.0f * amt;
            case Warp::off:
            case Warp::flip:
            case Warp::quantize:
            case Warp::am:
            case Warp::rm:
            case Warp::count:      break;
        }
        return 1.0f;
    }

    /** The inner oscillator loop, specialised per warp mode. Writes n samples to out. */
    template <Warp W>
    void runOsc (const Wavetable::FramePair& fp, float& phase, float inc, float amt, const float* mod, float& last,
                 float* out, int n) noexcept
    {
        float p = phase;
        const float syncRatio = 1.0f + 15.0f * amt;
        const float bendK = 1.0f + 6.0f * amt;
        const float pwmW = 1.0f - 0.97f * amt;
        const float asymM = W == Warp::asymPlus ? 0.5f + 0.48f * amt : 0.5f - 0.48f * amt;
        const float flipStart = 1.0f - 0.5f * amt;
        const float qSteps = std::exp2 (1.0f + (1.0f - amt) * 10.0f);

        for (int i = 0; i < n; ++i)
        {
            float q = p;
            float gain = 1.0f;

            if constexpr (W == Warp::sync)
                q = frac (p * syncRatio);
            else if constexpr (W == Warp::windowSync)
            {
                q = frac (p * syncRatio);
                gain = std::sin (aa::dsp::pi * p);
            }
            else if constexpr (W == Warp::bendPlus)
                q = 1.0f - std::pow (1.0f - p, bendK);
            else if constexpr (W == Warp::bendMinus)
                q = std::pow (p, bendK);
            else if constexpr (W == Warp::bendBoth)
                q = p < 0.5f ? 0.5f * std::pow (2.0f * p, bendK) : 1.0f - 0.5f * std::pow (2.0f - 2.0f * p, bendK);
            else if constexpr (W == Warp::pwm)
                q = p < pwmW ? p / pwmW : 0.0f;
            else if constexpr (W == Warp::asymPlus || W == Warp::asymMinus)
                q = p < asymM ? 0.5f * p / asymM : 0.5f + 0.5f * (p - asymM) / (1.0f - asymM);
            else if constexpr (W == Warp::quantize)
                q = std::floor (p * qSteps) / qSteps;
            else if constexpr (W == Warp::fm)
                q = frac (p + amt * 2.0f * (mod != nullptr ? mod[i] : last));

            q = jlimit (0.0f, 0.99999994f, q);
            float s = Wavetable::read (fp, q) * gain;

            if constexpr (W == Warp::mirror)
            {
                const float m = p < 0.5f ? 2.0f * p : 2.0f - 2.0f * p;
                s += amt * (Wavetable::read (fp, jlimit (0.0f, 0.99999994f, m)) - s);
            }
            else if constexpr (W == Warp::flip)
            {
                if (p >= flipStart)
                    s = -s;
            }
            else if constexpr (W == Warp::am)
            {
                const float m = mod != nullptr ? mod[i] : last;
                s *= 1.0f - amt * 0.5f * (1.0f - m);
            }
            else if constexpr (W == Warp::rm)
            {
                const float m = mod != nullptr ? mod[i] : last;
                s += amt * (s * m - s);
            }

            out[i] = s;
            last = s;
            p += inc;
            if (p >= 1.0f)
                p -= 1.0f;
        }
        phase = p;
    }

    using OscFn = void (*) (const Wavetable::FramePair&, float&, float, float, const float*, float&, float*, int);

    OscFn oscFunction (Warp w) noexcept
    {
        switch (w)
        {
            case Warp::sync:       return &runOsc<Warp::sync>;
            case Warp::windowSync: return &runOsc<Warp::windowSync>;
            case Warp::bendPlus:   return &runOsc<Warp::bendPlus>;
            case Warp::bendMinus:  return &runOsc<Warp::bendMinus>;
            case Warp::bendBoth:   return &runOsc<Warp::bendBoth>;
            case Warp::pwm:        return &runOsc<Warp::pwm>;
            case Warp::asymPlus:   return &runOsc<Warp::asymPlus>;
            case Warp::asymMinus:  return &runOsc<Warp::asymMinus>;
            case Warp::flip:       return &runOsc<Warp::flip>;
            case Warp::mirror:     return &runOsc<Warp::mirror>;
            case Warp::quantize:   return &runOsc<Warp::quantize>;
            case Warp::fm:         return &runOsc<Warp::fm>;
            case Warp::am:         return &runOsc<Warp::am>;
            case Warp::rm:         return &runOsc<Warp::rm>;
            case Warp::off:
            case Warp::count:      break;
        }
        return &runOsc<Warp::off>;
    }

    bool warpUsesSource (Warp w) noexcept { return w == Warp::fm || w == Warp::am || w == Warp::rm; }
} // namespace

//==============================================================================
void Envelope::advance (float seconds, const float* p) noexcept
{
    float remaining = seconds;
    for (int guard = 0; remaining > 0.0f && guard < 8; ++guard)
    {
        switch (stage)
        {
            case Stage::idle:
                level = 0.0f;
                return;

            case Stage::attack:
            {
                const float a = p[0];
                if (a <= 0.0003f)
                {
                    level = 1.0f;
                    stage = Stage::hold;
                    t = 0.0f;
                    break;
                }
                const float dt = remaining / a;
                if (t + dt >= 1.0f)
                {
                    remaining -= (1.0f - t) * a;
                    level = 1.0f;
                    stage = Stage::hold;
                    t = 0.0f;
                }
                else
                {
                    t += dt;
                    remaining = 0.0f;
                    level = from + (1.0f - from) * LfoShape::curveSegment (t, p[5]);
                }
                break;
            }

            case Stage::hold:
            {
                const float h = p[1];
                const float dt = h <= 0.0f ? 1.0f : remaining / h;
                level = 1.0f;
                if (t + dt >= 1.0f)
                {
                    remaining -= h <= 0.0f ? 0.0f : (1.0f - t) * h;
                    stage = Stage::decay;
                    t = 0.0f;
                }
                else
                {
                    t += dt;
                    remaining = 0.0f;
                }
                break;
            }

            case Stage::decay:
            {
                const float d = jmax (0.001f, p[2]);
                const float dt = remaining / d;
                if (t + dt >= 1.0f)
                {
                    remaining -= (1.0f - t) * d;
                    stage = Stage::sustain;
                    level = p[3];
                }
                else
                {
                    t += dt;
                    remaining = 0.0f;
                    level = 1.0f + (p[3] - 1.0f) * LfoShape::curveSegment (t, p[6]);
                }
                break;
            }

            case Stage::sustain:
                level = p[3];
                remaining = 0.0f;
                break;

            case Stage::release:
            {
                const float r = jmax (0.001f, p[4]);
                const float dt = remaining / r;
                if (t + dt >= 1.0f)
                {
                    stage = Stage::idle;
                    level = 0.0f;
                    return;
                }
                t += dt;
                remaining = 0.0f;
                level = from * (1.0f - LfoShape::curveSegment (t, p[7]));
                break;
            }
        }
    }
}

//==============================================================================
Engine::Engine()
{
    ui.numDests = (int) modDests().size();
    ui.destMod = std::make_unique<std::atomic<float>[]> ((size_t) ui.numDests);
    for (int i = 0; i < ui.numDests; ++i)
        ui.destMod[(size_t) i].store (0.0f);
    fxMod.assign (fx::paramSpecs().size(), 0.0f);
}

void Engine::prepare (double sampleRate)
{
    sr = (float) sampleRate;
    for (auto& v : voices)
        for (auto& f : v.filter)
            f.prepare (sr);
    reset();
}

void Engine::reset()
{
    for (auto& v : voices)
    {
        v.active = false;
        v.gate = false;
        v.stealing = false;
        v.pendingNote = -1;
        v.ampLevel = 0.0f;
        for (auto& e : v.env)
            e.kill();
        for (auto& f : v.filter)
            f.reset();
    }
    heldCount = 0;
    sustainPedal = false;
    pitchBend = 0.0f;
    for (auto& l : globalLfo)
        l = {};
}

void Engine::beginBlock (const EngineBlock& block) noexcept
{
    base = block.values;
    baseNorm = block.normalised;
    numRoutes = jmin (block.numRoutes, maxModSlots);
    routesTouchVoices = false;
    anyFxMod = false;
    const auto& dests = modDests();
    for (int i = 0; i < numRoutes; ++i)
    {
        routes[(size_t) i] = block.routes[i];
        const auto& d = dests[(size_t) routes[(size_t) i].dest];
        if (d.vpIndex >= 0)
            routesTouchVoices = true;
        else
            anyFxMod = true;
    }
    bpm = jmax (20.0, block.bpm);
    ppq = block.ppq;
    playing = block.playing;
}

//==============================================================================
void Engine::handleMidi (const MidiMessage& m) noexcept
{
    if (base == nullptr)
        return;

    if (m.isNoteOn())
        noteOn (m.getChannel(), m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())
        noteOff (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isPitchWheel())
        pitchBend = jlimit (-1.0f, 1.0f, (float) (m.getPitchWheelValue() - 8192) / 8191.0f);
    else if (m.isController())
    {
        const int cc = m.getControllerNumber();
        if (cc == 1)
            modWheel = (float) m.getControllerValue() / 127.0f;
        else if (cc == 64)
        {
            const bool down = m.getControllerValue() >= 64;
            if (sustainPedal && ! down)
            {
                for (auto& v : voices)
                    if (v.active && v.sustained)
                        releaseVoice (v, 0.5f);
            }
            sustainPedal = down;
        }
        else if (cc == 123 || cc == 120)
            allNotesOff (cc == 120);
    }
    else if (m.isChannelPressure())
        channelPressure = (float) m.getChannelPressureValue() / 127.0f;
    else if (m.isAftertouch())
    {
        for (auto& v : voices)
            if (v.active && v.note == m.getNoteNumber())
                v.polyPressure = (float) m.getAfterTouchValue() / 127.0f;
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        allNotesOff (m.isAllSoundOff());
}

void Engine::allNotesOff (bool immediate) noexcept
{
    heldCount = 0;
    for (auto& v : voices)
    {
        v.pendingNote = -1;
        if (! v.active)
            continue;
        if (immediate)
        {
            v.stealing = true;   // fades out, then stops (no pending note)
            v.gate = false;
        }
        else
            releaseVoice (v, 0.5f);
    }
}

int Engine::countGated() const noexcept
{
    int n = 0;
    for (const auto& v : voices)
        if (v.active && ! v.stealing)
            ++n;
    return n;
}

Engine::Voice* Engine::newestVoice() noexcept
{
    Voice* best = nullptr;
    for (auto& v : voices)
        if (v.active && ! v.stealing && (best == nullptr || v.age > best->age))
            best = &v;
    return best;
}

Engine::Voice* Engine::findVoiceToSteal() noexcept
{
    Voice* oldestReleased = nullptr;
    Voice* oldest = nullptr;
    for (auto& v : voices)
    {
        if (! v.active || v.stealing)
            continue;
        if (! v.gate && (oldestReleased == nullptr || v.age < oldestReleased->age))
            oldestReleased = &v;
        if (oldest == nullptr || v.age < oldest->age)
            oldest = &v;
    }
    return oldestReleased != nullptr ? oldestReleased : oldest;
}

void Engine::noteOn (int channel, int note, float velocity) noexcept
{
    // keep a stack of held keys for mono / legato note priority (last note wins)
    for (int i = 0; i < heldCount; ++i)
        if (heldStack[(size_t) i] == note)
        {
            std::copy (heldStack.begin() + i + 1, heldStack.begin() + heldCount, heldStack.begin() + i);
            --heldCount;
            break;
        }
    if (heldCount < (int) heldStack.size())
        heldStack[(size_t) heldCount++] = note;

    const auto mode = (VoiceMode) jlimit (0, 2, (int) base[vp::global (vp::voiceMode)]);
    if (mode != VoiceMode::poly)
    {
        // Mono modes always use voice 0; make sure nothing else keeps sounding after a mode switch.
        for (int i = 1; i < maxVoices; ++i)
            if (voices[(size_t) i].active && voices[(size_t) i].gate)
                releaseVoice (voices[(size_t) i], 0.5f);

        auto& v = voices[0];
        const bool overlapping = v.active && v.gate && heldCount > 1;
        if (mode == VoiceMode::legato && overlapping && ! v.stealing)
        {
            const float glideSec = base[vp::global (vp::glide)] * 0.001f;
            v.glideTarget = (float) note;
            if (glideSec > 0.0f)
            {
                v.glideFrom = v.pitch;
                v.glideT = 0.0f;
            }
            else
            {
                v.pitch = (float) note;
                v.glideT = 1.0f;
            }
            v.note = note;
            v.velocity = velocity;
            lastNotePitch = (float) note;
            return;
        }
        startVoice (v, channel, note, velocity, ! v.active || v.stealing);
        return;
    }

    const int maxPoly = jlimit (1, maxVoices, (int) base[vp::global (vp::polyphony)]);
    Voice* target = nullptr;
    if (countGated() < maxPoly)
        for (auto& v : voices)
            if (! v.active)
            {
                target = &v;
                break;
            }

    if (target == nullptr)
    {
        target = findVoiceToSteal();
        if (target == nullptr)
            return;
        target->stealing = true;
        target->pendingNote = note;
        target->pendingVelocity = velocity;
        target->pendingChannel = channel;
        return;
    }

    startVoice (*target, channel, note, velocity, true);
}

void Engine::noteOff (int note, float velocity) noexcept
{
    for (int i = 0; i < heldCount; ++i)
        if (heldStack[(size_t) i] == note)
        {
            std::copy (heldStack.begin() + i + 1, heldStack.begin() + heldCount, heldStack.begin() + i);
            --heldCount;
            break;
        }

    const auto mode = (VoiceMode) jlimit (0, 2, (int) base[vp::global (vp::voiceMode)]);
    if (mode != VoiceMode::poly)
    {
        auto& v = voices[0];
        if (! v.active || v.note != note)
            return;

        if (heldCount > 0)
        {
            // fall back to the most recent key still held
            const int previous = heldStack[(size_t) heldCount - 1];
            if (mode == VoiceMode::legato)
            {
                const float glideSec = base[vp::global (vp::glide)] * 0.001f;
                v.glideTarget = (float) previous;
                if (glideSec > 0.0f)
                {
                    v.glideFrom = v.pitch;
                    v.glideT = 0.0f;
                }
                else
                {
                    v.pitch = (float) previous;
                    v.glideT = 1.0f;
                }
                v.note = previous;
                lastNotePitch = (float) previous;
            }
            else
                startVoice (v, v.channel, previous, v.velocity, false);
            return;
        }

        if (sustainPedal)
            v.sustained = true;
        else
            releaseVoice (v, velocity);
        return;
    }

    for (auto& v : voices)
    {
        if (v.stealing && v.pendingNote == note)
            continue;   // handled when the pending note starts
        if (v.active && v.gate && v.note == note && ! v.stealing)
        {
            if (sustainPedal)
                v.sustained = true;
            else
                releaseVoice (v, velocity);
        }
    }
}

void Engine::releaseVoice (Voice& v, float velocity) noexcept
{
    v.gate = false;
    v.sustained = false;
    v.releaseVelocity = velocity;
    for (auto& e : v.env)
        e.noteOff();
}

void Engine::startVoice (Voice& v, int channel, int note, float velocity, bool retrigger) noexcept
{
    const bool wasActive = v.active && ! v.stealing;
    v.note = note;
    v.channel = channel;
    v.velocity = velocity;
    v.releaseVelocity = 0.0f;
    v.gate = true;
    v.sustained = false;
    v.active = true;
    v.stealing = false;
    v.pendingNote = -1;
    v.fade = 1.0f;
    v.age = ++ageCounter;
    v.randomValue = rng.nextBipolar();
    alternateState = -alternateState;
    v.alternate = alternateState;
    v.polyPressure = 0.0f;

    // glide
    const float glideSec = base[vp::global (vp::glide)] * 0.001f;
    const bool legatoOnly = base[vp::global (vp::legatoGlide)] > 0.5f;
    v.glideTarget = (float) note;
    const float from = wasActive ? v.pitch : lastNotePitch;
    if (glideSec > 0.0f && from >= 0.0f && (! legatoOnly || heldCount > 1))
    {
        v.glideFrom = from;
        v.glideT = 0.0f;
        v.pitch = from;
    }
    else
    {
        v.pitch = (float) note;
        v.glideT = 1.0f;
    }
    lastNotePitch = (float) note;

    if (retrigger)
    {
        for (int o = 0; o < numOscs; ++o)
        {
            const float phase0 = base[vp::osc (o, OscField::phase)] / 360.0f;
            const float rnd = base[vp::osc (o, OscField::random)] * 0.01f;
            auto& ov = v.osc[(size_t) o];
            for (int u = 0; u < maxUnison; ++u)
            {
                ov.phase[(size_t) u] = frac (phase0 + rnd * rng.next01());
                ov.last[(size_t) u] = 0.0f;
            }
        }
        for (auto& m : v.mono)
            std::fill (m.begin(), m.end(), 0.0f);
        v.subPhase = 0.0f;
        v.noiseDone = false;
        v.noisePos = 0.0;
        if (tables.noise != nullptr && tables.noise->loops && ! tables.noise->left.empty())
            v.noisePos = rng.next01() * (double) tables.noise->left.size();
        for (auto& f : v.filter)
            f.reset();
        for (auto& e : v.env)
            e.kill();
        v.ampLevel = 0.0f;
    }

    for (auto& e : v.env)
        e.noteOn();

    for (int l = 0; l < numLfos; ++l)
    {
        auto& s = v.lfo[(size_t) l];
        const auto mode = (LfoMode) jlimit (0, 2, (int) base[vp::lfo (l, LfoField::mode)]);
        if (mode != LfoMode::free)
            s.phase = mode == LfoMode::trigger ? base[vp::lfo (l, LfoField::phase)] / 360.0 : 0.0;
        s.elapsed = 0.0f;
        s.finished = false;
        s.primed = false;
    }
}

//==============================================================================
float Engine::lfoRateHz (int index, const float* vals) const noexcept
{
    if (vals[vp::lfo (index, LfoField::sync)] > 0.5f)
        return (float) (bpm / 60.0 / lfoDivisionBeats ((int) vals[vp::lfo (index, LfoField::division)]));
    return jmax (0.001f, vals[vp::lfo (index, LfoField::rate)]);
}

void Engine::advanceLfo (LfoState& s, int index, const float* vals, float seconds, bool global) noexcept
{
    const auto mode = (LfoMode) jlimit (0, 2, (int) vals[vp::lfo (index, LfoField::mode)]);
    const float rate = lfoRateHz (index, vals);
    const float offset = vals[vp::lfo (index, LfoField::phase)] / 360.0f;
    float readPhase;

    if (global)
    {
        if (vals[vp::lfo (index, LfoField::sync)] > 0.5f && playing)
        {
            // lock to the song position so free-running synced LFOs line up with the bar
            const double beats = lfoDivisionBeats ((int) vals[vp::lfo (index, LfoField::division)]);
            s.phase = std::fmod (ppq / beats, 1.0);
        }
        else
            s.phase = std::fmod (s.phase + rate * seconds, 1.0);
        readPhase = frac ((float) s.phase + offset);
    }
    else if (mode == LfoMode::free)
    {
        readPhase = frac ((float) globalLfo[(size_t) index].phase + offset);
        s.phase = readPhase;
    }
    else if (mode == LfoMode::envelope)
    {
        if (! s.finished)
        {
            s.phase += rate * seconds;
            if (s.phase >= 1.0)
            {
                s.phase = 1.0;
                s.finished = true;
            }
        }
        readPhase = (float) s.phase;
    }
    else
    {
        s.phase = std::fmod (s.phase + rate * seconds, 1.0);
        readPhase = (float) s.phase;
    }

    float raw;
    if (const float* table = tables.lfo[(size_t) index])
    {
        const float x = jlimit (0.0f, 1.0f, readPhase) * (float) lfoTableSize;
        const int i = jmin (lfoTableSize - 1, (int) x);
        raw = table[i] + (x - (float) i) * (table[i + 1] - table[i]);
    }
    else
        raw = 0.5f + 0.5f * std::sin (aa::dsp::twoPi * readPhase);

    float amount = 1.0f;
    if (! global)
    {
        s.elapsed += seconds;
        const float delay = vals[vp::lfo (index, LfoField::delay)] * 0.001f;
        const float rise = vals[vp::lfo (index, LfoField::rise)] * 0.001f;
        if (s.elapsed < delay)
            amount = 0.0f;
        else if (rise > 0.0f)
            amount = jmin (1.0f, (s.elapsed - delay) / rise);
    }

    s.raw = raw;
    const float targetValue = raw * amount;
    const float smooth = vals[vp::lfo (index, LfoField::smooth)] * 0.01f;
    if (! s.primed || smooth <= 0.0f)
    {
        s.value = targetValue;
        s.primed = true;
    }
    else
    {
        const float tau = smooth * smooth * 0.5f / jmax (0.05f, rate);
        s.value += (1.0f - std::exp (-seconds / jmax (1.0e-4f, tau))) * (targetValue - s.value);
    }
}

void Engine::updateGlobalLfos (int numSamples) noexcept
{
    const float seconds = (float) numSamples / sr;
    for (int l = 0; l < numLfos; ++l)
        advanceLfo (globalLfo[(size_t) l], l, base, seconds, true);
    ppq += (double) numSamples * bpm / (60.0 * sr);
}

void Engine::computeVoiceSources (Voice& v) noexcept
{
    auto& s = v.src;
    for (int e = 0; e < numEnvs; ++e)
        s[(size_t) ModSource::env1 + (size_t) e] = v.env[(size_t) e].level;
    for (int l = 0; l < numLfos; ++l)
        s[(size_t) ModSource::lfo1 + (size_t) l] = v.lfo[(size_t) l].value;
    for (int m = 0; m < numMacros; ++m)
        s[(size_t) ModSource::macro1 + (size_t) m] = base[vp::macro (m)] * 0.01f;
    s[(size_t) ModSource::velocity] = v.velocity;
    s[(size_t) ModSource::note] = jlimit (-1.0f, 1.0f, ((float) v.note - 60.0f) / 48.0f);
    s[(size_t) ModSource::modWheel] = modWheel;
    s[(size_t) ModSource::aftertouch] = jmax (channelPressure, v.polyPressure);
    s[(size_t) ModSource::pitchBend] = pitchBend;
    s[(size_t) ModSource::random] = v.randomValue;
    s[(size_t) ModSource::alternate] = v.alternate;
    s[(size_t) ModSource::releaseVelocity] = v.releaseVelocity;
}

float Engine::modSourceValue (const std::array<float, numModSources>& src, const ModRoute& r) const noexcept
{
    float x = src[(size_t) r.source];
    if (r.bipolar)
        x = isBipolarSource (r.source) ? 0.5f * (x + 1.0f) : 2.0f * x - 1.0f;
    float amount = r.amount;
    if (r.aux != ModSource::none)
    {
        float y = src[(size_t) r.aux];
        if (isBipolarSource (r.aux))
            y = 0.5f * (y + 1.0f);
        amount *= y;
    }
    return x * amount;
}

void Engine::applyModulation (Voice& v) noexcept
{
    std::copy (base, base + vp::count, v.v.begin());
    if (! routesTouchVoices || ranges.size() != (size_t) vp::count)
        return;

    std::array<float, vp::count> acc;
    std::array<int, maxModSlots> touched;
    int numTouched = 0;
    const auto& dests = modDests();

    for (int i = 0; i < numRoutes; ++i)
    {
        const auto& r = routes[(size_t) i];
        const int vpi = dests[(size_t) r.dest].vpIndex;
        if (vpi < 0)
            continue;
        const float m = modSourceValue (v.src, r);
        bool seen = false;
        for (int k = 0; k < numTouched; ++k)
            if (touched[(size_t) k] == vpi)
                seen = true;
        if (! seen)
        {
            touched[(size_t) numTouched++] = vpi;
            acc[(size_t) vpi] = 0.0f;
        }
        acc[(size_t) vpi] += m;
    }

    for (int k = 0; k < numTouched; ++k)
    {
        const int vpi = touched[(size_t) k];
        const float norm = jlimit (0.0f, 1.0f, baseNorm[vpi] + acc[(size_t) vpi]);
        v.v[(size_t) vpi] = ranges[(size_t) vpi].convertFrom0to1 (norm);
    }
}

//==============================================================================
void Engine::addToBus (int route, const float* l, const float* r, int n) noexcept
{
    const bool parallel = base[vp::filterRouting] > 0.5f;
    auto add = [&] (int b, float g)
    {
        float* bl = bus[(size_t) b * 2].data();
        float* br = bus[(size_t) b * 2 + 1].data();
        for (int i = 0; i < n; ++i)
        {
            bl[i] += l[i] * g;
            br[i] += r[i] * g;
        }
    };

    switch ((Route) jlimit (0, 3, route))
    {
        case Route::filter1: add (0, 1.0f); break;
        case Route::filter2: add (1, 1.0f); break;
        case Route::both:
            if (parallel)
            {
                add (0, 0.7071f);
                add (1, 0.7071f);
            }
            else
                add (0, 1.0f);
            break;
        case Route::direct: add (2, 1.0f); break;
    }
}

void Engine::renderOsc (Voice& v, int o, float basePitch, int n) noexcept
{
    const float* vals = v.v.data();
    auto& mono = v.mono[(size_t) o];
    const Wavetable* wt = tables.osc[(size_t) o];
    if (vals[vp::osc (o, OscField::on)] < 0.5f || wt == nullptr)
    {
        std::fill (mono.begin(), mono.begin() + n, 0.0f);
        return;
    }

    const float position = jlimit (0.0f, 1.0f, vals[vp::osc (o, OscField::position)] * 0.01f);
    const auto warp = (Warp) jlimit (0, (int) Warp::count - 1, (int) vals[vp::osc (o, OscField::warp)]);
    const float amt = jlimit (0.0f, 1.0f, vals[vp::osc (o, OscField::warpAmount)] * 0.01f);
    const int srcIndex = jlimit (0, numOscs + 1, (int) vals[vp::osc (o, OscField::warpSource)]);
    const float pitch = basePitch + 12.0f * vals[vp::osc (o, OscField::octave)] + vals[vp::osc (o, OscField::semi)]
                        + vals[vp::osc (o, OscField::fine)] * 0.01f;
    const int numUnison = jlimit (1, maxUnison, (int) vals[vp::osc (o, OscField::unison)]);
    const float detune = vals[vp::osc (o, OscField::detune)] * 0.01f;
    const float blend = vals[vp::osc (o, OscField::blend)] * 0.01f;
    const float width = vals[vp::osc (o, OscField::width)] * 0.01f;
    const float level = vals[vp::osc (o, OscField::level)] * 0.01f;
    const float pan = vals[vp::osc (o, OscField::pan)] * 0.01f;
    const auto stack = (Stack) jlimit (0, (int) Stack::count - 1, (int) vals[vp::osc (o, OscField::stack)]);

    const float* modSrc = nullptr;
    if (warpUsesSource (warp) && srcIndex != o)
        modSrc = v.mono[(size_t) srcIndex].data();

    // centre vs side voices (blend), normalised for constant loudness
    int centreCount = numUnison == 1 ? 1 : (numUnison % 2 == 1 ? 1 : 2);
    int sideCount = numUnison - centreCount;
    float cg = std::cos (blend * MathConstants<float>::halfPi);
    float sg = std::sin (blend * MathConstants<float>::halfPi);
    if (sideCount == 0)
        cg = 1.0f;
    const float norm = 1.0f / std::sqrt (jmax (1.0e-4f, (float) centreCount * cg * cg + (float) sideCount * sg * sg));
    const float spreadSemis = std::pow (detune, 1.5f) * 0.75f;

    const auto fn = oscFunction (warp);
    const float bw = warpBandwidth (warp, amt);
    auto& ov = v.osc[(size_t) o];

    std::fill (tmpL.begin(), tmpL.begin() + n, 0.0f);
    std::fill (tmpR.begin(), tmpR.begin() + n, 0.0f);
    std::fill (mono.begin(), mono.begin() + n, 0.0f);
    std::array<float, controlBlock> s;

    for (int u = 0; u < numUnison; ++u)
    {
        const float off = numUnison == 1 ? 0.0f : 2.0f * (float) u / (float) (numUnison - 1) - 1.0f;
        const bool centre = numUnison == 1 || (numUnison % 2 == 1 ? u == numUnison / 2 : (u == numUnison / 2 || u == numUnison / 2 - 1));
        const float g = (centre ? cg : sg) * norm * level;

        const float semis = pitch + off * spreadSemis + stackOffset (stack, u);
        const float inc = jmin (0.49f, midiToHz (semis) / sr);

        if (g < 1.0e-5f)
        {
            ov.phase[(size_t) u] = frac (ov.phase[(size_t) u] + inc * (float) n);
            continue;
        }

        const int level0 = Wavetable::levelForIncrement (inc * bw);
        const auto fp = wt->framePair (level0, position);
        fn (fp, ov.phase[(size_t) u], inc, amt, modSrc, ov.last[(size_t) u], s.data(), n);

        const float p = jlimit (-1.0f, 1.0f, off * width + pan);
        const float gl = g * std::sqrt (1.0f - p);
        const float gr = g * std::sqrt (1.0f + p);
        for (int i = 0; i < n; ++i)
        {
            tmpL[(size_t) i] += s[(size_t) i] * gl;
            tmpR[(size_t) i] += s[(size_t) i] * gr;
            mono[(size_t) i] += s[(size_t) i] * g;
        }
    }

    addToBus ((int) vals[vp::osc (o, OscField::route)], tmpL.data(), tmpR.data(), n);
}

void Engine::renderSub (Voice& v, float basePitch, int n) noexcept
{
    const float* vals = v.v.data();
    auto& mono = v.mono[(size_t) numOscs];
    if (vals[vp::sub (vp::subOn)] < 0.5f)
    {
        std::fill (mono.begin(), mono.begin() + n, 0.0f);
        return;
    }

    const auto shape = (SubShape) jlimit (0, (int) SubShape::count - 1, (int) vals[vp::sub (vp::subShape)]);
    const float inc = jmin (0.49f, midiToHz (basePitch + 12.0f * vals[vp::sub (vp::subOctave)]) / sr);
    const float level = vals[vp::sub (vp::subLevel)] * 0.01f;
    const float pan = vals[vp::sub (vp::subPan)] * 0.01f;
    const float gl = level * std::sqrt (1.0f - pan), gr = level * std::sqrt (1.0f + pan);

    float p = v.subPhase;
    for (int i = 0; i < n; ++i)
    {
        float s = 0.0f;
        switch (shape)
        {
            case SubShape::sine:     s = std::sin (aa::dsp::twoPi * p); break;
            case SubShape::rounded:
            {
                const float x = std::sin (aa::dsp::twoPi * p);
                s = x * (1.5f - 0.5f * x * x);
                break;
            }
            case SubShape::triangle: s = 1.0f - 4.0f * std::abs (p - 0.5f); break;
            case SubShape::saw:      s = 2.0f * p - 1.0f - polyBlep (p, inc); break;
            case SubShape::square:
            case SubShape::pulse:
            {
                const float duty = shape == SubShape::square ? 0.5f : 0.25f;
                s = (p < duty ? 1.0f : -1.0f) + polyBlep (p, inc) - polyBlep (frac (p - duty + 1.0f), inc);
                if (shape == SubShape::pulse)
                    s += 0.5f;   // centre the 25 % pulse
                break;
            }
            case SubShape::count: break;
        }
        mono[(size_t) i] = s * level;
        tmpL[(size_t) i] = s * gl;
        tmpR[(size_t) i] = s * gr;
        p += inc;
        if (p >= 1.0f)
            p -= 1.0f;
    }
    v.subPhase = p;
    addToBus ((int) vals[vp::sub (vp::subRoute)], tmpL.data(), tmpR.data(), n);
}

void Engine::renderNoise (Voice& v, int n) noexcept
{
    const float* vals = v.v.data();
    auto& mono = v.mono[(size_t) numOscs + 1];
    const NoiseSource* src = tables.noise;
    if (vals[vp::noise (vp::noiseOn)] < 0.5f || src == nullptr || src->left.size() < 4 || v.noiseDone)
    {
        std::fill (mono.begin(), mono.begin() + n, 0.0f);
        return;
    }

    double ratio = std::exp2 ((vals[vp::noise (vp::noisePitch)] - 50.0f) / 25.0f) * src->sampleRate / sr;
    if (vals[vp::noise (vp::noiseKeyTrack)] > 0.5f)
        ratio *= std::exp2 ((v.pitch - 60.0f) / 12.0f);
    const float level = vals[vp::noise (vp::noiseLevel)] * 0.01f;
    const float pan = vals[vp::noise (vp::noisePan)] * 0.01f;
    const float gl = level * std::sqrt (1.0f - pan), gr = level * std::sqrt (1.0f + pan);

    const auto len = (double) src->left.size();
    const float* sl = src->left.data();
    const float* sr2 = src->right.size() == src->left.size() ? src->right.data() : sl;
    double pos = v.noisePos;

    for (int i = 0; i < n; ++i)
    {
        if (pos >= len - 1.0)
        {
            if (src->loops)
                pos = std::fmod (pos, len - 1.0);
            else
            {
                v.noiseDone = true;
                for (; i < n; ++i)
                    tmpL[(size_t) i] = tmpR[(size_t) i] = mono[(size_t) i] = 0.0f;
                break;
            }
        }
        const int k = (int) pos;
        const float f = (float) (pos - (double) k);
        const float l = sl[k] + f * (sl[k + 1] - sl[k]);
        const float r = sr2[k] + f * (sr2[k + 1] - sr2[k]);
        tmpL[(size_t) i] = l * gl;
        tmpR[(size_t) i] = r * gr;
        mono[(size_t) i] = 0.5f * (l + r) * level;
        pos += ratio;
    }
    v.noisePos = pos;
    addToBus ((int) vals[vp::noise (vp::noiseRoute)], tmpL.data(), tmpR.data(), n);
}

void Engine::renderVoice (Voice& v, float* outL, float* outR, int n) noexcept
{
    computeVoiceSources (v);
    applyModulation (v);
    const float* vals = v.v.data();
    const float seconds = (float) n / sr;

    // envelopes and LFOs advance to the end of this block (amp is ramped across it below)
    for (int e = 0; e < numEnvs; ++e)
    {
        const float p[8] = { vals[vp::env (e, EnvField::attack)] * 0.001f, vals[vp::env (e, EnvField::hold)] * 0.001f,
                             vals[vp::env (e, EnvField::decay)] * 0.001f, vals[vp::env (e, EnvField::sustain)] * 0.01f,
                             vals[vp::env (e, EnvField::release)] * 0.001f, vals[vp::env (e, EnvField::attackCurve)] * 0.01f,
                             vals[vp::env (e, EnvField::decayCurve)] * 0.01f, vals[vp::env (e, EnvField::releaseCurve)] * 0.01f };
        v.env[(size_t) e].advance (seconds, p);
    }
    for (int l = 0; l < numLfos; ++l)
        advanceLfo (v.lfo[(size_t) l], l, vals, seconds, false);

    // glide (constant time)
    if (v.glideT < 1.0f)
    {
        const float glideSec = jmax (0.001f, base[vp::global (vp::glide)] * 0.001f);
        v.glideT = jmin (1.0f, v.glideT + seconds / glideSec);
        v.pitch = v.glideFrom + (v.glideTarget - v.glideFrom) * v.glideT;
    }
    else
        v.pitch = v.glideTarget;

    const float basePitch = v.pitch + pitchBend * base[vp::global (vp::bendRange)]
                            + base[vp::global (vp::masterTune)] * 0.01f;

    for (auto& b : bus)
        std::fill (b.begin(), b.begin() + n, 0.0f);

    renderSub (v, basePitch, n);
    renderNoise (v, n);

    // Oscillators in dependency order, so FM / AM / RM sources are computed first where possible.
    bool done[numOscs] = { false, false, false };
    for (int pass = 0; pass < numOscs; ++pass)
    {
        int pick = -1;
        for (int o = 0; o < numOscs && pick < 0; ++o)
        {
            if (done[o])
                continue;
            const auto w = (Warp) (int) vals[vp::osc (o, OscField::warp)];
            const int s = (int) vals[vp::osc (o, OscField::warpSource)];
            if (! warpUsesSource (w) || s == o || s >= numOscs || done[s] || vals[vp::osc (s, OscField::on)] < 0.5f)
                pick = o;
        }
        if (pick < 0)
            for (int o = 0; o < numOscs && pick < 0; ++o)
                if (! done[o])
                    pick = o;   // dependency cycle: uses the source's previous block
        done[pick] = true;
        renderOsc (v, pick, basePitch, n);
    }

    // filters
    const bool parallel = vals[vp::filterRouting] > 0.5f;
    for (int f = 0; f < numFilters; ++f)
    {
        const bool on = vals[vp::filter (f, FilterField::on)] > 0.5f;
        float* l = parallel || f == 0 ? bus[(size_t) f * 2].data() : bus[2].data();
        float* r = parallel || f == 0 ? bus[(size_t) f * 2 + 1].data() : bus[3].data();

        if (! parallel && f == 1)
        {
            // serial: filter 1's output joins whatever was routed to filter 2
            for (int i = 0; i < n; ++i)
            {
                l[i] += bus[0][(size_t) i];
                r[i] += bus[1][(size_t) i];
            }
        }

        if (on)
        {
            const float key = vals[vp::filter (f, FilterField::keyTrack)] * 0.01f;
            const float cutoff = jlimit (20.0f, 20000.0f, vals[vp::filter (f, FilterField::cutoff)]
                                                           * std::exp2 ((v.pitch - 60.0f) / 12.0f * key));
            v.filter[(size_t) f].setParams ((FilterType) jlimit (0, (int) FilterType::count - 1, (int) vals[vp::filter (f, FilterField::type)]),
                                            cutoff, vals[vp::filter (f, FilterField::resonance)] * 0.01f,
                                            vals[vp::filter (f, FilterField::drive)] * 0.01f,
                                            vals[vp::filter (f, FilterField::var)] * 0.01f,
                                            vals[vp::filter (f, FilterField::mix)] * 0.01f);
            v.filter[(size_t) f].process (l, r, n);
        }
    }

    // sum the buses: serial -> filter 2 output (+ direct); parallel -> both filters (+ direct)
    const float* sumL[3] = { parallel ? bus[0].data() : nullptr, bus[2].data(), bus[4].data() };
    const float* sumR[3] = { parallel ? bus[1].data() : nullptr, bus[3].data(), bus[5].data() };

    // amp: envelope 1 x velocity, ramped across the block
    const float sens = base[vp::global (vp::velocity)] * 0.01f;
    const float velGain = 1.0f - sens + sens * v.velocity * v.velocity;
    const float targetAmp = v.env[0].level * velGain;
    const float startAmp = v.ampLevel;
    float fade0 = v.fade;
    float fade1 = fade0;
    if (v.stealing)
        fade1 = jmax (0.0f, fade0 - seconds / 0.003f);

    constexpr float voiceGain = 0.3f;   // headroom: a 4-note chord of full saws peaks around -6 dBFS
    for (int i = 0; i < n; ++i)
    {
        const float t = (float) (i + 1) / (float) n;
        const float g = (startAmp + (targetAmp - startAmp) * t) * (fade0 + (fade1 - fade0) * t) * voiceGain;
        float l = 0.0f, r = 0.0f;
        for (int b = 0; b < 3; ++b)
            if (sumL[b] != nullptr)
            {
                l += sumL[b][i];
                r += sumR[b][i];
            }
        outL[i] += l * g;
        outR[i] += r * g;
    }
    v.ampLevel = targetAmp;
    v.fade = fade1;

    if (v.stealing && v.fade <= 0.0f)
    {
        v.active = false;
        v.stealing = false;
        if (v.pendingNote >= 0)
        {
            const int note = v.pendingNote;
            startVoice (v, v.pendingChannel, note, v.pendingVelocity, true);
            // the key may already have been let go while we were fading
            bool held = false;
            for (int i = 0; i < heldCount; ++i)
                held = held || heldStack[(size_t) i] == note;
            if (! held && ! sustainPedal)
                releaseVoice (v, 0.5f);
        }
        return;
    }

    if (! v.env[0].isActive())
        v.active = false;
}

//==============================================================================
void Engine::render (float* left, float* right, int numSamples) noexcept
{
    std::fill (left, left + numSamples, 0.0f);
    std::fill (right, right + numSamples, 0.0f);
    if (base == nullptr || numSamples <= 0)
        return;
    jassert (numSamples <= controlBlock);

    updateGlobalLfos (numSamples);

    for (auto& v : voices)
        if (v.active)
            renderVoice (v, left, right, numSamples);

    // global sources (for FX modulation and the UI)
    Voice* newest = newestVoice();
    if (newest != nullptr)
        globalSrc = newest->src;
    else
    {
        globalSrc.fill (0.0f);
        for (int l = 0; l < numLfos; ++l)
            globalSrc[(size_t) ModSource::lfo1 + (size_t) l] = globalLfo[(size_t) l].value;
        for (int m = 0; m < numMacros; ++m)
            globalSrc[(size_t) ModSource::macro1 + (size_t) m] = base[vp::macro (m)] * 0.01f;
        globalSrc[(size_t) ModSource::modWheel] = modWheel;
        globalSrc[(size_t) ModSource::aftertouch] = channelPressure;
        globalSrc[(size_t) ModSource::pitchBend] = pitchBend;
    }

    if (anyFxMod)
    {
        std::fill (fxMod.begin(), fxMod.end(), 0.0f);
        const auto& dests = modDests();
        for (int i = 0; i < numRoutes; ++i)
        {
            const auto& r = routes[(size_t) i];
            const int fxi = dests[(size_t) r.dest].fxIndex;
            if (fxi >= 0)
                fxMod[(size_t) fxi] += modSourceValue (globalSrc, r);
        }
    }

    // ---- UI feed (every few blocks is plenty) ---------------------------------------
    if (++uiCounter >= 4)
    {
        uiCounter = 0;
        int active = 0;
        uint32_t held[4] = {};
        for (const auto& v : voices)
            if (v.active)
            {
                ++active;
                if (v.gate && isPositiveAndBelow (v.note, 128))
                    held[v.note >> 5] |= 1u << (v.note & 31);
            }
        ui.activeVoices.store (active, std::memory_order_relaxed);
        for (int i = 0; i < 4; ++i)
            ui.heldNotes[i].store (held[i], std::memory_order_relaxed);

        for (int e = 0; e < numEnvs; ++e)
            ui.envLevel[(size_t) e].store (newest != nullptr ? newest->env[(size_t) e].level : 0.0f, std::memory_order_relaxed);
        for (int l = 0; l < numLfos; ++l)
        {
            const auto& s = newest != nullptr ? newest->lfo[(size_t) l] : globalLfo[(size_t) l];
            const float offset = base[vp::lfo (l, LfoField::phase)] / 360.0f;
            const auto mode = (LfoMode) jlimit (0, 2, (int) base[vp::lfo (l, LfoField::mode)]);
            const float shownPhase = (newest == nullptr || mode == LfoMode::free) ? frac ((float) globalLfo[(size_t) l].phase + offset)
                                                                                   : (float) s.phase;
            ui.lfoPhase[(size_t) l].store (shownPhase, std::memory_order_relaxed);
            ui.lfoValue[(size_t) l].store (s.value, std::memory_order_relaxed);
        }
        for (int o = 0; o < numOscs; ++o)
        {
            const float* vals = newest != nullptr ? newest->v.data() : base;
            ui.oscPosition[(size_t) o].store (vals[vp::osc (o, OscField::position)] * 0.01f, std::memory_order_relaxed);
            ui.oscWarp[(size_t) o].store (vals[vp::osc (o, OscField::warpAmount)] * 0.01f, std::memory_order_relaxed);
        }

        for (int d = 0; d < ui.numDests; ++d)
            ui.destMod[(size_t) d].store (0.0f, std::memory_order_relaxed);
        for (int i = 0; i < numRoutes; ++i)
        {
            const auto& r = routes[(size_t) i];
            auto& slot = ui.destMod[(size_t) r.dest];
            slot.store (slot.load (std::memory_order_relaxed) + modSourceValue (globalSrc, r), std::memory_order_relaxed);
        }
    }
}
} // namespace ers
