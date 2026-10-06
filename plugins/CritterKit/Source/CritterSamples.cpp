#include "CritterSamples.h"

using namespace juce;

namespace critter
{
namespace
{
    void computeOverview (SampleData& data)
    {
        const int buckets = (int) data.overview.size();
        const int frames = data.audio.getNumSamples();
        float maxPeak = 1.0e-6f;
        for (int b = 0; b < buckets; ++b)
        {
            const int start = (int) ((int64) frames * b / buckets);
            const int end = jmax (start + 1, (int) ((int64) frames * (b + 1) / buckets));
            float peak = 0.0f;
            for (int ch = 0; ch < data.audio.getNumChannels(); ++ch)
                peak = jmax (peak, data.audio.getMagnitude (ch, start, jmin (end, frames) - start));
            data.overview[(size_t) b] = peak;
            maxPeak = jmax (maxPeak, peak);
        }
        for (auto& v : data.overview)
            v /= maxPeak;
    }

    std::unique_ptr<SampleData> decode (AudioFormatReader& reader)
    {
        const double rate = reader.sampleRate > 1000.0 ? reader.sampleRate : 44100.0;
        const int channels = jlimit (1, 2, (int) reader.numChannels);
        const int64 maxFrames = (int64) (rate * maxSampleSeconds);
        const bool truncated = reader.lengthInSamples > maxFrames;
        const int frames = (int) jmin (reader.lengthInSamples, maxFrames);
        if (frames < 8)
            return nullptr;

        auto data = std::make_unique<SampleData>();
        data->sourceRate = rate;
        data->audio.setSize (channels, frames);
        data->audio.clear();
        if (! reader.read (&data->audio, 0, frames, 0, true, channels > 1))
            return nullptr;

        // Keep it finite and tame.
        for (int ch = 0; ch < channels; ++ch)
        {
            auto* d = data->audio.getWritePointer (ch);
            for (int i = 0; i < frames; ++i)
                d[i] = std::isfinite (d[i]) ? jlimit (-4.0f, 4.0f, d[i]) : 0.0f;
        }

        // Identical stereo channels -> mono (half the work for the audio thread).
        if (channels == 2)
        {
            const float* l = data->audio.getReadPointer (0);
            const float* r = data->audio.getReadPointer (1);
            bool same = true;
            for (int i = 0; i < frames && same; ++i)
                same = std::abs (l[i] - r[i]) < 1.0e-6f;
            if (same)
                data->audio.setSize (1, frames, true);
        }

        // A clipped-off 10 s sample gets a short fade so it doesn't end with a click.
        if (truncated)
        {
            const int fade = jmin (frames, (int) (0.02 * rate));
            data->audio.applyGainRamp (frames - fade, fade, 1.0f, 0.0f);
        }

        computeOverview (*data);
        return data;
    }

    void encodeRaw16 (const AudioBuffer<float>& audio, double rate, MemoryBlock& out)
    {
        MemoryOutputStream stream (out, false);
        stream.writeInt (audio.getNumChannels());
        stream.writeInt (audio.getNumSamples());
        stream.writeDouble (rate);
        for (int i = 0; i < audio.getNumSamples(); ++i)
            for (int ch = 0; ch < audio.getNumChannels(); ++ch)
                stream.writeShort ((short) jlimit (-32767, 32767, roundToInt (audio.getSample (ch, i) * 32767.0f)));
    }

    void encodeEmbedded (SampleData& data)
    {
        data.embedded.reset();
       #if JUCE_USE_FLAC
        {
            FlacAudioFormat flac;
            if (flac.getPossibleSampleRates().contains (roundToInt (data.sourceRate))
                && std::abs (data.sourceRate - std::round (data.sourceRate)) < 0.001)
            {
                std::unique_ptr<OutputStream> stream = std::make_unique<MemoryOutputStream> (data.embedded, false);
                const auto options = AudioFormatWriterOptions().withSampleRate (data.sourceRate)
                                                               .withNumChannels (data.audio.getNumChannels())
                                                               .withBitsPerSample (16);
                if (auto writer = flac.createWriterFor (stream, options))
                {
                    // 16-bit with headroom: clamp so that loud samples survive the trip.
                    AudioBuffer<float> clipped (data.audio);
                    for (int ch = 0; ch < clipped.getNumChannels(); ++ch)
                    {
                        auto* d = clipped.getWritePointer (ch);
                        for (int i = 0; i < clipped.getNumSamples(); ++i)
                            d[i] = jlimit (-1.0f, 1.0f, d[i]);
                    }
                    writer->writeFromAudioSampleBuffer (clipped, 0, clipped.getNumSamples());
                    writer.reset();
                    if (data.embedded.getSize() > 0)
                    {
                        data.embeddedFormat = "flac";
                        return;
                    }
                }
            }
        }
       #endif
        data.embedded.reset();
        encodeRaw16 (data.audio, data.sourceRate, data.embedded);
        data.embeddedFormat = "raw16";
    }

    std::unique_ptr<SampleData> decodeRaw16 (const MemoryBlock& block)
    {
        MemoryInputStream in (block, false);
        const int channels = in.readInt();
        const int frames = in.readInt();
        const double rate = in.readDouble();
        if (channels < 1 || channels > 2 || frames < 8 || rate < 1000.0 || rate > 1.0e6
            || (int64) block.getSize() < 16 + (int64) frames * channels * 2)
            return nullptr;

        auto data = std::make_unique<SampleData>();
        data->sourceRate = rate;
        data->audio.setSize (channels, frames);
        for (int i = 0; i < frames; ++i)
            for (int ch = 0; ch < channels; ++ch)
                data->audio.setSample (ch, i, (float) in.readShort() / 32767.0f);
        return data;
    }
} // namespace

std::unique_ptr<SampleData> SampleCodec::fromFile (AudioFormatManager& formats, const File& file, String& error)
{
    if (! file.existsAsFile())
    {
        error = "File not found";
        return nullptr;
    }

    std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Can't read that kind of file";
        return nullptr;
    }

    auto data = decode (*reader);
    if (data == nullptr)
    {
        error = "That file is empty";
        return nullptr;
    }

    data->filePath = file.getFullPathName();
    data->displayName = file.getFileNameWithoutExtension();
    encodeEmbedded (*data);
    return data;
}

std::unique_ptr<SampleData> SampleCodec::fromEmbedded (AudioFormatManager& formats, const MemoryBlock& block,
                                                      const String& format, const String& path, const String& name)
{
    if (block.getSize() < 16)
        return nullptr;

    std::unique_ptr<SampleData> data;
    if (format == "raw16")
    {
        data = decodeRaw16 (block);
        if (data != nullptr)
            computeOverview (*data);
    }
    else
    {
        std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (std::make_unique<MemoryInputStream> (block, false)));
        if (reader != nullptr)
            data = decode (*reader);
    }

    if (data == nullptr)
        return nullptr;

    data->embedded = block;
    data->embeddedFormat = format == "raw16" ? "raw16" : "flac";
    data->filePath = path;
    data->displayName = name.isNotEmpty() ? name : File (path).getFileNameWithoutExtension();
    return data;
}

bool SampleCodec::canLoad (AudioFormatManager& formats, const String& path)
{
    const auto ext = File (path).getFileExtension();
    return ext.isNotEmpty() && formats.findFormatForFileExtension (ext) != nullptr;
}
} // namespace critter
