#pragma once
#include <juce_graphics/juce_graphics.h>
#include <cstdint>
#include <vector>
#include "core/dsp/Spectrogram.h"

namespace sonicator
{
// A time-frequency selection kept in absolute coordinates, so zooming and panning never move it.
// One canonical log raster (256 rows over `minFrequency`..`maxFrequency`) holds every tool's
// result: rectangles and lassos rasterise into it, the magic wand copies a flood fill of the
// displayed grid. Columns are about 0.5 ms per cell, capped, so a long window stays about 1 MB.
class SpectralMask
{
public:
    static constexpr int rowCount = 256;

    double startSeconds = 0, endSeconds = 0;
    double minFrequency = 20.0, maxFrequency = 20000.0;
    int columns = 0;
    std::vector<uint8_t> cells;

    bool empty() const { return maskedCells == 0; }
    int maskedCellCount() const { return maskedCells; }
    double length() const { return endSeconds > startSeconds ? endSeconds - startSeconds : 0.0; }
    void clear();
    // Grows or shrinks the window; existing cells are resampled into the new grid.
    void setWindow(double start, double end, double maxFrequency);

    int columnForTime(double seconds) const;
    int rowForFrequency(double hz) const;
    double timeForColumn(int column) const;
    double frequencyForRow(int row) const;
    bool cell(int row, int column) const;
    bool contains(double seconds, double hz) const;
    double lowFrequency() const;
    double highFrequency() const;

    void addRectangle(double start, double end, double lowHz, double highHz, bool subtract);
    // Points are (seconds, Hz) in the order the user drew them; the polygon closes itself.
    void addPolygon(const std::vector<juce::Point<double>>& pointsHz, bool subtract);
    // Copies the set cells of a spectrogram grid (the magic-wand flood fill) into the mask.
    void addRaster(const SpectrogramGrid& source, const std::vector<uint8_t>& sourceCells,
                   bool subtract);

private:
    int maskedCells = 0;
    void recount();
};
} // namespace sonicator
