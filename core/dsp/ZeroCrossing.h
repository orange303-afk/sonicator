#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cstdint>

namespace sonicator::zeroCrossing
{
// The magnet distance of one snap: how far from the requested frame a crossing may be. Five
// milliseconds is about two and a bit cycles of a 440 Hz tone, short enough to feel exact.
constexpr double defaultSearchMilliseconds = 5.0;

// Returns the frame in [frame - radius, frame + radius] that is closest to `frame` and sits on a
// sign change of the mixed channels, so a cut there starts or ends at silence instead of a step.
// Without a crossing the quietest frame of the window wins; a window that is silent throughout or
// holds no audio leaves `frame` unchanged. Channels are averaged, so a stereo edge is quiet in
// both.
std::int64_t snapFrame(const juce::AudioBuffer<float>& samples, std::int64_t frame, int radius);
} // namespace sonicator::zeroCrossing
