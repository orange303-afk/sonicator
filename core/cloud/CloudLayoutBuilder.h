#pragma once
#include "CloudModel.h"
#include <utility>

namespace sonicator::cloud
{
struct CloudLayoutOptions
{
    float islandBaseRadius = 13.0f;   // a tag with one file still gets a visible island
    float islandRadiusScale = 1.15f;   // area grows with the file count, so Dubtechno cannot eat the map
    float islandGap = 10.0f;
    float pointSpread = 0.66f;        // share of the island radius the acoustic projection may use
    float multiTagSpread = 0.7f;      // a bridge point stays inside the narrower of its islands
    float v2Spread = 0.4f;            // cap for the 84-value refinement displacement
    float refinementAlpha = 0.35f;
    int islandIterations = 300;
    int separationIterations = 200;
    int kNeighbours = 8;
    int algorithmVersion = 1;
};

// Pure, deterministic and free of threads, SQLite and JUCE components. The same CloudInput always
// produces the same coordinates, so a camera move or a filter change never reflows the map.
// `isCancelled` is checked between phases; a cancelled call returns an invalid layout.
CloudLayout buildCloudLayout(const CloudInput&, const CloudLayoutOptions& = {},
                             const std::function<bool()>& isCancelled = {});

// Nearest neighbours by the layout distance, for the listening report. Returns (point index,
// distance) ascending; `index` is the position in layout.points, which matches input.nodes.
std::vector<std::pair<size_t, float>> nearestNeighbours(const CloudLayout&, size_t index, int k);
}
