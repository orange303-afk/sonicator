#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "core/cloud/CloudLayoutBuilder.h"
#include <cmath>

using namespace sonicator;
using namespace sonicator::cloud;

namespace
{
CloudInput makeInput()
{
    CloudInput input;
    Tag kick { 1, "Kick", 0xffe05a5a, true, 6 };
    Tag drums { 2, "Drums", 0xff8fd06a, true, 5 };
    Tag pad { 3, "Pad", 0xffa96ae0, true, 1 };
    input.tags = { kick, drums, pad };
    input.cooccurrence = { { 1, 2, 3 } };

    auto node = [](int64_t id, std::vector<int64_t> tags, std::array<float, 8> basic, bool hasBasic)
    {
        CloudNode n;
        n.sampleId = id;
        n.name = "s" + juce::String(id);
        n.tagIds = std::move(tags);
        n.basic = basic;
        n.hasBasic = hasBasic;
        n.colour = 0xff888888;
        return n;
    };
    // Kick island: six files whose brightness varies, so the projection has something to say.
    for (int i = 0; i < 6; ++i)
        input.nodes.push_back(node(10 + i, { 1 }, { (float) (i * 0.15), -20, -10, (float) i, (float) i * 2, 0.2f, 0, 1 }, true));
    for (int i = 0; i < 5; ++i)
        input.nodes.push_back(node(20 + i, { 2 }, { 1.5f + (float) (i * 0.15), -20, -10, (float) i, (float) i * 2, 0.2f, 0, 1 }, true));
    input.nodes.push_back(node(30, { 3 }, { 2.5f, -20, -10, 1, 2, 0.2f, 0, 1 }, true));
    input.nodes.push_back(node(31, { 1, 2 }, { 1.0f, -20, -10, 1, 2, 0.2f, 0, 1 }, true));
    input.v2.assign(input.nodes.size(), {});
    return input;
}

const CloudPoint& pointFor(const CloudLayout& layout, int64_t sampleId)
{
    for (const auto& p : layout.points) if (p.sampleId == sampleId) return p;
    throw std::runtime_error("missing point");
}
const CloudIsland& islandFor(const CloudLayout& layout, int64_t tagId)
{
    for (const auto& i : layout.islands) if (i.tagId == tagId) return i;
    throw std::runtime_error("missing island");
}
} // namespace

TEST_CASE("Cloud layout is deterministic and inside its bounds", "[cloud]")
{
    const auto input = makeInput();
    const auto a = buildCloudLayout(input);
    const auto b = buildCloudLayout(input);
    REQUIRE(a.points.size() == input.nodes.size());
    for (size_t i = 0; i < a.points.size(); ++i)
    {
        REQUIRE(a.points[i].x == b.points[i].x);
        REQUIRE(a.points[i].y == b.points[i].y);
        REQUIRE(a.bounds.contains(a.points[i].x, a.points[i].y));
        REQUIRE(std::isfinite(a.points[i].x));
        REQUIRE(std::isfinite(a.points[i].y));
    }
    REQUIRE(a.fingerprint == input.fingerprint);
}

TEST_CASE("Cloud islands never overlap and a single-file tag stays visible", "[cloud]")
{
    auto input = makeInput();
    const auto layout = buildCloudLayout(input);
    for (size_t i = 0; i < layout.islands.size(); ++i)
        for (size_t j = i + 1; j < layout.islands.size(); ++j)
        {
            const auto& a = layout.islands[i];
            const auto& b = layout.islands[j];
            const auto distance = std::hypot(a.x - b.x, a.y - b.y);
            REQUIRE(distance >= a.radius + b.radius);
        }
    REQUIRE(islandFor(layout, 3).radius >= CloudLayoutOptions {}.islandBaseRadius);
    REQUIRE(islandFor(layout, 1).radius > islandFor(layout, 3).radius);
}

TEST_CASE("A multi-tag file sits between its islands", "[cloud]")
{
    const auto input = makeInput();
    const auto layout = buildCloudLayout(input);
    const auto& kick = islandFor(layout, 1);
    const auto& drums = islandFor(layout, 2);
    const auto& bridge = pointFor(layout, 31);
    const auto toKick = std::hypot(bridge.x - kick.x, bridge.y - kick.y);
    const auto toDrums = std::hypot(bridge.x - drums.x, bridge.y - drums.y);
    const auto midpoint = juce::Point<float>((kick.x + drums.x) / 2, (kick.y + drums.y) / 2);
    const auto toMidpoint = std::hypot(bridge.x - midpoint.x, bridge.y - midpoint.y);
    REQUIRE(toMidpoint < std::min(toKick, toDrums));
    REQUIRE(bridge.has(cloudMultiTag));
}

TEST_CASE("A file without descriptors sits at its island centre", "[cloud]")
{
    auto input = makeInput();
    CloudNode meta;
    meta.sampleId = 40;
    meta.name = "pad.rx2";
    meta.tagIds = { 3 };
    meta.hasBasic = false;
    meta.colour = 0xffa96ae0;
    input.nodes.push_back(meta);
    input.v2.emplace_back();
    const auto layout = buildCloudLayout(input);
    const auto& pad = islandFor(layout, 3);
    const auto& point = pointFor(layout, 40);
    REQUIRE(point.x == Catch::Approx(pad.x).margin(0.01));
    REQUIRE(point.y == Catch::Approx(pad.y).margin(0.01));
    REQUIRE(point.has(cloudNoPosition));
}

TEST_CASE("NaN feature values never produce NaN coordinates", "[cloud]")
{
    auto input = makeInput();
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    input.nodes[0].basic.fill(nan);
    input.nodes[1].hasV2 = true;
    input.v2[1] = std::vector<float>(84, nan);
    input.nodes[2].hasV2 = true;
    input.v2[2] = std::vector<float>(84, 0.5f);
    input.nodes[3].hasV2 = true;
    input.v2[3] = std::vector<float>(84, 0.6f);
    input.nodes[4].hasV2 = true;
    input.v2[4] = std::vector<float>(84, 0.7f);
    const auto layout = buildCloudLayout(input);
    for (const auto& p : layout.points)
    {
        REQUIRE(std::isfinite(p.x));
        REQUIRE(std::isfinite(p.y));
    }
}

TEST_CASE("Identical feature vectors do not land on one exact spot", "[cloud]")
{
    auto input = makeInput();
    for (size_t i = 0; i < 6; ++i) input.nodes[i].basic = { 1, -20, -10, 2, 3, 0.2f, 0, 1 };
    const auto layout = buildCloudLayout(input);
    for (size_t i = 0; i < layout.points.size(); ++i)
        for (size_t j = i + 1; j < layout.points.size(); ++j)
            REQUIRE(std::hypot(layout.points[i].x - layout.points[j].x,
                               layout.points[i].y - layout.points[j].y) > 0.5f);
}

TEST_CASE("Nearest neighbours are ordered by layout distance", "[cloud]")
{
    const auto layout = buildCloudLayout(makeInput());
    const auto neighbours = nearestNeighbours(layout, 0, 3);
    REQUIRE(neighbours.size() == 3);
    REQUIRE(neighbours[0].second <= neighbours[1].second);
    REQUIRE(neighbours[1].second <= neighbours[2].second);
    REQUIRE(nearestNeighbours(layout, 0, 1000).size() == layout.points.size() - 1);
}
