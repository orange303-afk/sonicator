#include "AkaiSampleReader.h"
#include <array>
#include <cmath>
#include <vector>
namespace sonicator {
namespace {
constexpr uint64_t headerBytes = 150;
uint16_t le16(const std::byte* p)
{
    return (uint16_t) (std::to_integer<unsigned>(p[0]) | (std::to_integer<unsigned>(p[1]) << 8));
}
uint32_t le32(const std::byte* p)
{
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value |= (uint32_t) std::to_integer<unsigned>(p[i]) << (8 * i);
    return value;
}
} // namespace
DiscResult<SampleAudioPtr> decodeAkaiSample(ByteSourcePtr source, uint64_t declaredBytes,
                                            const DiscLimits& limits, CancelCheck cancel)
{
    DiscResult<SampleAudioPtr> result;
    if (source == nullptr) { result.status = DiscStatus::ioError; result.error = "no sample source"; return result; }
    if (cancel && cancel()) { result.status = DiscStatus::cancelled; return result; }
    if (source->size() < declaredBytes)
    {
        result.status = DiscStatus::malformed;
        result.error = "declared sample size exceeds the source";
        return result;
    }
    if (declaredBytes < headerBytes + 2)
    {
        result.status = DiscStatus::malformed;
        result.error = "sample is shorter than its header";
        return result;
    }
    std::array<std::byte, headerBytes> header {};
    const auto headerRead = source->readAt(0, header, cancel);
    if (! headerRead.ok() || headerRead.value != headerBytes)
    {
        result.status = DiscStatus::malformed;
        result.error = "sample header cannot be read";
        return result;
    }
    const auto headerId = std::to_integer<unsigned>(header[0]);
    if (headerId != 1 && headerId != 3)
    {
        result.status = DiscStatus::unsupported;
        result.error = "unverified Akai sample header id";
        return result;
    }
    const uint64_t frames = le32(header.data() + 0x1a);
    // The verified profile: exactly 150 header bytes and 2 bytes per frame. A 190-byte variant
    // or any other layout is refused rather than guessed.
    if (frames == 0 || frames > (declaredBytes - headerBytes) / 2
        || headerBytes + frames * 2 != declaredBytes)
    {
        result.status = DiscStatus::unsupported;
        result.error = "unverified Akai sample header variant (header/data size mismatch)";
        return result;
    }
    const auto rate = le16(header.data() + 0x8a);
    if (rate < 8000 || rate > 96000)
    {
        result.status = DiscStatus::malformed;
        result.error = "Akai sample rate is outside the supported range";
        return result;
    }
    if (frames > limits.maxDecodedSourceBytes / sizeof(float))
    {
        result.status = DiscStatus::limitExceeded;
        result.error = "Akai sample exceeds the decoded memory limit";
        return result;
    }
    auto audio = std::make_shared<SampleAudio>();
    audio->sampleRate = rate;
    audio->channels = 1;
    audio->bitsPerSample = 16;
    audio->floatingPoint = false;
    audio->frames = (int64_t) frames;
    audio->left.resize((size_t) frames);
    constexpr int64_t chunk = 65536;
    std::vector<std::byte> bytes((size_t) juce::jmin<int64_t>(chunk, (int64_t) frames) * 2);
    for (int64_t offset = 0; offset < (int64_t) frames; offset += chunk)
    {
        if (cancel && cancel()) { result.status = DiscStatus::cancelled; return result; }
        const auto count = juce::jmin<int64_t>(chunk, (int64_t) frames - offset);
        const auto read = source->readAt(headerBytes + (uint64_t) offset * 2,
                                         std::span<std::byte>(bytes.data(), (size_t) count * 2), cancel);
        if (! read.ok() || read.value != (size_t) count * 2)
        {
            result.status = read.ok() ? DiscStatus::malformed : read.status;
            result.error = read.ok() ? "sample data is truncated" : read.error;
            return result;
        }
        for (int64_t i = 0; i < count; ++i)
        {
            const auto raw = (int16_t) le16(bytes.data() + (size_t) i * 2);
            audio->left[(size_t) (offset + i)] = (float) raw / 32768.0f;
        }
    }
    result.status = DiscStatus::ready;
    result.value = std::move(audio);
    return result;
}
} // namespace sonicator
