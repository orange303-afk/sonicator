#pragma once
#include "core/sampler/SamplerTypes.h"
namespace sonicator {
// "KICK-L.wav" / "KICK R.wav" / "KICK.L.wav" -> base "KICK", side -1 for L or +1 for R.
struct StereoName
{
    juce::String base;
    int side = 0; // -1 left, +1 right, 0 no marker
    juce::String suffix; // the exact marker text, kept so the partner uses the same style
};
StereoName parseStereoName(const juce::String& fileName);
struct StereoPairResult
{
    bool paired = false; // the selected name had an L/R marker
    bool ok = false;
    juce::String error;
    SampleAudioPtr audio;
};
// Resolves the complementary mono file in the same directory, validates rate/frames and returns
// one interleaved stereo snapshot. A missing/already-stereo/invalid partner yields paired=false
// or ok=false with a reason; the caller can fall back to the ordinary single-file path.
StereoPairResult loadStereoPair(const juce::File& selected, int64_t maxDecodedBytes = 128LL * 1024 * 1024);
} // namespace sonicator
