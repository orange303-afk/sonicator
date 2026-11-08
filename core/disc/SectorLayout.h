#pragma once
#include "ReadOnlyByteSource.h"
namespace sonicator {
// Which bytes of a physical sector carry user data. Audio sectors are deliberately absent: they
// are not a data filesystem and this stage does not extract CDDA.
enum class SectorProfile
{
    cooked2048,   // 2048-byte sectors, data at offset 0
    mode1_2352,   // 2352-byte Mode 1, user data at offset 16
    xaForm1_2352  // 2352-byte Mode 2 XA Form 1, data at offset 24
};
struct TrackLayout
{
    uint64_t fileOffset = 0; // byte offset of the first sector inside the source
    uint64_t sectorCount = 0;
    SectorProfile profile = SectorProfile::cooked2048;
};
// Validates the declared layout against the source and returns a bounded logical-block view.
// Raw profiles validate sync/mode/subheader per sector and refuse Mode 2 Form 2.
DiscResult<BlockSourcePtr> makeTrackSource(ByteSourcePtr bytes, const TrackLayout& layout,
                                           const DiscLimits& limits);
} // namespace sonicator
