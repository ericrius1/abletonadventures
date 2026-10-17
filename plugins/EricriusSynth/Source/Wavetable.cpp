#include "Wavetable.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <cstring>

using namespace juce;

namespace ers
{
namespace
{
    constexpr int fftOrder = 11;                 // 2^11 = wtSize
    constexpr int maxHarmonic = wtSize / 2;      // Nyquist bin of one frame
    constexpr int stride = wtSize + 1;           // frame + guard sample

    static_assert ((1 << fftOrder) == wtSize, "FFT order must match the frame size");

    inline int topHarmonic (int level) noexcept { return maxHarmonic >> level; }

    /** Per-level harmonic gains: flat up to ~90% of the kept band, then a raised-cosine roll-off so the
        band edge doesn't ring (Gibbs). Level 0 is stored as the raw frame and doesn't use this. */
    using GainTable = std::array<std::array<float, maxHarmonic + 1>, wtLevels>;

    const GainTable& levelGains()
    {
        static const GainTable table = []
        {
            GainTable t {};
            for (int level = 0; level < wtLevels; ++level)
            {
                auto& g = t[(size_t) level];
                std::fill (g.begin(), g.end(), 0.0f);
                const int top = topHarmonic (level);
                const int roll = top / 10;
                const int flatEnd = top - roll;

                for (int h = 1; h <= top; ++h)
                {
                    if (h <= flatEnd)
                        g[(size_t) h] = 1.0f;
                    else
                        g[(size_t) h] = 0.5f + 0.5f * std::cos (MathConstants<float>::pi
                                                               * (float) (h - flatEnd) / (float) (roll + 1));
                }
            }
            return t;
        }();
        return table;
    }

    inline float sanitise (float x) noexcept { return std::isfinite (x) ? x : 0.0f; }

    /** Catmull-Rom interpolation on a buffer; out-of-range indices read as zero. */
    inline float cubicAt (const float* x, int n, double pos) noexcept
    {
        const auto i = (int) std::floor (pos);
        const auto f = (float) (pos - (double) i);
        auto s = [x, n] (int k) { return (k >= 0 && k < n) ? x[k] : 0.0f; };
        const float y0 = s (i - 1), y1 = s (i), y2 = s (i + 1), y3 = s (i + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    /** Resample one cycle of n samples to wtSize samples, treating the input as periodic. */
    void resampleCycle (const float* src, int n, float* dst)
    {
        if (n == wtSize)
        {
            std::memcpy (dst, src, sizeof (float) * (size_t) wtSize);
            return;
        }

        if (n < wtSize)
        {
            // cyclic Catmull-Rom
            const double step = (double) n / (double) wtSize;
            for (int j = 0; j < wtSize; ++j)
            {
                const double pos = j * step;
                const int i = (int) pos;
                const float f = (float) (pos - i);
                const float y0 = src[(i - 1 + n) % n], y1 = src[i % n], y2 = src[(i + 1) % n], y3 = src[(i + 2) % n];
                const float c1 = 0.5f * (y2 - y0);
                const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
                const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                dst[j] = ((c3 * f + c2) * f + c1) * f + y1;
            }
            return;
        }

        // shrinking: box filter via the running integral (periodic signal, so any aliasing stays harmonic)
        std::vector<double> integral ((size_t) n + 1, 0.0);
        for (int i = 0; i < n; ++i)
            integral[(size_t) i + 1] = integral[(size_t) i] + (double) src[i];

        auto integralAt = [&] (double x)
        {
            const int i = jlimit (0, n - 1, (int) x);
            return integral[(size_t) i] + (x - i) * (double) src[i];
        };

        const double r = (double) n / (double) wtSize;
        double prev = 0.0;
        for (int j = 0; j < wtSize; ++j)
        {
            const double next = integralAt (jmin ((double) n, (j + 1) * r));
            dst[j] = (float) ((next - prev) / r);
            prev = next;
        }
    }

    //==============================================================================
    /** Walks the RIFF chunks of a WAV stream looking for a "clm " chunk with "<!>NNNN". Returns NNNN or 0. */
    int findClmFrameSize (InputStream& in)
    {
        char id[4] = {};
        if (! in.setPosition (0) || in.read (id, 4) != 4 || std::memcmp (id, "RIFF", 4) != 0)
            return 0;

        in.readInt();
        if (in.read (id, 4) != 4 || std::memcmp (id, "WAVE", 4) != 0)
            return 0;

        for (int guard = 0; guard < 512; ++guard)
        {
            if (in.read (id, 4) != 4)
                break;

            const auto size = (uint32) in.readInt();
            const auto start = in.getPosition();

            if (std::memcmp (id, "clm ", 4) == 0)
            {
                char text[128] = {};
                const int n = in.read (text, (int) std::min<uint32> (size, (uint32) sizeof (text) - 1));
                for (int i = 0; i + 3 < n; ++i)
                {
                    if (text[i] == '<' && text[i + 1] == '!' && text[i + 2] == '>')
                    {
                        int value = 0;
                        for (int k = i + 3; k < n && text[k] >= '0' && text[k] <= '9' && value < 1000000; ++k)
                            value = value * 10 + (text[k] - '0');
                        return value;
                    }
                }
                return 0;
            }

            const auto next = start + (int64) size + (int64) (size & 1);
            if (next <= start || ! in.setPosition (next) || in.getPosition() != next)
                break;
        }
        return 0;
    }

    /** Reads n samples mixed to mono (anything past the end reads as zero). */
    void readMono (AudioFormatReader& reader, int64 start, int n, float* dest)
    {
        const int channels = jlimit (1, 8, (int) reader.numChannels);
        AudioBuffer<float> buffer (channels, n);
        buffer.clear();

        if (! reader.read (buffer.getArrayOfWritePointers(), channels, start, n))
        {
            std::fill (dest, dest + n, 0.0f);
            return;
        }

        const float scale = 1.0f / (float) channels;
        for (int i = 0; i < n; ++i)
        {
            float sum = 0.0f;
            for (int c = 0; c < channels; ++c)
                sum += buffer.getSample (c, i);
            dest[i] = sanitise (sum * scale);
        }
    }

    /** Picks up to wtMaxFrames evenly spaced frame indices out of count. */
    std::vector<int64> chooseFrames (int64 count)
    {
        std::vector<int64> indices;
        const int n = (int) std::min<int64> (count, wtMaxFrames);
        indices.reserve ((size_t) n);
        for (int i = 0; i < n; ++i)
            indices.push_back (n == count ? (int64) i
                                          : (int64) std::llround ((double) i * (double) (count - 1) / (double) (n - 1)));
        return indices;
    }

    //==============================================================================
    /** Pitch period (in samples, fractional) of x[0..n) by normalised autocorrelation (McLeod's NSDF),
        searched between minLag and maxLag. Returns 0 when there's no clear pitch. */
    double detectPeriod (const float* x, int n, int minLag, int maxLag)
    {
        maxLag = jmin (maxLag, n / 2);
        if (maxLag <= minLag + 2)
            return 0.0;

        int order = 1;
        while ((1 << order) < 2 * n)
            ++order;

        dsp::FFT fft (order);
        const int size = 1 << order;
        std::vector<float> buf ((size_t) size * 2, 0.0f);
        std::copy (x, x + n, buf.begin());

        fft.performRealOnlyForwardTransform (buf.data(), true);
        for (int k = 0; k <= size / 2; ++k)
        {
            const float re = buf[(size_t) (2 * k)], im = buf[(size_t) (2 * k + 1)];
            buf[(size_t) (2 * k)] = re * re + im * im;
            buf[(size_t) (2 * k + 1)] = 0.0f;
        }
        fft.performRealOnlyInverseTransform (buf.data());   // buf[tau] = autocorrelation r(tau)

        std::vector<float> nsdf ((size_t) maxLag + 2, 0.0f);
        double m = 0.0;
        for (int i = 0; i < n; ++i)
            m += 2.0 * (double) x[i] * (double) x[i];

        if (m < 1.0e-9)
            return 0.0;

        for (int tau = 0; tau <= maxLag + 1; ++tau)
        {
            if (tau > 0)
                m -= (double) x[tau - 1] * x[tau - 1] + (double) x[n - tau] * x[n - tau];
            nsdf[(size_t) tau] = m > 1.0e-12 ? (float) (2.0 * buf[(size_t) tau] / m) : 0.0f;
        }

        // key maxima: the highest point of each positive lobe (after the one around lag 0)
        std::vector<int> peaks;
        float best = 0.0f;
        int tau = 1;
        while (tau <= maxLag && nsdf[(size_t) tau] > 0.0f)
            ++tau;

        while (tau <= maxLag)
        {
            while (tau <= maxLag && nsdf[(size_t) tau] <= 0.0f)
                ++tau;
            int peak = -1;
            while (tau <= maxLag && nsdf[(size_t) tau] > 0.0f)
            {
                if (peak < 0 || nsdf[(size_t) tau] > nsdf[(size_t) peak])
                    peak = tau;
                ++tau;
            }
            if (peak >= minLag)
            {
                peaks.push_back (peak);
                best = jmax (best, nsdf[(size_t) peak]);
            }
        }

        if (best < 0.5f)
            return 0.0;

        for (auto p : peaks)
        {
            if (nsdf[(size_t) p] >= 0.88f * best)
            {
                const float a = nsdf[(size_t) p - 1], b = nsdf[(size_t) p], c = nsdf[(size_t) p + 1];
                const float denom = a - 2.0f * b + c;
                const float shift = std::abs (denom) > 1.0e-9f ? jlimit (-0.5f, 0.5f, 0.5f * (a - c) / denom) : 0.0f;
                return (double) p + (double) shift;
            }
        }
        return 0.0;
    }

    /** One loopable cycle from a longer sample, starting near `start`. */
    void sliceCycle (const float* x, int n, int start, double sampleRate, float* dst)
    {
        const int minLag = jmax (2, (int) (sampleRate / 2000.0));
        const int maxLag = (int) (sampleRate / 20.0);
        const int window = jmin (n - start, jmax (2048, 2 * maxLag + 2));

        const double period = window > 64 ? detectPeriod (x + start, window, minLag, maxLag) : 0.0;

        if (period > 0.0)
        {
            // begin on a rising zero crossing so neighbouring slices line up
            double z = start;
            for (int i = start; i < jmin (n - 1, start + (int) period); ++i)
            {
                if (x[i] <= 0.0f && x[i + 1] > 0.0f)
                {
                    z = i + (double) (x[i] / (x[i] - x[i + 1]));
                    break;
                }
            }

            const double step = period / (double) wtSize;
            for (int j = 0; j < wtSize; ++j)
                dst[j] = cubicAt (x, n, z + j * step);

            // remove the small step left by decay / drift so the cycle wraps without a click
            const float mismatch = cubicAt (x, n, z + period) - dst[0];
            for (int j = 0; j < wtSize; ++j)
                dst[j] -= mismatch * (float) j / (float) wtSize;
            return;
        }

        // no clear pitch: a 2048-sample window whose tail crossfades into its head
        constexpr int fade = 256;
        auto at = [x, n] (int i) { return (i >= 0 && i < n) ? x[i] : 0.0f; };
        for (int j = 0; j < wtSize; ++j)
        {
            if (j < fade)
            {
                const float w = 0.5f - 0.5f * std::cos (MathConstants<float>::pi * (float) j / (float) fade);
                dst[j] = at (start + j) * w + at (start + wtSize + j) * (1.0f - w);
            }
            else
            {
                dst[j] = at (start + j);
            }
        }
    }

    //==============================================================================
    WavetablePtr loadFromReader (std::unique_ptr<AudioFormatReader> reader, int clmFrameSize,
                                 const String& name, String* error)
    {
        auto fail = [error] (const String& message) -> WavetablePtr
        {
            if (error != nullptr)
                *error = message;
            return nullptr;
        };

        if (reader == nullptr)
            return fail ("Can't read that kind of file");

        const int64 length = reader->lengthInSamples;
        if (length < 4 || reader->numChannels == 0)
            return fail ("That file is empty");

        double sampleRate = reader->sampleRate;
        if (! (sampleRate >= 1000.0 && sampleRate <= 1.0e6))
            sampleRate = 44100.0;

        std::vector<float> frames;
        int numFrames = 0;

        auto readFrames = [&] (int frameSize)
        {
            const auto indices = chooseFrames (length / frameSize);
            numFrames = (int) indices.size();
            frames.assign ((size_t) numFrames * wtSize, 0.0f);
            std::vector<float> cycle ((size_t) frameSize);
            for (int f = 0; f < numFrames; ++f)
            {
                readMono (*reader, indices[(size_t) f] * frameSize, frameSize, cycle.data());
                resampleCycle (cycle.data(), frameSize, frames.data() + (size_t) f * wtSize);
            }
        };

        if (clmFrameSize >= 16 && clmFrameSize <= 65536 && length >= clmFrameSize)
        {
            readFrames (clmFrameSize);                                // 1. "clm " marker
        }
        else if (length % wtSize == 0)
        {
            readFrames (wtSize);                                      // 2. whole 2048-sample frames
        }
        else if (length <= 2 * wtSize)
        {
            readFrames ((int) length);                                // 3. one single cycle
        }
        else
        {
            // 4. a sample: slice single cycles out of (at most) the first 30 seconds
            const int n = (int) std::min<int64> (length, (int64) (sampleRate * 30.0));
            std::vector<float> audio ((size_t) n);
            readMono (*reader, 0, n, audio.data());

            numFrames = jlimit (1, 64, n / wtSize);
            frames.assign ((size_t) numFrames * wtSize, 0.0f);
            const int span = jmax (0, n - (wtSize + 512));
            for (int f = 0; f < numFrames; ++f)
            {
                const int start = numFrames > 1 ? (int) ((int64) span * f / (numFrames - 1)) : 0;
                sliceCycle (audio.data(), n, start, sampleRate, frames.data() + (size_t) f * wtSize);
            }
        }

        if (numFrames <= 0)
            return fail ("That file is empty");

        float peak = 0.0f;
        for (auto v : frames)
            peak = jmax (peak, std::abs (v));

        if (peak < 1.0e-6f)
            return fail ("That file is silent");

        return Wavetable::fromFrames (name, frames, numFrames);
    }

    void writeChunkHeader (OutputStream& out, const char* id, uint32 size)
    {
        out.write (id, 4);
        out.writeInt ((int) size);
    }
} // namespace

//==============================================================================
std::shared_ptr<Wavetable> Wavetable::fromFrames (const String& tableName, const std::vector<float>& frames,
                                                  int frameCount)
{
    std::shared_ptr<Wavetable> table (new Wavetable());
    table->name = tableName;
    table->numFrames = jlimit (1, wtMaxFrames, frameCount);
    const int count = table->numFrames;
    table->data.assign ((size_t) wtLevels * (size_t) count * stride, 0.0f);

    auto frameData = [&] (int level, int frame)
    {
        return table->data.data() + ((size_t) level * (size_t) count + (size_t) frame) * stride;
    };

    // level 0: sanitised, DC-free copy of the input
    float peak = 0.0f;
    for (int f = 0; f < count; ++f)
    {
        float* dst = frameData (0, f);
        const size_t offset = (size_t) f * wtSize;
        double sum = 0.0;

        for (int i = 0; i < wtSize; ++i)
        {
            const size_t k = offset + (size_t) i;
            dst[i] = k < frames.size() ? sanitise (frames[k]) : 0.0f;
            sum += dst[i];
        }

        const auto mean = (float) (sum / wtSize);
        for (int i = 0; i < wtSize; ++i)
        {
            dst[i] -= mean;
            peak = jmax (peak, std::abs (dst[i]));
        }
    }

    const float gain = peak > 1.0e-9f ? 1.0f / peak : 1.0f;
    for (int f = 0; f < count; ++f)
    {
        float* dst = frameData (0, f);
        FloatVectorOperations::multiply (dst, gain, wtSize);
        dst[wtSize] = dst[0];
    }

    // levels 1..: FFT once per frame, then one inverse transform per bandwidth
    const auto& gains = levelGains();
    dsp::FFT fft (fftOrder);
    std::vector<float> spectrum ((size_t) wtSize * 2), work ((size_t) wtSize * 2);

    for (int f = 0; f < count; ++f)
    {
        std::copy (frameData (0, f), frameData (0, f) + wtSize, spectrum.begin());
        std::fill (spectrum.begin() + wtSize, spectrum.end(), 0.0f);
        fft.performRealOnlyForwardTransform (spectrum.data(), true);

        for (int level = 1; level < wtLevels; ++level)
        {
            const int top = topHarmonic (level);
            const auto& g = gains[(size_t) level];

            work[0] = work[1] = 0.0f;
            for (int h = 1; h <= top; ++h)
            {
                work[(size_t) (2 * h)] = spectrum[(size_t) (2 * h)] * g[(size_t) h];
                work[(size_t) (2 * h + 1)] = spectrum[(size_t) (2 * h + 1)] * g[(size_t) h];
            }
            std::fill (work.begin() + 2 * (top + 1), work.end(), 0.0f);

            fft.performRealOnlyInverseTransform (work.data());

            float* dst = frameData (level, f);
            std::copy (work.begin(), work.begin() + wtSize, dst);
            dst[wtSize] = dst[0];
        }
    }

    return table;
}

const float* Wavetable::getFrame (int level, int frame) const noexcept
{
    level = jlimit (0, wtLevels - 1, level);
    frame = jlimit (0, numFrames - 1, frame);
    return data.data() + ((size_t) level * (size_t) numFrames + (size_t) frame) * stride;
}

int Wavetable::levelForIncrement (float inc) noexcept
{
    constexpr float limit = 0.45f;
    inc = std::abs (inc);
    if (! (inc > 0.0f))
        return 0;

    int level = 0;
    float top = (float) maxHarmonic;
    while (level < wtLevels - 1 && top * inc >= limit)
    {
        top *= 0.5f;
        ++level;
    }
    return level;
}

Wavetable::FramePair Wavetable::framePair (int level, float position) const noexcept
{
    FramePair fp;
    if (numFrames <= 1)
    {
        fp.a = fp.b = getFrame (level, 0);
        return fp;
    }

    const float pos = position >= 0.0f ? (position <= 1.0f ? position : 1.0f) : 0.0f;   // also catches NaN
    const float x = pos * (float) (numFrames - 1);
    const int i = jmin ((int) x, numFrames - 2);
    fp.a = getFrame (level, i);
    fp.b = getFrame (level, i + 1);
    fp.t = jlimit (0.0f, 1.0f, x - (float) i);
    return fp;
}

std::vector<float> Wavetable::getRawFrames() const
{
    std::vector<float> out ((size_t) numFrames * wtSize);
    for (int f = 0; f < numFrames; ++f)
        std::copy (getFrame (0, f), getFrame (0, f) + wtSize, out.begin() + (std::ptrdiff_t) f * wtSize);
    return out;
}

void Wavetable::getDisplayFrame (int frame, float* dest, int n) const
{
    if (dest == nullptr || n <= 0)
        return;

    const float* src = getFrame (0, frame);
    if (n == 1)
    {
        dest[0] = src[0];
        return;
    }

    const float step = (float) wtSize / (float) (n - 1);
    for (int i = 0; i < n; ++i)
    {
        const float x = jmin ((float) wtSize, (float) i * step);
        const int k = jmin ((int) x, wtSize - 1);
        const float f = x - (float) k;
        dest[i] = src[k] + f * (src[k + 1] - src[k]);
    }
}

//==============================================================================
WavetablePtr loadWavetableFile (const File& file, String* error)
{
    if (! file.existsAsFile())
    {
        if (error != nullptr)
            *error = "File not found";
        return nullptr;
    }

    int clmFrameSize = 0;
    {
        FileInputStream in (file);
        if (in.openedOk())
            clmFrameSize = findClmFrameSize (in);
    }

    AudioFormatManager formats;
    formats.registerBasicFormats();
    return loadFromReader (std::unique_ptr<AudioFormatReader> (formats.createReaderFor (file)), clmFrameSize,
                           file.getFileNameWithoutExtension(), error);
}

WavetablePtr loadWavetableData (const void* data, size_t numBytes, const String& name, String* error)
{
    if (data == nullptr || numBytes < 12)
    {
        if (error != nullptr)
            *error = "That file is empty";
        return nullptr;
    }

    int clmFrameSize = 0;
    {
        MemoryInputStream in (data, numBytes, false);
        clmFrameSize = findClmFrameSize (in);
    }

    AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> reader (
        formats.createReaderFor (std::make_unique<MemoryInputStream> (data, numBytes, false)));
    return loadFromReader (std::move (reader), clmFrameSize, name, error);
}

bool saveWavetableFile (const Wavetable& table, const File& file)
{
    const auto samples = table.getRawFrames();
    const auto dataBytes = (uint32) (samples.size() * sizeof (float));

    static constexpr char clmText[] = "<!>2048 00000000 ericriusSynth";
    const auto clmBytes = (uint32) (sizeof (clmText) - 1);
    const auto clmPadded = clmBytes + (clmBytes & 1);

    constexpr uint32 fmtBytes = 18, factBytes = 4;
    const uint32 riffBytes = 4 + (8 + fmtBytes) + (8 + factBytes) + (8 + clmPadded) + (8 + dataBytes);

    MemoryOutputStream out;
    out.preallocate ((size_t) riffBytes + 8);

    writeChunkHeader (out, "RIFF", riffBytes);
    out.write ("WAVE", 4);

    writeChunkHeader (out, "fmt ", fmtBytes);
    out.writeShort (3);                      // WAVE_FORMAT_IEEE_FLOAT
    out.writeShort (1);                      // mono
    out.writeInt (44100);
    out.writeInt (44100 * 4);                // bytes per second
    out.writeShort (4);                      // block align
    out.writeShort (32);                     // bits per sample
    out.writeShort (0);                      // no extension

    writeChunkHeader (out, "fact", factBytes);
    out.writeInt ((int) samples.size());

    writeChunkHeader (out, "clm ", clmBytes);
    out.write (clmText, clmBytes);
    if (clmPadded != clmBytes)
        out.writeByte (0);

    writeChunkHeader (out, "data", dataBytes);
    for (auto s : samples)
        out.writeFloat (s);

    return file.replaceWithData (out.getData(), out.getDataSize());
}
} // namespace ers
