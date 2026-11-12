#include "DiscAudioReader.h"
#include "core/audio/SonicatorFormats.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>
namespace sonicator {
namespace {
class ByteSourceInputStream final : public juce::InputStream
{
public:
    explicit ByteSourceInputStream(ByteSourcePtr sourceBytes) : source(std::move(sourceBytes)) {}
    int read(void* destination, int bytes) override
    {
        if (bytes <= 0 || position >= source->size()) return 0;
        const auto wanted = (size_t) juce::jmin<uint64_t>((uint64_t) bytes, source->size() - position);
        const auto read = source->readAt(position, { static_cast<std::byte*>(destination), wanted }, {});
        if (! read.ok()) { error = read.status; return 0; }
        position += read.value;
        return (int) read.value;
    }
    bool setPosition(juce::int64 newPosition) override
    {
        if (newPosition < 0) return false;
        position = (uint64_t) newPosition;
        return true;
    }
    juce::int64 getPosition() override { return (juce::int64) position; }
    juce::int64 getTotalLength() override { return (juce::int64) source->size(); }
    bool isExhausted() override { return position >= source->size(); }
    DiscStatus error = DiscStatus::ready;
private:
    ByteSourcePtr source;
    uint64_t position = 0;
};
bool looksLikeAudioContainer(const ReadOnlyByteSource& source)
{
    std::array<std::byte, 12> header {};
    const auto read = source.readAt(0, header, {});
    if (! read.ok() || read.value < 4) return false;
    auto is = [&](const char* tag) { return std::memcmp(header.data(), tag, 4) == 0; };
    return is("RIFF") || is("FORM") || is("fLaC") || is("ID3\0") || is("OggS");
}
// Structural check for uncompressed containers before a decoder can hide a physically truncated
// payload behind a zero-filled read. Only declared-size violations are reported here.
bool declaredPayloadFits(const ReadOnlyByteSource& source, juce::String& error)
{
    std::array<std::byte, 12> header {};
    if (! source.readAt(0, header, {}).ok()) return true;
    auto is4 = [&](const std::byte* p, const char* tag) { return std::memcmp(p, tag, 4) == 0; };
    const bool wav = (is4(header.data(), "RIFF") || is4(header.data(), "RIFX") || is4(header.data(), "RF64"))
        && is4(header.data() + 8, "WAVE");
    const bool aiff = is4(header.data(), "FORM")
        && (is4(header.data() + 8, "AIFF") || is4(header.data() + 8, "AIFC"));
    if (! wav && ! aiff) return true;
    const bool big = aiff || is4(header.data(), "RIFX");
    const uint64_t total = source.size();
    for (uint64_t offset = 12; offset + 8 <= total;)
    {
        std::array<std::byte, 8> chunk {};
        if (! source.readAt(offset, chunk, {}).ok()) return true;
        auto u32 = [big](const std::byte* p)
        {
            uint32_t value = 0;
            for (int i = 0; i < 4; ++i)
            {
                const auto shift = big ? (8 * (3 - i)) : (8 * i);
                value |= (uint32_t) std::to_integer<unsigned>(p[i]) << shift;
            }
            return value;
        };
        const uint32_t size = u32(chunk.data() + 4);
        if (size == 0xffffffffu) return true; // RF64 extended size: not guessed here
        const bool audio = (wav && is4(chunk.data(), "data")) || (aiff && is4(chunk.data(), "SSND"));
        if (audio && (uint64_t) size > total - offset - 8)
        {
            error = "the audio payload is truncated";
            return false;
        }
        const uint64_t padded = (uint64_t) size + (size & 1);
        if (padded > total - offset - 8) break;
        offset += 8 + padded;
    }
    return true;
}
} // namespace
DiscResult<SampleAudioPtr> decodeDiscAudio(ByteSourcePtr source, const juce::String& decoderHint,
                                           const DiscLimits& limits, CancelCheck cancel)
{
    DiscResult<SampleAudioPtr> result;
    if (source == nullptr) { result.status = DiscStatus::ioError; result.error = "no audio source"; return result; }
    if (cancel && cancel()) { result.status = DiscStatus::cancelled; return result; }
    juce::String structuralError;
    if (! declaredPayloadFits(*source, structuralError))
    {
        result.status = DiscStatus::malformed;
        result.error = structuralError;
        return result;
    }

    juce::AudioFormatManager formats;
    registerSonicatorFormats(formats);
    auto stream = std::make_unique<ByteSourceInputStream>(source);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(std::move(stream)));
    if (reader == nullptr)
    {
        result.status = looksLikeAudioContainer(*source) ? DiscStatus::malformed : DiscStatus::unsupported;
        result.error = result.status == DiscStatus::malformed
            ? "the audio container cannot be decoded" : "no decoder accepted this file";
        return result;
    }
    const auto channels = (int) reader->numChannels;
    const auto frames = reader->lengthInSamples;
    if (channels <= 0 || channels > 2)
    {
        result.status = DiscStatus::unsupported;
        result.error = "only mono and stereo sources are supported";
        return result;
    }
    if (! std::isfinite(reader->sampleRate) || reader->sampleRate <= 0 || reader->sampleRate > 1000000)
    {
        result.status = DiscStatus::malformed;
        result.error = "the audio sample rate is invalid";
        return result;
    }
    if (frames <= 0)
    {
        result.status = DiscStatus::unknown;
        result.error = "the audio source is empty";
        return result;
    }
    if ((uint64_t) frames > limits.maxDecodedSourceBytes / ((uint64_t) channels * sizeof(float)))
    {
        result.status = DiscStatus::limitExceeded;
        result.error = "the decoded source exceeds the memory limit";
        return result;
    }
    const int sourceBits = (int) reader->bitsPerSample;
    const auto formatName = reader->getFormatName();
    const auto dot = decoderHint.lastIndexOfChar('.');
    const auto extension = dot >= 0 ? decoderHint.substring(dot).toLowerCase() : juce::String();
    const bool uncompressed = extension == ".wav" || extension == ".bwf"
        || extension == ".aif" || extension == ".aiff"
        || formatName.containsIgnoreCase("WAV") || formatName.containsIgnoreCase("AIFF");
    // Lossy/compressed decoders may expose float32 internally (macOS CoreAudio does for MP3).
    // The snapshot declares decoded precision, not that internal buffer format.
    const bool compressed = ! uncompressed;
    int outBits = 24;
    bool floating = false;
    if (compressed) { outBits = 24; floating = false; }
    else if (reader->usesFloatingPointData && sourceBits == 32) { outBits = 32; floating = true; }
    else if (sourceBits <= 16) outBits = 16;
    else if (sourceBits <= 24) outBits = 24;
    else
    {
        result.status = DiscStatus::unsupported;
        result.error = "integer audio above 24 bits is not supported without losing precision";
        return result;
    }

    auto audio = std::make_shared<SampleAudio>();
    audio->sampleRate = (int) juce::jlimit(1.0, 1000000.0, reader->sampleRate);
    audio->channels = channels;
    audio->bitsPerSample = outBits;
    audio->floatingPoint = floating;
    audio->frames = frames;
    audio->left.resize((size_t) frames);
    if (channels == 2) audio->right.resize((size_t) frames);

    constexpr int chunk = 65536;
    juce::AudioBuffer<float> buffer(channels, chunk);
    for (int64_t offset = 0; offset < frames; offset += chunk)
    {
        if (cancel && cancel()) { result.status = DiscStatus::cancelled; return result; }
        const int count = (int) juce::jmin<int64_t>(chunk, frames - offset);
        buffer.clear();
        if (! reader->read(&buffer, 0, count, offset, true, channels == 2))
        {
            result.status = DiscStatus::malformed;
            result.error = "audio decoding failed";
            return result;
        }
        for (int channel = 0; channel < channels; ++channel)
        {
            auto* destination = channel == 0 ? audio->left.data() : audio->right.data();
            const auto* samples = buffer.getReadPointer(channel);
            for (int i = 0; i < count; ++i)
            {
                if (! std::isfinite(samples[i]))
                {
                    result.status = DiscStatus::malformed;
                    result.error = "decoded audio contains a non-finite sample";
                    return result;
                }
                destination[(size_t) (offset + i)] = samples[i];
            }
        }
    }
    juce::ignoreUnused(decoderHint);
    result.status = DiscStatus::ready;
    result.value = std::move(audio);
    return result;
}
} // namespace sonicator
