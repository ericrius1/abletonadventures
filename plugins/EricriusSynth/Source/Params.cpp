#include "Params.h"
#include "WavetableFactory.h"
#include "Filters.h"
#include <unordered_map>

using namespace juce;
namespace P = aa::params;

namespace ers
{
StringArray warpNames()
{
    return { "Off", "Sync", "Window Sync", "Bend +", "Bend -", "Bend +/-", "PWM", "Asym +", "Asym -",
             "Flip", "Mirror", "Quantize", "FM", "AM", "RM" };
}

StringArray routeNames()     { return { "Filter 1", "Filter 2", "Both", "Direct" }; }
StringArray stackNames()     { return { "Off", "+12", "-12", "+12 / -12", "+7", "+12 +19", "Chord" }; }
StringArray subShapeNames()  { return { "Sine", "Rounded", "Triangle", "Saw", "Square", "Pulse" }; }

StringArray lfoDivisionNames()
{
    return { "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/2 T", "1/4 D", "1/4", "1/4 T", "1/8 D", "1/8",
             "1/8 T", "1/16 D", "1/16", "1/16 T", "1/32", "1/64" };
}

double lfoDivisionBeats (int index)
{
    static const double beats[] = { 32.0, 16.0, 8.0, 4.0, 2.0, 4.0 / 3.0, 1.5, 1.0, 2.0 / 3.0, 0.75, 0.5,
                                    1.0 / 3.0, 0.375, 0.25, 1.0 / 6.0, 0.125, 0.0625 };
    return beats[jlimit (0, (int) std::size (beats) - 1, index)];
}

//==============================================================================
namespace
{
    enum class Kind { floating, integer, choice, toggle };

    struct Spec
    {
        String id, name;
        Kind kind = Kind::floating;
        float min = 0.0f, max = 1.0f, def = 0.0f;
        P::Unit unit = P::Unit::none;
        float skew = P::noSkew;
        float step = 0.0f;
        StringArray choices;
        bool modulatable = false;
        bool degrees = false;
        bool automatable = true;
        String suffix;
    };

    const char* oscPrefix (int o) { return o == 0 ? "a" : (o == 1 ? "b" : "c"); }
    String oscName (int o)        { return "Osc " + String::charToString ((juce_wchar) ('A' + o)); }

    std::vector<Spec> buildSpecs()
    {
        std::vector<Spec> s ((size_t) vp::count);

        auto set = [&] (int index, Spec spec) { s[(size_t) index] = std::move (spec); };
        auto pct = [] (String id, String name, float def, bool mod = true)
        {
            Spec sp { id, name, Kind::floating, 0.0f, 100.0f, def, P::Unit::percent };
            sp.modulatable = mod;
            return sp;
        };
        auto bipct = [] (String id, String name, float def, bool mod = true)
        {
            Spec sp { id, name, Kind::floating, -100.0f, 100.0f, def, P::Unit::percent };
            sp.modulatable = mod;
            return sp;
        };
        auto choice = [] (String id, String name, StringArray choices, int def)
        {
            Spec sp { id, name, Kind::choice, 0.0f, (float) (choices.size() - 1), (float) def };
            sp.choices = std::move (choices);
            return sp;
        };
        auto toggle = [] (String id, String name, bool def)
        {
            return Spec { id, name, Kind::toggle, 0.0f, 1.0f, def ? 1.0f : 0.0f };
        };
        auto integer = [] (String id, String name, int min, int max, int def, String suffix = {})
        {
            Spec sp { id, name, Kind::integer, (float) min, (float) max, (float) def };
            sp.suffix = suffix;
            return sp;
        };
        auto ms = [] (String id, String name, float min, float max, float def, float centre, bool mod = true)
        {
            Spec sp { id, name, Kind::floating, min, max, def, P::Unit::ms, centre };
            sp.modulatable = mod;
            return sp;
        };
        auto degrees = [] (String id, String name, float def)
        {
            Spec sp { id, name, Kind::floating, 0.0f, 360.0f, def };
            sp.degrees = true;
            sp.modulatable = true;
            return sp;
        };

        // ---- oscillators ---------------------------------------------------------
        auto tableChoices = getFactoryTableNames();
        tableChoices.add ("Custom");
        const StringArray warpSources { "Osc A", "Osc B", "Osc C", "Sub", "Noise" };

        for (int o = 0; o < numOscs; ++o)
        {
            const String p = oscPrefix (o), n = oscName (o) + " ";
            set (vp::osc (o, OscField::on), toggle (p + "On", n + "On", o == 0));
            {
                auto t = choice (p + "Table", n + "Wavetable", tableChoices, 0);
                t.automatable = false;
                set (vp::osc (o, OscField::table), t);
            }
            set (vp::osc (o, OscField::position), pct (p + "Pos", n + "Position", o == 0 ? 50.0f : 0.0f));
            set (vp::osc (o, OscField::warp), choice (p + "Warp", n + "Warp", warpNames(), 0));
            set (vp::osc (o, OscField::warpAmount), pct (p + "WarpAmt", n + "Warp Amount", 0.0f));
            set (vp::osc (o, OscField::warpSource), choice (p + "WarpSrc", n + "Warp Source", warpSources, o == 0 ? 1 : 0));
            set (vp::osc (o, OscField::octave), integer (p + "Oct", n + "Octave", -4, 4, 0, " oct"));
            {
                Spec semi { p + "Semi", n + "Semitone", Kind::floating, -24.0f, 24.0f, 0.0f, P::Unit::semitones };
                semi.step = 1.0f;
                semi.modulatable = true;
                set (vp::osc (o, OscField::semi), semi);
            }
            {
                Spec fine { p + "Fine", n + "Fine", Kind::floating, -100.0f, 100.0f, 0.0f, P::Unit::cents };
                fine.modulatable = true;
                set (vp::osc (o, OscField::fine), fine);
            }
            set (vp::osc (o, OscField::unison), integer (p + "Unison", n + "Unison", 1, maxUnison, 1, " voices"));
            set (vp::osc (o, OscField::detune), pct (p + "Detune", n + "Detune", 25.0f));
            set (vp::osc (o, OscField::blend), pct (p + "Blend", n + "Blend", 75.0f));
            set (vp::osc (o, OscField::width), pct (p + "Width", n + "Width", 100.0f));
            set (vp::osc (o, OscField::phase), degrees (p + "Phase", n + "Phase", 0.0f));
            set (vp::osc (o, OscField::random), pct (p + "Rand", n + "Phase Random", 100.0f));
            set (vp::osc (o, OscField::level), pct (p + "Level", n + "Level", 75.0f));
            set (vp::osc (o, OscField::pan), bipct (p + "Pan", n + "Pan", 0.0f));
            set (vp::osc (o, OscField::route), choice (p + "Route", n + "Route", routeNames(), 0));
            set (vp::osc (o, OscField::stack), choice (p + "Stack", n + "Unison Stack", stackNames(), 0));
        }

        // ---- sub + noise -----------------------------------------------------------
        set (vp::sub (vp::subOn), toggle ("subOn", "Sub On", false));
        set (vp::sub (vp::subShape), choice ("subShape", "Sub Shape", subShapeNames(), 0));
        set (vp::sub (vp::subOctave), integer ("subOct", "Sub Octave", -4, 2, -1, " oct"));
        set (vp::sub (vp::subLevel), pct ("subLevel", "Sub Level", 75.0f));
        set (vp::sub (vp::subPan), bipct ("subPan", "Sub Pan", 0.0f));
        set (vp::sub (vp::subRoute), choice ("subRoute", "Sub Route", routeNames(), 0));

        {
            auto t = choice ("noiseType", "Noise Type", getNoiseNames(), 0);
            t.automatable = false;
            set (vp::noise (vp::noiseType), t);
        }
        set (vp::noise (vp::noiseOn), toggle ("noiseOn", "Noise On", false));
        set (vp::noise (vp::noisePitch), pct ("noisePitch", "Noise Pitch", 50.0f));
        set (vp::noise (vp::noiseLevel), pct ("noiseLevel", "Noise Level", 25.0f));
        set (vp::noise (vp::noisePan), bipct ("noisePan", "Noise Pan", 0.0f));
        set (vp::noise (vp::noiseRoute), choice ("noiseRoute", "Noise Route", routeNames(), 0));
        set (vp::noise (vp::noiseKeyTrack), toggle ("noiseKey", "Noise Key Track", false));

        // ---- filters ---------------------------------------------------------------
        for (int f = 0; f < numFilters; ++f)
        {
            const String p = "f" + String (f + 1), n = "Filter " + String (f + 1) + " ";
            set (vp::filter (f, FilterField::on), toggle (p + "On", n + "On", false));
            set (vp::filter (f, FilterField::type), choice (p + "Type", n + "Type", filterTypeNames(), f == 0 ? 1 : 0));
            {
                Spec c { p + "Cutoff", n + "Cutoff", Kind::floating, 20.0f, 20000.0f, f == 0 ? 2500.0f : 6000.0f,
                         P::Unit::hz, 1000.0f };
                c.modulatable = true;
                set (vp::filter (f, FilterField::cutoff), c);
            }
            set (vp::filter (f, FilterField::resonance), pct (p + "Reso", n + "Resonance", 15.0f));
            set (vp::filter (f, FilterField::drive), pct (p + "Drive", n + "Drive", 0.0f));
            set (vp::filter (f, FilterField::var), pct (p + "Var", n + "Var", 0.0f));
            set (vp::filter (f, FilterField::mix), pct (p + "Mix", n + "Mix", 100.0f));
            set (vp::filter (f, FilterField::keyTrack), pct (p + "Key", n + "Key Track", 0.0f));
        }
        set (vp::filterRouting, choice ("filterRouting", "Filter Routing", { "Serial", "Parallel" }, 0));

        // ---- envelopes -------------------------------------------------------------
        for (int e = 0; e < numEnvs; ++e)
        {
            const String p = "e" + String (e + 1), n = "Env " + String (e + 1) + " ";
            const bool amp = e == 0;
            set (vp::env (e, EnvField::attack), ms (p + "Attack", n + "Attack", 0.0f, 32000.0f, amp ? 2.0f : 1.0f, 600.0f));
            set (vp::env (e, EnvField::hold), ms (p + "Hold", n + "Hold", 0.0f, 32000.0f, 0.0f, 600.0f));
            set (vp::env (e, EnvField::decay), ms (p + "Decay", n + "Decay", 1.0f, 32000.0f, amp ? 1000.0f : 600.0f, 800.0f));
            set (vp::env (e, EnvField::sustain), pct (p + "Sustain", n + "Sustain", amp ? 100.0f : 0.0f));
            set (vp::env (e, EnvField::release), ms (p + "Release", n + "Release", 1.0f, 32000.0f, amp ? 60.0f : 300.0f, 800.0f));
            set (vp::env (e, EnvField::attackCurve), bipct (p + "ACurve", n + "Attack Curve", 0.0f, false));
            set (vp::env (e, EnvField::decayCurve), bipct (p + "DCurve", n + "Decay Curve", 50.0f, false));
            set (vp::env (e, EnvField::releaseCurve), bipct (p + "RCurve", n + "Release Curve", 50.0f, false));
        }

        // ---- LFOs ------------------------------------------------------------------
        for (int l = 0; l < numLfos; ++l)
        {
            const String p = "l" + String (l + 1), n = "LFO " + String (l + 1) + " ";
            {
                Spec r { p + "Rate", n + "Rate", Kind::floating, 0.01f, 40.0f, 1.0f, P::Unit::hz, 2.0f };
                r.modulatable = true;
                set (vp::lfo (l, LfoField::rate), r);
            }
            set (vp::lfo (l, LfoField::sync), toggle (p + "Sync", n + "Sync", true));
            set (vp::lfo (l, LfoField::division), choice (p + "Div", n + "Division", lfoDivisionNames(), 7));
            set (vp::lfo (l, LfoField::mode), choice (p + "Mode", n + "Mode", { "Trigger", "Free", "Envelope" }, 0));
            set (vp::lfo (l, LfoField::rise), ms (p + "Rise", n + "Rise", 0.0f, 10000.0f, 0.0f, 800.0f));
            set (vp::lfo (l, LfoField::delay), ms (p + "Delay", n + "Delay", 0.0f, 10000.0f, 0.0f, 800.0f));
            set (vp::lfo (l, LfoField::smooth), pct (p + "Smooth", n + "Smooth", 0.0f));
            set (vp::lfo (l, LfoField::phase), degrees (p + "Phase", n + "Phase", 0.0f));
        }

        // ---- macros + global -------------------------------------------------------
        for (int m = 0; m < numMacros; ++m)
            set (vp::macro (m), pct ("m" + String (m + 1), "Macro " + String (m + 1), 0.0f, false));

        set (vp::global (vp::voiceMode), choice ("voiceMode", "Voice Mode", { "Poly", "Mono", "Legato" }, 0));
        set (vp::global (vp::polyphony), integer ("polyphony", "Polyphony", 1, maxVoices, 16, " voices"));
        set (vp::global (vp::glide), ms ("glide", "Glide", 0.0f, 5000.0f, 0.0f, 300.0f, false));
        set (vp::global (vp::legatoGlide), toggle ("legatoGlide", "Glide Legato Only", false));
        set (vp::global (vp::bendRange), integer ("bendRange", "Pitch Bend Range", 0, 24, 2, " st"));
        set (vp::global (vp::velocity), pct ("velocity", "Velocity Sensitivity", 70.0f, false));
        {
            Spec t { "masterTune", "Master Tune", Kind::floating, -100.0f, 100.0f, 0.0f, P::Unit::cents };
            set (vp::global (vp::masterTune), t);
        }
        set (vp::global (vp::volume), Spec { "volume", "Volume", Kind::floating, -48.0f, 12.0f, 0.0f, P::Unit::db });

        for (int i = 0; i < vp::count; ++i)
            jassert (s[(size_t) i].id.isNotEmpty());

        return s;
    }

    const std::vector<Spec>& specs()
    {
        static const std::vector<Spec> s = buildSpecs();
        return s;
    }

    std::unique_ptr<RangedAudioParameter> makeParameter (const Spec& s)
    {
        switch (s.kind)
        {
            case Kind::choice:
                return std::make_unique<AudioParameterChoice> (ParameterID { s.id, 1 }, s.name, s.choices, (int) s.def,
                                                               AudioParameterChoiceAttributes().withAutomatable (s.automatable));
            case Kind::toggle:
                return P::toggle (s.id, s.name, s.def > 0.5f);
            case Kind::integer:
            {
                const String suffix = s.suffix;
                const bool signedDisplay = s.min < 0.0f;
                return std::make_unique<AudioParameterInt> (
                    ParameterID { s.id, 1 }, s.name, (int) s.min, (int) s.max, (int) s.def,
                    AudioParameterIntAttributes()
                        .withStringFromValueFunction ([suffix, signedDisplay] (int v, int)
                        {
                            return (signedDisplay && v > 0 ? "+" : "") + String (v) + suffix;
                        })
                        .withValueFromStringFunction ([] (const String& t)
                        {
                            return roundToInt (t.retainCharacters ("0123456789.-").getFloatValue());
                        }));
            }
            case Kind::floating:
                break;
        }

        if (s.degrees)
        {
            return std::make_unique<AudioParameterFloat> (
                ParameterID { s.id, 1 }, s.name, NormalisableRange<float> (s.min, s.max), s.def,
                AudioParameterFloatAttributes()
                    .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + juce_wchar (0x00b0); })
                    .withValueFromStringFunction ([] (const String& t)
                    {
                        return t.retainCharacters ("0123456789.-").getFloatValue();
                    }));
        }

        return P::floatParam (s.id, s.name, s.min, s.max, s.def, s.unit, s.skew, s.step);
    }

    std::vector<ModDest> buildModDests()
    {
        std::vector<ModDest> d;
        for (int i = 0; i < vp::count; ++i)
        {
            const auto& s = specs()[(size_t) i];
            if (s.modulatable)
                d.push_back ({ s.id, s.name, i, -1 });
        }

        const auto& fxSpecs = fx::paramSpecs();
        for (int i = 0; i < (int) fxSpecs.size(); ++i)
            if (fxSpecs[(size_t) i].modulatable && fxSpecs[(size_t) i].type == fx::ParamSpec::Type::floating)
                d.push_back ({ fxSpecs[(size_t) i].id, fxSpecs[(size_t) i].name, -1, i });

        return d;
    }
} // namespace

String vpId (int index)
{
    return isPositiveAndBelow (index, vp::count) ? specs()[(size_t) index].id : String();
}

//==============================================================================
String modSourceName (ModSource s)
{
    switch (s)
    {
        case ModSource::none:            return "None";
        case ModSource::env1:            return "Env 1";
        case ModSource::env2:            return "Env 2";
        case ModSource::env3:            return "Env 3";
        case ModSource::env4:            return "Env 4";
        case ModSource::lfo1:            return "LFO 1";
        case ModSource::lfo2:            return "LFO 2";
        case ModSource::lfo3:            return "LFO 3";
        case ModSource::lfo4:            return "LFO 4";
        case ModSource::macro1:          return "Macro 1";
        case ModSource::macro2:          return "Macro 2";
        case ModSource::macro3:          return "Macro 3";
        case ModSource::macro4:          return "Macro 4";
        case ModSource::velocity:        return "Velocity";
        case ModSource::note:            return "Note";
        case ModSource::modWheel:        return "Mod Wheel";
        case ModSource::aftertouch:      return "Aftertouch";
        case ModSource::pitchBend:       return "Pitch Bend";
        case ModSource::random:          return "Random";
        case ModSource::alternate:       return "Alternate";
        case ModSource::releaseVelocity: return "Release Vel";
        case ModSource::count:           break;
    }
    return {};
}

String modSourceShortName (ModSource s)
{
    switch (s)
    {
        case ModSource::env1: return "E1";
        case ModSource::env2: return "E2";
        case ModSource::env3: return "E3";
        case ModSource::env4: return "E4";
        case ModSource::lfo1: return "L1";
        case ModSource::lfo2: return "L2";
        case ModSource::lfo3: return "L3";
        case ModSource::lfo4: return "L4";
        case ModSource::macro1: return "M1";
        case ModSource::macro2: return "M2";
        case ModSource::macro3: return "M3";
        case ModSource::macro4: return "M4";
        case ModSource::velocity: return "VEL";
        case ModSource::note: return "NOTE";
        case ModSource::modWheel: return "MW";
        case ModSource::aftertouch: return "AT";
        case ModSource::pitchBend: return "PB";
        case ModSource::random: return "RND";
        case ModSource::alternate: return "ALT";
        case ModSource::releaseVelocity: return "RVEL";
        case ModSource::none:
        case ModSource::count: break;
    }
    return {};
}

bool isBipolarSource (ModSource s)
{
    // LFOs are unipolar (0..1, as drawn); a slot's "bipolar" switch centres them.
    return s == ModSource::note || s == ModSource::pitchBend || s == ModSource::random || s == ModSource::alternate;
}

const std::vector<ModDest>& modDests()
{
    static const std::vector<ModDest> d = buildModDests();
    return d;
}

int modDestIndex (const String& paramId)
{
    static const auto map = []
    {
        std::unordered_map<std::string, int> m;
        const auto& d = modDests();
        for (int i = 0; i < (int) d.size(); ++i)
            m[d[(size_t) i].paramId.toStdString()] = i;
        return m;
    }();
    const auto it = map.find (paramId.toStdString());
    return it == map.end() ? -1 : it->second;
}

int modDestForVp (int vpIndex)
{
    static const auto table = []
    {
        std::vector<int> t ((size_t) vp::count, -1);
        const auto& d = modDests();
        for (int i = 0; i < (int) d.size(); ++i)
            if (d[(size_t) i].vpIndex >= 0)
                t[(size_t) d[(size_t) i].vpIndex] = i;
        return t;
    }();
    return isPositiveAndBelow (vpIndex, vp::count) ? table[(size_t) vpIndex] : -1;
}

//==============================================================================
const std::vector<int>& vpLayoutOrder()
{
    static const std::vector<int> order = []
    {
        // Push shows parameters in banks of eight: make the first bank the most playable.
        std::vector<int> first { vp::macro (0), vp::macro (1), vp::macro (2), vp::macro (3),
                                 vp::filter (0, FilterField::cutoff), vp::filter (0, FilterField::resonance),
                                 vp::osc (0, OscField::position), vp::osc (1, OscField::position) };
        std::vector<int> o = first;
        for (int i = 0; i < vp::count; ++i)
            if (std::find (first.begin(), first.end(), i) == first.end())
                o.push_back (i);
        return o;
    }();
    return order;
}

AudioProcessorValueTreeState::ParameterLayout createLayout()
{
   #if JUCE_DEBUG
    {
        // JUCE's VST3 wrapper derives parameter IDs from String::hashCode(), so two different
        // ids with the same hash would silently share one host parameter. Catch that early.
        std::unordered_map<int, String> seen;
        auto check = [&seen] (const String& id)
        {
            const int h = id.hashCode() & 0x7fffffff;
            jassert (seen.find (h) == seen.end());   // hash collision: rename one of the ids
            seen[h] = id;
        };
        for (int i = 0; i < vp::count; ++i)
            check (specs()[(size_t) i].id);
        for (const auto& s : fx::paramSpecs())
            check (s.id);
    }
   #endif

    P::Layout layout;
    for (int index : vpLayoutOrder())
        layout.add (makeParameter (specs()[(size_t) index]));

    for (const auto& s : fx::paramSpecs())
    {
        switch (s.type)
        {
            case fx::ParamSpec::Type::choice: layout.add (P::choice (s.id, s.name, s.choices, (int) s.def)); break;
            case fx::ParamSpec::Type::toggle: layout.add (P::toggle (s.id, s.name, s.def > 0.5f)); break;
            case fx::ParamSpec::Type::floating:
                if (s.id.endsWith ("Stereo"))
                {
                    // stereo phase offsets read better in degrees
                    Spec deg { s.id, s.name, Kind::floating, s.min, s.max, s.def };
                    deg.degrees = true;
                    layout.add (makeParameter (deg));
                }
                else
                    layout.add (P::floatParam (s.id, s.name, s.min, s.max, s.def, s.unit, s.skewCentre, s.step));
                break;
        }
    }
    return layout;
}
} // namespace ers
