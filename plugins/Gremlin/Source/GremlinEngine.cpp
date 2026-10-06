#include "GremlinEngine.h"

using namespace juce;

namespace gremlin
{
namespace
{
    inline float smoothStep (float t)
    {
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        return t * t * (3.0f - 2.0f * t);
    }

    constexpr double maxHistorySeconds = 8.0;
}

//==============================================================================
void Engine::prepare (double sampleRate)
{
    sr = sampleRate;
    int64_t size = 1;
    while (size < (int64_t) (maxHistorySeconds * sampleRate) + 8192)
        size <<= 1;

    bufL.assign ((size_t) size, 0.0f);
    bufR.assign ((size_t) size, 0.0f);
    capacity = size;
    mask = size - 1;

    mixSmoother.reset (sampleRate, 0.03, 1.0f);
    freeRng.seed ((uint32_t) Random::getSystemRandom().nextInt() | 1u);
    peakRelease = (float) std::exp (-1.0 / (0.2 * sampleRate));
    reset();
}

void Engine::reset()
{
    std::fill (bufL.begin(), bufL.end(), 0.0f);
    std::fill (bufR.begin(), bufR.end(), 0.0f);
    writeCount = 0;
    for (auto& v : voices)
        v = Voice {};
    active = -1;
    currentStep = std::numeric_limits<int64_t>::min();
    lastGridIndex = -1;
    spanRemaining = 0;
    forceHeld = {};
    forceOrder = {};
    desiredForce = Fx::none;
    inPeak = outPeak = 0.0f;
    activeFx.store (0);
    activeForced.store (false);
}

//==============================================================================
void Engine::read (double pos, float& l, float& r) const
{
    const double latest = (double) (writeCount - 1);
    const double oldest = (double) (writeCount - capacity + 8);
    pos = jlimit (oldest, latest, pos);

    const double fl = std::floor (pos);
    const auto i = (int64_t) fl;
    const float f = (float) (pos - fl);

    if (f < 1.0e-5f)
    {
        l = bufL[(size_t) (i & mask)];
        r = bufR[(size_t) (i & mask)];
        return;
    }

    if ((double) (i + 2) > latest)
    {
        // right at the write head: linear (never touch samples that haven't been written yet)
        const auto a = (size_t) (i & mask), b = (size_t) ((i + 1) & mask);
        l = bufL[a] + (bufL[b] - bufL[a]) * f;
        r = bufR[a] + (bufR[b] - bufR[a]) * f;
        return;
    }

    auto hermite = [f] (float xm1, float x0, float x1, float x2)
    {
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    };

    const auto im1 = (size_t) ((i - 1) & mask), i0 = (size_t) (i & mask);
    const auto i1 = (size_t) ((i + 1) & mask), i2 = (size_t) ((i + 2) & mask);
    l = hermite (bufL[im1], bufL[i0], bufL[i1], bufL[i2]);
    r = hermite (bufR[im1], bufR[i0], bufR[i1], bufR[i2]);
}

//==============================================================================
double Engine::repLenFor (const Voice& v, int k) const
{
    if (v.slices > 0)
        return jmax (2.0, v.len / (double) v.slices);

    // accelerating roll: 1/4, 1/4, 1/8, 1/8, 1/16, 1/16 ... of the step
    const int octave = jmin (12, 2 + k / 2);
    return jmax (v.minRepLen, v.len / (double) (1 << octave));
}

double Engine::rateFor (const Voice& v, int k) const
{
    if (k == 0 || std::abs (v.ratchet) < 0.01f)
        return 1.0;

    const double semis = jlimit (-24.0, 24.0, (double) k * (double) v.ratchet);
    const double rate = std::pow (2.0, semis / 12.0);
    // never read ahead of the write head (the repeat's material must already be recorded)
    return jmin (rate, 1.0 + 0.95 * (double) k);
}

void Engine::advanceRepeats (Voice& v)
{
    int guard = 0;
    while (v.n - v.repStart >= v.repLen && ++guard < 64)
    {
        v.prevRepStart = v.repStart;
        v.prevRate = v.rate;
        v.repStart += v.repLen;
        ++v.repIdx;
        v.repLen = repLenFor (v, v.repIdx);
        v.rate = rateFor (v, v.repIdx);
        v.xfLen = jmax (8, (int) jmin (smoothSamples, v.repLen * 0.3));
        v.xfLeft = v.xfLen;
    }
}

//==============================================================================
void Engine::renderVoice (Voice& v, float& l, float& r)
{
    const double now = (double) (writeCount - 1);

    switch (v.fx)
    {
        case Fx::none:
            read (now, l, r);
            break;

        case Fx::stutter:
        {
            advanceRepeats (v);
            const double m = v.n - v.repStart;
            read (v.t0 + m * v.rate, l, r);

            if (v.xfLeft > 0)
            {
                float pl, pr;
                read (v.t0 + (v.n - v.prevRepStart) * v.prevRate, pl, pr);
                const float g = smoothStep (1.0f - (float) v.xfLeft / (float) v.xfLen);
                l = pl + (l - pl) * g;
                r = pr + (r - pr) * g;
                --v.xfLeft;
            }
            break;
        }

        case Fx::reverse:
            read (v.t0 - 1.0 - v.n, l, r);
            break;

        case Fx::tapeStop:
        {
            const double u = jmin (1.0, v.n / v.rampLen);
            read (v.t0 + v.rampLen * (u - 0.5 * u * u), l, r);
            const float speed = (float) (1.0 - u);
            const float fc = 90.0f + 17000.0f * speed * speed;
            const float a = 1.0f - std::exp (-aa::dsp::twoPi * jmin (fc, (float) sr * 0.45f) / (float) sr);
            v.lpL += a * (l - v.lpL);
            v.lpR += a * (r - v.lpR);
            const float g = smoothStep (speed * 4.5f);
            l = v.lpL * g;
            r = v.lpR * g;
            v.lastSpeed = speed;
            break;
        }

        case Fx::halfSpeed:
            read (v.t0 + v.n * 0.5, l, r);
            break;

        case Fx::crush:
        {
            float xl, xr;
            read (now, xl, xr);
            v.holdPhase += 1.0f;
            if (v.holdPhase >= v.holdLen)
            {
                v.holdPhase -= v.holdLen;
                v.heldL = std::round (xl * v.levels) / v.levels;
                v.heldR = std::round (xr * v.levels) / v.levels;
            }
            l = v.heldL;
            r = v.heldR;
            break;
        }

        case Fx::gate:
        {
            advanceRepeats (v);
            const double m = v.n - v.repStart;
            const bool open = ((v.mask >> (v.repIdx & 15)) & 1u) != 0;
            float g = 0.0f;
            if (open)
            {
                const double openLen = v.repLen * (double) v.duty;
                if (m < openLen)
                {
                    const double ramp = jmax (8.0, jmin (smoothSamples, v.repLen * 0.2));
                    const float rise = v.repIdx == 0 ? 1.0f : (float) (m / ramp);
                    const float fall = (float) ((openLen - m) / ramp);
                    g = smoothStep (jmin (rise, fall));
                }
            }
            read (now, l, r);
            l *= g;
            r *= g;
            v.lastGate = g;
            break;
        }

        case Fx::scatter:
            read (v.t0 - v.offset + v.n, l, r);
            break;
    }

    v.n += 1.0;
}

//==============================================================================
void Engine::startVoice (const Decision& d, bool held, double t0, double n0, const Settings& s)
{
    const double xf = jlimit (16.0, jmax (16.0, stepLen * 0.45), smoothSamples);

    int slot = -1;
    for (int i = 0; i < maxVoices; ++i)
        if (! voices[(size_t) i].alive)
        {
            slot = i;
            break;
        }

    if (slot < 0)
    {
        // steal the quietest voice that isn't the one currently playing
        float quietest = 2.0f;
        for (int i = 0; i < maxVoices; ++i)
            if (i != active && voices[(size_t) i].fade < quietest)
            {
                quietest = voices[(size_t) i].fade;
                slot = i;
            }
    }

    const bool hadVoice = active >= 0 && voices[(size_t) active].alive;
    if (hadVoice && slot != active)
        voices[(size_t) active].fadeInc = (float) (-1.0 / xf);

    auto& v = voices[(size_t) slot];
    v = Voice {};
    v.alive = true;
    v.fx = d.fx;
    v.held = held;
    v.t0 = t0;
    v.n = n0;
    v.len = jmax (16.0, stepLen * (held ? 1.0 : (double) d.span));
    v.fade = hadVoice ? 0.0f : 1.0f;
    v.fadeInc = hadVoice ? (float) (1.0 / xf) : 0.0f;

    const double barSamples = jmax (1.0, ppqPerBar * samplesPerPpq);

    switch (d.fx)
    {
        case Fx::stutter:
        case Fx::gate:
        {
            v.slices = d.slices;
            v.ratchet = d.fx == Fx::stutter ? s.ratchet : 0.0f;
            v.minRepLen = jmax (v.len / 64.0, 0.002 * sr);
            v.mask = d.gateMask;
            v.duty = jlimit (0.3f, 0.7f, 0.5f * d.variation);
            v.repIdx = 0;
            v.repStart = 0.0;
            v.repLen = repLenFor (v, 0);
            v.rate = 1.0;
            // joining mid-step (transport jump / late force): skip ahead through the schedule
            int guard = 0;
            while (v.repStart + v.repLen <= v.n && ++guard < 4096)
            {
                v.repStart += v.repLen;
                ++v.repIdx;
                v.repLen = repLenFor (v, v.repIdx);
            }
            v.rate = rateFor (v, v.repIdx);
            v.prevRate = v.rate;
            v.prevRepStart = v.repStart;
            break;
        }

        case Fx::reverse:
            if (held)
                v.cycleLen = jmin (barSamples, (double) (capacity - 16384) * 0.5);
            break;

        case Fx::halfSpeed:
            if (held)
                v.cycleLen = jmin (2.0 * barSamples, (double) (capacity - 16384) * 2.0);
            break;

        case Fx::tapeStop:
        {
            v.rampLen = held ? jmax (stepLen, samplesPerPpq) : v.len;
            float l = 0.0f, r = 0.0f;
            read ((double) (writeCount - 1), l, r);
            v.lpL = l;
            v.lpR = r;
            break;
        }

        case Fx::crush:
        {
            const float depth = jlimit (0.0f, 1.0f, s.crush * d.variation);
            const float bits = 16.0f - depth * 13.0f;
            v.levels = std::pow (2.0f, bits - 1.0f);
            const double targetHz = 44100.0 * std::pow (2.0, -(double) depth * 4.6);
            v.holdLen = (float) jmax (1.0, sr / targetHz);
            v.holdPhase = v.holdLen;
            break;
        }

        case Fx::scatter:
            v.offset = (double) d.scatterSteps * stepLen;
            break;

        case Fx::none:
            break;
    }

    active = slot;
}

//==============================================================================
void Engine::pushEvent (Fx fx, bool forced, int span, int slices)
{
    StepEvent e;
    e.ppq = stepPpq;
    e.lengthPpq = (float) (gridBeatsFor (lastGridIndex) * (double) span);
    e.fx = (int8_t) fx;
    e.forced = forced;
    e.slices = (int8_t) slices;
    events.push (e);
    stepCounter.fetch_add (1);
}

void Engine::decideStep (bool fromBoundary, const Settings& s)
{
    const double n0 = jmax (0.0, (double) writeCount - stepStartAbs);
    const bool activeHeld = active >= 0 && voices[(size_t) active].alive && voices[(size_t) active].held;

    if (desiredForce != Fx::none)
    {
        spanRemaining = 0;
        const bool alreadyRunning = activeHeld && voices[(size_t) active].fx == desiredForce;
        Decision d;
        d.fx = desiredForce;
        d.slices = slicesFor (s.dice.sliceMode, freeRng.next01());
        if (! alreadyRunning)
            startVoice (d, true, stepStartAbs, n0, s);
        pushEvent (desiredForce, true, 1, alreadyRunning ? voices[(size_t) active].slices : d.slices);
        return;
    }

    if (fromBoundary && spanRemaining > 0 && ! activeHeld)
    {
        --spanRemaining;
        return;
    }
    spanRemaining = 0;

    Decision d;
    if (s.lockBars > 0)
        d = lockedDecision (s.dice, s.seed, s.lockBars, s.gridIndex, stepPpq, ppqPerBar);
    else
        d = decide (freeRng, s.dice);

    if (d.fx == Fx::scatter)
    {
        // only jump back as far as we actually have audio for
        const double history = (double) jmin (writeCount, capacity - 16384) - stepLen - 4.0 * smoothSamples;
        const int maxSteps = (int) std::floor (history / jmax (1.0, stepLen));
        if (maxSteps < 1)
            d.fx = Fx::none;
        else
            d.scatterSteps = jmin (d.scatterSteps, maxSteps);
    }

    const bool liveNow = active >= 0 && voices[(size_t) active].alive && voices[(size_t) active].fx == Fx::none && ! activeHeld;
    if (! (d.fx == Fx::none && liveNow))
        startVoice (d, false, stepStartAbs, n0, s);

    spanRemaining = d.span - 1;
    pushEvent (d.fx, false, d.span, d.fx == Fx::scatter ? d.scatterSteps : d.slices);
}

bool Engine::updateForce (const std::array<bool, 4>& held)
{
    for (size_t k = 0; k < 4; ++k)
    {
        if (held[k] && ! forceHeld[k])
            forceOrder[k] = ++forceCounter;
        forceHeld[k] = held[k];
    }

    Fx wanted = Fx::none;
    uint32_t newest = 0;
    for (int k = 0; k < 4; ++k)
        if (forceHeld[(size_t) k] && forceOrder[(size_t) k] >= newest)
        {
            newest = forceOrder[(size_t) k];
            wanted = forceFx (k);
        }

    if (wanted == desiredForce)
        return false;

    desiredForce = wanted;
    return true;
}

//==============================================================================
void Engine::process (float* left, float* right, int numSamples, const Settings& s, const TimeInfo& time)
{
    if (capacity == 0 || numSamples <= 0 || left == nullptr)
        return;

    const double bpm = jlimit (20.0, 999.0, time.bpm);
    samplesPerPpq = 60.0 * sr / bpm;
    const double ppqPerSample = 1.0 / samplesPerPpq;
    const double grid = gridBeatsFor (s.gridIndex);
    stepLen = grid * samplesPerPpq;
    smoothSamples = jmax (8.0, (double) s.smoothMs * 0.001 * sr);
    ppqPerBar = jmax (0.25, time.ppqPerBar);
    mixSmoother.setTarget (jlimit (0.0f, 1.0f, s.mix));

    if (s.gridIndex != lastGridIndex)
    {
        lastGridIndex = s.gridIndex;
        currentStep = std::numeric_limits<int64_t>::min();
    }

    bool forceChanged = updateForce (s.force);

    int i = 0;
    while (i < numSamples)
    {
        const double ppq = time.ppq + (double) i * ppqPerSample;
        const auto step = (int64_t) std::floor (ppq / grid + 1.0e-9);

        if (step != currentStep)
        {
            const bool contiguous = currentStep != std::numeric_limits<int64_t>::min() && step == currentStep + 1;
            const double offset = contiguous ? 0.0 : jmax (0.0, (ppq - (double) step * grid) * samplesPerPpq);
            currentStep = step;
            if (! contiguous)
                spanRemaining = 0;
            stepStartAbs = (double) writeCount - offset;
            stepPpq = (double) step * grid;
            decideStep (true, s);
            forceChanged = false;
        }
        else if (forceChanged)
        {
            // A pad was hit: if we're only just past the grid line, apply it to this step
            // (automation lands a block late); otherwise wait for the next step.
            const double into = (double) writeCount - stepStartAbs;
            const double tolerance = jmin (stepLen * 0.5, 0.03 * sr + (double) numSamples);
            if (into <= tolerance)
                decideStep (false, s);
            forceChanged = false;
        }

        const double nextPpq = (double) (step + 1) * grid;
        int end = i + jmax (1, (int) std::ceil ((nextPpq - ppq) * samplesPerPpq - 1.0e-7));
        end = jmin (end, numSamples);

        for (int j = i; j < end; ++j)
        {
            const float inL = left[j];
            const float inR = right != nullptr ? right[j] : inL;
            bufL[(size_t) (writeCount & mask)] = inL;
            bufR[(size_t) (writeCount & mask)] = inR;
            ++writeCount;

            if (active >= 0)
            {
                auto& av = voices[(size_t) active];
                if (av.alive && av.held && av.cycleLen > 0.0 && av.n >= av.cycleLen)
                {
                    Decision d;
                    d.fx = av.fx;
                    startVoice (d, true, (double) (writeCount - 1), 0.0, s);
                }
            }

            float wetL = 0.0f, wetR = 0.0f;
            for (auto& v : voices)
            {
                if (! v.alive)
                    continue;

                float l = 0.0f, r = 0.0f;
                renderVoice (v, l, r);
                const float g = smoothStep (v.fade);
                wetL += l * g;
                wetR += r * g;

                v.fade += v.fadeInc;
                if (v.fade >= 1.0f)
                {
                    v.fade = 1.0f;
                    v.fadeInc = 0.0f;
                }
                else if (v.fade <= 0.0f && v.fadeInc < 0.0f)
                {
                    v.alive = false;
                }
            }

            const float m = mixSmoother.next();
            const float outL = inL + (wetL - inL) * m;
            const float outR = inR + (wetR - inR) * m;

            left[j] = outL;
            if (right != nullptr)
                right[j] = outR;

            const float ia = jmax (std::abs (inL), std::abs (inR));
            const float oa = jmax (std::abs (outL), std::abs (outR));
            inPeak = ia > inPeak ? ia : inPeak * peakRelease;
            outPeak = oa > outPeak ? oa : outPeak * peakRelease;
        }

        i = end;
    }

    // ---- UI feed ---------------------------------------------------------------
    if (active >= 0)
    {
        const auto& av = voices[(size_t) active];
        activeFx.store ((int) av.fx);
        activeForced.store (av.held);
        gateLevel.store (av.fx == Fx::gate ? av.lastGate : 1.0f);
        tapeSpeed.store (av.fx == Fx::tapeStop ? av.lastSpeed : 1.0f);
        repeatIndex.store (av.repIdx);
    }
    uiPpq.store (time.ppq + (double) numSamples * ppqPerSample);
    uiBpm.store (bpm);
    uiPpqPerBar.store (ppqPerBar);
    inLevel.store (inPeak);
    outLevel.store (outPeak);
}
} // namespace gremlin
