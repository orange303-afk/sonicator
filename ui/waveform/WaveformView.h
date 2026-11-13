#pragma once
#include "WaveformSelection.h"

namespace sonicator::waveview
{
// Visible slice of a source in seconds; the whole file when start is 0 and end is its length.
struct View
{
    double start = 0, end = 0;
    bool valid() const { return std::isfinite(start) && std::isfinite(end) && end > start; }
    double length() const { return valid() ? end - start : 0.0; }
};

View whole(double totalLength);
View clampView(View, double totalLength);
// `factor > 1` zooms in; the time under `anchorSeconds` keeps its relative position. `minLength` is
// the shortest view allowed, so a sample cannot be magnified past roughly one sample per pixel.
View zoomAround(View, double anchorSeconds, double factor, double totalLength, double minLength);
View panBy(View, double seconds, double totalLength);
double timeAtX(double x, int width, View);
float xAtTime(double time, int width, View);
bool isWholeFile(View, double totalLength);
} // namespace sonicator::waveview
