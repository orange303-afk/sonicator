#include "SpectralMask.h"
#include <algorithm>
#include <cmath>

namespace sonicator
{
namespace
{
int capColumns(int columns) { return juce::jlimit(64, 4096, columns); }
}

void SpectralMask::clear()
{
    cells.clear();
    columns = 0;
    startSeconds = endSeconds = 0;
    maskedCells = 0;
}

void SpectralMask::setWindow(double start, double end, double nyquist)
{
    if (! (end > start) || ! std::isfinite(start) || ! std::isfinite(end))
    {
        clear();
        return;
    }
    const int newColumns = capColumns((int) std::ceil((end - start) * 2000.0));
    if (newColumns == columns && start == startSeconds && end == endSeconds
        && nyquist == maxFrequency && ! cells.empty())
        return;

    const bool hadCells = ! cells.empty() && columns > 0 && endSeconds > startSeconds;
    std::vector<uint8_t> previous;
    double oldStart = startSeconds, oldEnd = endSeconds;
    int oldColumns = columns;
    if (hadCells)
    {
        previous = cells;
        oldStart = startSeconds;
        oldEnd = endSeconds;
        oldColumns = columns;
    }

    startSeconds = start;
    endSeconds = end;
    columns = newColumns;
    this->maxFrequency = nyquist > minFrequency ? nyquist : 20000.0;
    cells.assign((size_t) rowCount * (size_t) columns, 0);
    maskedCells = 0;

    if (hadCells && oldColumns > 0)
    {
        const auto oldLength = oldEnd - oldStart;
        const auto newLength = end - start;
        for (int row = 0; row < rowCount; ++row)
            for (int column = 0; column < columns; ++column)
            {
                const auto time = start + (column + 0.5) * newLength / (double) columns;
                const auto oldColumn = (int) std::floor((time - oldStart) / oldLength * oldColumns);
                if (oldColumn < 0 || oldColumn >= oldColumns) continue;
                if (previous[(size_t) row * (size_t) oldColumns + (size_t) oldColumn] != 0)
                {
                    cells[(size_t) row * (size_t) columns + (size_t) column] = 1;
                    ++maskedCells;
                }
            }
    }
}

int SpectralMask::columnForTime(double seconds) const
{
    if (! (endSeconds > startSeconds) || columns <= 0) return -1;
    const auto position = (seconds - startSeconds) / (endSeconds - startSeconds);
    const int column = (int) std::floor(position * (double) columns);
    return column < 0 || column >= columns ? -1 : column;
}

int SpectralMask::rowForFrequency(double hz) const
{
    if (! (maxFrequency > minFrequency) || hz <= 0.0) return -1;
    const auto position = std::log(hz / minFrequency) / std::log(maxFrequency / minFrequency);
    const int row = (int) std::floor(position * (double) rowCount);
    return row < 0 || row >= rowCount ? -1 : row;
}

double SpectralMask::timeForColumn(int column) const
{
    if (columns <= 0) return startSeconds;
    return startSeconds + (column + 0.5) * length() / (double) columns;
}

double SpectralMask::frequencyForRow(int row) const
{
    return minFrequency * std::pow(maxFrequency / minFrequency,
                                   ((double) row + 0.5) / (double) rowCount);
}

bool SpectralMask::cell(int row, int column) const
{
    if (row < 0 || row >= rowCount || column < 0 || column >= columns) return false;
    return cells[(size_t) row * (size_t) columns + (size_t) column] != 0;
}

bool SpectralMask::contains(double seconds, double hz) const
{
    return cell(rowForFrequency(hz), columnForTime(seconds));
}

double SpectralMask::lowFrequency() const
{
    for (int row = 0; row < rowCount; ++row)
        for (int column = 0; column < columns; ++column)
            if (cell(row, column)) return frequencyForRow(row);
    return 0.0;
}

double SpectralMask::highFrequency() const
{
    for (int row = rowCount - 1; row >= 0; --row)
        for (int column = 0; column < columns; ++column)
            if (cell(row, column)) return frequencyForRow(row);
    return 0.0;
}

void SpectralMask::addRectangle(double start, double end, double lowHz, double highHz, bool subtract)
{
    if (! (endSeconds > startSeconds)) return;
    const auto fromTime = juce::jmin(start, end), toTime = juce::jmax(start, end);
    const auto fromHz = juce::jmin(lowHz, highHz), toHz = juce::jmax(lowHz, highHz);
    if (toTime < startSeconds || fromTime > endSeconds) return;
    const auto length = this->length();
    const auto columnAt = [this, length](double time)
    {
        return (int) std::floor((time - startSeconds) / length * (double) columns);
    };
    const auto rowAt = [this](double hz)
    {
        const auto position = std::log(hz / minFrequency) / std::log(maxFrequency / minFrequency);
        return (int) std::floor(position * (double) rowCount);
    };
    const int firstColumn = juce::jlimit(0, columns - 1, columnAt(fromTime));
    int lastColumn = juce::jlimit(0, columns - 1, columnAt(toTime) - 1);
    const int firstRow = juce::jlimit(0, rowCount - 1, rowAt(juce::jmax(fromHz, minFrequency)));
    int lastRow = juce::jlimit(0, rowCount - 1, rowAt(juce::jmin(toHz, maxFrequency)) - 1);
    // A drag thinner than one cell still selects that cell: an empty result reads as "nothing was
    // selected" and silently loses the gesture.
    lastColumn = juce::jmax(firstColumn, lastColumn);
    lastRow = juce::jmax(firstRow, lastRow);
    for (int row = firstRow; row <= lastRow; ++row)
        for (int column = firstColumn; column <= lastColumn; ++column)
        {
            auto& value = cells[(size_t) row * (size_t) columns + (size_t) column];
            value = subtract ? 0 : 1;
        }
    recount();
}

void SpectralMask::addPolygon(const std::vector<juce::Point<double>>& pointsHz, bool subtract)
{
    if (pointsHz.size() < 3 || columns <= 0) return;
    const auto length = this->length();
    const auto columnAt = [this, length](double time)
    {
        return (time - startSeconds) / length * (double) columns;
    };
    int touchedRows = 0;
    for (int row = 0; row < rowCount; ++row)
    {
        const auto frequency = frequencyForRow(row);
        std::vector<double> crossings;
        for (size_t i = 0; i < pointsHz.size(); ++i)
        {
            const auto& a = pointsHz[i];
            const auto& b = pointsHz[(i + 1) % pointsHz.size()];
            const bool aBelow = a.y <= frequency, bBelow = b.y <= frequency;
            if (aBelow == bBelow) continue;
            const auto t = (frequency - a.y) / (b.y - a.y);
            crossings.push_back(columnAt(a.x + (b.x - a.x) * t));
        }
        if (crossings.size() < 2) continue;
        std::sort(crossings.begin(), crossings.end());
        for (size_t i = 0; i + 1 < crossings.size(); i += 2)
        {
            // A span narrower than one column still marks the column it sits in, so a hairline
            // lasso selects something instead of nothing.
            const int first = juce::jlimit(0, columns - 1, (int) std::ceil(crossings[i]));
            const int last = juce::jlimit(0, columns - 1,
                                          juce::jmax(first, (int) std::floor(crossings[i + 1])));
            for (int column = first; column <= last; ++column)
                cells[(size_t) row * (size_t) columns + (size_t) column] = subtract ? 0 : 1;
        }
        ++touchedRows;
    }
    // A polygon thinner than the row spacing falls between two row centres; it still has to select
    // its own band, so mark the row nearest its middle over the columns it covers.
    if (touchedRows == 0 && ! subtract)
    {
        double lowHz = pointsHz.front().y, highHz = pointsHz.front().y;
        double firstTime = pointsHz.front().x, lastTime = pointsHz.front().x;
        for (const auto& point : pointsHz)
        {
            lowHz = juce::jmin(lowHz, point.y);
            highHz = juce::jmax(highHz, point.y);
            firstTime = juce::jmin(firstTime, point.x);
            lastTime = juce::jmax(lastTime, point.x);
        }
        if (highHz >= lowHz && maxFrequency > minFrequency && minFrequency > 0.0)
        {
            const auto position = std::log(juce::jmax(lowHz, minFrequency) / minFrequency)
                / std::log(maxFrequency / minFrequency);
            const int row = juce::jlimit(0, rowCount - 1,
                                         (int) std::floor(position * (double) rowCount));
            const int first = juce::jlimit(0, columns - 1, (int) std::ceil(columnAt(firstTime)));
            const int last = juce::jlimit(0, columns - 1,
                                          juce::jmax(first, (int) std::floor(columnAt(lastTime))));
            for (int column = first; column <= last; ++column)
                cells[(size_t) row * (size_t) columns + (size_t) column] = 1;
        }
    }
    recount();
}

void SpectralMask::addRaster(const SpectrogramGrid& source, const std::vector<uint8_t>& sourceCells,
                             bool subtract)
{
    if (! source.valid() || sourceCells.size() != (size_t) source.rows * (size_t) source.columns)
        return;
    const auto frequencyAt = [&source](double position)
    {
        return source.logFrequency && source.minFrequency > 0.0
            ? source.minFrequency * std::pow(source.maxFrequency / source.minFrequency, position)
            : source.minFrequency + (source.maxFrequency - source.minFrequency) * position;
    };
    const auto timeAt = [&source](double position)
    {
        return source.startSeconds
            + position * (source.endSeconds - source.startSeconds);
    };
    const auto clampRow = [this](int row)
    {
        return juce::jlimit(0, rowCount - 1, row < 0 ? rowCount - 1 : row);
    };
    const auto clampColumn = [this](int column)
    {
        return juce::jlimit(0, columns - 1, column < 0 ? columns - 1 : column);
    };
    // Each source cell covers a block, not a point: filling only its centre would leave rows and
    // columns empty when the wand's grid is coarser than the mask (a whole-file overview is).
    for (int row = 0; row < source.rows; ++row)
    {
        const int firstRow = clampRow(rowForFrequency(frequencyAt((double) row / source.rows)));
        const int lastRow = clampRow(rowForFrequency(
            frequencyAt((double) (row + 1) / source.rows)) - (row + 1 == source.rows ? 0 : 1));
        for (int column = 0; column < source.columns; ++column)
        {
            if (sourceCells[(size_t) row * (size_t) source.columns + (size_t) column] == 0)
                continue;
            const int firstColumn = clampColumn(columnForTime(
                timeAt((double) column / source.columns)));
            const int lastColumn = clampColumn(columnForTime(
                timeAt((double) (column + 1) / source.columns)) - (column + 1 == source.columns ? 0 : 1));
            for (int r = juce::jmin(firstRow, lastRow); r <= juce::jmax(firstRow, lastRow); ++r)
                for (int c = juce::jmin(firstColumn, lastColumn);
                     c <= juce::jmax(firstColumn, lastColumn); ++c)
                    cells[(size_t) r * (size_t) columns + (size_t) c] = subtract ? 0 : 1;
        }
    }
    recount();
}

void SpectralMask::recount()
{
    maskedCells = 0;
    for (auto value : cells)
        if (value != 0) ++maskedCells;
}
} // namespace sonicator
