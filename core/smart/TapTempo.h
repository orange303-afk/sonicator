#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace sonicator
{
// Turns a sequence of tap timestamps into a tempo. The last eight intervals matter; intervals that
// differ from the median by more than a quarter are dropped, so one clumsy tap cannot move the
// result. Three seconds without a tap starts a new sequence.
class TapTempo
{
public:
    static constexpr int maxIntervals = 8;
    static constexpr int64_t resetAfterMs = 3000;

    void tap(int64_t timeMs);
    void reset();
    bool hasValue() const;
    double bpm() const;
    int tapCount() const { return (int) taps.size(); }

private:
    std::vector<int64_t> taps;
};
} // namespace sonicator
