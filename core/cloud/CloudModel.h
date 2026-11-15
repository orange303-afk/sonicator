#pragma once
#include "core/database/LibraryQuery.h"
#include <juce_graphics/juce_graphics.h>
#include <array>
#include <cstdint>
#include <vector>

namespace sonicator::cloud
{
// One node is one catalogued file with at least one tag. Only these files appear on the map: an
// untagged file has no island, and 340k of them would bury the structure the view exists for.
struct CloudNode
{
    int64_t sampleId = 0;
    juce::String name, path, format;
    double duration = 0, rate = 0;
    int channels = 0;
    int64_t dateAnalyzed = 0;
    std::vector<int64_t> tagIds; // ascending, no duplicates
    // The measured descriptors stored on every analysed row. False for metadata-only containers
    // (RX2, EXS, NKI, SF2, AKAI, tracker modules) and unreadable files: those have no sound
    // position on the map and never sound on hover.
    bool hasBasic = false;
    // The 84-value schema-2 vector exists for 13,991 of 35,636 tagged files. It sharpens the local
    // neighbourhood; it is not a claim that every neighbour is the same instrument.
    bool hasV2 = false;
    // log duration, RMS dBFS, peak dBFS, log centroid, log rolloff, flatness, log rate, log channels.
    std::array<float, 8> basic {};
    uint32_t colour = 0xff8a8f9c; // blend of the tag colours, equal weight per tag
};

struct CloudCooccurrence
{
    int64_t tagA = 0, tagB = 0; // tagA < tagB
    int count = 0;
};

struct CloudInput
{
    std::vector<CloudNode> nodes;                  // ascending sampleId
    std::vector<Tag> tags;                         // ascending id, only tags that have files
    std::vector<CloudCooccurrence> cooccurrence;   // ascending (tagA, tagB)
    std::vector<std::vector<float>> v2;            // parallel to nodes, empty when !hasV2
    uint64_t fingerprint = 0;
    bool empty() const { return nodes.empty(); }
};

enum CloudFlag : uint8_t
{
    cloudHasBasic = 1 << 0,
    cloudHasV2 = 1 << 1,
    cloudMultiTag = 1 << 2,
    cloudNoPosition = 1 << 3 // no descriptors at all: island centre, no hover audio
};

struct CloudPoint
{
    int64_t sampleId = 0;
    float x = 0, y = 0;
    float radiusPx = 2.0f;
    uint32_t colour = 0xff8a8f9c;
    int islandIndex = -1;
    uint8_t flags = 0;
    bool has(CloudFlag f) const { return (flags & f) != 0; }
};

struct CloudIsland
{
    int64_t tagId = 0;
    juce::String name;
    uint32_t colour = 0xff8a8f9c;
    float x = 0, y = 0, radius = 0;
    int count = 0;
};

struct CloudLayout
{
    std::vector<CloudPoint> points; // ascending sampleId
    std::vector<CloudIsland> islands; // ascending tagId
    juce::Rectangle<float> bounds;
    uint64_t fingerprint = 0;
    int algorithmVersion = 1;
    bool valid() const { return ! points.empty(); }
};

} // namespace sonicator::cloud
