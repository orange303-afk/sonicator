#pragma once
#include "DiscTypes.h"
#include "ReadOnlyByteSource.h"
#include "core/sampler/SamplerTypes.h"
namespace sonicator {
// Decodes the verified raw Akai sample variant: a 150-byte header followed by signed 16-bit
// little-endian mono frames. The header length is not guessed: it must equal
// declaredBytes - frames*2 exactly, otherwise the sample is refused as an unverified variant.
DiscResult<SampleAudioPtr> decodeAkaiSample(ByteSourcePtr source, uint64_t declaredBytes,
                                            const DiscLimits& limits, CancelCheck cancel);
} // namespace sonicator
