#pragma once
#include "DiscTypes.h"
#include "ReadOnlyByteSource.h"
#include "core/sampler/SamplerTypes.h"
namespace sonicator {
// Structural facts of a raw Akai sample header, as stored by the two verified families: S1000
// (150-byte header) and S3000 (192-byte header), both followed by signed 16-bit LE mono frames.
struct AkaiSampleHeader
{
    unsigned id = 0;          // byte 0x00, 1 or 3
    uint32_t headerBytes = 0; // 150 or 192
    uint32_t frames = 0;      // u32 LE at 0x1a
    uint32_t startFrame = 0;  // u32 LE at 0x1e
    uint32_t endFrame = 0;    // u32 LE at 0x22
    uint16_t sampleRate = 0;  // u16 LE at 0x8a
    uint8_t rootNote = 0;     // byte 0x02
};

// Reads and validates the header. The header length is not guessed: it must be 150 or 192 and
// equal declaredBytes - frames*2 exactly; every other variant is refused as unverified.
DiscResult<AkaiSampleHeader> readAkaiSampleHeader(const ReadOnlyByteSource& source,
                                                  uint64_t declaredBytes, CancelCheck cancel);

// Decodes the verified raw Akai sample variants: a 150- or 192-byte header followed by signed
// 16-bit little-endian mono frames. All declared frames are returned, without trimming to the
// header's start/end points.
DiscResult<SampleAudioPtr> decodeAkaiSample(ByteSourcePtr source, uint64_t declaredBytes,
                                            const DiscLimits& limits, CancelCheck cancel);
} // namespace sonicator
