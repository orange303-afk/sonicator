#include "WaveformView.h"
#include <cmath>

namespace sonicator::waveview
{
View whole(const double totalLength)
{
    if (! (totalLength > 0) || ! std::isfinite(totalLength)) return {};
    return { 0.0, totalLength };
}

View clampView(View view, const double totalLength)
{
    if (! (totalLength > 0) || ! std::isfinite(totalLength)) return {};
    if (! view.valid()) return whole(totalLength);
    const auto length = juce::jmin(view.length(), totalLength);
    const auto start = juce::jlimit(0.0, juce::jmax(0.0, totalLength - length), view.start);
    return { start, start + length };
}

View zoomAround(View view, const double anchorSeconds, const double factor,
                const double totalLength, const double minLength)
{
    if (! view.valid()) view = whole(totalLength);
    if (! view.valid() || ! (factor > 0) || ! std::isfinite(factor)) return view;
    const auto anchor = juce::jlimit(view.start, view.end, anchorSeconds);
    const auto length = juce::jlimit(juce::jmin(minLength, totalLength),
                                     totalLength, view.length() / factor);
    const auto position = view.length() > 0 ? (anchor - view.start) / view.length() : 0.5;
    const auto start = juce::jlimit(0.0, juce::jmax(0.0, totalLength - length),
                                    anchor - position * length);
    return { start, start + length };
}

View panBy(View view, const double seconds, const double totalLength)
{
    if (! view.valid()) return whole(totalLength);
    const auto start = juce::jlimit(0.0, juce::jmax(0.0, totalLength - view.length()),
                                    view.start + seconds);
    return { start, start + view.length() };
}

double timeAtX(const double x, const int width, const View view)
{
    if (! view.valid() || ! std::isfinite(x)) return view.start;
    const auto track = (double) width - 2.0 * selection::inset;
    if (track <= 0) return view.start;
    return view.start + juce::jlimit(0.0, 1.0, (x - selection::inset) / track) * view.length();
}

float xAtTime(const double time, const int width, const View view)
{
    if (! view.valid()) return (float) selection::inset;
    const auto track = (double) width - 2.0 * selection::inset;
    if (track <= 0) return (float) selection::inset;
    const auto position = juce::jlimit(0.0, 1.0, (time - view.start) / view.length());
    return (float) (selection::inset + position * track);
}

bool isWholeFile(const View view, const double totalLength)
{
    return view.valid() && totalLength > 0 && view.start <= 1e-9
        && std::abs(view.end - totalLength) <= 1e-9;
}
} // namespace sonicator::waveview
