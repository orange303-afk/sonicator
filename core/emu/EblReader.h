#pragma once
#include <juce_core/juce_core.h>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
namespace sonicator {
// A loop from the EBL trailer. `end` is exclusive; the WAV smpl chunk makes it inclusive.
struct EblLoop
{
    uint32_t start = 0;
    uint32_t end = 0;
};
struct EblSample
{
    juce::String name;          // header name, trimmed; may be empty
    uint32_t sampleRate = 0;
    int channels = 0;           // 1 or 2
    int64_t frames = 0;         // per channel
    std::vector<int16_t> pcm;   // interleaved, frames * channels
    std::optional<EblLoop> loop;
};
struct EblResult
{
    std::optional<EblSample> sample;
    juce::String error;
    bool ok() const { return sample.has_value(); }
};
// Decodes one E-mu Emulator X .ebl payload. Every read is bounds-checked; a malformed file
// comes back as an error and never as a partial sample.
EblResult parseEbl(std::span<const std::byte> bytes, uint64_t maxDecodedBytes = 128ull << 20);
// Reads the whole file, refusing anything larger than the decoded cap plus a 64 KiB header.
EblResult readEblFile(const juce::File&, uint64_t maxDecodedBytes = 128ull << 20);
bool isEblFile(const juce::File&);
} // namespace sonicator
