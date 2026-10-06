#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <array>

/** Musical vocabulary shared by the sequencer (audio thread) and the editor: scales, speeds,
    note naming and the six planets. Everything here is allocation-free except the StringArray
    helpers, which are only used while building the parameter layout / UI. */
namespace orrery
{
constexpr int numOrbits = 6;
constexpr int maxDegree = 14;
constexpr int maxChord = 16;

//==============================================================================
struct Scale
{
    const char* name;
    int size;
    int steps[12];
};

inline const std::array<Scale, 10>& scales()
{
    static const std::array<Scale, 10> s { {
        { "Major",            7, { 0, 2, 4, 5, 7, 9, 11 } },
        { "Minor",            7, { 0, 2, 3, 5, 7, 8, 10 } },
        { "Major Pentatonic", 5, { 0, 2, 4, 7, 9 } },
        { "Minor Pentatonic", 5, { 0, 3, 5, 7, 10 } },
        { "Dorian",           7, { 0, 2, 3, 5, 7, 9, 10 } },
        { "Lydian",           7, { 0, 2, 4, 6, 7, 9, 11 } },
        { "Mixolydian",       7, { 0, 2, 4, 5, 7, 9, 10 } },
        { "Whole Tone",       6, { 0, 2, 4, 6, 8, 10 } },
        { "Hirajoshi",        5, { 0, 2, 3, 7, 8 } },
        { "Chromatic",       12, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
    } };
    return s;
}

inline juce::StringArray scaleNames()
{
    juce::StringArray names;
    for (auto& s : scales())
        names.add (s.name);
    return names;
}

inline juce::StringArray rootNames()
{
    return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

inline juce::StringArray speedNames()
{
    return { "x1/4", "x1/3", "x1/2", "x2/3", "x1", "x3/2", "x2", "x3", "x4" };
}

inline double speedValue (int index)
{
    static const double v[] = { 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 1.5, 2.0, 3.0, 4.0 };
    return v[juce::jlimit (0, 8, index)];
}

/** Maps a scale degree (0..14) + octave shift to a MIDI note. When a chord is supplied
    (Follow MIDI with held notes) the degrees walk up through the chord instead. */
inline int noteForDegree (int degree, int octave, int scaleIndex, int root, const int* chord, int chordSize)
{
    degree = juce::jlimit (0, maxDegree, degree);
    if (chord != nullptr && chordSize > 0)
    {
        const int oct = degree / chordSize;
        return juce::jlimit (0, 127, chord[degree % chordSize] + 12 * (oct + octave));
    }

    const auto& sc = scales()[(size_t) juce::jlimit (0, (int) scales().size() - 1, scaleIndex)];
    const int oct = degree / sc.size;
    return juce::jlimit (0, 127, 60 + root + sc.steps[degree % sc.size] + 12 * (oct + octave));
}

/** Note names use Ableton's convention (MIDI 60 = C3). */
inline juce::String noteName (int note)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((note % 12) + 12) % 12]) + juce::String (note / 12 - 2);
}

inline juce::String ordinal (int n)
{
    const int mod100 = n % 100;
    const char* suffix = "th";
    if (mod100 < 11 || mod100 > 13)
    {
        switch (n % 10)
        {
            case 1: suffix = "st"; break;
            case 2: suffix = "nd"; break;
            case 3: suffix = "rd"; break;
            default: break;
        }
    }
    return juce::String (n) + suffix;
}

//==============================================================================
/** The six classical planets of an antique orrery, innermost first. */
struct Planet
{
    const char* name;
    const char* numeral;
    juce::uint32 colour;
    float size;       // body radius in base pixels
    float pan;        // -1..1 stereo position at full spread
};

inline const std::array<Planet, numOrbits>& planets()
{
    static const std::array<Planet, numOrbits> p { {
        { "Mercury", "I",   0xff7fe3c4, 7.5f, -0.15f },
        { "Venus",   "II",  0xffff9fcb, 9.0f, -0.65f },
        { "Earth",   "III", 0xff5fb2ff, 9.5f,  0.6f },
        { "Mars",    "IV",  0xffff6a4d, 8.5f, -0.9f },
        { "Jupiter", "V",   0xffefb36d, 12.5f, 0.15f },
        { "Saturn",  "VI",  0xffb8a2ff, 10.5f, 0.9f },
    } };
    return p;
}

inline juce::String orbitParamId (int orbit, const char* suffix)
{
    return "o" + juce::String (orbit + 1) + "_" + suffix;
}
} // namespace orrery
