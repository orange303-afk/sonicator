#include "TapTempo.h"
#include <algorithm>
#include <cmath>

namespace sonicator
{
void TapTempo::tap(const int64_t timeMs)
{
    if (! taps.empty() && timeMs - taps.back() > resetAfterMs) taps.clear();
    taps.push_back(timeMs);
    // One more timestamp than intervals is enough to describe the window.
    while ((int) taps.size() > maxIntervals + 1) taps.erase(taps.begin());
}

void TapTempo::reset()
{
    taps.clear();
}

bool TapTempo::hasValue() const
{
    return bpm() > 0.0;
}

double TapTempo::bpm() const
{
    if (taps.size() < 3) return 0.0;
    std::vector<double> intervals;
    for (size_t i = 1; i < taps.size(); ++i) intervals.push_back((double) (taps[i] - taps[i - 1]));
    auto sorted = intervals;
    std::sort(sorted.begin(), sorted.end());
    const auto median = sorted[sorted.size() / 2];
    if (! (median > 0.0)) return 0.0;
    double sum = 0.0;
    int kept = 0;
    for (const auto interval : intervals)
    {
        if (std::abs(interval - median) > median * 0.25) continue;
        sum += interval;
        ++kept;
    }
    if (kept < 2 || ! (sum > 0.0)) return 0.0;
    return 60000.0 / (sum / (double) kept);
}
} // namespace sonicator
