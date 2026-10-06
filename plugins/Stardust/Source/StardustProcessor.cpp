#include "StardustProcessor.h"
#include "StardustEditor.h"

using namespace juce;
namespace P = aa::params;

namespace
{
    float parseNumber (const String& s)
    {
        return s.retainCharacters ("0123456789.-").getFloatValue();
    }

    /** Frequency parameter whose text parser understands "1.2 kHz" as well as "1200". */
    std::unique_ptr<AudioParameterFloat> hzParam (const String& id, const String& name, float min, float max,
                                                  float def, float centre)
    {
        NormalisableRange<float> range (min, max);
        range.setSkewForCentre (centre);
        return std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, range, def,
            AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return P::formatValue (v, P::Unit::hz); })
                .withValueFromStringFunction ([] (const String& s)
                {
                    const float v = parseNumber (s);
                    return s.containsIgnoreCase ("k") ? v * 1000.0f : v;
                }));
    }

    /** Time parameter in ms whose text parser understands "1.50 s" as well as "350 ms". */
    std::unique_ptr<AudioParameterFloat> msParam (const String& id, const String& name, float min, float max,
                                                  float def, float centre)
    {
        NormalisableRange<float> range (min, max);
        range.setSkewForCentre (centre);
        return std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, range, def,
            AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return P::formatValue (v, P::Unit::ms); })
                .withValueFromStringFunction ([] (const String& s)
                {
                    const float v = parseNumber (s);
                    const auto t = s.trim().toLowerCase();
                    return (t.endsWith ("s") && ! t.endsWith ("ms")) ? v * 1000.0f : v;
                }));
    }

    const StringArray& shapeNames()
    {
        static const StringArray names { "Sine", "Triangle", "Saw", "Square" };
        return names;
    }

    String shapeToText (float v)
    {
        static const char* shortNames[] = { "Sine", "Tri", "Saw", "Square" };
        v = jlimit (0.0f, 3.0f, v);
        const int i = jmin (2, (int) v);
        const float f = v - (float) i;
        if (f < 0.02f) return shapeNames()[i];
        if (f > 0.98f) return shapeNames()[i + 1];
        return String (shortNames[i]) + "/" + shortNames[i + 1] + " " + String (roundToInt (f * 100.0f)) + "%";
    }

    float textToShape (const String& s)
    {
        const auto t = s.trim();
        for (int i = 0; i < 4; ++i)
        {
            const String shortName = i == 1 ? "Tri" : shapeNames()[i];
            if (t.startsWithIgnoreCase (shortName))
            {
                if (t.containsChar ('%'))
                    return jlimit (0.0f, 3.0f, (float) i + parseNumber (t.fromLastOccurrenceOf (" ", false, false)) / 100.0f);
                return (float) i;
            }
        }
        return jlimit (0.0f, 3.0f, parseNumber (t));
    }

    std::unique_ptr<AudioParameterFloat> shapeParam (const String& id, const String& name, float def)
    {
        return std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 3.0f), def,
            AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return shapeToText (v); })
                .withValueFromStringFunction ([] (const String& s) { return textToShape (s); }));
    }

    /** Integer parameter shown with an explicit sign ("+1 oct", "-7 st"). */
    std::unique_ptr<AudioParameterInt> signedInt (const String& id, const String& name, int min, int max, int def,
                                                  const String& suffix)
    {
        return std::make_unique<AudioParameterInt> (
            ParameterID { id, 1 }, name, min, max, def,
            AudioParameterIntAttributes()
                .withStringFromValueFunction ([suffix] (int v, int) { return (v > 0 ? "+" : "") + String (v) + suffix; })
                .withValueFromStringFunction ([] (const String& s) { return roundToInt (parseNumber (s)); }));
    }

    inline float safetyClip (float x)
    {
        const float a = std::abs (x);
        if (a <= 0.8f)
            return x;
        const float y = 0.8f + 0.2f * std::tanh ((a - 0.8f) * 5.0f);
        return x < 0.0f ? -y : y;
    }
} // namespace

//==============================================================================
AudioProcessorValueTreeState::ParameterLayout StardustProcessor::createLayout()
{
    P::Layout layout;

    // The first eight are the most playable ones (Push shows parameters in banks of eight).
    layout.add (hzParam ("cutoff", "Cutoff", 20.0f, 20000.0f, 2200.0f, 1000.0f));
    layout.add (P::percent ("resonance", "Resonance", 18.0f));
    layout.add (P::floatParam ("filterEnv", "Filter Env", -100.0f, 100.0f, 30.0f, P::Unit::percent));
    layout.add (shapeParam ("shapeA", "Osc A Shape", 2.0f));
    layout.add (P::percent ("detune", "Detune", 30.0f));
    layout.add (P::percent ("twinkle", "Twinkle", 35.0f));
    layout.add (P::floatParam ("gravity", "Gravity", -100.0f, 100.0f, 20.0f, P::Unit::percent));
    layout.add (P::percent ("spaceMix", "Space Mix", 32.0f));

    // Amp envelope
    layout.add (msParam ("aAttack", "Amp Attack", 1.0f, 10000.0f, 450.0f, 400.0f));
    layout.add (msParam ("aDecay", "Amp Decay", 5.0f, 10000.0f, 1500.0f, 600.0f));
    layout.add (P::percent ("aSustain", "Amp Sustain", 85.0f));
    layout.add (msParam ("aRelease", "Amp Release", 5.0f, 15000.0f, 2400.0f, 1200.0f));

    // Filter
    layout.add (P::percent ("drive", "Drive", 12.0f));
    layout.add (P::percent ("keyTrack", "Key Track", 40.0f));
    layout.add (P::choice ("filterType", "Filter Type", { "Low-pass", "Band-pass", "High-pass" }, 0));
    layout.add (msParam ("fAttack", "Filter Attack", 1.0f, 10000.0f, 800.0f, 400.0f));
    layout.add (msParam ("fDecay", "Filter Decay", 5.0f, 10000.0f, 2500.0f, 600.0f));
    layout.add (P::percent ("fSustain", "Filter Sustain", 35.0f));
    layout.add (msParam ("fRelease", "Filter Release", 5.0f, 15000.0f, 2500.0f, 1200.0f));

    // Oscillator A (the supersaw)
    layout.add (P::integer ("unison", "Unison", 1, stardust::maxUnison, 7, " voices"));
    layout.add (P::percent ("spread", "Spread", 85.0f));
    layout.add (P::percent ("levelA", "Osc A Level", 80.0f));
    layout.add (signedInt ("octaveA", "Osc A Octave", -2, 2, 0, " oct"));

    // Oscillator B, sub, noise
    layout.add (shapeParam ("shapeB", "Osc B Shape", 1.0f));
    layout.add (signedInt ("octaveB", "Osc B Octave", -3, 3, 1, " oct"));
    layout.add (signedInt ("semiB", "Osc B Semitone", -12, 12, 0, " st"));
    layout.add (P::floatParam ("fineB", "Osc B Fine", -100.0f, 100.0f, 5.0f, P::Unit::cents));
    layout.add (P::percent ("levelB", "Osc B Level", 30.0f));
    layout.add (P::percent ("sub", "Sub", 20.0f));
    layout.add (P::percent ("noise", "Noise", 0.0f));

    // LFO
    layout.add (P::choice ("lfoShape", "LFO Shape", { "Sine", "Triangle", "Saw", "Square", "S&H", "Smooth" }, 0));
    layout.add (P::floatParam ("lfoRate", "LFO Rate", 0.02f, 20.0f, 0.25f, P::Unit::hz, 1.5f));
    layout.add (P::toggle ("lfoSync", "LFO Sync", false));
    layout.add (P::choice ("lfoDivision", "LFO Division", stardust::lfoDivisionNames(), 2));
    layout.add (P::percent ("lfoPitch", "LFO > Pitch", 0.0f));
    layout.add (P::floatParam ("lfoCutoff", "LFO > Cutoff", -100.0f, 100.0f, 12.0f, P::Unit::percent));
    layout.add (P::percent ("lfoShapeMod", "LFO > Shape", 0.0f));

    // Character + voicing
    layout.add (P::percent ("drift", "Drift", 30.0f));
    layout.add (P::choice ("voiceMode", "Voice Mode", { "Poly", "Mono", "Legato" }, 0));
    layout.add (msParam ("glide", "Glide", 0.0f, 3000.0f, 0.0f, 300.0f));
    layout.add (P::percent ("velocity", "Velocity", 40.0f));

    // FX + output
    layout.add (P::percent ("chorus", "Chorus", 35.0f));
    layout.add (P::percent ("spaceSize", "Space Size", 65.0f));
    layout.add (P::floatParam ("volume", "Volume", -40.0f, 6.0f, 0.0f, P::Unit::db));
    return layout;
}

StardustProcessor::StardustProcessor()
    : aa::PluginBase (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true), createLayout())
{
    pCutoff = raw ("cutoff");
    pResonance = raw ("resonance");
    pFilterEnv = raw ("filterEnv");
    pShapeA = raw ("shapeA");
    pDetune = raw ("detune");
    pTwinkle = raw ("twinkle");
    pGravity = raw ("gravity");
    pSpaceMix = raw ("spaceMix");
    pAAttack = raw ("aAttack");
    pADecay = raw ("aDecay");
    pASustain = raw ("aSustain");
    pARelease = raw ("aRelease");
    pDrive = raw ("drive");
    pKeyTrack = raw ("keyTrack");
    pFilterType = raw ("filterType");
    pFAttack = raw ("fAttack");
    pFDecay = raw ("fDecay");
    pFSustain = raw ("fSustain");
    pFRelease = raw ("fRelease");
    pUnison = raw ("unison");
    pSpread = raw ("spread");
    pLevelA = raw ("levelA");
    pOctaveA = raw ("octaveA");
    pShapeB = raw ("shapeB");
    pOctaveB = raw ("octaveB");
    pSemiB = raw ("semiB");
    pFineB = raw ("fineB");
    pLevelB = raw ("levelB");
    pSub = raw ("sub");
    pNoise = raw ("noise");
    pLfoShape = raw ("lfoShape");
    pLfoRate = raw ("lfoRate");
    pLfoSync = raw ("lfoSync");
    pLfoDivision = raw ("lfoDivision");
    pLfoPitch = raw ("lfoPitch");
    pLfoCutoff = raw ("lfoCutoff");
    pLfoShapeMod = raw ("lfoShapeMod");
    pDrift = raw ("drift");
    pVoiceMode = raw ("voiceMode");
    pGlide = raw ("glide");
    pVelocity = raw ("velocity");
    pChorus = raw ("chorus");
    pSpaceSize = raw ("spaceSize");
    pVolume = raw ("volume");

    tailSeconds = 15.0;

    setFactoryPresets ({
        { "Stardust Pad", {} },
        { "Nebula Strings", { { "cutoff", 4200.0f }, { "resonance", 8.0f }, { "filterEnv", 12.0f }, { "detune", 22.0f },
                              { "spread", 100.0f }, { "shapeB", 2.0f }, { "octaveB", 0.0f }, { "fineB", -9.0f },
                              { "levelB", 35.0f }, { "sub", 0.0f }, { "aAttack", 380.0f }, { "aDecay", 2000.0f },
                              { "aSustain", 90.0f }, { "aRelease", 1600.0f }, { "fAttack", 500.0f }, { "lfoPitch", 15.0f },
                              { "lfoRate", 4.8f }, { "lfoCutoff", 0.0f }, { "twinkle", 20.0f }, { "drift", 40.0f },
                              { "gravity", 0.0f }, { "chorus", 55.0f }, { "spaceSize", 70.0f }, { "spaceMix", 34.0f } } },
        { "Supernova Lead", { { "voiceMode", 2.0f }, { "glide", 90.0f }, { "shapeA", 2.35f }, { "unison", 5.0f },
                              { "detune", 18.0f }, { "spread", 60.0f }, { "shapeB", 3.0f }, { "octaveB", 1.0f },
                              { "fineB", 4.0f }, { "levelB", 28.0f }, { "sub", 25.0f }, { "cutoff", 2400.0f },
                              { "resonance", 32.0f }, { "drive", 40.0f }, { "filterEnv", 45.0f }, { "fAttack", 2.0f },
                              { "fDecay", 600.0f }, { "fSustain", 30.0f }, { "fRelease", 400.0f }, { "aAttack", 3.0f },
                              { "aDecay", 800.0f }, { "aSustain", 90.0f }, { "aRelease", 320.0f }, { "lfoPitch", 14.0f },
                              { "lfoRate", 5.5f }, { "lfoCutoff", 0.0f }, { "velocity", 55.0f }, { "twinkle", 25.0f },
                              { "drift", 20.0f }, { "gravity", 0.0f }, { "chorus", 25.0f }, { "spaceSize", 55.0f },
                              { "spaceMix", 22.0f } } },
        { "Cosmic Bass", { { "voiceMode", 1.0f }, { "glide", 40.0f }, { "octaveA", -1.0f }, { "unison", 3.0f },
                           { "detune", 12.0f }, { "spread", 30.0f }, { "shapeB", 3.0f }, { "octaveB", -1.0f },
                           { "fineB", 0.0f }, { "levelB", 45.0f }, { "sub", 60.0f }, { "cutoff", 380.0f },
                           { "resonance", 30.0f }, { "drive", 45.0f }, { "filterEnv", 55.0f }, { "keyTrack", 30.0f },
                           { "fAttack", 1.0f }, { "fDecay", 350.0f }, { "fSustain", 10.0f }, { "fRelease", 200.0f },
                           { "aAttack", 1.0f }, { "aDecay", 500.0f }, { "aSustain", 85.0f }, { "aRelease", 120.0f },
                           { "lfoCutoff", 0.0f }, { "velocity", 50.0f }, { "twinkle", 0.0f }, { "drift", 10.0f },
                           { "gravity", 0.0f }, { "chorus", 0.0f }, { "spaceSize", 30.0f }, { "spaceMix", 6.0f } } },
        { "Aurora Bells", { { "shapeA", 0.6f }, { "unison", 3.0f }, { "detune", 10.0f }, { "spread", 70.0f },
                            { "shapeB", 0.0f }, { "octaveB", 2.0f }, { "semiB", 7.0f }, { "fineB", 3.0f },
                            { "levelB", 45.0f }, { "sub", 0.0f }, { "cutoff", 6000.0f }, { "resonance", 10.0f },
                            { "filterEnv", 20.0f }, { "fAttack", 1.0f }, { "fDecay", 1200.0f }, { "fSustain", 0.0f },
                            { "aAttack", 2.0f }, { "aDecay", 2600.0f }, { "aSustain", 0.0f }, { "aRelease", 2600.0f },
                            { "velocity", 70.0f }, { "lfoCutoff", 0.0f }, { "twinkle", 70.0f }, { "drift", 15.0f },
                            { "gravity", 0.0f }, { "chorus", 30.0f }, { "spaceSize", 80.0f }, { "spaceMix", 40.0f } } },
        { "Galactic Pluck", { { "unison", 5.0f }, { "detune", 25.0f }, { "levelB", 20.0f }, { "cutoff", 700.0f },
                              { "resonance", 30.0f }, { "filterEnv", 65.0f }, { "fAttack", 1.0f }, { "fDecay", 280.0f },
                              { "fSustain", 0.0f }, { "fRelease", 300.0f }, { "aAttack", 1.0f }, { "aDecay", 900.0f },
                              { "aSustain", 0.0f }, { "aRelease", 500.0f }, { "velocity", 60.0f }, { "lfoCutoff", 0.0f },
                              { "twinkle", 30.0f }, { "gravity", 0.0f }, { "chorus", 35.0f }, { "spaceSize", 55.0f },
                              { "spaceMix", 28.0f } } },
        { "Black Hole Drone", { { "shapeA", 2.6f }, { "detune", 55.0f }, { "shapeB", 2.0f }, { "octaveB", -2.0f },
                                { "levelB", 50.0f }, { "sub", 45.0f }, { "noise", 8.0f }, { "cutoff", 450.0f },
                                { "resonance", 45.0f }, { "drive", 35.0f }, { "filterEnv", 0.0f }, { "lfoShape", 5.0f },
                                { "lfoRate", 0.08f }, { "lfoCutoff", 45.0f }, { "lfoShapeMod", 40.0f },
                                { "aAttack", 3000.0f }, { "aSustain", 100.0f }, { "aRelease", 8000.0f },
                                { "twinkle", 15.0f }, { "drift", 70.0f }, { "gravity", -80.0f }, { "chorus", 40.0f },
                                { "spaceSize", 100.0f }, { "spaceMix", 48.0f }, { "volume", -5.0f } } },
        { "Comet Tail", { { "glide", 250.0f }, { "detune", 34.0f }, { "cutoff", 1800.0f }, { "filterEnv", 40.0f },
                          { "fAttack", 60.0f }, { "fDecay", 1800.0f }, { "fSustain", 25.0f }, { "fRelease", 5000.0f },
                          { "aAttack", 120.0f }, { "aRelease", 5000.0f }, { "lfoCutoff", 0.0f }, { "twinkle", 55.0f },
                          { "drift", 35.0f }, { "gravity", 70.0f }, { "chorus", 40.0f }, { "spaceSize", 90.0f },
                          { "spaceMix", 42.0f } } },
        { "Satellite Arp Pluck", { { "shapeA", 2.8f }, { "unison", 2.0f }, { "detune", 8.0f }, { "spread", 70.0f },
                                   { "shapeB", 1.0f }, { "octaveB", 1.0f }, { "levelB", 30.0f }, { "sub", 10.0f },
                                   { "cutoff", 1500.0f }, { "resonance", 40.0f }, { "filterEnv", 50.0f },
                                   { "fAttack", 1.0f }, { "fDecay", 180.0f }, { "fSustain", 0.0f }, { "fRelease", 220.0f },
                                   { "aAttack", 1.0f }, { "aDecay", 300.0f }, { "aSustain", 0.0f }, { "aRelease", 250.0f },
                                   { "lfoSync", 1.0f }, { "lfoDivision", 10.0f }, { "lfoShape", 4.0f },
                                   { "lfoCutoff", 20.0f }, { "velocity", 60.0f }, { "twinkle", 40.0f }, { "drift", 15.0f },
                                   { "gravity", 0.0f }, { "chorus", 20.0f }, { "spaceSize", 45.0f }, { "spaceMix", 30.0f } } },
        { "Moon Choir", { { "shapeA", 1.6f }, { "detune", 20.0f }, { "spread", 100.0f }, { "levelA", 90.0f },
                          { "shapeB", 0.0f }, { "octaveB", 1.0f }, { "levelB", 30.0f }, { "sub", 10.0f }, { "noise", 12.0f },
                          { "filterType", 1.0f }, { "cutoff", 1300.0f }, { "resonance", 30.0f }, { "filterEnv", 10.0f },
                          { "keyTrack", 50.0f }, { "lfoPitch", 14.0f }, { "lfoRate", 5.0f }, { "lfoCutoff", 0.0f },
                          { "aAttack", 600.0f }, { "aRelease", 2500.0f }, { "twinkle", 30.0f }, { "drift", 45.0f }, { "volume", 3.5f },
                          { "gravity", -30.0f }, { "chorus", 70.0f }, { "spaceSize", 85.0f }, { "spaceMix", 40.0f } } },
        { "Warp Drive Sweep", { { "detune", 45.0f }, { "cutoff", 320.0f }, { "resonance", 55.0f }, { "drive", 30.0f },
                                { "filterEnv", 0.0f }, { "lfoShape", 2.0f }, { "lfoSync", 1.0f }, { "lfoDivision", 1.0f },
                                { "lfoCutoff", -75.0f }, { "lfoShapeMod", 20.0f }, { "aAttack", 200.0f },
                                { "aRelease", 3000.0f }, { "twinkle", 50.0f }, { "drift", 30.0f }, { "gravity", 40.0f },
                                { "chorus", 30.0f }, { "spaceSize", 75.0f }, { "spaceMix", 36.0f }, { "volume", -4.0f } } },
        { "Starlight Keys", { { "shapeA", 1.4f }, { "unison", 3.0f }, { "detune", 10.0f }, { "spread", 60.0f },
                              { "shapeB", 0.0f }, { "octaveB", 1.0f }, { "levelB", 30.0f }, { "sub", 15.0f },
                              { "cutoff", 3000.0f }, { "resonance", 12.0f }, { "filterEnv", 30.0f }, { "fAttack", 2.0f },
                              { "fDecay", 800.0f }, { "fSustain", 20.0f }, { "aAttack", 2.0f }, { "aDecay", 1500.0f },
                              { "aSustain", 40.0f }, { "aRelease", 900.0f }, { "velocity", 80.0f }, { "lfoCutoff", 0.0f },
                              { "twinkle", 35.0f }, { "drift", 20.0f }, { "gravity", 0.0f }, { "chorus", 30.0f },
                              { "spaceSize", 55.0f }, { "spaceMix", 26.0f } } },
    });
}

//==============================================================================
stardust::Settings StardustProcessor::readSettings() const
{
    stardust::Settings s;
    s.shapeA = pShapeA->load();
    s.detune = pDetune->load() / 100.0f;
    s.spread = pSpread->load() / 100.0f;
    s.levelA = pLevelA->load() / 100.0f;
    s.unison = (int) pUnison->load();
    s.octaveA = (int) pOctaveA->load();
    s.shapeB = pShapeB->load();
    s.levelB = pLevelB->load() / 100.0f;
    s.fineB = pFineB->load();
    s.octaveB = (int) pOctaveB->load();
    s.semiB = (int) pSemiB->load();
    s.subLevel = pSub->load() / 100.0f;
    s.noiseLevel = pNoise->load() / 100.0f;

    s.filterType = (stardust::FilterType) jlimit (0, 2, (int) pFilterType->load());
    s.cutoff = pCutoff->load();
    s.resonance = pResonance->load() / 100.0f;
    s.drive = pDrive->load() / 100.0f;
    s.envAmount = pFilterEnv->load() / 100.0f;
    s.keyTrack = pKeyTrack->load() / 100.0f;
    s.fAttack = pFAttack->load() * 0.001f;
    s.fDecay = pFDecay->load() * 0.001f;
    s.fSustain = pFSustain->load() / 100.0f;
    s.fRelease = pFRelease->load() * 0.001f;
    s.aAttack = pAAttack->load() * 0.001f;
    s.aDecay = pADecay->load() * 0.001f;
    s.aSustain = pASustain->load() / 100.0f;
    s.aRelease = pARelease->load() * 0.001f;
    s.velocity = pVelocity->load() / 100.0f;

    s.lfoShape = jlimit (0, 5, (int) pLfoShape->load());
    s.lfoRate = pLfoRate->load();
    s.lfoSync = pLfoSync->load() > 0.5f;
    s.lfoDivision = (int) pLfoDivision->load();
    s.lfoPitch = pLfoPitch->load() / 100.0f;
    s.lfoCutoff = pLfoCutoff->load() / 100.0f;
    s.lfoShapeMod = pLfoShapeMod->load() / 100.0f;

    s.twinkle = pTwinkle->load() / 100.0f;
    s.drift = pDrift->load() / 100.0f;
    s.gravity = pGravity->load() / 100.0f;

    s.voiceMode = (stardust::VoiceMode) jlimit (0, 2, (int) pVoiceMode->load());
    s.glide = pGlide->load() * 0.001f;
    return s;
}

void StardustProcessor::prepareToPlay (double sampleRate, int)
{
    sr = (float) sampleRate;
    transport.prepare (sampleRate);
    engine.prepare (sampleRate);
    chorus.prepare (sampleRate);
    reverb.prepare (sampleRate);
    volume.reset (sampleRate, 0.03, aa::dsp::dbToGain (pVolume->load()));
    meterL.prepare (sr);
    meterR.prepare (sr);
    firstBlock = true;
    appliedSize = -1.0f;
    dcCoeff = std::exp (-aa::dsp::twoPi * 8.0f / sr);
    dcInL = dcInR = dcOutL = dcOutR = 0.0f;
    muteGain = muteTarget = 1.0f;
}

void StardustProcessor::reset()
{
    engine.reset();
    chorus.reset();
    reverb.reset();
    keyboardState.reset();
}

//==============================================================================
void StardustProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    if (numSamples == 0 || buffer.getNumChannels() == 0)
        return;

    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    transport.update (getPlayHead(), numSamples);
    engine.setTransport (transport.ppqAtBlockStart, transport.bpm);
    engine.setSettings (readSettings());

    chorusTarget = pChorus->load() / 100.0f;
    spaceMixTarget = pSpaceMix->load() / 100.0f;
    spaceSizeTarget = pSpaceSize->load() / 100.0f;
    volume.setTarget (aa::dsp::dbToGain (pVolume->load()));
    if (firstBlock)
    {
        chorusAmt = chorusTarget;
        spaceMix = spaceMixTarget;
        spaceSize = spaceSizeTarget;
        volume.snap (aa::dsp::dbToGain (pVolume->load()));
        firstBlock = false;
    }

    int pos = 0;
    for (const auto meta : midi)
    {
        const int eventPos = jlimit (0, numSamples, meta.samplePosition);
        if (eventPos > pos)
        {
            renderRange (buffer, pos, eventPos);
            pos = eventPos;
        }
        engine.handleMidi (meta.getMessage());
    }
    if (pos < numSamples)
        renderRange (buffer, pos, numSamples);

    meterL.publish();
    meterR.publish();
    outputLevelL.store (meterL.level.load(), std::memory_order_relaxed);
    outputLevelR.store (meterR.level.load(), std::memory_order_relaxed);
}

void StardustProcessor::renderRange (AudioBuffer<float>& buffer, int start, int end)
{
    const int numChannels = buffer.getNumChannels();
    float* outL = buffer.getWritePointer (0);
    float* outR = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int pos = start; pos < end;)
    {
        const int n = jmin (stardust::maxChunk, end - pos);
        engine.render (synthL.data(), synthR.data(), glintL.data(), glintR.data(), n);

        if (engine.takeFlushRequest())
            muteTarget = 0.0f;

        // DC blocker: saturating asymmetric waveforms (e.g. saw + square) can leave a little offset.
        for (int i = 0; i < n; ++i)
        {
            const float l = synthL[(size_t) i], r = synthR[(size_t) i];
            dcOutL = l - dcInL + dcCoeff * dcOutL;
            dcOutR = r - dcInR + dcCoeff * dcOutR;
            dcInL = l;
            dcInR = r;
            synthL[(size_t) i] = dcOutL;
            synthR[(size_t) i] = dcOutR;
        }

        processFx (synthL.data(), synthR.data(), n);

        int w = scopeWrite.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            const float l = synthL[(size_t) i], r = synthR[(size_t) i];
            meterL.process (l);
            meterR.process (r);
            scopeL[(size_t) w].store (l, std::memory_order_relaxed);
            scopeR[(size_t) w].store (r, std::memory_order_relaxed);
            w = (w + 1) & (scopeSize - 1);

            if (outR != nullptr)
            {
                outL[pos + i] = l;
                outR[pos + i] = r;
            }
            else
                outL[pos + i] = 0.5f * (l + r);
        }
        scopeWrite.store (w, std::memory_order_release);
        pos += n;
    }
}

void StardustProcessor::processFx (float* left, float* right, int n)
{
    // Work in small slices so that smoothed FX amounts never step audibly.
    constexpr int slice = 32;
    for (int s = 0; s < n; s += slice)
    {
        const int len = jmin (slice, n - s);
        float* l = left + s;
        float* r = right + s;
        const float* gl = glintL.data() + s;
        const float* gr = glintR.data() + s;

        const float c = 1.0f - std::exp (-(float) len / (0.05f * sr));
        chorusAmt += c * (chorusTarget - chorusAmt);
        const float mix0 = spaceMix;
        spaceMix += c * (spaceMixTarget - spaceMix);
        spaceSize += (1.0f - std::exp (-(float) len / (0.25f * sr))) * (spaceSizeTarget - spaceSize);

        if (std::abs (spaceSize - appliedSize) > 0.002f)
        {
            appliedSize = spaceSize;
            aa::dsp::FdnReverb::Params rp;
            rp.size = 0.35f + 0.65f * spaceSize;
            rp.decaySeconds = 1.2f + 15.0f * spaceSize * spaceSize;
            rp.damping = 0.5f - 0.25f * spaceSize;
            rp.predelayMs = 12.0f + 25.0f * spaceSize;
            rp.modDepth = 0.55f;
            rp.modRate = 0.33f;
            rp.width = 1.0f;
            rp.lowCutHz = 110.0f;
            reverb.setParams (rp);
        }

        chorus.process (l, r, len, 0.21f + 0.25f * chorusAmt, 0.35f + 0.55f * chorusAmt, chorusAmt);

        for (int i = 0; i < len; ++i)
        {
            const float t = (float) (i + 1) / (float) len;
            const float m = mix0 + (spaceMix - mix0) * t;
            const float wet = m;
            const float dry = 1.0f - 0.5f * m * m;
            const float glintSend = jmax (0.55f, m);

            float revL, revR;
            reverb.processSample (l[i] * wet + gl[i] * glintSend, r[i] * wet + gr[i] * glintSend, revL, revR);

            muteGain += muteTarget > muteGain ? jmin (0.002f, muteTarget - muteGain) : jmax (-0.005f, muteTarget - muteGain);
            const float g = volume.next() * muteGain;
            l[i] = safetyClip ((l[i] * dry + gl[i] * 0.6f + revL) * g);
            r[i] = safetyClip ((r[i] * dry + gr[i] * 0.6f + revR) * g);
        }

        if (muteTarget == 0.0f && muteGain <= 0.0f)
        {
            chorus.reset();
            reverb.reset();
            muteTarget = 1.0f;
        }
    }
}

AudioProcessorEditor* StardustProcessor::createEditor()
{
    return new StardustEditor (*this);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StardustProcessor();
}
