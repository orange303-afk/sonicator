#include "SectorLayout.h"
#include <cstring>
#include <vector>
namespace sonicator {
namespace {
struct Geometry
{
    uint32_t physical = 0, userOffset = 0, payload = 0;
};
bool geometryFor(SectorProfile profile, Geometry& geometry)
{
    switch (profile)
    {
        case SectorProfile::cooked2048: geometry = { 2048, 0, 2048 }; return true;
        case SectorProfile::mode1_2352: geometry = { 2352, 16, 2048 }; return true;
        case SectorProfile::xaForm1_2352: geometry = { 2352, 24, 2048 }; return true;
    }
    return false;
}
bool multiplicationFits(uint64_t a, uint64_t b, uint64_t& product)
{
    if (a != 0 && b > UINT64_MAX / a) return false;
    product = a * b;
    return true;
}
bool hasSyncPattern(const std::byte* sector)
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(sector);
    if (bytes[0] != 0x00 || bytes[11] != 0x00) return false;
    for (int i = 1; i <= 10; ++i) if (bytes[i] != 0xff) return false;
    return true;
}
class TrackBlockSource final : public ReadOnlyBlockSource
{
public:
    TrackBlockSource(ByteSourcePtr bytes, TrackLayout layout, Geometry geometry, uint64_t maxRequest)
        : source(std::move(bytes)), track(layout), shape(geometry), maxRead(maxRequest)
    {
    }
    uint32_t logicalBlockSize() const override { return shape.payload; }
    uint64_t logicalBlockCount() const override { return track.sectorCount; }
    DiscResult<size_t> readBlocks(uint64_t firstBlock, uint32_t blockCount,
                                  std::span<std::byte> output, CancelCheck cancel) const override
    {
        DiscResult<size_t> result;
        if (cancel && cancel()) { result.status = DiscStatus::cancelled; return result; }
        const uint64_t wanted = (uint64_t) blockCount * shape.payload;
        if (wanted > maxRead)
        {
            result.status = DiscStatus::limitExceeded;
            result.error = "block request exceeds the read limit";
            return result;
        }
        if (blockCount == 0) { result.status = DiscStatus::ready; result.value = 0; return result; }
        if (output.size() < wanted)
        {
            result.status = DiscStatus::malformed;
            result.error = "output buffer is smaller than the requested blocks";
            return result;
        }
        uint64_t end = 0;
        if (! multiplicationFits(track.sectorCount, shape.physical, end)
            || firstBlock > track.sectorCount || blockCount > track.sectorCount - firstBlock)
        {
            result.status = DiscStatus::ioError;
            result.error = "block range is outside the track";
            return result;
        }
        std::vector<std::byte> scratch(shape.physical);
        for (uint32_t i = 0; i < blockCount; ++i)
        {
            const uint64_t sector = firstBlock + i;
            const uint64_t at = track.fileOffset + sector * shape.physical;
            const auto read = source->readAt(at, scratch, cancel);
            if (! read.ok())
            {
                result.status = read.status;
                result.error = read.error;
                return result;
            }
            if (read.value != shape.physical)
            {
                result.status = DiscStatus::ioError;
                result.error = "short physical sector";
                return result;
            }
            if (shape.physical == 2352)
            {
                if (! hasSyncPattern(scratch.data()) || std::to_integer<int>(scratch[15]) != (shape.userOffset == 16 ? 1 : 2))
                {
                    result.status = DiscStatus::malformed;
                    result.error = "sector sync/mode header is invalid";
                    return result;
                }
                if (shape.userOffset == 24)
                {
                    if (std::memcmp(scratch.data() + 16, scratch.data() + 20, 4) != 0)
                    {
                        result.status = DiscStatus::malformed;
                        result.error = "XA subheader copies disagree";
                        return result;
                    }
                    for (int copy = 0; copy < 2; ++copy)
                        if ((std::to_integer<unsigned char>(scratch[18 + copy * 4]) & 0x20) != 0)
                        {
                            result.status = DiscStatus::unsupported;
                            result.error = "Mode 2 Form 2 sectors are not data";
                            return result;
                        }
                }
            }
            std::memcpy(output.data() + (size_t) i * shape.payload,
                        scratch.data() + shape.userOffset, shape.payload);
        }
        result.status = DiscStatus::ready;
        result.value = (size_t) wanted;
        return result;
    }
private:
    ByteSourcePtr source;
    TrackLayout track;
    Geometry shape;
    uint64_t maxRead = 0;
};
} // namespace
DiscResult<BlockSourcePtr> makeTrackSource(ByteSourcePtr bytes, const TrackLayout& layout,
                                           const DiscLimits& limits)
{
    DiscResult<BlockSourcePtr> result;
    if (bytes == nullptr) { result.status = DiscStatus::ioError; result.error = "no source"; return result; }
    Geometry shape;
    if (! geometryFor(layout.profile, shape))
    {
        result.status = DiscStatus::unsupported;
        result.error = "unsupported sector profile";
        return result;
    }
    uint64_t bytesNeeded = 0;
    if (! multiplicationFits(layout.sectorCount, shape.physical, bytesNeeded)
        || layout.fileOffset > bytes->size() || bytesNeeded > bytes->size() - layout.fileOffset)
    {
        result.status = DiscStatus::malformed;
        result.error = "track layout exceeds the source";
        return result;
    }
    result.status = DiscStatus::ready;
    result.value = std::make_shared<TrackBlockSource>(std::move(bytes), layout, shape,
                                                      juce::jmax<uint64_t>(1, limits.maxReadBytes));
    return result;
}
} // namespace sonicator
