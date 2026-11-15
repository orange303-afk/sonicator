#pragma once
#include "BatchPublisher.h"
#include "BatchResampler.h"
#include <functional>
#include <memory>

namespace sonicator::batch
{
struct EncodeSettings
{
    Format format = Format::wav;
    int bits = 24;
    bool floating = false;
    int sampleRate = 0;
    int channels = 0;
    int flacCompressionLevel = 5;
    int mp3BitrateKbps = 192;
    bool dither = false;
    uint64_t ditherSeed = 0;
    juce::StringPairArray metadata;
};

struct EncodedAudio
{
    bool ok = false;
    ErrorCode code = ErrorCode::none;
    juce::String error;
    int64_t frames = 0;
    float peak = 0.0f;
    int64_t saturatedFrames = 0;
    bool floatOverUnity = false;
};

// Writes the converted stream to an existing-free temporary and reopens it for a structural and
// full decode validation. The temporary is deleted on any failure.
EncodedAudio encodeToTemporary(const juce::File& temporary, const EncodeSettings&,
                               ConvertedStream&, CancelCheck,
                               const std::function<void(const juce::String&)>& phase = {});

// Empty when the file matches rate/channels/depth/float flag/frame count and decodes end to end.
std::vector<Issue> validateEncodedFile(const juce::File&, const EncodeSettings&, int64_t frames,
                                       float& decodedPeak, CancelCheck = {});

// Full output pipeline for one planned file. Takes ownership of PreparedAudio because the
// converted stream owns the reader. Never publishes a file that failed validation or clipping.
OutputResult renderAndPublish(std::unique_ptr<PreparedAudio>, const OutputPlan&,
                              const SourceSnapshot&, const Recipe&, ConflictPolicy,
                              const InputProtectionSet&, CancelCheck = {},
                              const std::function<void(const juce::String&)>& phase = {});
} // namespace sonicator::batch
