#pragma once
#include "CloudModel.h"
#include <utility>

namespace sonicator::cloud
{
struct CloudLayoutOptions
{
    float islandBaseRadius = 13.0f;   // a tag with one file still gets a visible island
    float islandRadiusScale = 1.15f;   // area grows with the file count, so Dubtechno cannot eat the map
    // Islands are packed so their circles touch and overlap a little: the map reads as one cloud
    // with colour gradients at the seams, not as separate discs. Share of the smaller radius.
    float islandOverlap = 0.22f;
    float islandGap = 0.0f;           // extra spacing after the overlap, normally zero
    float pointSpread = 0.72f;        // share of the island radius the acoustic projection may use
    float multiTagSpread = 0.7f;      // a bridge point stays inside the narrower of its islands
    float v2Spread = 0.4f;            // cap for the 84-value refinement displacement
    float refinementAlpha = 0.35f;
    // A deterministic per-file jitter that breaks the lines left by identical acoustic vectors.
    // Share of the island radius, never random: the hash is taken from the sample id.
    float cloudDither = 0.10f;
    int islandIterations = 420;
    int separationIterations = 200;
    int kNeighbours = 8;
    int algorithmVersion = 2;
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
