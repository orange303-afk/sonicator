#include "AkaiSampleReader.h"
#include <array>
#include <cmath>
#include <vector>
namespace sonicator {
namespace {
constexpr uint64_t s1000HeaderBytes = 150;
constexpr uint64_t s3000HeaderBytes = 192;
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

DiscResult<AkaiSampleHeader> readAkaiSampleHeader(const ReadOnlyByteSource& source,
                                                  uint64_t declaredBytes, CancelCheck cancel)
{
    DiscResult<AkaiSampleHeader> result;
    if (cancel && cancel()) { result.status = DiscStatus::cancelled; return result; }
    if (declaredBytes > source.size())
    {
        result.status = DiscStatus::malformed;
        result.error = "declared sample size exceeds the source";
        return result;
    }
    if (declaredBytes < s1000HeaderBytes + 2)
    {
        result.status = DiscStatus::malformed;
        result.error = "sample is shorter than its header";
        return result;
    }
    std::array<std::byte, s3000HeaderBytes> header {};
    const auto want = (size_t) juce::jmin<uint64_t>(s3000HeaderBytes, declaredBytes);
    const auto read = source.readAt(0, std::span<std::byte>(header.data(), want), cancel);
    if (! read.ok() || read.value != want)
    {
        result.status = DiscStatus::malformed;
        result.error = "sample header cannot be read";
        return result;
    }
    const auto id = std::to_integer<unsigned>(header[0]);
    if (id != 1 && id != 3)
    {
        result.status = DiscStatus::unsupported;
        result.error = "unverified Akai sample header id";
        return result;
    }
    AkaiSampleHeader parsed;
    parsed.id = id;
    parsed.frames = le32(header.data() + 0x1a);
    parsed.startFrame = le32(header.data() + 0x1e);
    parsed.endFrame = le32(header.data() + 0x22);
    parsed.sampleRate = le16(header.data() + 0x8a);
    parsed.rootNote = std::to_integer<unsigned>(header[2]);
    // Only the exact size equation identifies the family: a 150-byte and a 192-byte header
    // cannot both satisfy it for the same declared size.
    const uint64_t dataBytes = (uint64_t) parsed.frames * 2;
    if (parsed.frames == 0)
    {
        result.status = DiscStatus::unsupported;
        result.error = "unverified Akai sample header variant (header/data size mismatch)";
        return result;
    }
    if (declaredBytes == s1000HeaderBytes + dataBytes) parsed.headerBytes = s1000HeaderBytes;
    else if (declaredBytes == s3000HeaderBytes + dataBytes) parsed.headerBytes = s3000HeaderBytes;
    else
    {
        result.status = DiscStatus::unsupported;
        result.error = "unverified Akai sample header variant (header/data size mismatch)";
        return result;
    }
    // The project-wide audio policy elsewhere (DiscAudioReader, StereoPair) accepts any positive
    // rate; a zero rate field is not a sample rate, and the S3000 field is 16-bit, so the upper
    // bound needs no separate check. Low rates such as 1280 Hz occur on real Akai discs.
    if (parsed.sampleRate == 0)
    {
        result.status = DiscStatus::malformed;
        result.error = "Akai sample rate is zero";
        return result;
    }
    result.status = DiscStatus::ready;
    result.value = parsed;
    return result;
}

DiscResult<std::vector<int16_t>> readAkaiPcm16(const ReadOnlyByteSource& source,
                                               const AkaiSampleHeader& header, CancelCheck cancel)
{
    DiscResult<std::vector<int16_t>> result;
    if (cancel && cancel()) { result.status = DiscStatus::cancelled; return result; }
    const uint64_t needed = (uint64_t) header.headerBytes + (uint64_t) header.frames * 2;
    if (header.frames == 0 || needed > source.size())
    {
        result.status = DiscStatus::malformed;
        result.error = "sample extent is shorter than its header declares";
        return result;
    }
    result.value.resize(header.frames);
    constexpr int64_t chunk = 65536;
    std::vector<std::byte> bytes((size_t) juce::jmin<int64_t>(chunk, (int64_t) header.frames) * 2);
    for (int64_t offset = 0; offset < (int64_t) header.frames; offset += chunk)
    {
        if (cancel && cancel()) { result.status = DiscStatus::cancelled; result.value.clear(); return result; }
        const auto count = juce::jmin<int64_t>(chunk, (int64_t) header.frames - offset);
        const auto read = source.readAt((uint64_t) header.headerBytes + (uint64_t) offset * 2,
                                        std::span<std::byte>(bytes.data(), (size_t) count * 2), cancel);
        if (! read.ok() || read.value != (size_t) count * 2)
        {
            result.status = read.ok() ? DiscStatus::malformed : read.status;
            result.error = read.ok() ? "sample data is truncated" : read.error;
            result.value.clear();
            return result;
        }
        for (int64_t i = 0; i < count; ++i)
            result.value[(size_t) (offset + i)] = (int16_t) le16(bytes.data() + (size_t) i * 2);
    }
    result.status = DiscStatus::ready;
    return result;
}

DiscResult<SampleAudioPtr> decodeAkaiSample(ByteSourcePtr source, uint64_t declaredBytes,
                                            const DiscLimits& limits, CancelCheck cancel)
{
    DiscResult<SampleAudioPtr> result;
    if (source == nullptr) { result.status = DiscStatus::ioError; result.error = "no sample source"; return result; }
    if (cancel && cancel()) { result.status = DiscStatus::cancelled; return result; }
    const auto header = readAkaiSampleHeader(*source, declaredBytes, cancel);
    if (! header.ok()) { result.status = header.status; result.error = header.error; return result; }
    const uint64_t headerLength = header.value.headerBytes;
    const uint64_t frames = header.value.frames;
    if (frames > limits.maxDecodedSourceBytes / sizeof(float))
    {
        result.status = DiscStatus::limitExceeded;
        result.error = "Akai sample exceeds the decoded memory limit";
        return result;
    }
    auto audio = std::make_shared<SampleAudio>();
    audio->sampleRate = header.value.sampleRate;
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
        const auto read = source->readAt(headerLength + (uint64_t) offset * 2,
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
