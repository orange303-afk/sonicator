#include "DiscImageProbe.h"
#include <array>
#include <cstring>
#include <vector>
namespace sonicator {
namespace {
uint16_t le16(const std::byte* p)
{
    return (uint16_t) (std::to_integer<unsigned>(p[0])
                     | (std::to_integer<unsigned>(p[1]) << 8));
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
bool asciiIs(const std::byte* p, const char* text, size_t count)
{
    return std::memcmp(p, text, count) == 0;
}
bool akaiNameByte(unsigned char value) { return value <= 0x28; }
} // namespace
DiscResult<DiscProbe> probeDisc(const ReadOnlyByteSource& source, const DiscLimits& limits,
                                CancelCheck cancel)
{
    DiscResult<DiscProbe> result;
    if (cancel && cancel()) { result.status = DiscStatus::cancelled; return result; }

    std::array<std::byte, 64> header {};
    const auto headerRead = source.readAt(0, header, cancel);
    if (! headerRead.ok()) { result.status = headerRead.status; result.error = headerRead.error; return result; }
    if (headerRead.value >= 16 && asciiIs(header.data(), "MEDIA DESCRIPTOR", 16))
    {
        result.status = DiscStatus::unsupported;
        result.value.kind = DiscImageKind::mdx;
        result.error = "MDX requires a separate decoder and licence gate";
        return result;
    }

    constexpr uint64_t descriptorSize = 2048;
    constexpr uint64_t firstDescriptor = 16;
    bool sawDescriptor = false, primary = false, terminated = false;
    uint64_t volumeBlocks = 0;
    uint32_t blockSize = 0;
    bool joliet = false;
    const uint32_t descriptorLimit = juce::jlimit(1u, 256u, limits.maxVolumeDescriptors);
    for (uint32_t i = 0; i < descriptorLimit; ++i)
    {
        if (cancel && cancel()) { result.status = DiscStatus::cancelled; return result; }
        std::array<std::byte, 2048> descriptor {};
        const auto read = source.readAt((firstDescriptor + i) * descriptorSize, descriptor, cancel);
        if (! read.ok())
        {
            if (! sawDescriptor)
            {
                // Not an ISO layout at all; the Akai check below still gets a chance.
                break;
            }
            result.status = DiscStatus::malformed;
            result.error = "volume descriptor area is truncated";
            return result;
        }
        const auto type = std::to_integer<unsigned>(descriptor[0]);
        if (! asciiIs(descriptor.data() + 1, "CD001", 5) || std::to_integer<unsigned>(descriptor[6]) != 1)
        {
            if (! sawDescriptor && type == 0 && asciiIs(descriptor.data() + 1, "BEA01", 5))
            {
                result.status = DiscStatus::unsupported;
                result.error = "UDF is not supported";
                return result;
            }
            break;
        }
        sawDescriptor = true;
        if (type == 255) { terminated = true; break; }
        if (type == 1)
        {
            primary = true;
            const auto littleBlocks = le32(descriptor.data() + 80);
            const auto bigBlocks = be32(descriptor.data() + 84);
            if (littleBlocks != bigBlocks)
            {
                result.status = DiscStatus::malformed;
                result.error = "volume space size copies disagree";
                return result;
            }
            volumeBlocks = littleBlocks;
            const auto littleBlockSize = le16(descriptor.data() + 128);
            const auto bigBlockSize = (uint16_t) ((std::to_integer<unsigned>(descriptor[130]) << 8)
                                                | std::to_integer<unsigned>(descriptor[131]));
            if (littleBlockSize != bigBlockSize)
            {
                result.status = DiscStatus::malformed;
                result.error = "logical block size copies disagree";
                return result;
            }
            blockSize = littleBlockSize;
        }
        else if (type == 2 && asciiIs(descriptor.data() + 88, "%/@", 3))
            joliet = true;
        else if (type == 2 && asciiIs(descriptor.data() + 88, "%/C", 3))
            joliet = true;
        else if (type == 2 && asciiIs(descriptor.data() + 88, "%/E", 3))
            joliet = true;
    }
    if (sawDescriptor && ! primary)
    {
        result.status = DiscStatus::malformed;
        result.error = "no primary volume descriptor";
        return result;
    }
    if (primary)
    {
        if (! terminated)
        {
            result.status = DiscStatus::malformed;
            result.error = "volume descriptor sequence has no terminator";
            return result;
        }
        if (blockSize != 2048)
        {
            result.status = DiscStatus::unsupported;
            result.error = "only ISO-9660 logical blocks of 2048 bytes are supported";
            return result;
        }
        if (volumeBlocks > source.size() / 2048)
        {
            result.status = DiscStatus::malformed;
            result.error = "declared volume space exceeds the image";
            return result;
        }
        result.status = DiscStatus::ready;
        result.value.kind = DiscImageKind::iso9660;
        result.value.joliet = joliet;
        result.value.volumeSpaceBlocks = volumeBlocks;
        result.value.logicalBlockSize = blockSize;
        return result;
    }

    // Raw Akai: volume entry 1 at 0xca and the three reserved FAT markers. These are structural
    // facts of the documented layout, not a guess from the file name or a size multiple.
    std::array<std::byte, 16> volume {};
    const auto volumeRead = source.readAt(0xca, volume, cancel);
    if (volumeRead.ok() && volumeRead.value == 16)
    {
        bool nameIsAkai = true;
        for (int i = 0; i < 12; ++i)
            if (! akaiNameByte(std::to_integer<unsigned char>(volume[(size_t) i]))) nameIsAkai = false;
        const auto type = le16(volume.data() + 12);
        const auto start = le16(volume.data() + 14);
        std::array<std::byte, 6> fat {};
        const auto fatRead = source.readAt(0x70a, fat, cancel);
        // Real S1000/S3000 images disagree on FAT byte order; both reserved-marker patterns are
        // structural evidence, so accept either and let the filesystem remember which one it saw.
        auto big16 = [](const std::byte* p) { return (uint16_t) ((std::to_integer<unsigned>(p[0]) << 8) | std::to_integer<unsigned>(p[1])); };
        const bool fatMarkers = fatRead.ok() && fatRead.value == 6
            && ((le16(fat.data()) == 0x40 && le16(fat.data() + 2) == 0x40 && le16(fat.data() + 4) == 0x40)
             || (big16(fat.data()) == 0x40 && big16(fat.data() + 2) == 0x40 && big16(fat.data() + 4) == 0x40));
        if (nameIsAkai && (type == 1 || type == 3) && start != 0 && fatMarkers)
        {
            result.status = DiscStatus::ready;
            result.value.kind = DiscImageKind::akaiRaw;
            result.value.akaiVolumeType = type;
            result.value.akaiVolumeStartBlock = start;
            return result;
        }
    }
    result.status = DiscStatus::unknown;
    result.error = "unrecognised disc image layout";
    return result;
}
} // namespace sonicator
