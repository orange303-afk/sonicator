#pragma once
#include <cmath>
namespace sonicator {
enum class SelectionBoundary { none, restart, stop };
// Preview-grade boundary: at or past the end, loop back or stop. Invalid ranges never trigger.
inline SelectionBoundary selectionBoundary(double position, double start, double end, bool looping)
{
    if (! std::isfinite(position) || ! std::isfinite(start) || ! std::isfinite(end) || end <= start)
        return SelectionBoundary::none;
    if (position < end) return SelectionBoundary::none;
    return looping ? SelectionBoundary::restart : SelectionBoundary::stop;
}
} // namespace sonicator
