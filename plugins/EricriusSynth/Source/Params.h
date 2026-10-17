#pragma once

#include <aakit/PluginBase.h>
#include "Fx.h"

/** ericriusSynth parameter model.

    Every synth-voice parameter has a slot in the flat "VP" (voice parameter) index space so the
    engine can read a whole block's worth of values from one array, and the modulation matrix can
    target any of them by index. FX parameters come from fx::paramSpecs() and follow after. */
namespace ers
{
constexpr int numOscs = 3;
constexpr int numFilters = 2;
constexpr int numEnvs = 4;
constexpr int numLfos = 4;
constexpr int numMacros = 4;
constexpr int maxUnison = 16;
constexpr int maxVoices = 32;
constexpr int maxModSlots = 32;
constexpr int controlBlock = 32;     // samples per modulation update
constexpr int lfoTableSize = 1024;

//==============================================================================
enum class Warp
{
    off, sync, windowSync, bendPlus, bendMinus, bendBoth, pwm, asymPlus, asymMinus,
    flip, mirror, quantize, fm, am, rm, count
};
juce::StringArray warpNames();

enum class Route { filter1, filter2, both, direct };
juce::StringArray routeNames();

enum class Stack { off, octave, octaveDown, octaveBoth, fifth, octaveFifth, chord, count };
juce::StringArray stackNames();

enum class SubShape { sine, rounded, triangle, saw, square, pulse, count };
juce::StringArray subShapeNames();

enum class LfoMode { trigger, free, envelope };
enum class VoiceMode { poly, mono, legato };

/** Tempo divisions for synced LFOs (beats per cycle). */
juce::StringArray lfoDivisionNames();
double lfoDivisionBeats (int index);

//==============================================================================
// Per-oscillator fields, in order. Oscillator o's field f lives at VP index osc (o, f).
enum class OscField
{
    on, table, position, warp, warpAmount, warpSource, octave, semi, fine, unison, detune, blend, width,
    phase, random, level, pan, route, stack, count
};
enum class FilterField { on, type, cutoff, resonance, drive, var, mix, keyTrack, count };
enum class EnvField { attack, hold, decay, sustain, release, attackCurve, decayCurve, releaseCurve, count };
enum class LfoField { rate, sync, division, mode, rise, delay, smooth, phase, count };

namespace vp
{
    constexpr int oscFields = (int) OscField::count;
    constexpr int filterFields = (int) FilterField::count;
    constexpr int envFields = (int) EnvField::count;
    constexpr int lfoFields = (int) LfoField::count;

    constexpr int oscBase = 0;
    constexpr int subBase = oscBase + numOscs * oscFields;
    enum SubField { subOn = 0, subShape, subOctave, subLevel, subPan, subRoute, subFields };
    constexpr int noiseBase = subBase + subFields;
    enum NoiseField { noiseOn = 0, noiseType, noisePitch, noiseLevel, noisePan, noiseRoute, noiseKeyTrack, noiseFields };
    constexpr int filterBase = noiseBase + noiseFields;
    constexpr int filterRouting = filterBase + numFilters * filterFields;
    constexpr int envBase = filterRouting + 1;
    constexpr int lfoBase = envBase + numEnvs * envFields;
    constexpr int macroBase = lfoBase + numLfos * lfoFields;
    constexpr int globalBase = macroBase + numMacros;
    enum GlobalField { voiceMode = 0, polyphony, glide, legatoGlide, bendRange, velocity, masterTune, volume, globalFields };
    constexpr int count = globalBase + globalFields;

    constexpr int osc (int o, OscField f) noexcept         { return oscBase + o * oscFields + (int) f; }
    constexpr int sub (int f) noexcept                     { return subBase + f; }
    constexpr int noise (int f) noexcept                   { return noiseBase + f; }
    constexpr int filter (int i, FilterField f) noexcept   { return filterBase + i * filterFields + (int) f; }
    constexpr int env (int e, EnvField f) noexcept         { return envBase + e * envFields + (int) f; }
    constexpr int lfo (int l, LfoField f) noexcept         { return lfoBase + l * lfoFields + (int) f; }
    constexpr int macro (int m) noexcept                   { return macroBase + m; }
    constexpr int global (int f) noexcept                  { return globalBase + f; }
} // namespace vp

/** Parameter ID for a VP index ("aPos", "f1Cutoff", "e2Attack", "l3Rate", "m1", "volume"...). */
juce::String vpId (int index);

//==============================================================================
// Modulation
enum class ModSource
{
    none,
    env1, env2, env3, env4,
    lfo1, lfo2, lfo3, lfo4,
    macro1, macro2, macro3, macro4,
    velocity, note, modWheel, aftertouch, pitchBend, random, alternate, releaseVelocity,
    count
};
constexpr int numModSources = (int) ModSource::count;
juce::String modSourceName (ModSource);
juce::String modSourceShortName (ModSource);
/** True for sources that are naturally bipolar (-1..1): note, pitch bend, random, alternate. */
bool isBipolarSource (ModSource);

/** A modulation target: either a voice parameter (per-voice modulation) or an FX parameter (global). */
struct ModDest
{
    juce::String paramId;
    juce::String name;
    int vpIndex = -1;      // >= 0 for voice params
    int fxIndex = -1;      // >= 0 for FX params
};

/** All modulatable parameters (voice params first, then FX), in a stable order. */
const std::vector<ModDest>& modDests();
int modDestIndex (const juce::String& paramId);   // -1 if not modulatable
/** Index into modDests() for a VP index, or -1. */
int modDestForVp (int vpIndex);

//==============================================================================
/** Builds the whole parameter layout. Push-friendly: the first eight are the macros and the
    most playable knobs. */
juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

/** VP indices in the order they are added to the layout (for the first-eight-on-Push ordering). */
const std::vector<int>& vpLayoutOrder();
} // namespace ers
