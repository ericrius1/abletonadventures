#include "GremlinProcessor.h"
#include "GremlinEditor.h"

using namespace juce;
namespace P = aa::params;

const StringArray& GremlinProcessor::weightIDs()
{
    static const StringArray ids { "wStutter", "wReverse", "wTape", "wHalf", "wCrush", "wGate", "wScatter" };
    return ids;
}

const StringArray& GremlinProcessor::forceIDs()
{
    static const StringArray ids { "forceStutter", "forceReverse", "forceTape", "forceHalf" };
    return ids;
}

AudioProcessorValueTreeState::ParameterLayout GremlinProcessor::createLayout()
{
    P::Layout layout;
    // Ordered so the first eight (Push's first bank) are the most playable.
    layout.add (P::percent ("chaos", "Chaos", 30.0f));
    layout.add (P::choice ("grid", "Grid", gremlin::gridNames(), 1));
    layout.add (P::percent ("mix", "Mix", 100.0f));
    layout.add (P::toggle ("forceStutter", "Force Stutter", false));
    layout.add (P::toggle ("forceReverse", "Force Reverse", false));
    layout.add (P::toggle ("forceTape", "Force Tape Stop", false));
    layout.add (P::toggle ("forceHalf", "Force Half Speed", false));
    layout.add (P::floatParam ("ratchet", "Ratchet", -12.0f, 12.0f, 0.0f, P::Unit::semitones, -1.0f, 1.0f));

    layout.add (P::percent ("wStutter", "Stutter Odds", 70.0f));
    layout.add (P::percent ("wReverse", "Reverse Odds", 40.0f));
    layout.add (P::percent ("wTape", "Tape Stop Odds", 25.0f));
    layout.add (P::percent ("wHalf", "Half Speed Odds", 30.0f));
    layout.add (P::percent ("wCrush", "Crush Odds", 25.0f));
    layout.add (P::percent ("wGate", "Gate Odds", 30.0f));
    layout.add (P::percent ("wScatter", "Scatter Odds", 35.0f));
    layout.add (P::choice ("slice", "Slice", { "1/2", "1/4", "1/8", "1/16", "Dice", "Accel" }, 4));

    layout.add (P::percent ("crush", "Crush Depth", 55.0f));
    layout.add (P::floatParam ("smooth", "Smooth", 1.0f, 20.0f, 4.0f, P::Unit::ms, 6.0f));
    layout.add (P::integer ("seed", "Seed", 1, 999, 23));
    layout.add (P::choice ("lock", "Lock", { "Free", "1 Bar", "2 Bars", "4 Bars" }, 0));
    return layout;
}

GremlinProcessor::GremlinProcessor()
    : aa::PluginBase (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true),
                      createLayout())
{
    chaos = raw ("chaos");
    grid = raw ("grid");
    mix = raw ("mix");
    for (int i = 0; i < 4; ++i)
        force[(size_t) i] = raw (forceIDs()[i]);
    ratchet = raw ("ratchet");
    for (int i = 0; i < gremlin::numDiceFx; ++i)
        weights[(size_t) i] = raw (weightIDs()[i]);
    slice = raw ("slice");
    crush = raw ("crush");
    smooth = raw ("smooth");
    seed = raw ("seed");
    lock = raw ("lock");

    tailSeconds = 2.0;

    // grid: 0 = 1/4, 1 = 1/8, 2 = 1/16, 3 = 1/32   slice: 0..3 = 1/2..1/16, 4 = Dice, 5 = Accel
    // lock: 0 = Free, 1 = 1 bar, 2 = 2 bars, 3 = 4 bars
    setFactoryPresets ({
        { "Mild Mischief", {} },
        { "Total Chaos", { { "chaos", 92.0f }, { "grid", 2.0f }, { "wStutter", 90.0f }, { "wReverse", 70.0f },
                           { "wTape", 45.0f }, { "wHalf", 55.0f }, { "wCrush", 70.0f }, { "wGate", 70.0f },
                           { "wScatter", 80.0f }, { "ratchet", 3.0f }, { "crush", 75.0f }, { "smooth", 3.0f } } },
        { "Stutter Funk", { { "chaos", 45.0f }, { "grid", 2.0f }, { "wStutter", 100.0f }, { "wReverse", 10.0f },
                            { "wTape", 0.0f }, { "wHalf", 0.0f }, { "wCrush", 0.0f }, { "wGate", 55.0f },
                            { "wScatter", 30.0f }, { "slice", 1.0f }, { "lock", 2.0f }, { "seed", 77.0f },
                            { "smooth", 3.0f } } },
        { "Tape Monster", { { "chaos", 45.0f }, { "grid", 0.0f }, { "wStutter", 10.0f }, { "wReverse", 20.0f },
                            { "wTape", 100.0f }, { "wHalf", 70.0f }, { "wCrush", 0.0f }, { "wGate", 0.0f },
                            { "wScatter", 0.0f }, { "smooth", 8.0f } } },
        { "Reverse Ghosts", { { "chaos", 50.0f }, { "grid", 1.0f }, { "wStutter", 15.0f }, { "wReverse", 100.0f },
                              { "wTape", 10.0f }, { "wHalf", 25.0f }, { "wCrush", 0.0f }, { "wGate", 0.0f },
                              { "wScatter", 50.0f }, { "smooth", 12.0f } } },
        { "8-Bit Goblin", { { "chaos", 60.0f }, { "grid", 2.0f }, { "wStutter", 50.0f }, { "wReverse", 10.0f },
                            { "wTape", 0.0f }, { "wHalf", 10.0f }, { "wCrush", 100.0f }, { "wGate", 45.0f },
                            { "wScatter", 20.0f }, { "slice", 2.0f }, { "crush", 85.0f }, { "smooth", 1.5f } } },
        { "Half-Time Hijinks", { { "chaos", 55.0f }, { "grid", 0.0f }, { "wStutter", 15.0f }, { "wReverse", 25.0f },
                                 { "wTape", 30.0f }, { "wHalf", 100.0f }, { "wCrush", 0.0f }, { "wGate", 0.0f },
                                 { "wScatter", 15.0f }, { "smooth", 10.0f }, { "lock", 2.0f }, { "seed", 12.0f } } },
        { "Ratchet Riot", { { "chaos", 50.0f }, { "grid", 1.0f }, { "wStutter", 100.0f }, { "wReverse", 0.0f },
                            { "wTape", 0.0f }, { "wHalf", 0.0f }, { "wCrush", 15.0f }, { "wGate", 20.0f },
                            { "wScatter", 0.0f }, { "slice", 5.0f }, { "ratchet", 2.0f }, { "smooth", 2.0f } } },
        { "Locked Loop Gremlin", { { "chaos", 40.0f }, { "grid", 2.0f }, { "lock", 2.0f }, { "seed", 42.0f } } },
        { "Scatterbrain", { { "chaos", 55.0f }, { "grid", 2.0f }, { "wStutter", 35.0f }, { "wReverse", 25.0f },
                            { "wTape", 0.0f }, { "wHalf", 0.0f }, { "wCrush", 0.0f }, { "wGate", 15.0f },
                            { "wScatter", 100.0f }, { "lock", 1.0f }, { "seed", 303.0f }, { "smooth", 5.0f } } },
        { "Chop Shop", { { "chaos", 55.0f }, { "grid", 1.0f }, { "wStutter", 40.0f }, { "wReverse", 0.0f },
                         { "wTape", 0.0f }, { "wHalf", 0.0f }, { "wCrush", 20.0f }, { "wGate", 100.0f },
                         { "wScatter", 0.0f }, { "slice", 2.0f }, { "smooth", 2.0f } } },
        { "Background Imp", { { "chaos", 22.0f }, { "grid", 2.0f }, { "mix", 55.0f }, { "wTape", 10.0f },
                              { "wCrush", 15.0f }, { "smooth", 9.0f }, { "lock", 3.0f }, { "seed", 7.0f } } },
    });
}

gremlin::DiceSettings GremlinProcessor::diceSettings (double ppqPerBar) const
{
    gremlin::DiceSettings d;
    d.chaos = jlimit (0.0f, 1.0f, chaos->load() / 100.0f);
    for (size_t i = 0; i < weights.size(); ++i)
        d.weights[i] = jlimit (0.0f, 1.0f, weights[i]->load() / 100.0f);
    d.sliceMode = jlimit (0, 5, (int) slice->load());
    d.gridBeats = gremlin::gridBeatsFor (gridIndex());
    d.stepsPerBar = jmax (1, roundToInt (ppqPerBar / d.gridBeats));
    return d;
}

void GremlinProcessor::prepareToPlay (double sampleRate, int)
{
    transport.prepare (sampleRate);
    engine.prepare (sampleRate);
}

void GremlinProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = jmin (buffer.getNumChannels(), getTotalNumOutputChannels());

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numChannels == 0 || numSamples == 0)
        return;

    transport.update (getPlayHead(), numSamples);
    const double ppqPerBar = 4.0 * (double) transport.timeSigNumerator / (double) jmax (1, transport.timeSigDenominator);

    gremlin::Engine::Settings s;
    s.dice = diceSettings (ppqPerBar);
    s.gridIndex = gridIndex();
    s.ratchet = ratchet->load();
    s.crush = crush->load() / 100.0f;
    s.smoothMs = smooth->load();
    s.mix = mix->load() / 100.0f;
    s.seed = seedValue();
    s.lockBars = lockBars();
    for (size_t i = 0; i < 4; ++i)
        s.force[i] = force[i]->load() > 0.5f;

    gremlin::Engine::TimeInfo t;
    t.ppq = transport.ppqAtBlockStart;
    t.bpm = transport.bpm;
    t.ppqPerBar = ppqPerBar;

    engine.process (buffer.getWritePointer (0), numChannels > 1 ? buffer.getWritePointer (1) : nullptr, numSamples, s, t);
}

AudioProcessorEditor* GremlinProcessor::createEditor()
{
    return new GremlinEditor (*this);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GremlinProcessor();
}
