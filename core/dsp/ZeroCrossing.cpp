#include "ZeroCrossing.h"
#include <cmath>
#include <limits>

namespace sonicator::zeroCrossing
{
namespace
{
float mixAt(const juce::AudioBuffer<float>& samples, std::int64_t frame)
{
    const int channels = samples.getNumChannels();
    if (channels <= 0) return 0.0f;
    const int index = (int) frame;
    float sum = 0.0f;
    for (int channel = 0; channel < channels; ++channel)
        sum += samples.getSample(channel, index);
    return sum / (float) channels;
}
} // namespace

std::int64_t snapFrame(const juce::AudioBuffer<float>& samples, std::int64_t frame, int radius)
{
    const auto frames = (std::int64_t) samples.getNumSamples();
    if (frames <= 0 || samples.getNumChannels() <= 0) return frame;
    frame = juce::jlimit<std::int64_t>(0, frames - 1, frame);
    if (radius <= 0 || frames < 2) return frame;

    const auto low = juce::jmax<std::int64_t>(1, frame - radius);
    const auto high = juce::jmin<std::int64_t>(frames - 1, frame + radius);
    if (high < low) return frame;

    // A silent window has no meaningful crossing: leave the requested frame alone rather than
    // sliding the edge to the window border.
    float windowPeak = 0.0f;
    for (auto i = low - 1; i <= high; ++i)
        windowPeak = juce::jmax(windowPeak, std::abs(mixAt(samples, i)));
    if (windowPeak < 1.0e-6f) return frame;

    std::int64_t bestCrossing = -1, bestQuiet = frame;
    std::int64_t bestCrossingDistance = 0;
    float bestCrossingMagnitude = 0.0f;
    float bestQuietMagnitude = std::abs(mixAt(samples, frame));
    for (auto i = low; i <= high; ++i)
    {
        const auto previous = mixAt(samples, i - 1);
        const auto current = mixAt(samples, i);
        const auto magnitude = std::abs(current);
        // The sign changes between i - 1 and i: the edge starts at i, where the value is already
        // near zero instead of at the peak that precedes the crossing.
        const bool crossing = (previous <= 0.0f && current > 0.0f)
            || (previous >= 0.0f && current < 0.0f);
        if (crossing)
        {
            const auto distance = std::abs(i - frame);
            if (bestCrossing < 0 || distance < bestCrossingDistance
                || (distance == bestCrossingDistance && magnitude < bestCrossingMagnitude))
            {
                bestCrossing = i;
                bestCrossingDistance = distance;
                bestCrossingMagnitude = magnitude;
            }
            continue;
        }
        if (magnitude < bestQuietMagnitude)
        {
            bestQuietMagnitude = magnitude;
            bestQuiet = i;
        }
    }
    return bestCrossing >= 0 ? bestCrossing : bestQuiet;
}
} // namespace sonicator::zeroCrossing
