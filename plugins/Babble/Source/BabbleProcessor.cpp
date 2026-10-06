#include "BabbleProcessor.h"
#include "BabbleEditor.h"

using namespace juce;
namespace P = aa::params;

AudioProcessorValueTreeState::ParameterLayout BabbleProcessor::createLayout()
{
    P::Layout layout;

    // Ordered so the first eight (Push's first bank) are the most playable.
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "vowel", 1 }, "Vowel", NormalisableRange<float> (0.0f, 4.0f), 0.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return babble::vowelPositionText (v); })
            .withValueFromStringFunction ([] (const String& s) { return babble::vowelPositionFromText (s); })));
    layout.add (P::percent ("babble", "Babble", 40.0f));
    layout.add (P::floatParam ("babbleRate", "Babble Rate", 0.5f, 12.0f, 4.0f, P::Unit::hz, 3.5f));
    layout.add (P::floatParam ("shift", "Formant Shift", -12.0f, 12.0f, 0.0f, P::Unit::semitones));
    layout.add (P::percent ("breath", "Breath", 12.0f));
    layout.add (P::percent ("vibDepth", "Vibrato Depth", 30.0f));
    layout.add (P::percent ("reverb", "Reverb", 30.0f));
    layout.add (P::floatParam ("volume", "Volume", -36.0f, 6.0f, 0.0f, P::Unit::db));

    layout.add (P::choice ("voice", "Voice", babble::voiceTypeNames(), babble::alto));
    layout.add (P::percent ("bright", "Brightness", 55.0f));
    layout.add (P::floatParam ("vibRate", "Vibrato Rate", 0.5f, 10.0f, 5.2f, P::Unit::hz, 4.0f));
    layout.add (P::floatParam ("vibDelay", "Vibrato Delay", 0.0f, 2000.0f, 400.0f, P::Unit::ms, 500.0f));
    layout.add (P::floatParam ("glide", "Glide", 0.0f, 1000.0f, 0.0f, P::Unit::ms, 150.0f));
    layout.add (P::toggle ("babbleSync", "Babble Sync", false));
    layout.add (P::choice ("babbleDiv", "Babble Division", aa::dsp::syncDivisionNames(), 5));
    layout.add (P::integer ("choir", "Choir Voices", 1, babble::maxChoir, 3));
    layout.add (P::percent ("detune", "Detune", 35.0f));
    layout.add (P::floatParam ("attack", "Attack", 2.0f, 3000.0f, 60.0f, P::Unit::ms, 200.0f));
    layout.add (P::floatParam ("release", "Release", 10.0f, 5000.0f, 500.0f, P::Unit::ms, 600.0f));
    layout.add (P::percent ("chorus", "Chorus", 25.0f));
    return layout;
}

BabbleProcessor::BabbleProcessor()
    : aa::PluginBase (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true), createLayout())
{
    vowel = raw ("vowel");
    babbleAmount = raw ("babble");
    babbleRate = raw ("babbleRate");
    shift = raw ("shift");
    breath = raw ("breath");
    vibDepth = raw ("vibDepth");
    reverb = raw ("reverb");
    volume = raw ("volume");
    voice = raw ("voice");
    brightness = raw ("bright");
    vibRate = raw ("vibRate");
    vibDelay = raw ("vibDelay");
    glide = raw ("glide");
    babbleSync = raw ("babbleSync");
    babbleDiv = raw ("babbleDiv");
    choir = raw ("choir");
    detune = raw ("detune");
    attack = raw ("attack");
    release = raw ("release");
    chorus = raw ("chorus");

    tailSeconds = 6.0;

    setFactoryPresets ({
        { "Hello Choir", {} },
        { "Choir of Robots", { { "voice", 5.0f }, { "choir", 4.0f }, { "detune", 45.0f }, { "babble", 55.0f },
                               { "babbleRate", 5.0f }, { "vibDepth", 35.0f }, { "vibRate", 6.0f }, { "breath", 0.0f },
                               { "bright", 60.0f }, { "chorus", 35.0f }, { "reverb", 35.0f }, { "glide", 60.0f },
                               { "attack", 20.0f }, { "release", 400.0f } } },
        { "Oooh Pad", { { "vowel", 3.5f }, { "babble", 0.0f }, { "choir", 4.0f }, { "detune", 45.0f },
                        { "attack", 900.0f }, { "release", 2200.0f }, { "vibDepth", 18.0f }, { "vibRate", 4.5f },
                        { "vibDelay", 800.0f }, { "breath", 22.0f }, { "bright", 35.0f }, { "chorus", 50.0f },
                        { "reverb", 55.0f } } },
        { "Gibberish Lead", { { "voice", 1.0f }, { "choir", 1.0f }, { "babble", 85.0f }, { "babbleRate", 6.5f },
                              { "glide", 70.0f }, { "vowel", 1.0f }, { "attack", 8.0f }, { "release", 180.0f },
                              { "vibDepth", 30.0f }, { "vibDelay", 250.0f }, { "bright", 65.0f }, { "breath", 8.0f },
                              { "reverb", 22.0f }, { "chorus", 10.0f } } },
        { "Monk Drone", { { "voice", 0.0f }, { "vowel", 3.0f }, { "shift", -3.0f }, { "babble", 0.0f },
                          { "breath", 8.0f }, { "bright", 30.0f }, { "vibDepth", 8.0f }, { "vibRate", 4.0f },
                          { "choir", 4.0f }, { "detune", 22.0f }, { "attack", 700.0f }, { "release", 3000.0f },
                          { "reverb", 65.0f }, { "chorus", 20.0f } } },
        { "Baby Talk", { { "voice", 4.0f }, { "babble", 90.0f }, { "babbleRate", 5.5f }, { "shift", 2.0f },
                         { "glide", 45.0f }, { "choir", 1.0f }, { "vibDepth", 35.0f }, { "vibRate", 6.5f },
                         { "vibDelay", 150.0f }, { "bright", 62.0f }, { "breath", 10.0f }, { "attack", 12.0f },
                         { "release", 220.0f }, { "reverb", 25.0f } } },
        { "Opera Diva", { { "voice", 3.0f }, { "vowel", 0.25f }, { "babble", 0.0f }, { "vibDepth", 70.0f },
                          { "vibRate", 5.8f }, { "vibDelay", 450.0f }, { "choir", 1.0f }, { "bright", 72.0f },
                          { "glide", 90.0f }, { "attack", 90.0f }, { "release", 700.0f }, { "breath", 6.0f },
                          { "reverb", 45.0f }, { "chorus", 10.0f } } },
        { "Alien Chatter", { { "voice", 4.0f }, { "shift", 9.0f }, { "babble", 100.0f }, { "babbleRate", 9.0f },
                             { "glide", 120.0f }, { "vibDepth", 0.0f }, { "choir", 2.0f }, { "detune", 70.0f },
                             { "breath", 15.0f }, { "bright", 70.0f }, { "attack", 5.0f }, { "release", 150.0f },
                             { "chorus", 60.0f }, { "reverb", 30.0f }, { "volume", 3.0f } } },
        { "Doo-Wop Bass", { { "voice", 0.0f }, { "vowel", 4.0f }, { "shift", -1.0f }, { "babble", 30.0f },
                            { "babbleSync", 1.0f }, { "babbleDiv", 8.0f }, { "glide", 40.0f }, { "attack", 6.0f },
                            { "release", 200.0f }, { "choir", 1.0f }, { "vibDepth", 12.0f }, { "bright", 45.0f },
                            { "breath", 6.0f }, { "reverb", 18.0f }, { "chorus", 0.0f } } },
        { "Whisper Pad", { { "breath", 100.0f }, { "vowel", 2.5f }, { "choir", 3.0f }, { "detune", 40.0f },
                           { "attack", 900.0f }, { "release", 2500.0f }, { "babble", 20.0f }, { "babbleRate", 1.5f },
                           { "vibDepth", 0.0f }, { "reverb", 60.0f }, { "chorus", 40.0f } } },
        { "Barbershop", { { "voice", 1.0f }, { "vowel", 3.3f }, { "babble", 35.0f }, { "babbleSync", 1.0f },
                          { "babbleDiv", 5.0f }, { "choir", 2.0f }, { "detune", 15.0f }, { "vibDepth", 40.0f },
                          { "vibRate", 5.6f }, { "vibDelay", 300.0f }, { "bright", 55.0f }, { "attack", 25.0f },
                          { "release", 350.0f }, { "reverb", 25.0f }, { "chorus", 15.0f } } },
    });
}

babble::Settings BabbleProcessor::readSettings() const
{
    babble::Settings s;
    s.vowel = jlimit (0.0f, 4.0f, vowel->load());
    s.voiceType = jlimit (0, babble::numVoiceTypes - 1, (int) voice->load());
    s.shift = shift->load();
    s.breath = breath->load() / 100.0f;
    s.brightness = brightness->load() / 100.0f;
    s.vibRate = vibRate->load();
    s.vibDepth = vibDepth->load() / 100.0f;
    s.vibDelay = vibDelay->load() * 0.001f;
    s.glide = glide->load() * 0.001f;
    s.babble = babbleAmount->load() / 100.0f;
    s.babbleRate = babbleRate->load();
    s.babbleSync = babbleSync->load() > 0.5f;
    s.babbleDivision = (int) babbleDiv->load();
    s.choir = jlimit (1, babble::maxChoir, (int) std::round (choir->load()));
    s.detune = detune->load() / 100.0f;
    s.attack = attack->load() * 0.001f;
    s.release = release->load() * 0.001f;
    s.chorus = chorus->load() / 100.0f;
    s.reverb = reverb->load() / 100.0f;
    s.volumeDb = volume->load();
    return s;
}

void BabbleProcessor::prepareToPlay (double sampleRate, int)
{
    transport.prepare (sampleRate);
    engine.setSettings (readSettings());
    engine.prepare (sampleRate);
}

void BabbleProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numChannels == 0 || numSamples == 0)
        return;

    transport.update (getPlayHead(), numSamples);
    engine.setSettings (readSettings());

    float* left = buffer.getWritePointer (0);
    float* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = jlimit (0, numSamples, meta.samplePosition);
        if (at > pos)
        {
            engine.render (left + pos, right != nullptr ? right + pos : nullptr, at - pos, transport, pos);
            pos = at;
        }
        engine.handleMidi (meta.getMessage());
    }

    if (pos < numSamples)
        engine.render (left + pos, right != nullptr ? right + pos : nullptr, numSamples - pos, transport, pos);

    for (int ch = 2; ch < numChannels; ++ch)
        buffer.clear (ch, 0, numSamples);

    engine.publishUi();
}

AudioProcessorEditor* BabbleProcessor::createEditor()
{
    return new BabbleEditor (*this);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BabbleProcessor();
}
