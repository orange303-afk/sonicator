#pragma once
#include "core/scanner/FileScanner.h"
#include <cstdint>
#include <optional>
#include <span>
namespace sonicator {
// Read-only disc image access. Every layer reports a typed outcome; `unknown` is never a success
// and a short read is an error, not a zero-filled buffer.
enum class DiscStatus
{
    ready,
    unknown,
    unsupported,
    malformed,
    ioError,
    cancelled,
    limitExceeded,
    ambiguous
};
template <typename T>
struct DiscResult
{
    DiscStatus status = DiscStatus::unknown;
    T value {};
    juce::String error;
    bool ok() const { return status == DiscStatus::ready; }
};
template <>
struct DiscResult<void>
{
    DiscStatus status = DiscStatus::unknown;
    juce::String error;
    bool ok() const { return status == DiscStatus::ready; }
};
// Server-side budgets. Values are deliberately explicit so a parser cannot silently scan an
// entire image: callers pass the policy they were given.
struct DiscLimits
{
    uint64_t maxReadBytes = 1u << 20;              // 1 MiB per request
    uint64_t maxMetadataCacheBytes = 64u << 20;    // 64 MiB metadata budget
    uint64_t maxDecodedCacheBytes = 256u << 20;    // 256 MiB decoded PCM budget
    uint64_t maxDecodedSourceBytes = 128u << 20;   // one immutable source admission
    uint64_t maxChainSteps = 1u << 20;
    uint32_t maxDirectoryDepth = 256;
    uint32_t maxDirectoryRecords = 100000;
    uint32_t maxVolumeDescriptors = 256;
    uint32_t maxCueBytes = 1u << 20;
    uint32_t maxTracks = 99;
};
} // namespace sonicator
