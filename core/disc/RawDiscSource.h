#pragma once
#include "ReadOnlyByteSource.h"
namespace sonicator {
// Opens a local regular file read-only. The source owns its handle and never writes, mounts or
// resolves anything else. Missing, non-regular or unreadable paths return ioError.
DiscResult<ByteSourcePtr> openRawDisc(const juce::File& file, const DiscLimits& limits);

// A bounded view over an existing source. Its lifetime retains the parent.
DiscResult<ByteSourcePtr> subrange(ByteSourcePtr parent, uint64_t offset, uint64_t length);
} // namespace sonicator
