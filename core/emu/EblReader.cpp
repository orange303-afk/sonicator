#include "EblReader.h"
#include <algorithm>
#include <array>
#include <cstring>
namespace sonicator {
namespace {
constexpr size_t firstSection = 0x14;
constexpr size_t secondSectionOffsetField = firstSection + 8;
constexpr size_t secondSectionLen = 14;
constexpr size_t nameLen = 64;
constexpr size_t v2Field = nameLen + 4;    // audio anchor
constexpr size_t v3Field = nameLen + 8;    // per-channel byte length
constexpr size_t rateField = nameLen + 36; // sample rate
constexpr size_t channelField = nameLen + 44; // V12
constexpr size_t blockLen = nameLen + 12 * 4 + nameLen;
constexpr uint32_t stereoChannelByte = 0x03;
constexpr uint64_t readCapSlack = 64 * 1024;
constexpr char sLoopMarker[4] { 'E', 'X', 'L', 'Z' };
constexpr char sMark[4] { 'M', 'A', 'R', 'K' };
constexpr auto equalsChar = [] (std::byte b, char c) { return b == (std::byte) c; };

EblResult fail(juce::String error)
{
    EblResult result;
    result.error = std::move(error);
    return result;
}
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
uint32_t be32(const std::byte* p)
{
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value = (value << 8) | (uint32_t) std::to_integer<unsigned>(p[i]);
    return value;
}
bool hasMagic(std::span<const std::byte> bytes, size_t offset, const char* magic)
{
    return offset + 4 <= bytes.size() && std::memcmp(bytes.data() + offset, magic, 4) == 0;
}
// The UTF-16LE name is space-padded and NUL-padded; both ends are trimmed.
juce::String decodeName(const std::byte* raw)
{
    std::array<int16_t, 33> units {};
    size_t n = 0;
    for (; n < 32; ++n)
    {
        const auto unit = le16(raw + 2 * n);
        if (unit == 0) break;
        units[n] = (int16_t) unit;
    }
    units[n] = 0;
    return juce::String(juce::CharPointer_UTF16(units.data())).trim();
}
// Where the optional EXLZ trailer begins, or -1. Only a complete MARK sub-chunk counts,
// so a damaged trailer degrades into audio instead of a wrong length.
int64_t trailerOffset(std::span<const std::byte> bytes)
{
    if (bytes.size() < 4) return -1;
    for (int64_t i = (int64_t) bytes.size() - 4; i >= 0; --i)
    {
        if (std::memcmp(bytes.data() + i, sLoopMarker, 4) != 0) continue;
        const auto begin = bytes.begin() + i + 4;
        const auto mark = std::search(begin, bytes.end(), sMark, sMark + 4, equalsChar);
        if (mark == bytes.end()) continue;
        const auto markOffset = (int64_t) (mark - bytes.begin());
        if (markOffset + 16 > (int64_t) bytes.size()) continue;
        return i;
    }
    return -1;
}
} // namespace

bool isEblFile(const juce::File& file)
{
    return file.hasFileExtension("ebl");
}

EblResult parseEbl(std::span<const std::byte> bytes, uint64_t maxDecodedBytes)
{
    if (bytes.size() < firstSection + 12 || ! hasMagic(bytes, 0, "FORM"))
        return fail("not a FORM");
    if (bytes.size() < 16 || std::memcmp(bytes.data() + 8, "E5B0TOC2", 8) != 0)
        return fail("not an EBL (no E5B0TOC2)");
    if (! hasMagic(bytes, firstSection, "E5S1"))
        return fail("first section is not E5S1");
    const int64_t second = (int64_t) be32(bytes.data() + secondSectionOffsetField);
    if (second + 4 > (int64_t) bytes.size() || ! hasMagic(bytes, (size_t) second, "E5S1"))
        return fail("second section is not where the header says");
    const int64_t block = second + (int64_t) secondSectionLen;
    if (block + (int64_t) channelField + 4 > (int64_t) bytes.size())
        return fail("data block runs past the end");

    const auto blockBytes = bytes.data() + block;
    const uint32_t rate = le32(blockBytes + rateField);
    if (rate == 0) return fail("sample rate is zero");
    const auto v2 = le32(blockBytes + v2Field);
    const bool stereo = ((le32(blockBytes + channelField) >> 16) & 0xff) == stereoChannelByte;

    const int64_t audioStart = block + (int64_t) v2 - 4;
    const int64_t trailer = trailerOffset(bytes);
    int64_t audioEnd = trailer >= 0 ? trailer : (int64_t) bytes.size();
    audioEnd = std::min(audioEnd, (int64_t) bytes.size());
    int64_t length = audioStart >= 0 ? audioEnd - audioStart : 0;
    if (length < 0) length = 0;
    length -= length % 2;

    EblSample sample;
    sample.name = decodeName(blockBytes);
    sample.sampleRate = rate;
    sample.channels = stereo ? 2 : 1;
    if (stereo)
    {
        // Stereo is LLLL...RRRR. The per-channel length is V3−V2 and the true start is
        // anchored from the end: the front anchor is two bytes late on some banks.
        const int64_t perChannel = (int64_t) le32(blockBytes + v3Field) - (int64_t) v2;
        const int64_t stereoStart = audioEnd - 2 * perChannel;
        int64_t frames = 0;
        bool fromEnd = false;
        if (perChannel > 0 && stereoStart >= block + (int64_t) blockLen)
        {
            frames = perChannel / 2;
            fromEnd = true;
        }
        else
        {
            // A truncated rip: split the front-anchored span at its midpoint and lose the tail.
            frames = (length / 2) / 2;
        }
        if (frames <= 0) return fail("no audio");
        if ((uint64_t) frames * 2 * sizeof(int16_t) > maxDecodedBytes)
            return fail("decoded size exceeds the limit");
        sample.frames = frames;
        sample.pcm.resize((size_t) (frames * 2));
        const int64_t left = fromEnd ? stereoStart : audioStart;
        const int64_t right = fromEnd ? stereoStart + perChannel : audioStart + length / 2;
        for (int64_t i = 0; i < frames; ++i)
        {
            sample.pcm[(size_t) (2 * i)] = (int16_t) le16(bytes.data() + left + 2 * i);
            sample.pcm[(size_t) (2 * i + 1)] = (int16_t) le16(bytes.data() + right + 2 * i);
        }
    }
    else
    {
        const int64_t frames = length / 2;
        if (frames <= 0) return fail("no audio");
        if ((uint64_t) frames * sizeof(int16_t) > maxDecodedBytes)
            return fail("decoded size exceeds the limit");
        sample.frames = frames;
        sample.pcm.resize((size_t) frames);
        for (int64_t i = 0; i < frames; ++i)
            sample.pcm[(size_t) i] = (int16_t) le16(bytes.data() + audioStart + 2 * i);
    }

    if (trailer >= 0)
    {
        const auto begin = bytes.begin() + trailer;
        const auto mark = std::search(begin, bytes.end(), sMark, sMark + 4, equalsChar);
        const auto markOffset = (int64_t) (mark - bytes.begin());
        const uint32_t start = le32(bytes.data() + markOffset + 8);
        const uint32_t end = le32(bytes.data() + markOffset + 12);
        if (start < end && (int64_t) end <= sample.frames)
            sample.loop = EblLoop { start, end };
    }
    EblResult result;
    result.sample = std::move(sample);
    return result;
}

EblResult readEblFile(const juce::File& file, uint64_t maxDecodedBytes)
{
    auto input = file.createInputStream();
    if (! input) return fail("cannot open file");
    const auto total = input->getTotalLength();
    if (total <= 0) return fail("empty file");
    if ((uint64_t) total > maxDecodedBytes + readCapSlack)
        return fail("decoded size exceeds the limit");
    std::vector<std::byte> bytes((size_t) total);
    size_t done = 0;
    while (done < bytes.size())
    {
        const int n = input->read(bytes.data() + done,
                                  (int) std::min<size_t>(bytes.size() - done, 1u << 20));
        if (n <= 0) break;
        done += (size_t) n;
    }
    if (done != bytes.size()) return fail("cannot read file");
    return parseEbl(bytes, maxDecodedBytes);
}
} // namespace sonicator
