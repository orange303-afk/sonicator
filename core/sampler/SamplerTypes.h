#pragma once
#include <juce_core/juce_core.h>
#include <cstdint>
#include <memory>
#include <vector>
namespace sonicator {
// Stable within one loaded sampler generation. A sample name is not an identifier.
using SampleId = int32_t;
constexpr SampleId invalidSampleId = -1;
struct SampleZoneRange
{
    int keyLow = 0, keyHigh = 127, velocityLow = 0, velocityHigh = 127;
};
// One audio source used by at least one zone of the selected instrument. Stereo pairs are one
// descriptor. An empty error means the descriptor is playable; otherwise the reason is shown.
struct SampleDescriptor
{
    SampleId id = invalidSampleId;
    juce::String name, error;
    int sampleRate = 0, channels = 0;
    int64_t frameCount = 0, loopStart = 0, loopEnd = 0;
    bool looping = false;
    std::vector<SampleZoneRange> zones;
    bool isPlayable() const
    {
        return id >= 0 && error.isEmpty() && sampleRate > 0 && channels > 0 && frameCount > 0;
    }
};
// Root key and loop of a sampler source, written into the WAV `smpl` chunk on export. Offsets are
// frames; `loopEndExclusive` follows JUCE's exclusive convention.
struct SamplerChunk
{
    int rootKey = 60;
    bool looping = false;
    int64_t loopStart = 0, loopEndExclusive = 0;
};
// Immutable decoded PCM snapshot shared by the waveform, audition and export. Channel vectors
// always hold exactly `frames` values; right is empty for mono.
struct SampleAudio
{
    int sampleRate = 0, channels = 0, bitsPerSample = 16;
    bool floatingPoint = false;
    int64_t frames = 0;
    std::vector<float> left, right;
    float sampleAt(int channel, int64_t frame) const
    {
        if (frame < 0 || frame >= frames) return 0.0f;
        if (channel <= 0 || right.empty()) return left[(size_t) frame];
        return right[(size_t) frame];
    }
};
using SampleAudioPtr = std::shared_ptr<const SampleAudio>;
} // namespace sonicator
