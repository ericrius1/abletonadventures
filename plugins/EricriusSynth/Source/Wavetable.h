#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <vector>

namespace ers
{
constexpr int wtSize = 2048;               // samples per frame (power of two)
constexpr int wtMask = wtSize - 1;
constexpr int wtLevels = 11;               // mip level k keeps harmonics 1 .. ((wtSize / 2) >> k)
constexpr int wtMaxFrames = 256;

/** An immutable, band-limited wavetable: up to 256 single-cycle frames of 2048 samples.

    Each frame is stored at wtLevels bandwidths ("mip levels") built with an FFT, so the
    oscillator can pick the level whose highest harmonic stays below Nyquist.
    Creation allocates and runs FFTs: message thread or a background thread only.
    Reading (getFrame / framePair / read) is lock-free and safe on the audio thread. */
class Wavetable
{
public:
    /** frames: numFrames * wtSize samples, frame after frame. Removes DC per frame, normalises the
        whole table so its loudest frame peaks at 1 (keeping the relative level of frames), then
        builds the mip levels. numFrames is clamped to 1..wtMaxFrames. */
    static std::shared_ptr<Wavetable> fromFrames (const juce::String& name, const std::vector<float>& frames,
                                                  int numFrames);

    const juce::String& getName() const noexcept { return name; }
    int getNumFrames() const noexcept { return numFrames; }

    /** wtSize + 1 samples (the extra sample repeats sample 0 so linear interpolation never wraps). */
    const float* getFrame (int level, int frame) const noexcept;

    /** Mip level for a phase increment in cycles per sample (0.5 = Nyquist). Level 0 is used for
        low notes; higher levels drop harmonics so that (harmonics * inc) < ~0.45. */
    static int levelForIncrement (float inc) noexcept;

    /** Two neighbouring frames and the crossfade between them for a table position 0..1. */
    struct FramePair
    {
        const float* a = nullptr;
        const float* b = nullptr;
        float t = 0.0f;
    };
    FramePair framePair (int level, float position) const noexcept;

    /** Linear-interpolated read of a frame pair at phase 0..1 (phase must already be wrapped). */
    static inline float read (const FramePair& fp, float phase) noexcept
    {
        const float x = phase * (float) wtSize;
        const int i = (int) x;
        const float f = x - (float) i;
        const float a = fp.a[i] + f * (fp.a[i + 1] - fp.a[i]);
        const float b = fp.b[i] + f * (fp.b[i + 1] - fp.b[i]);
        return a + fp.t * (b - a);
    }

    /** Full-bandwidth frames (numFrames * wtSize), e.g. for saving into a preset. */
    std::vector<float> getRawFrames() const;

    /** n points of a frame (level 0) resampled for drawing. */
    void getDisplayFrame (int frame, float* dest, int n) const;

private:
    Wavetable() = default;

    juce::String name;
    int numFrames = 0;
    // [level][frame][wtSize + 1]
    std::vector<float> data;
};

using WavetablePtr = std::shared_ptr<Wavetable>;

/** Load a wavetable from an audio file (WAV / AIFF / FLAC / anything JUCE's basic formats read).

    Frame size detection, in order:
      1. a "clm " chunk containing "<!>NNNN" (the de-facto marker many wavetable synths write) gives
         the frame size; frames are resampled to wtSize if NNNN != 2048;
      2. total length divisible by 2048 -> 2048-sample frames;
      3. length <= 4096 -> a single cycle, resampled to one frame;
      4. otherwise the file is treated as a sample: sliced into up to 64 evenly spaced single cycles
         (pitch-detected per slice when possible, else 2048-sample windows).
    Stereo files are mixed to mono. Returns nullptr and fills error on failure. */
WavetablePtr loadWavetableFile (const juce::File& file, juce::String* error = nullptr);
WavetablePtr loadWavetableData (const void* data, size_t numBytes, const juce::String& name,
                                juce::String* error = nullptr);

/** Write a 32-bit float mono WAV with a "clm " chunk (frame size 2048) so other synths can read it. */
bool saveWavetableFile (const Wavetable& table, const juce::File& file);
} // namespace ers
