#pragma once

#include <juce_core/juce_core.h>
#include <array>

/** Static data shared by the DSP engine and the UI: formant tables, consonants and syllables. */
namespace babble
{
constexpr int numFormants = 5;
constexpr int numVowels = 5;      // A E I O U
constexpr int numVoiceTypes = 6;
constexpr int maxVoices = 8;
constexpr int maxChoir = 4;
constexpr int numConsonants = 17;

enum VoiceType { bass = 0, tenor, alto, soprano, child, robot };

inline const juce::StringArray& voiceTypeNames()
{
    static const juce::StringArray names { "Bass", "Tenor", "Alto", "Soprano", "Child", "Robot" };
    return names;
}

inline const char* vowelLetter (int v)
{
    static const char* letters[] = { "A", "E", "I", "O", "U" };
    return letters[juce::jlimit (0, numVowels - 1, v)];
}

/** One vowel for one voice: centre frequency (Hz), peak level (dB) and bandwidth (Hz) per formant. */
struct VowelShape
{
    std::array<float, numFormants> freq, db, bw;
};

/** Classic formant tables. Bass/tenor/alto/soprano are the Csound "formant values" tables
    (after Peterson & Barney / Sundberg); the child voice uses Peterson & Barney's children's
    averages for F1-F3 with extrapolated F4/F5; the robot is a narrow, buzzy tenor. */
inline const VowelShape& vowelShape (int type, int vowel)
{
    static const VowelShape table[numVoiceTypes][numVowels] = {
        { // Bass
          { { 600, 1040, 2250, 2450, 2750 }, { 0, -7, -9, -9, -20 },    { 60, 70, 110, 120, 130 } },
          { { 400, 1620, 2400, 2800, 3100 }, { 0, -12, -9, -12, -18 },  { 40, 80, 100, 120, 120 } },
          { { 250, 1750, 2600, 3050, 3340 }, { 0, -30, -16, -22, -28 }, { 60, 90, 100, 120, 120 } },
          { { 400, 750, 2400, 2600, 2900 },  { 0, -11, -21, -20, -40 }, { 40, 80, 100, 120, 120 } },
          { { 350, 600, 2400, 2675, 2950 },  { 0, -20, -32, -28, -36 }, { 40, 80, 100, 120, 120 } } },
        { // Tenor
          { { 650, 1080, 2650, 2900, 3250 }, { 0, -6, -7, -8, -22 },    { 80, 90, 120, 130, 140 } },
          { { 400, 1700, 2600, 3200, 3580 }, { 0, -14, -12, -14, -20 }, { 70, 80, 100, 120, 120 } },
          { { 290, 1870, 2800, 3250, 3540 }, { 0, -15, -18, -20, -30 }, { 40, 90, 100, 120, 120 } },
          { { 400, 800, 2600, 2800, 3000 },  { 0, -10, -12, -12, -26 }, { 40, 80, 100, 120, 120 } },
          { { 350, 600, 2700, 2900, 3300 },  { 0, -20, -17, -14, -26 }, { 40, 60, 100, 120, 120 } } },
        { // Alto
          { { 800, 1150, 2800, 3500, 4950 }, { 0, -4, -20, -36, -60 },  { 80, 90, 120, 130, 140 } },
          { { 400, 1600, 2700, 3300, 4950 }, { 0, -24, -30, -35, -60 }, { 60, 80, 120, 150, 200 } },
          { { 350, 1700, 2700, 3700, 4950 }, { 0, -20, -30, -36, -60 }, { 50, 100, 120, 150, 200 } },
          { { 450, 800, 2830, 3500, 4950 },  { 0, -9, -16, -28, -55 },  { 70, 80, 100, 130, 135 } },
          { { 325, 700, 2530, 3500, 4950 },  { 0, -12, -30, -40, -64 }, { 50, 60, 170, 180, 200 } } },
        { // Soprano
          { { 800, 1150, 2900, 3900, 4950 }, { 0, -6, -32, -20, -50 },  { 80, 90, 120, 130, 140 } },
          { { 350, 2000, 2800, 3600, 4950 }, { 0, -20, -15, -40, -56 }, { 60, 100, 120, 150, 200 } },
          { { 270, 2140, 2950, 3900, 4950 }, { 0, -12, -26, -26, -44 }, { 60, 90, 100, 120, 120 } },
          { { 450, 800, 2830, 3800, 4950 },  { 0, -11, -22, -22, -50 }, { 70, 80, 100, 130, 135 } },
          { { 325, 700, 2700, 3800, 4950 },  { 0, -16, -35, -40, -60 }, { 50, 60, 170, 180, 200 } } },
        { // Child
          { { 1030, 1370, 3170, 4300, 5200 }, { 0, -5, -25, -30, -50 },  { 90, 100, 140, 150, 160 } },
          { { 690, 2610, 3570, 4500, 5300 },  { 0, -12, -20, -36, -50 }, { 70, 110, 140, 160, 200 } },
          { { 370, 3200, 3730, 4600, 5400 },  { 0, -10, -20, -30, -44 }, { 60, 110, 120, 140, 150 } },
          { { 680, 1060, 3180, 4300, 5200 },  { 0, -8, -28, -30, -50 },  { 80, 90, 120, 150, 160 } },
          { { 430, 1170, 3260, 4300, 5200 },  { 0, -14, -35, -40, -60 }, { 60, 80, 170, 180, 200 } } },
        { // Robot: tenor-ish, narrow resonances and a hot top end
          { { 650, 1080, 2650, 3300, 4200 }, { 0, -4, -4, -6, -12 },    { 45, 50, 70, 80, 90 } },
          { { 400, 1700, 2600, 3300, 4200 }, { 0, -8, -6, -8, -12 },    { 40, 50, 60, 70, 80 } },
          { { 290, 1870, 2800, 3400, 4200 }, { 0, -8, -10, -12, -16 },  { 30, 50, 60, 70, 80 } },
          { { 400, 800, 2600, 3300, 4200 },  { 0, -6, -8, -10, -16 },   { 30, 45, 60, 70, 80 } },
          { { 350, 600, 2700, 3300, 4200 },  { 0, -12, -12, -12, -18 }, { 30, 40, 60, 70, 80 } } }
    };

    return table[juce::jlimit (0, numVoiceTypes - 1, type)][juce::jlimit (0, numVowels - 1, vowel)];
}

//==============================================================================
/** A consonant used by the babbler: how the voice closes, where the formants point during the
    closure (the "locus"), and what kind of noise it makes. */
struct Consonant
{
    const char* text;
    float depth;        // 0..1 how far the voiced sound is closed off
    float closeMs;      // closure length
    float locus[3];     // F1..F3 multipliers at the closure
    float murmur;       // 0..1 low-pass "voice bar" during the closure (voiced stops, nasals)
    float noiseHz, noiseQ, noiseLevel, noiseMs;
    bool burst;         // true: noise burst at the release (stops); false: frication during the closure
    float aspiration;   // breathy "h" noise through the formants
    float lips;         // 0..1 how much the lips close (for the face)
    bool soft;          // allowed at gentle babble amounts
    float weight;       // how often it gets picked
};

inline const std::array<Consonant, numConsonants>& consonants()
{
    static const std::array<Consonant, numConsonants> list { {
        //  text  depth  ms    locus F1  F2    F3     murmur noiseHz Q    level ms    burst  asp   lips  soft   weight
        { "b",    0.92f, 55.0f, { 0.55f, 0.75f, 0.85f }, 0.8f, 900.0f,  1.2f, 0.10f, 8.0f,  true,  0.0f, 1.0f, true,  3.0f },
        { "d",    0.90f, 50.0f, { 0.55f, 1.12f, 1.00f }, 0.8f, 3500.0f, 1.5f, 0.14f, 8.0f,  true,  0.0f, 0.25f, true, 3.0f },
        { "g",    0.90f, 55.0f, { 0.55f, 1.15f, 0.88f }, 0.8f, 2200.0f, 1.8f, 0.16f, 10.0f, true,  0.0f, 0.2f, false, 1.5f },
        { "m",    0.72f, 70.0f, { 0.60f, 0.80f, 0.90f }, 1.0f, 0.0f,    1.0f, 0.0f,  0.0f,  false, 0.0f, 1.0f, true,  3.0f },
        { "n",    0.68f, 60.0f, { 0.60f, 1.08f, 1.00f }, 1.0f, 0.0f,    1.0f, 0.0f,  0.0f,  false, 0.0f, 0.3f, true,  2.0f },
        { "l",    0.42f, 55.0f, { 0.75f, 0.85f, 1.00f }, 0.4f, 0.0f,    1.0f, 0.0f,  0.0f,  false, 0.0f, 0.2f, true,  3.5f },
        { "w",    0.38f, 70.0f, { 0.70f, 0.60f, 0.90f }, 0.3f, 0.0f,    1.0f, 0.0f,  0.0f,  false, 0.0f, 0.8f, true,  1.5f },
        { "y",    0.36f, 60.0f, { 0.60f, 1.35f, 1.10f }, 0.2f, 0.0f,    1.0f, 0.0f,  0.0f,  false, 0.0f, 0.1f, true,  1.5f },
        { "r",    0.36f, 60.0f, { 0.80f, 0.90f, 0.72f }, 0.3f, 0.0f,    1.0f, 0.0f,  0.0f,  false, 0.0f, 0.4f, true,  1.0f },
        { "p",    0.97f, 60.0f, { 0.55f, 0.75f, 0.85f }, 0.0f, 1000.0f, 1.0f, 0.28f, 14.0f, true,  0.40f, 1.0f, false, 1.5f },
        { "t",    0.95f, 50.0f, { 0.55f, 1.10f, 1.00f }, 0.0f, 4500.0f, 1.6f, 0.30f, 12.0f, true,  0.30f, 0.25f, false, 2.0f },
        { "k",    0.95f, 55.0f, { 0.55f, 1.15f, 0.90f }, 0.0f, 2000.0f, 2.0f, 0.32f, 15.0f, true,  0.35f, 0.2f, false, 2.0f },
        { "s",    0.86f, 90.0f, { 0.70f, 1.10f, 1.00f }, 0.0f, 7000.0f, 2.5f, 0.30f, 70.0f, false, 0.0f, 0.3f, false, 1.5f },
        { "sh",   0.86f, 95.0f, { 0.70f, 1.00f, 0.90f }, 0.0f, 2800.0f, 2.0f, 0.30f, 75.0f, false, 0.0f, 0.6f, false, 1.0f },
        { "f",    0.80f, 80.0f, { 0.70f, 0.85f, 0.95f }, 0.0f, 5000.0f, 0.8f, 0.16f, 60.0f, false, 0.0f, 0.9f, false, 1.0f },
        { "h",    0.50f, 60.0f, { 1.00f, 1.00f, 1.00f }, 0.0f, 0.0f,    1.0f, 0.0f,  0.0f,  false, 1.0f, 0.0f, false, 1.5f },
        { "z",    0.80f, 80.0f, { 0.65f, 1.10f, 1.00f }, 0.5f, 6000.0f, 2.2f, 0.18f, 60.0f, false, 0.0f, 0.3f, false, 0.7f },
    } };
    return list;
}

/** How a vowel is spelled in the speech bubble. */
inline const char* vowelSpelling (int v)
{
    static const char* spell[] = { "a", "e", "ee", "o", "oo" };
    return spell[juce::jlimit (0, numVowels - 1, v)];
}

inline juce::String syllableText (int consonant, int vowel)
{
    const auto& list = consonants();
    juce::String c = juce::isPositiveAndBelow (consonant, (int) list.size()) ? list[(size_t) consonant].text : "";
    return c + vowelSpelling (vowel);
}

/** Long, sung form of a vowel ("Aaah") for when the singer isn't babbling. */
inline const char* sungVowel (int v)
{
    static const char* sung[] = { "Aaah", "Ehhh", "Eeee", "Oooh", "Oooo" };
    return sung[juce::jlimit (0, numVowels - 1, v)];
}

/** Text for the vowel morph position, e.g. "A", "A-E 40%". */
inline juce::String vowelPositionText (float v)
{
    v = juce::jlimit (0.0f, (float) (numVowels - 1), v);
    const int i = juce::jlimit (0, numVowels - 2, (int) std::floor (v));
    const float f = v - (float) i;
    if (f < 0.05f)
        return vowelLetter (i);
    if (f > 0.95f)
        return vowelLetter (i + 1);
    return juce::String (vowelLetter (i)) + "-" + vowelLetter (i + 1) + " " + juce::String (juce::roundToInt (f * 100.0f)) + "%";
}

inline float vowelPositionFromText (const juce::String& text)
{
    const auto t = text.trim().toUpperCase();
    const juce::String letters ("AEIOU");
    if (t.length() == 1 && letters.containsChar (t[0]))
        return (float) letters.indexOfChar (t[0]);

    if (t.length() >= 3 && letters.containsChar (t[0]) && letters.containsChar (t[2]))
    {
        const float a = (float) letters.indexOfChar (t[0]);
        const float pct = t.substring (3).retainCharacters ("0123456789.").getFloatValue();
        return juce::jlimit (0.0f, 4.0f, a + pct / 100.0f);
    }
    return juce::jlimit (0.0f, 4.0f, t.retainCharacters ("0123456789.").getFloatValue());
}
} // namespace babble
