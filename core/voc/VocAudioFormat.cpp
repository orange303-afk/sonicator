#include "VocAudioFormat.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
namespace sonicator {
namespace {
constexpr int64_t maxFileBytes = 256 * 1024 * 1024;
constexpr int64_t maxDecodedFrames = 128LL * 1024 * 1024;
constexpr const char* vocMagic = "Creative Voice File";

int64_t readLe(const juce::uint8* p, int count)
{
    int64_t value = 0;
    for (int i = 0; i < count; ++i) value |= (int64_t) p[i] << (8 * i);
    return value;
}

int rateFromTimeConstant(int rateByte)
{
    // VOC stores a time constant: 256 - 1e6/rate. A 0 byte is the fastest representable value.
    const int divisor = 256 - juce::jlimit(0, 255, rateByte);
    return (int) std::lround(1000000.0 / (double) juce::jmax(1, divisor));
}

struct VoiceFormat
{
    int rate = 0;
    int channels = 1;
    int bits = 8;
    int codec = 0; // 0 PCM, 1 4-bit ADPCM, 2 3-bit, 3 2-bit
    bool adpcm = false;
};

struct AdpcmState
{
    int predictor[2] { 0, 0 };
    int step[2] { 0, 0 };
};

struct Output
{
    int rate = 0;
    int channels = 0;
    int bits = 8;
    std::vector<float> left, right;
    int64_t frames() const { return (int64_t) left.size(); }
};
} // namespace

bool decodeVoc(const std::vector<std::byte>& input, VocAudio& out, juce::String& error)
{
    error.clear();
    out = {};
    if (input.size() < 26 || input.size() > (size_t) maxFileBytes)
    {
        error = "Not a Creative Voice File or the file is too large";
        return false;
    }
    const auto* bytes = (const juce::uint8*) input.data();
    const auto size = (int64_t) input.size();
    // The first nineteen bytes are the signature. Byte 19 is normally 0x1A, but some old DOS
    // tools wrote a stray value there while keeping a valid offset and version; the offset check
    // below is the stronger structural test, so the terminator byte is not required.
    if (std::memcmp(bytes, vocMagic, 19) != 0)
    {
        error = "Not a Creative Voice File (missing signature)";
        return false;
    }
    int64_t position = readLe(bytes + 20, 2);
    if (position < 26 || position >= size)
    {
        error = "Creative Voice File has an invalid data offset";
        return false;
    }

    Output output;
    bool haveFormat = false;
    VoiceFormat lastFormat;
    AdpcmState adpcm;
    bool pendingStereo = false;
    int64_t repeatStart = -1;
    int repeatCount = 1;

    auto fail = [&] (const juce::String& reason) { error = reason; return false; };
    auto ensureFormat = [&] (int rate, int channels, int bits)
    {
        if (rate <= 0) return fail("VOC audio block has an invalid sample rate");
        if (channels < 1 || channels > 2) return fail("VOC audio block has an unsupported channel count");
        if (! haveFormat || output.frames() == 0)
        {
            output.rate = rate;
            output.channels = channels;
            output.bits = bits;
            haveFormat = true;
            return true;
        }
        if (output.rate == rate && output.channels == channels)
        {
            output.bits = bits;
            return true;
        }
        // VOC compilations concatenate clips recorded at different rates and widths. Keep one
        // output stream by resampling the part decoded so far to the new block's format; this is
        // what a DOS player effectively did when the application changed the DAC rate.
        const int64_t oldFrames = output.frames();
        const int oldRate = output.rate;
        const int oldChannels = output.channels;
        const int64_t newFrames = rate == oldRate ? oldFrames
            : juce::jmax<int64_t>(1, std::llround((double) oldFrames * rate / oldRate));
        auto resample = [&] (const std::vector<float>& source)
        {
            std::vector<float> result((size_t) newFrames);
            if (source.empty() || oldFrames <= 0) return result;
            if (rate == oldRate)
            {
                std::copy(source.begin(), source.end(), result.begin());
                return result;
            }
            for (int64_t i = 0; i < newFrames; ++i)
            {
                const double position = (double) i * oldRate / rate;
                const auto index = (int64_t) position;
                const double fraction = position - (double) index;
                const auto a = source[(size_t) juce::jlimit<int64_t>(0, oldFrames - 1, index)];
                const auto b = source[(size_t) juce::jlimit<int64_t>(0, oldFrames - 1, index + 1)];
                result[(size_t) i] = (float) ((double) a * (1.0 - fraction) + (double) b * fraction);
            }
            return result;
        };
        auto left = resample(output.left);
        auto right = oldChannels > 1 ? resample(output.right) : std::vector<float>();
        if (channels == 2 && oldChannels == 1) right = left;
        if (channels == 1 && oldChannels == 2)
        {
            for (int64_t i = 0; i < newFrames; ++i)
                left[(size_t) i] = 0.5f * (left[(size_t) i] + right[(size_t) i]);
            right.clear();
        }
        if (repeatStart >= 0)
            repeatStart = rate == oldRate ? repeatStart
                : (int64_t) std::llround((double) repeatStart * rate / oldRate);
        output.rate = rate;
        output.channels = channels;
        output.bits = bits;
        output.left = std::move(left);
        output.right = std::move(right);
        return true;
    };
    auto reserveFrames = [&] (int64_t extra)
    {
        if (extra < 0 || output.frames() + extra > maxDecodedFrames)
            return fail("VOC exceeds the 128 MiB decoded audio limit");
        return true;
    };
    auto append = [&] (float left, float right)
    {
        if (output.frames() >= maxDecodedFrames) return false;
        output.left.push_back(left);
        if (output.channels > 1) output.right.push_back(right);
        return true;
    };
    auto appendSilence = [&] (int64_t frames)
    {
        if (frames <= 0) return true;
        if (! reserveFrames(frames)) return false;
        output.left.insert(output.left.end(), (size_t) frames, 0.0f);
        if (output.channels > 1) output.right.insert(output.right.end(), (size_t) frames, 0.0f);
        return true;
    };
    auto expand = [&] (int channel, int nibble, int width, int shift) -> float
    {
        const int sign = nibble & (1 << (width - 1));
        const int delta = nibble & ((1 << (width - 1)) - 1);
        const int diff = delta << (7 + adpcm.step[channel] + shift);
        int predictor = adpcm.predictor[channel] + (sign != 0 ? -diff : diff);
        predictor = juce::jlimit(-16384, 16256, predictor);
        adpcm.predictor[channel] = predictor;
        if (delta >= 2 * width - 3 && adpcm.step[channel] < 3) ++adpcm.step[channel];
        else if (delta == 0 && adpcm.step[channel] > 0) --adpcm.step[channel];
        return (float) predictor / 32768.0f;
    };
    auto decodeVoice = [&] (const juce::uint8* data, int64_t length, VoiceFormat format,
                            bool startsNew) -> bool
    {
        if (! ensureFormat(format.rate, format.channels, format.bits)) return false;
        lastFormat = format;
        if (! format.adpcm)
        {
            const int bytesPerSample = format.bits == 16 ? 2 : 1;
            const int frameBytes = bytesPerSample * format.channels;
            if (frameBytes <= 0) return fail("VOC PCM block has an invalid frame size");
            const int64_t frames = length / frameBytes;
            if (! reserveFrames(frames)) return false;
            for (int64_t frame = 0; frame < frames; ++frame)
            {
                const auto* p = data + frame * frameBytes;
                float channels[2] { 0.0f, 0.0f };
                for (int ch = 0; ch < format.channels; ++ch)
                {
                    if (format.bits == 16)
                    {
                        const auto raw = (int16_t) (juce::uint16) readLe(p + ch * 2, 2);
                        channels[ch] = (float) raw / 32768.0f;
                    }
                    else
                        channels[ch] = ((float) p[ch] - 128.0f) / 128.0f;
                }
                if (! append(channels[0], channels[1])) return fail("VOC exceeds the decoded audio limit");
            }
            return true;
        }
        if (startsNew)
        {
            if (length < format.channels) return fail("VOC ADPCM block is missing its first sample");
            for (int ch = 0; ch < format.channels; ++ch)
            {
                adpcm.predictor[ch] = 128 * ((int) data[ch] - 128);
                adpcm.step[ch] = 0;
            }
            data += format.channels;
            length -= format.channels;
            // Creative ADPCM begins with one raw 8-bit sample per channel; it is audio, not a
            // header byte (the decoder state after it is what the first nibble modulates).
            if (! reserveFrames(1)) return false;
            if (! append((float) adpcm.predictor[0] / 32768.0f,
                         format.channels > 1 ? (float) adpcm.predictor[1] / 32768.0f : 0.0f))
                return fail("VOC exceeds the decoded audio limit");
        }
        if (format.codec == 1)
        {
            if (format.channels == 2)
            {
                if (! reserveFrames(length)) return false;
                for (int64_t i = 0; i < length; ++i)
                {
                    const auto left = expand(0, data[i] >> 4, 4, 0);
                    const auto right = expand(1, data[i] & 0x0F, 4, 0);
                    if (! append(left, right)) return fail("VOC exceeds the decoded audio limit");
                }
            }
            else
            {
                if (! reserveFrames(length * 2)) return false;
                for (int64_t i = 0; i < length; ++i)
                {
                    if (! append(expand(0, data[i] >> 4, 4, 0), 0.0f)
                        || ! append(expand(0, data[i] & 0x0F, 4, 0), 0.0f))
                        return fail("VOC exceeds the decoded audio limit");
                }
            }
            return true;
        }
        if (format.codec == 2)
        {
            if (format.channels != 1)
                return fail("Stereo 2.6-bit Creative ADPCM is not supported");
            if (! reserveFrames(length * 3)) return false;
            for (int64_t i = 0; i < length; ++i)
            {
                const auto first = expand(0, data[i] >> 5, 3, 0);
                const auto second = expand(0, (data[i] >> 2) & 0x07, 3, 0);
                const auto third = expand(0, data[i] & 0x03, 2, 0);
                if (! append(first, 0.0f) || ! append(second, 0.0f) || ! append(third, 0.0f))
                    return fail("VOC exceeds the decoded audio limit");
            }
            return true;
        }
        // 2-bit Creative ADPCM: four 2-bit values per byte, interleaved when stereo.
        if (format.channels == 2)
        {
            if (! reserveFrames(length * 2)) return false;
            for (int64_t i = 0; i < length; ++i)
                for (int pair = 0; pair < 2; ++pair)
                {
                    const auto left = expand(0, (data[i] >> (6 - pair * 4)) & 0x03, 2, 2);
                    const auto right = expand(1, (data[i] >> (4 - pair * 4)) & 0x03, 2, 2);
                    if (! append(left, right)) return fail("VOC exceeds the decoded audio limit");
                }
        }
        else
        {
            if (! reserveFrames(length * 4)) return false;
            for (int64_t i = 0; i < length; ++i)
                for (int part = 3; part >= 0; --part)
                    if (! append(expand(0, (data[i] >> (part * 2)) & 0x03, 2, 2), 0.0f))
                        return fail("VOC exceeds the decoded audio limit");
        }
        return true;
    };

    int guard = 0;
    while (position < size)
    {
        if (++guard > 1000000) return fail("VOC has too many blocks");
        const int type = bytes[position];
        if (type == 0) break; // terminator: only the type byte is present
        if (position + 4 > size) return fail("VOC block header is truncated");
        const int64_t length = readLe(bytes + position + 1, 3);
        position += 4;
        if (length < 0 || position + length > size)
            return fail("VOC block data is truncated");
        const auto* data = bytes + position;
        position += length;

        if (type == 1)
        {
            if (length < 2) return fail("VOC sound block is too short");
            VoiceFormat format;
            format.rate = rateFromTimeConstant(data[0]);
            format.channels = pendingStereo ? 2 : 1;
            format.bits = 8;
            format.codec = data[1];
            format.adpcm = format.codec >= 1 && format.codec <= 3;
            if (format.codec > 3) return fail("Unsupported VOC compression type " + juce::String(format.codec));
            if (! decodeVoice(data + 2, length - 2, format, true)) return false;
            pendingStereo = false;
        }
        else if (type == 2)
        {
            if (! haveFormat) return fail("VOC sound continuation has no preceding sound block");
            if (! decodeVoice(data, length, lastFormat, false)) return false;
        }
        else if (type == 3)
        {
            if (length < 3) return fail("VOC silence block is too short");
            const int frames = (int) readLe(data, 2) + 1;
            const int rate = rateFromTimeConstant(data[2]);
            if (output.rate == 0)
            {
                output.rate = rate;
                output.channels = 1;
                output.bits = 8;
                haveFormat = true;
            }
            const int64_t scaled = output.rate == rate
                ? frames : (int64_t) std::llround((double) frames * output.rate / rate);
            if (! appendSilence(scaled)) return false;
        }
        else if (type == 4 || type == 5)
        {
            // Marker and ASCII text carry no audio.
        }
        else if (type == 6)
        {
            if (length < 2) return fail("VOC repeat block is too short");
            repeatCount = (int) readLe(data, 2);
            if (repeatCount == 0xFFFF) return fail("Endless VOC repeats are not supported");
            repeatStart = output.frames();
        }
        else if (type == 7)
        {
            if (repeatStart >= 0 && repeatCount > 1)
            {
                const int64_t end = output.frames();
                const int64_t segment = end - repeatStart;
                if (segment > 0 && ! reserveFrames(segment * (repeatCount - 1))) return false;
                for (int copy = 1; copy < repeatCount; ++copy)
                {
                    output.left.insert(output.left.end(), output.left.begin() + repeatStart,
                                       output.left.begin() + end);
                    if (output.channels > 1)
                        output.right.insert(output.right.end(), output.right.begin() + repeatStart,
                                            output.right.begin() + end);
                }
            }
            repeatStart = -1;
            repeatCount = 1;
        }
        else if (type == 8)
        {
            if (length < 4) return fail("VOC extended attribute block is too short");
            // Block 8 is an attribute extension for the following block 1: time constant, pack
            // method and voice mode. Only the mono/stereo mode affects playback.
            pendingStereo = data[3] != 0;
        }
        else if (type == 9)
        {
            if (length < 12) return fail("VOC new-format block is too short");
            VoiceFormat format;
            format.rate = (int) readLe(data, 4);
            format.bits = data[4];
            format.channels = data[5];
            const int codec = (int) readLe(data + 6, 2);
            format.codec = codec;
            if (format.rate <= 0) return fail("VOC new-format block has an invalid sample rate");
            if (codec == 0 || codec == 4) format.bits = format.bits == 16 || codec == 4 ? 16 : 8;
            else if (codec == 1 || codec == 2 || codec == 3) format.bits = 8;
            else if (codec == 6 || codec == 7)
                return fail("VOC A-law/u-law companding is not supported");
            else
                return fail("Unsupported VOC new-format codec " + juce::String(codec));
            format.adpcm = codec >= 1 && codec <= 3;
            if (! decodeVoice(data + 12, length - 12, format, true)) return false;
        }
        // Unknown block types are skipped by their declared length.
    }
    if (! haveFormat || output.frames() <= 0)
    {
        error = "VOC file has no audio data";
        return false;
    }
    out.sampleRate = output.rate;
    out.channels = output.channels;
    out.bitsPerSample = output.bits;
    out.frames = output.frames();
    out.left = std::move(output.left);
    out.right = std::move(output.right);
    if (out.channels > 1 && out.right.size() != out.left.size())
    {
        error = "VOC stereo channels have different lengths";
        return false;
    }
    return true;
}

namespace {
class VocAudioReader final : public juce::AudioFormatReader
{
public:
    VocAudioReader(VocAudio audio, juce::InputStream* stream)
        : juce::AudioFormatReader(stream, "Creative VOC"), audio(std::move(audio))
    {
        sampleRate = this->audio.sampleRate;
        bitsPerSample = this->audio.bitsPerSample;
        usesFloatingPointData = false;
        lengthInSamples = this->audio.frames;
        numChannels = (unsigned int) this->audio.channels;
    }
    bool readSamples(int* const* destChannels, int numDestChannels, int startOffsetInDestBuffer,
                     juce::int64 startSampleInFile, int numSamples) override
    {
        const auto available = lengthInSamples - startSampleInFile;
        const int toRead = (int) juce::jlimit<juce::int64>(0, numSamples, available);
        for (int channel = 0; channel < numDestChannels; ++channel)
        {
            if (destChannels[channel] == nullptr) continue;
            const auto& source = channel == 1 && audio.right.size() == audio.left.size()
                ? audio.right : audio.left;
            for (int i = 0; i < toRead; ++i)
            {
                const float value = source[(size_t) (startSampleInFile + i)];
                const float scaled = juce::jlimit(-1.0f, 1.0f, value) * 2147483647.0f;
                destChannels[channel][startOffsetInDestBuffer + i] = (int) std::lround(scaled);
            }
            juce::zeromem(destChannels[channel] + startOffsetInDestBuffer + toRead,
                          (size_t) (numSamples - toRead) * sizeof(int));
        }
        return true;
    }
private:
    VocAudio audio;
};
} // namespace

VocAudioFormat::VocAudioFormat() : juce::AudioFormat("Creative VOC", ".voc") {}
juce::Array<int> VocAudioFormat::getPossibleSampleRates() { return {}; }
juce::Array<int> VocAudioFormat::getPossibleBitDepths() { return { 8, 16 }; }

juce::AudioFormatReader* VocAudioFormat::createReaderFor(juce::InputStream* sourceStream,
                                                         bool deleteStreamIfOpeningFails)
{
    auto refuse = [sourceStream, deleteStreamIfOpeningFails]() -> juce::AudioFormatReader*
    {
        if (deleteStreamIfOpeningFails) delete sourceStream;
        return nullptr;
    };
    if (sourceStream == nullptr) return nullptr;
    const auto total = sourceStream->getTotalLength();
    if (total <= 0 || total > maxFileBytes) return refuse();
    sourceStream->setPosition(0);
    std::vector<std::byte> bytes((size_t) total);
    size_t done = 0;
    while (done < bytes.size())
    {
        const int n = sourceStream->read(bytes.data() + done,
                                         (int) std::min<size_t>(bytes.size() - done, 1u << 20));
        if (n <= 0) break;
        done += (size_t) n;
    }
    if (done != bytes.size()) return refuse();
    VocAudio audio;
    juce::String error;
    if (! decodeVoc(bytes, audio, error)) return refuse();
    return new VocAudioReader(std::move(audio), sourceStream);
}

std::unique_ptr<juce::AudioFormatWriter> VocAudioFormat::createWriterFor(
    std::unique_ptr<juce::OutputStream>&, const juce::AudioFormatWriterOptions&)
{
    return nullptr; // VOC is read-only in Sonicator.
}
} // namespace sonicator
