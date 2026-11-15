#pragma once
#include "core/cloud/CloudModel.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace sonicator::cloud
{
// Uniform grid over the finished layout. A 35k-point map must answer "what is under the cursor"
// in constant time; walking the points would make every mouse move a full-library scan.
class CloudIndex
{
public:
    static constexpr size_t none = (size_t) -1;

    void build(const CloudLayout& layout, float cellSize = 24.0f);
    void clear();
    bool empty() const { return cells.empty(); }
    size_t size() const { return count; }
    // Index into layout.points of the nearest point within radiusPx of (x, y), or none.
    size_t hitTest(float x, float y, float radiusPx) const;
    // Every point whose centre is inside the rectangle, ascending by index.
    std::vector<size_t> within(juce::Rectangle<float> area) const;

private:
    static int64_t keyOf(int cx, int cy) { return ((int64_t) cx << 32) ^ (int64_t) (uint32_t) cy; }
    float cell = 24.0f;
    size_t count = 0;
    std::unordered_map<int64_t, std::vector<size_t>> cells;
    std::vector<juce::Point<float>> positions;
};
}
