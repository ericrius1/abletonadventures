#include "Presets.h"
#include "SynthProcessor.h"

using namespace juce;
using namespace ers;

namespace
{
    struct Route
    {
        ModSource source;
        const char* dest;
        float amount;
        bool bipolar = false;
        ModSource aux = ModSource::none;
    };

    struct Def
    {
        String name;
        std::vector<std::pair<String, float>> values;
        std::vector<Route> routes;
        std::array<LfoShape::Preset, numLfos> lfo { LfoShape::Preset::sine, LfoShape::Preset::triangle,
                                                    LfoShape::Preset::sawDown, LfoShape::Preset::steps };
        StringArray macroNames;
    };

    float table (const char* name)
    {
        for (int i = 0; i < getNumFactoryTables(); ++i)
            if (getFactoryTableInfo (i).name == name)
                return (float) i;
        jassertfalse;   // unknown table name
        return 0.0f;
    }

    float division (const char* name)
    {
        const int i = lfoDivisionNames().indexOf (name);
        jassert (i >= 0);
        return (float) jmax (0, i);
    }

    float filterType (FilterType t) { return (float) (int) t; }
    float warp (Warp w)             { return (float) (int) w; }

    aa::FactoryPreset build (Def d)
    {
        aa::FactoryPreset p;
        p.name = d.name;
        p.values = d.values;
        auto routes = d.routes;
        auto lfo = d.lfo;
        auto macroNames = d.macroNames;
        p.applyExtra = [routes, lfo, macroNames] (aa::PluginBase& base)
        {
            auto& proc = static_cast<SynthProcessor&> (base);
            for (int l = 0; l < numLfos; ++l)
                proc.setLfoShape (l, LfoShape::preset (lfo[(size_t) l], 7 + l));
            for (const auto& r : routes)
            {
                const int dest = modDestIndex (r.dest);
                jassert (dest >= 0);
                const int slot = proc.matrix.add (r.source, dest, r.amount);
                if (slot >= 0)
                {
                    proc.matrix.slots[(size_t) slot].bipolar = r.bipolar;
                    proc.matrix.slots[(size_t) slot].aux = (int) r.aux;
                }
            }
            for (int m = 0; m < macroNames.size() && m < numMacros; ++m)
                proc.setMacroName (m, macroNames[m]);
        };
        return p;
    }
} // namespace

std::vector<aa::FactoryPreset> makeFactoryPresets()
{
    using S = ModSource;
    using L = LfoShape::Preset;
    std::vector<Def> defs;

    // ---------------------------------------------------------------------------------------
    defs.push_back ({ "Init", {}, {} });

    // ---- BASS -------------------------------------------------------------------------------
    defs.push_back ({ "BS Reese Ritual",
        { { "voiceMode", 2 }, { "glide", 70 }, { "aTable", table ("Reese Cycle") }, { "aPos", 0 }, { "aUnison", 4 },
          { "aDetune", 22 }, { "aBlend", 55 }, { "aWidth", 60 }, { "aLevel", 70 },
          { "bOn", 1 }, { "bTable", table ("Basic Shapes") }, { "bPos", 50 }, { "bOct", -1 }, { "bLevel", 45 },
          { "subOn", 1 }, { "subOct", -1 }, { "subLevel", 55 }, { "subRoute", 3 },
          { "f1On", 1 }, { "f1Type", filterType (FilterType::ladder) }, { "f1Cutoff", 700 }, { "f1Reso", 22 }, { "f1Drive", 45 },
          { "e1Release", 120 }, { "e2Attack", 2 }, { "e2Decay", 450 }, { "e2Sustain", 25 },
          { "l1Div", division ("2 bars") }, { "m1", 30 } },
        { { S::env2, "f1Cutoff", 0.22f }, { S::lfo1, "aPos", 0.85f }, { S::macro1, "f1Cutoff", 0.45f },
          { S::macro1, "f1Drive", 0.4f } },
        { L::triangle, L::sine, L::sawDown, L::steps }, { "Growl", "Macro 2", "Macro 3", "Macro 4" } });

    defs.push_back ({ "BS Growl Talker",
        { { "voiceMode", 2 }, { "glide", 40 }, { "aTable", table ("Growl Talker") }, { "aPos", 0 }, { "aUnison", 2 },
          { "aDetune", 10 }, { "aLevel", 80 },
          { "subOn", 1 }, { "subOct", -1 }, { "subLevel", 60 }, { "subRoute", 3 },
          { "f1On", 1 }, { "f1Type", filterType (FilterType::lp24) }, { "f1Cutoff", 1800 }, { "f1Reso", 30 }, { "f1Drive", 30 },
          { "l1Sync", 1 }, { "l1Div", division ("1/8") }, { "l1Smooth", 25 }, { "e1Release", 90 } },
        { { S::lfo1, "aPos", 1.0f }, { S::lfo1, "f1Cutoff", 0.25f }, { S::macro1, "l1Smooth", 0.6f } },
        { L::bounce, L::sine, L::sawDown, L::steps }, { "Smooth", "Macro 2", "Macro 3", "Macro 4" } });

    defs.push_back ({ "BS Sub Pulse",
        { { "aTable", table ("Sub Weight") }, { "aPos", 30 }, { "aLevel", 85 }, { "voiceMode", 1 },
          { "f1On", 1 }, { "f1Type", filterType (FilterType::lp24) }, { "f1Cutoff", 250 }, { "f1Reso", 18 }, { "f1Drive", 20 },
          { "e1Attack", 1 }, { "e1Decay", 380 }, { "e1Sustain", 55 }, { "e1Release", 80 },
          { "e2Attack", 0 }, { "e2Decay", 180 }, { "e2Sustain", 0 } },
        { { S::env2, "f1Cutoff", 0.4f }, { S::velocity, "f1Cutoff", 0.15f }, { S::env2, "aPos", 0.3f } } });

    defs.push_back ({ "BS FM Wobble",
        { { "voiceMode", 2 }, { "glide", 30 }, { "aTable", table ("FM Growl") }, { "aPos", 20 }, { "aLevel", 75 },
          { "aWarp", warp (Warp::fm) }, { "aWarpSrc", 1 }, { "aWarpAmt", 0 },
          { "bOn", 1 }, { "bTable", table ("Basic Shapes") }, { "bPos", 0 }, { "bLevel", 30 }, { "bRoute", 3 },
          { "subOn", 1 }, { "subOct", -1 }, { "subLevel", 50 },
          { "f1On", 1 }, { "f1Type", filterType (FilterType::lp12) }, { "f1Cutoff", 3500 }, { "f1Reso", 20 },
          { "l1Sync", 1 }, { "l1Div", division ("1/4 T") } },
        { { S::lfo1, "aWarpAmt", 0.55f }, { S::lfo1, "aPos", 0.5f }, { S::lfo1, "f1Cutoff", -0.3f },
          { S::macro1, "aWarpAmt", 0.35f } },
        { L::sine, L::sine, L::sawDown, L::steps }, { "FM Depth", "Macro 2", "Macro 3", "Macro 4" } });

    defs.push_back ({ "BS West Coast Fold",
        { { "voiceMode", 1 }, { "aTable", table ("West Fold") }, { "aPos", 10 }, { "aLevel", 80 },
          { "subOn", 1 }, { "subOct", -1 }, { "subShape", 2 }, { "subLevel", 45 },
          { "e1Attack", 1 }, { "e1Decay", 500 }, { "e1Sustain", 40 }, { "e1Release", 100 },
          { "e2Attack", 1 }, { "e2Decay", 260 }, { "e2Sustain", 10 } },
        { { S::env2, "aPos", 0.7f }, { S::velocity, "aPos", 0.2f }, { S::macro1, "aPos", 0.6f } },
        { L::sine, L::triangle, L::sawDown, L::steps }, { "Fold", "Macro 2", "Macro 3", "Macro 4" } });

    // ---- LEADS ------------------------------------------------------------------------------
    defs.push_back ({ "LD Comet Lead",
        { { "voiceMode", 2 }, { "glide", 90 }, { "aTable", table ("Saw Swarm") }, { "aPos", 40 }, { "aUnison", 7 },
          { "aDetune", 20 }, { "aBlend", 70 }, { "aLevel", 70 },
          { "bOn", 1 }, { "bTable", table ("Basic Shapes") }, { "bPos", 75 }, { "bOct", 1 }, { "bLevel", 30 },
          { "f1On", 1 }, { "f1Type", filterType (FilterType::lp12) }, { "f1Cutoff", 4200 }, { "f1Reso", 15 },
          { "l2Sync", 0 }, { "l2Rate", 5.5f }, { "l2Delay", 250 }, { "l2Rise", 400 }, { "e1Release", 260 } },
        { { S::lfo2, "aFine", 0.08f, true }, { S::lfo2, "bFine", 0.08f, true }, { S::modWheel, "f1Cutoff", 0.3f },
          { S::macro1, "aPos", 0.5f } },
        { L::sine, L::sine, L::sawDown, L::steps }, { "Brightness", "Macro 2", "Macro 3", "Macro 4" } });

    defs.push_back ({ "LD Sync Scream",
        { { "voiceMode", 2 }, { "glide", 50 }, { "aTable", table ("Basic Shapes") }, { "aPos", 50 },
          { "aWarp", warp (Warp::sync) }, { "aWarpAmt", 20 }, { "aUnison", 3 }, { "aDetune", 12 }, { "aLevel", 75 },
          { "f1On", 1 }, { "f1Type", filterType (FilterType::lp24) }, { "f1Cutoff", 6000 }, { "f1Reso", 10 }, { "f1Drive", 35 },
          { "e2Attack", 1 }, { "e2Decay", 900 }, { "e2Sustain", 20 } },
        { { S::env2, "aWarpAmt", 0.55f }, { S::modWheel, "aWarpAmt", 0.4f }, { S::macro1, "aWarpAmt", 0.5f } },
        { L::sine, L::triangle, L::sawDown, L::steps }, { "Scream", "Macro 2", "Macro 3", "Macro 4" } });

    defs.push_back ({ "LD Chip Hero",
        { { "voiceMode", 2 }, { "glide", 25 }, { "aTable", table ("Chip Pulses") }, { "aPos", 30 }, { "aLevel", 70 },
          { "aWarp", warp (Warp::quantize) }, { "aWarpAmt", 30 },
          { "l1Sync", 1 }, { "l1Div", division ("1/16") }, { "l2Sync", 0 }, { "l2Rate", 6 }, { "l2Delay", 300 },
          { "e1Release", 80 } },
        { { S::lfo1, "aPos", 0.4f }, { S::lfo2, "aFine", 0.12f, true } },
        { L::stairs, L::triangle, L::sawDown, L::steps } });

    defs.push_back ({ "LD Vowel Singer",
        { { "voiceMode", 2 }, { "glide", 120 }, { "aTable", table ("Vowel Glide") }, { "aPos", 0 }, { "aUnison", 3 },
          { "aDetune", 10 }, { "aLevel", 75 },
          { "f1On", 1 }, { "f1Type", filterType (FilterType::formant) }, { "f1Cutoff", 1000 }, { "f1Reso", 35 }, { "f1Mix", 55 },
          { "l1Sync", 0 }, { "l1Rate", 0.35f }, { "l2Sync", 0 }, { "l2Rate", 5 }, { "l2Delay", 400 }, { "l2Rise", 500 },
          { "e1Attack", 40 }, { "e1Release", 300 } },
        { { S::lfo1, "aPos", 1.0f }, { S::lfo1, "f1Var", 0.8f }, { S::lfo2, "aFine", 0.1f, true },
          { S::macro1, "f1Var", 1.0f } },
        { L::triangle, L::sine, L::sawDown, L::steps }, { "Vowel", "Macro 2", "Macro 3", "Macro 4" } });

    // ---- PADS -------------------------------------------------------------------------------
    defs.push_back ({ "PD Nebula Bloom",
        { { "aTable", table ("Nebula Pad") }, { "aPos", 20 }, { "aUnison", 8 }, { "aDetune", 28 }, { "aLevel", 65 },
          { "bOn", 1 }, { "bTable", table ("Spectral Tilt") }, { "bPos", 30 }, { "bOct", 1 }, { "bUnison", 4 },
          { "bDetune", 20 }, { "bLevel", 35 },
          { "f1On", 1 }, { "f1Type", filterType (FilterType::lp12) }, { "f1Cutoff", 2400 }, { "f1Reso", 12 },
          { "e1Attack", 1400 }, { "e1Decay", 2000 }, { "e1Sustain", 85 }, { "e1Release", 3500 },
          { "l1Sync", 0 }, { "l1Rate", 0.08f }, { "l2Sync", 0 }, { "l2Rate", 0.13f } },
        { { S::lfo1, "aPos", 0.6f }, { S::lfo2, "bPos", 0.5f }, { S::lfo2, "f1Cutoff", 0.15f },
          { S::macro1, "f1Cutoff", 0.4f } },
        { L::sine, L::triangle, L::sawDown, L::steps }, { "Bloom", "Macro 2", "Macro 3", "Macro 4" } });

    defs.push_back ({ "PD Choir of Stars",
        { { "aTable", table ("Choir Air") }, { "aPos", 30 }, { "aUnison", 6 }, { "aDetune", 18 }, { "aLevel", 65 },
          { "bOn", 1 }, { "bTable", table ("Vowel Glide") }, { "bPos", 50 }, { "bUnison", 3 }, { "bLevel", 35 },
          { "noiseOn", 1 }, { "noiseLevel", 8 },
          { "f1On", 1 }, { "f1Type", filterType (FilterType::formant) }, { "f1Cutoff", 1000 }, { "f1Var", 25 },
          { "f1Reso", 20 }, { "f1Mix", 60 },
          { "e1Attack", 900 }, { "e1Sustain", 90 }, { "e1Release", 2800 },
          { "l1Sync", 0 }, { "l1Rate", 0.1f } },
        { { S::lfo1, "f1Var", 0.6f }, { S::lfo1, "aPos", 0.4f }, { S::macro1, "f1Var", 0.8f } },
        { L::triangle, L::triangle, L::sawDown, L::steps }, { "Vowel", "Macro 2", "Macro 3", "Macro 4" } });

    defs.push_back ({ "PD Prism Drift",
        { { "aTable", table ("Prism Scatter") }, { "aPos", 0 }, { "aUnison", 5 }, { "aDetune", 15 }, { "aLevel", 65 },
          { "bOn", 1 }, { "bTable", table ("Odd to Even") }, { "bPos", 50 }, { "bOct", -1 }, { "bLevel", 40 },
          { "e1Attack", 800 }, { "e1Sustain", 100 }, { "e1Release", 2500 },
          { "l1Sync", 1 }, { "l1Div", division ("4 bars") }, { "l1Mode", 1 }, { "l2Sync", 1 }, { "l2Div", division ("8 bars") }, { "l2Mode", 1 } },
        { { S::lfo1, "aPos", 1.0f }, { S::lfo2, "bPos", 0.8f, true }, { S::macro1, "aDetune", 0.5f } },
        { L::triangle, L::sine, L::sawDown, L::steps }, { "Spread", "Macro 2", "Macro 3", "Macro 4" } });

    defs.push_back ({ "PD Bowed Horizon",
        { { "aTable", table ("Bowed Strings") }, { "aPos", 40 }, { "aUnison", 6 }, { "aDetune", 16 }, { "aLevel", 70 },
          { "f1On", 1 }, { "f1Type", filterType (FilterType::lp12) }, { "f1Cutoff", 3200 }, { "f1Reso", 8 }, { "f1Key", 40 },
          { "e1Attack", 450 }, { "e1Sustain", 90 }, { "e1Release", 1600 },
          { "l2Sync", 0 }, { "l2Rate", 4.8f }, { "l2Delay", 500 }, { "l2Rise", 800 } },
        { { S::lfo2, "aFine", 0.07f, true }, { S::velocity, "f1Cutoff", 0.2f }, { S::modWheel, "aPos", 0.5f } } });

    defs.push_back ({ "PD Resonant Tide",
        { { "aTable", table ("Resonant Sweep") }, { "aPos", 0 }, { "aUnison", 4 }, { "aDetune", 20 }, { "aLevel", 70 },
          { "e1Attack", 600 }, { "e1Sustain", 100 }, { "e1Release", 2200 },
          { "l1Sync", 1 }, { "l1Div", division ("1 bar") }, { "l1Mode", 1 } },
        { { S::lfo1, "aPos", 1.0f } },
        { L::triangle, L::triangle, L::sawDown, L::steps } });

    // ---- PLUCKS & KEYS ----------------------------------------------------------------------
    defs.push_back ({ "PL Glass Pluck",
        { { "aTable", table ("FM Bell") }, { "aPos", 40 }, { "aUnison", 2 }, { "aDetune", 8 }, { "aLevel", 75 },
          { "e1Attack", 1 }, { "e1Decay", 700 }, { "e1Sustain", 0 }, { "e1Release", 500 },
          { "e2Attack", 0 }, { "e2Decay", 250 }, { "e2Sustain", 0 } },
        { { S::env2, "aPos", 0.5f }, { S::velocity, "aPos", 0.25f } } });

    defs.push_back ({ "PL Harmonic Pluck",
        { { "aTable", table ("Harmonic Ladder") }, { "aPos", 10 }, { "aUnison", 3 }, { "aDetune", 14 }, { "aLevel", 75 },
          { "f1On", 1 }, { "f1Type", filterType (FilterType::lp24) }, { "f1Cutoff", 900 }, { "f1Reso", 25 },
          { "e1Attack", 1 }, { "e1Decay", 450 }, { "e1Sustain", 0 }, { "e1Release", 350 },
          { "e2Attack", 0 }, { "e2Decay", 220 }, { "e2Sustain", 0 } },
        { { S::env2, "aPos", 0.8f }, { S::env2, "f1Cutoff", 0.45f }, { S::velocity, "f1Cutoff", 0.2f } } });

    defs.push_back ({ "KY Tine Dream",
        { { "aTable", table ("Tine Keys") }, { "aPos", 20 }, { "aLevel", 75 }, { "velocity", 85 },
          { "e1Attack", 1 }, { "e1Decay", 2600 }, { "e1Sustain", 20 }, { "e1Release", 700 },
          { "e2Attack", 0 }, { "e2Decay", 500 }, { "e2Sustain", 0 },
          { "l1Sync", 0 }, { "l1Rate", 4.5f } },
        { { S::env2, "aPos", 0.35f, false, S::velocity }, { S::lfo1, "aPan", 0.25f, true } } });

    defs.push_back ({ "KY Drawbar Dreams",
        { { "aTable", table ("Drawbar Organ") }, { "aPos", 50 }, { "aLevel", 70 }, { "velocity", 0 },
          { "e1Attack", 4 }, { "e1Sustain", 100 }, { "e1Release", 90 },
          { "l1Sync", 0 }, { "l1Rate", 6.2f }, { "l1Mode", 1 } },
        { { S::lfo1, "aPan", 0.3f, true }, { S::lfo1, "aFine", 0.02f, true }, { S::modWheel, "aPos", 0.5f } } });

    defs.push_back ({ "KY Phase Res Keys",
        { { "aTable", table ("Phase Res") }, { "aPos", 10 }, { "aUnison", 2 }, { "aDetune", 10 }, { "aLevel", 75 },
          { "e1Attack", 2 }, { "e1Decay", 1200 }, { "e1Sustain", 30 }, { "e1Release", 500 },
          { "e2Attack", 0 }, { "e2Decay", 600 }, { "e2Sustain", 10 } },
        { { S::env2, "aPos", 0.6f }, { S::velocity, "aPos", 0.3f } } });

    // ---- SEQUENCED / RHYTHMIC -----------------------------------------------------------------
    defs.push_back ({ "SQ Byte Gate",
        { { "aTable", table ("Byte Logic") }, { "aPos", 0 }, { "aUnison", 3 }, { "aDetune", 12 }, { "aLevel", 75 },
          { "e1Attack", 2 }, { "e1Sustain", 100 }, { "e1Release", 150 },
          { "l1Sync", 1 }, { "l1Div", division ("1/16") }, { "l1Mode", 1 },
          { "l2Sync", 1 }, { "l2Div", division ("1 bar") }, { "l2Mode", 1 } },
        { { S::lfo1, "aLevel", -0.75f }, { S::lfo2, "aPos", 0.9f } },
        { L::pulse, L::steps, L::sawDown, L::steps } });

    defs.push_back ({ "SQ Ring Runner",
        { { "aTable", table ("Ring Sweep") }, { "aPos", 20 }, { "aLevel", 75 },
          { "f1On", 1 }, { "f1Type", filterType (FilterType::bp12) }, { "f1Cutoff", 1500 }, { "f1Reso", 35 },
          { "e1Attack", 1 }, { "e1Decay", 220 }, { "e1Sustain", 0 }, { "e1Release", 200 },
          { "l1Sync", 1 }, { "l1Div", division ("2 bars") }, { "l1Mode", 1 } },
        { { S::lfo1, "f1Cutoff", 0.5f }, { S::lfo1, "aPos", 0.6f }, { S::random, "aPan", 0.4f } },
        { L::random, L::triangle, L::sawDown, L::steps } });

    // ---- FX ---------------------------------------------------------------------------------
    defs.push_back ({ "FX Rising Star",
        { { "aTable", table ("Sine to Static") }, { "aPos", 0 }, { "aUnison", 6 }, { "aDetune", 35 }, { "aLevel", 55 },
          { "noiseOn", 1 }, { "noiseLevel", 30 },
          { "f1On", 1 }, { "f1Type", filterType (FilterType::lp24) }, { "f1Cutoff", 200 }, { "f1Reso", 40 },
          { "e1Attack", 4000 }, { "e1Sustain", 100 }, { "e1Release", 1500 },
          { "l1Mode", 2 }, { "l1Sync", 1 }, { "l1Div", division ("4 bars") } },
        { { S::lfo1, "f1Cutoff", 0.85f }, { S::lfo1, "aPos", 1.0f }, { S::lfo1, "aSemi", 0.25f } },
        { L::sawUp, L::triangle, L::sawDown, L::steps } });

    defs.push_back ({ "FX Laser Zap",
        { { "aTable", table ("Basic Shapes") }, { "aPos", 0 }, { "aLevel", 80 },
          { "e1Attack", 0 }, { "e1Decay", 260 }, { "e1Sustain", 0 }, { "e1Release", 120 },
          { "e2Attack", 0 }, { "e2Decay", 240 }, { "e2Sustain", 0 }, { "e2DCurve", 80 } },
        { { S::env2, "aSemi", 0.5f }, { S::env2, "aPos", 0.6f } } });

    std::vector<aa::FactoryPreset> presets;
    for (auto& d : defs)
        presets.push_back (build (std::move (d)));
    return presets;
}
