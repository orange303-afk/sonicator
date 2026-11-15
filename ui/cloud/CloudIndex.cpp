#include "CloudIndex.h"
#include <algorithm>
#include <cmath>

namespace sonicator::cloud
{
void CloudIndex::build(const CloudLayout& layout, float cellSize)
{
    clear();
    cell = cellSize > 1.0f ? cellSize : 24.0f;
    positions.reserve(layout.points.size());
    for (size_t i = 0; i < layout.points.size(); ++i)
    {
        const auto& point = layout.points[i];
        positions.emplace_back(point.x, point.y);
        cells[keyOf((int) std::floor(point.x / cell), (int) std::floor(point.y / cell))].push_back(i);
    }
    count = layout.points.size();
}

void CloudIndex::clear()
{
    cells.clear();
    positions.clear();
    count = 0;
}

size_t CloudIndex::hitTest(float x, float y, float radiusPx) const
{
    if (cells.empty()) return none;
    const auto radius = std::max(radiusPx, 0.5f);
    const auto minX = (int) std::floor((x - radius) / cell);
    const auto maxX = (int) std::floor((x + radius) / cell);
    const auto minY = (int) std::floor((y - radius) / cell);
    const auto maxY = (int) std::floor((y + radius) / cell);
    size_t best = none;
    float bestDistance = radius * radius;
    for (int cx = minX; cx <= maxX; ++cx)
        for (int cy = minY; cy <= maxY; ++cy)
        {
            auto found = cells.find(keyOf(cx, cy));
            if (found == cells.end()) continue;
            for (auto index : found->second)
            {
                const auto& position = positions[index];
                const auto dx = position.x - x, dy = position.y - y;
                const auto distance = dx * dx + dy * dy;
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    best = index;
                }
            }
        }
    return best;
}

std::vector<size_t> CloudIndex::within(juce::Rectangle<float> area) const
{
    std::vector<size_t> out;
    if (cells.empty()) return out;
    const auto minX = (int) std::floor(area.getX() / cell);
    const auto maxX = (int) std::floor(area.getRight() / cell);
    const auto minY = (int) std::floor(area.getY() / cell);
    const auto maxY = (int) std::floor(area.getBottom() / cell);
    for (int cx = minX; cx <= maxX; ++cx)
        for (int cy = minY; cy <= maxY; ++cy)
        {
            auto found = cells.find(keyOf(cx, cy));
            if (found == cells.end()) continue;
            for (auto index : found->second)
                if (area.contains(positions[index])) out.push_back(index);
        }
    std::sort(out.begin(), out.end());
    return out;
}
}
