#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
namespace sonicator::selection {
// Waveform drawing inset used by both the thumbnail and all pixel/time mapping.
inline constexpr double inset = 6.0;
struct Range
{
    double start = 0, end = 0;
    bool valid() const { return std::isfinite(start) && std::isfinite(end) && end > start; }
    double length() const { return valid() ? end - start : 0.0; }
};
inline double timeAtX(double x, int width, double totalLength)
{
    if (! (totalLength > 0) || ! std::isfinite(totalLength) || ! std::isfinite(x)) return 0;
    const double track = (double) width - 2.0 * inset;
    if (track <= 0) return 0;
    return juce::jlimit(0.0, totalLength, (x - inset) / track * totalLength);
}
inline float xAtTime(double time, int width, double totalLength)
{
    if (! (totalLength > 0) || ! std::isfinite(totalLength) || ! std::isfinite(time))
        return (float) inset;
    const double track = (double) width - 2.0 * inset;
    if (track <= 0) return (float) inset;
    return (float) (inset + juce::jlimit(0.0, totalLength, time) / totalLength * track);
}
inline Range orderedRange(double a, double b, double totalLength)
{
    if (! std::isfinite(a) || ! std::isfinite(b) || ! (totalLength > 0) || ! std::isfinite(totalLength))
        return {};
    a = juce::jlimit(0.0, totalLength, a);
    b = juce::jlimit(0.0, totalLength, b);
    return { juce::jmin(a, b), juce::jmax(a, b) };
}
inline bool exceedsDragThreshold(int dx, int dy) { return dx * dx + dy * dy > 9; }
inline bool contains(const Range& r, double time)
{
    return r.valid() && std::isfinite(time) && time >= r.start && time <= r.end;
}
inline juce::String formatRange(const Range& r)
{
    return juce::String(r.start, 3) + juce::String::fromUTF8("–") + juce::String(r.end, 3) + " s";
}
} // namespace sonicator::selection
