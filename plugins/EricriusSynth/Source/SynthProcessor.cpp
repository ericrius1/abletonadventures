#include "SynthProcessor.h"
#include "SynthEditor.h"
#include "Presets.h"

using namespace juce;
using namespace ers;

namespace
{
    inline float safetyClip (float x)
    {
        const float a = std::abs (x);
        if (a <= 0.85f)
            return x;
        const float y = 0.85f + 0.15f * std::tanh ((a - 0.85f) / 0.15f);
        return x < 0.0f ? -y : y;
    }

    const LfoShape::Preset defaultLfoShapes[numLfos] = { LfoShape::Preset::sine, LfoShape::Preset::triangle,
                                                         LfoShape::Preset::sawDown, LfoShape::Preset::steps };

    //==============================================================================
    // Custom wavetables are saved in the state as gzipped 16-bit PCM, base64 encoded.
    String encodeTable (const Wavetable& table)
    {
        const auto frames = table.getRawFrames();
        MemoryOutputStream raw;
        {
            GZIPCompressorOutputStream gz (raw, 9);
            for (float s : frames)
                gz.writeShort ((short) jlimit (-32767, 32767, roundToInt (s * 32767.0f)));
        }
        return Base64::toBase64 (raw.getData(), raw.getDataSize());
    }

    WavetablePtr decodeTable (const String& name, const String& data, int numFrames)
    {
        MemoryOutputStream decoded;
        if (! Base64::convertFromBase64 (decoded, data) || numFrames < 1 || numFrames > wtMaxFrames)
            return nullptr;

        MemoryInputStream in (decoded.getData(), decoded.getDataSize(), false);
        GZIPDecompressorInputStream gz (in);
        std::vector<float> frames ((size_t) numFrames * wtSize);
        for (auto& s : frames)
        {
            if (gz.isExhausted())
                return nullptr;
            s = (float) gz.readShort() / 32767.0f;
        }
        return Wavetable::fromFrames (name, frames, numFrames);
    }
} // namespace

//==============================================================================
SynthProcessor::SynthProcessor()
    : aa::PluginBase (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true), createLayout())
{
    vpRanges.resize ((size_t) vp::count);
    for (int i = 0; i < vp::count; ++i)
    {
        vpParams[(size_t) i] = param (vpId (i));
        vpRaw[(size_t) i] = raw (vpId (i));
        jassert (vpParams[(size_t) i] != nullptr && vpRaw[(size_t) i] != nullptr);
        vpRanges[(size_t) i] = vpParams[(size_t) i]->getNormalisableRange();
    }
    engine.setRanges (vpRanges);

    for (const auto& spec : fx::paramSpecs())
    {
        fxParams.push_back (param (spec.id));
        fxRaw.push_back (raw (spec.id));
    }
    fxValues.resize (fxParams.size());
    fxBase.resize (fxParams.size());
    fxNorm.resize (fxParams.size());

    for (int m = 0; m < numMacros; ++m)
        macroNames.add ("Macro " + String (m + 1));

    resetExtraState();
    tailSeconds = 12.0;
    setFactoryPresets (makeFactoryPresets());
    syncTables();
    startTimerHz (20);
}

SynthProcessor::~SynthProcessor()
{
    stopTimer();
}

//==============================================================================
WavetablePtr SynthProcessor::factoryTable (int index)
{
    index = jlimit (0, getNumFactoryTables() - 1, index);
    if (auto it = factoryCache.find (index); it != factoryCache.end())
        return it->second;

    auto table = createFactoryTable (index);
    if (factoryCache.size() >= 10)
    {
        // drop a cached table nobody is using
        for (auto it = factoryCache.begin(); it != factoryCache.end(); ++it)
            if (it->second.use_count() == 1)
            {
                factoryCache.erase (it);
                break;
            }
    }
    factoryCache[index] = table;
    return table;
}

void SynthProcessor::retire (std::shared_ptr<const void> object)
{
    if (object != nullptr)
        graveyard.emplace_back (std::move (object), Time::getMillisecondCounterHiRes());
}

void SynthProcessor::syncTables()
{
    bool changed = false;
    for (int o = 0; o < numOscs; ++o)
    {
        const int index = (int) vpRaw[(size_t) vp::osc (o, OscField::table)]->load();
        WavetablePtr wanted;
        if (index >= getCustomTableIndex())
            wanted = customTables[(size_t) o] != nullptr ? customTables[(size_t) o] : factoryTable (0);
        else
            wanted = factoryTable (index);

        if (wanted != oscTables[(size_t) o])
        {
            retire (oscTables[(size_t) o]);
            oscTables[(size_t) o] = wanted;
            oscLive[(size_t) o].store (wanted.get(), std::memory_order_release);
            changed = true;
        }
        loadedTableIndex[(size_t) o] = index;
    }

    const int noiseIndex = (int) vpRaw[(size_t) vp::noise (vp::noiseType)]->load();
    if (noiseIndex != loadedNoiseIndex || noise == nullptr)
    {
        auto n = createNoise (jlimit (0, getNumNoiseTypes() - 1, noiseIndex));
        retire (noise);
        noise = n;
        noiseLive.store (n.get(), std::memory_order_release);
        loadedNoiseIndex = noiseIndex;
    }

    if (changed)
        ++tableVersion;
}

void SynthProcessor::timerCallback()
{
    syncTables();

    // Free retired tables once the audio thread can no longer be using them.
    const double now = Time::getMillisecondCounterHiRes();
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
                                     [now] (const auto& g) { return now - g.second > 2000.0; }),
                     graveyard.end());
}

WavetablePtr SynthProcessor::getOscTable (int osc) const
{
    return oscTables[(size_t) osc];
}

String SynthProcessor::getOscTableName (int osc) const
{
    const auto& t = oscTables[(size_t) osc];
    return t != nullptr ? t->getName() : String();
}

bool SynthProcessor::loadCustomTable (int osc, const File& file, String& error)
{
    auto table = loadWavetableFile (file, &error);
    if (table == nullptr)
        return false;
    return loadCustomTable (osc, table);
}

bool SynthProcessor::loadCustomTable (int osc, WavetablePtr table)
{
    if (table == nullptr || ! isPositiveAndBelow (osc, numOscs))
        return false;

    customTables[(size_t) osc] = table;
    auto* p = vpParams[(size_t) vp::osc (osc, OscField::table)];
    p->setValueNotifyingHost (p->convertTo0to1 ((float) getCustomTableIndex()));
    retire (oscTables[(size_t) osc]);   // force a re-sync even if the index didn't change
    oscTables[(size_t) osc] = nullptr;
    syncTables();
    return true;
}

void SynthProcessor::selectFactoryTable (int osc, int index)
{
    auto* p = vpParams[(size_t) vp::osc (osc, OscField::table)];
    p->setValueNotifyingHost (p->convertTo0to1 ((float) jlimit (0, getNumFactoryTables() - 1, index)));
    syncTables();
}

//==============================================================================
void SynthProcessor::publishLfoTable (int lfo)
{
    auto table = std::make_shared<LfoTable>();
    lfoShapes[(size_t) lfo].render (table->data.data());
    retire (lfoTables[(size_t) lfo]);
    lfoTables[(size_t) lfo] = table;
    lfoLive[(size_t) lfo].store (table.get(), std::memory_order_release);
}

void SynthProcessor::setLfoShape (int lfo, const LfoShape& shape)
{
    if (! isPositiveAndBelow (lfo, numLfos))
        return;
    lfoShapes[(size_t) lfo] = shape;
    lfoShapes[(size_t) lfo].sanitise();
    publishLfoTable (lfo);
    ++lfoShapeVersion;
}

fx::Order SynthProcessor::getFxOrder() const
{
    fx::Order o;
    for (int i = 0; i < fx::numKinds; ++i)
        o[(size_t) i] = (fx::Kind) fxOrder[(size_t) i].load();
    return o;
}

void SynthProcessor::setFxOrder (const fx::Order& order)
{
    // only accept permutations
    std::array<bool, fx::numKinds> seen {};
    for (auto k : order)
    {
        if (! isPositiveAndBelow ((int) k, fx::numKinds) || seen[(size_t) k])
            return;
        seen[(size_t) k] = true;
    }
    for (int i = 0; i < fx::numKinds; ++i)
        fxOrder[(size_t) i].store ((int) order[(size_t) i]);
}

//==============================================================================
void SynthProcessor::saveExtraState (ValueTree& tree)
{
    tree.appendChild (matrix.toValueTree(), nullptr);

    for (int l = 0; l < numLfos; ++l)
    {
        auto t = lfoShapes[(size_t) l].toValueTree ("LfoShape");
        t.setProperty ("index", l, nullptr);
        tree.appendChild (t, nullptr);
    }

    ValueTree macros ("Macros");
    macros.setProperty ("names", macroNames.joinIntoString ("|"), nullptr);
    tree.appendChild (macros, nullptr);

    StringArray order;
    for (auto k : getFxOrder())
        order.add (fx::kindPrefix (k));
    ValueTree fxo ("FxOrder");
    fxo.setProperty ("order", order.joinIntoString (","), nullptr);
    tree.appendChild (fxo, nullptr);

    for (int o = 0; o < numOscs; ++o)
    {
        const auto& t = customTables[(size_t) o];
        if (t == nullptr)
            continue;
        ValueTree c ("CustomTable");
        c.setProperty ("osc", o, nullptr);
        c.setProperty ("name", t->getName(), nullptr);
        c.setProperty ("frames", t->getNumFrames(), nullptr);
        c.setProperty ("data", encodeTable (*t), nullptr);
        tree.appendChild (c, nullptr);
    }
}

void SynthProcessor::loadExtraState (const ValueTree& tree)
{
    resetExtraState();

    if (auto m = tree.getChildWithName ("ModMatrix"); m.isValid())
        matrix.fromValueTree (m);

    for (auto child : tree)
    {
        if (child.hasType ("LfoShape"))
        {
            const int l = child.getProperty ("index", -1);
            if (isPositiveAndBelow (l, numLfos))
                setLfoShape (l, LfoShape::fromValueTree (child));
        }
        else if (child.hasType ("CustomTable"))
        {
            const int o = child.getProperty ("osc", -1);
            if (isPositiveAndBelow (o, numOscs))
                customTables[(size_t) o] = decodeTable (child.getProperty ("name").toString(),
                                                        child.getProperty ("data").toString(),
                                                        child.getProperty ("frames", 0));
        }
    }

    if (auto m = tree.getChildWithName ("Macros"); m.isValid())
    {
        const auto names = StringArray::fromTokens (m.getProperty ("names").toString(), "|", {});
        for (int i = 0; i < numMacros && i < names.size(); ++i)
            if (names[i].isNotEmpty())
                macroNames.set (i, names[i]);
    }

    if (auto f = tree.getChildWithName ("FxOrder"); f.isValid())
    {
        const auto items = StringArray::fromTokens (f.getProperty ("order").toString(), ",", {});
        fx::Order order = fx::defaultOrder();
        int n = 0;
        for (const auto& prefix : items)
            for (int k = 0; k < fx::numKinds; ++k)
                if (fx::kindPrefix ((fx::Kind) k) == prefix && n < fx::numKinds)
                    order[(size_t) n++] = (fx::Kind) k;
        if (n == fx::numKinds)
            setFxOrder (order);
    }

}

void SynthProcessor::resetExtraState()
{
    matrix.clear();
    for (int l = 0; l < numLfos; ++l)
        setLfoShape (l, LfoShape::preset (defaultLfoShapes[l]));
    for (int m = 0; m < numMacros; ++m)
        macroNames.set (m, "Macro " + String (m + 1));
    setFxOrder (fx::defaultOrder());
    for (auto& t : customTables)
        t = nullptr;
}

void SynthProcessor::stateLoaded()
{
    // Custom tables may have changed even though the table index didn't: rebuild the selection.
    for (auto& t : oscTables)
    {
        retire (t);
        t = nullptr;
    }
    syncTables();
}

//==============================================================================
void SynthProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = (float) sampleRate;
    transport.prepare (sampleRate);
    engine.prepare (sampleRate);
    rack.prepare (sampleRate, jmax (samplesPerBlock, controlBlock));
    volume.reset (sampleRate, 0.03, aa::dsp::dbToGain (vpRaw[(size_t) vp::global (vp::volume)]->load()));
    meterL.prepare (sr);
    meterR.prepare (sr);
    firstBlock = true;
    dcCoeff = std::exp (-aa::dsp::twoPi * 6.0f / sr);
    dcInL = dcInR = dcOutL = dcOutR = 0.0f;
    if (MessageManager::getInstanceWithoutCreating() != nullptr
        && MessageManager::getInstance()->isThisTheMessageThread())
        syncTables();
}

void SynthProcessor::reset()
{
    engine.reset();
    rack.reset();
    keyboardState.reset();
}

void SynthProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    if (numSamples == 0 || buffer.getNumChannels() == 0)
        return;

    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);
    transport.update (getPlayHead(), numSamples);

    EngineTables tables;
    for (int o = 0; o < numOscs; ++o)
        tables.osc[(size_t) o] = oscLive[(size_t) o].load (std::memory_order_acquire);
    tables.noise = noiseLive.load (std::memory_order_acquire);
    for (int l = 0; l < numLfos; ++l)
    {
        auto* t = lfoLive[(size_t) l].load (std::memory_order_acquire);
        tables.lfo[(size_t) l] = t != nullptr ? t->data.data() : nullptr;
    }
    engine.setTables (tables);

    for (int i = 0; i < vp::count; ++i)
    {
        blockValues[(size_t) i] = vpRaw[(size_t) i]->load (std::memory_order_relaxed);
        blockNorm[(size_t) i] = vpRanges[(size_t) i].convertTo0to1 (blockValues[(size_t) i]);
    }

    EngineBlock block;
    block.values = blockValues.data();
    block.normalised = blockNorm.data();
    block.routes = routeSnapshot.data();
    block.numRoutes = matrix.snapshot (routeSnapshot);
    block.bpm = transport.bpm;
    block.ppq = transport.ppqAtBlockStart;
    block.playing = transport.hostPlaying;
    engine.beginBlock (block);

    rack.setOrder (getFxOrder());
    for (size_t i = 0; i < fxRaw.size(); ++i)
    {
        fxBase[i] = fxRaw[i]->load (std::memory_order_relaxed);
        fxNorm[i] = fxParams[i]->convertTo0to1 (fxBase[i]);
    }

    volume.setTarget (aa::dsp::dbToGain (blockValues[(size_t) vp::global (vp::volume)]));
    if (firstBlock)
    {
        volume.snap (aa::dsp::dbToGain (blockValues[(size_t) vp::global (vp::volume)]));
        firstBlock = false;
    }

    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    std::array<float, controlBlock> l, r;

    auto renderTo = [&] (int start, int end)
    {
        for (int pos = start; pos < end;)
        {
            const int n = jmin (controlBlock, end - pos);
            engine.render (l.data(), r.data(), n);

            // DC blocker: asymmetric warps (PWM, FM, flip) can leave an offset
            for (int i = 0; i < n; ++i)
            {
                const float xl = l[(size_t) i], xr = r[(size_t) i];
                dcOutL = xl - dcInL + dcCoeff * dcOutL;
                dcOutR = xr - dcInR + dcCoeff * dcOutR;
                dcInL = xl;
                dcInR = xr;
                l[(size_t) i] = dcOutL;
                r[(size_t) i] = dcOutR;
            }

            processFx (l.data(), r.data(), n);

            int w = scopeWrite.load (std::memory_order_relaxed);
            for (int i = 0; i < n; ++i)
            {
                const float g = volume.next();
                const float sl = safetyClip (l[(size_t) i] * g);
                const float sr2 = safetyClip (r[(size_t) i] * g);
                meterL.process (sl);
                meterR.process (sr2);
                scope[(size_t) w].store (0.5f * (sl + sr2), std::memory_order_relaxed);
                w = (w + 1) & (scopeSize - 1);
                if (outR != nullptr)
                {
                    outL[pos + i] = sl;
                    outR[pos + i] = sr2;
                }
                else
                    outL[pos + i] = 0.5f * (sl + sr2);
            }
            scopeWrite.store (w, std::memory_order_release);
            pos += n;
        }
    };

    int pos = 0;
    for (const auto meta : midi)
    {
        const int eventPos = jlimit (0, numSamples, meta.samplePosition);
        if (eventPos > pos)
        {
            renderTo (pos, eventPos);
            pos = eventPos;
        }
        engine.handleMidi (meta.getMessage());
    }
    if (pos < numSamples)
        renderTo (pos, numSamples);

    meterL.publish();
    meterR.publish();
    outputLevelL.store (meterL.level.load(), std::memory_order_relaxed);
    outputLevelR.store (meterR.level.load(), std::memory_order_relaxed);
}

void SynthProcessor::processFx (float* l, float* r, int n)
{
    if (engine.hasFxModulation())
    {
        const auto& specs = fx::paramSpecs();
        for (size_t i = 0; i < fxBase.size(); ++i)
        {
            const float m = engine.getFxModulation ((int) i);
            fxValues[i] = (m != 0.0f && specs[i].modulatable)
                              ? fxParams[i]->convertFrom0to1 (jlimit (0.0f, 1.0f, fxNorm[i] + m))
                              : fxBase[i];
        }
    }
    else
        std::copy (fxBase.begin(), fxBase.end(), fxValues.begin());

    fx::TempoInfo tempo;
    tempo.bpm = transport.bpm;
    tempo.ppq = transport.ppqAtBlockStart;
    tempo.playing = transport.hostPlaying;
    rack.process (l, r, n, fxValues.data(), tempo);
}

AudioProcessorEditor* SynthProcessor::createEditor()
{
    return new SynthEditor (*this);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SynthProcessor();
}
