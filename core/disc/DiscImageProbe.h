#pragma once
#include "ReadOnlyByteSource.h"
namespace sonicator {
enum class DiscImageKind
{
    unknown,
    iso9660,
    akaiRaw,
    mdx
};
struct DiscProbe
{
    DiscImageKind kind = DiscImageKind::unknown;
    bool joliet = false;
    uint64_t volumeSpaceBlocks = 0;
    uint32_t logicalBlockSize = 0;
    uint16_t akaiVolumeType = 0;
    uint16_t akaiVolumeStartBlock = 0;
};
// Content-only recognition. The image has no file name here, so an extension can never select a
// parser. Candidate recognition is cheap and bounded; it never walks a directory tree.
DiscResult<DiscProbe> probeDisc(const ReadOnlyByteSource& source, const DiscLimits& limits,
                                CancelCheck cancel);
} // namespace sonicator
