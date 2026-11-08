#pragma once
#include "DiscTypes.h"
#include "ReadOnlyByteSource.h"
#include "core/sampler/SamplerTypes.h"
namespace sonicator {
// Decodes a supported audio file from any bounded byte source. The source may be an ISO extent or
// a sector-extracted range; it never becomes a fake file path. Channels are limited to mono/stereo
// and precision follows the documented export policy (16-bit for 8/16, 24-bit for 24/lossy,
// float32 for float); integer 32-bit sources are refused rather than silently truncated.
DiscResult<SampleAudioPtr> decodeDiscAudio(ByteSourcePtr source, const juce::String& decoderHint,
                                           const DiscLimits& limits, CancelCheck cancel);
} // namespace sonicator
