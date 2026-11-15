#include <catch2/catch_test_macros.hpp>
#include "core/cloud/CloudBuildService.h"
#include "TestHelpers.h"

using namespace sonicator;
using namespace sonicator::cloud;

namespace
{
CloudInput smallInput(uint64_t fingerprint)
{
    CloudInput input;
    input.tags = { Tag { 1, "Kick", 0xffe05a5a, true, 8 }, Tag { 2, "Drums", 0xff8fd06a, true, 8 } };
    input.cooccurrence = { { 1, 2, 2 } };
    for (int i = 0; i < 8; ++i)
    {
        CloudNode node;
        node.sampleId = 100 + i;
        node.name = "s" + juce::String(i);
        node.tagIds = { i % 3 == 0 ? 2 : 1 };
        node.hasBasic = true;
        node.basic = { (float) i, -20.0f + (float) i, -10.0f, (float) i, 2.0f, 0.2f, 0, 1 };
        node.colour = 0xff888888;
        input.nodes.push_back(node);
        input.v2.emplace_back();
    }
    input.fingerprint = fingerprint;
    return input;
}

template <typename Predicate>
bool waitFor(Predicate done, int timeoutMs = 8000)
{
    const auto start = juce::Time::getMillisecondCounter();
    while (! done() && juce::Time::getMillisecondCounter() - start < (juce::uint32) timeoutMs)
        juce::Thread::sleep(5);
    return done();
}
} // namespace

TEST_CASE("Cloud cache round-trips and rejects stale or broken files", "[cloud]")
{
    TestDirectory t;
    const auto input = smallInput(0x1234);
    const auto layout = buildCloudLayout(input);
    const auto file = cloudCacheFile(0x1234, t.dir);
    REQUIRE(writeCloudLayout(layout, file));
    REQUIRE(file.existsAsFile());

    const auto loaded = readCloudLayout(file, 0x1234);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->points.size() == layout.points.size());
    REQUIRE(loaded->islands.size() == layout.islands.size());
    for (size_t i = 0; i < layout.points.size(); ++i)
    {
        REQUIRE(loaded->points[i].sampleId == layout.points[i].sampleId);
        REQUIRE(loaded->points[i].x == layout.points[i].x);
        REQUIRE(loaded->points[i].y == layout.points[i].y);
    }
    REQUIRE(loaded->islands[0].name == layout.islands[0].name);

    REQUIRE_FALSE(readCloudLayout(file, 0x9999).has_value()); // a different catalog state
    file.replaceWithText("not a layout at all");
    REQUIRE_FALSE(readCloudLayout(file, 0x1234).has_value());
}

TEST_CASE("Cloud cache pruning keeps the newest files and never touches others", "[cloud]")
{
    TestDirectory t;
    auto make = [&t](const juce::String& name, int64_t ageMs)
    {
        auto f = t.dir.getChildFile(name);
        f.replaceWithText("x");
        f.setLastModificationTime(juce::Time::getCurrentTime() - juce::RelativeTime::milliseconds(ageMs));
        return f;
    };
    const auto newest = make("layout-1.bin", 1000);
    const auto older = make("layout-2.bin", 2000);
    const auto oldest = make("layout-3.bin", 3000);
    const auto unrelated = make("notes.txt", 5000);
    pruneCloudCache(t.dir, 2);
    REQUIRE(newest.existsAsFile());
    REQUIRE(older.existsAsFile());
    REQUIRE_FALSE(oldest.existsAsFile());
    REQUIRE(unrelated.existsAsFile());
}

TEST_CASE("Cloud build service publishes one result and reuses the cache", "[cloud]")
{
    TestDirectory t;
    CloudBuildService service(t.dir.getChildFile("cache"));
    service.start(smallInput(0xabc));
    REQUIRE(waitFor([&service] { return ! service.isRunning(); }));
    auto layout = service.poll();
    REQUIRE(layout.has_value());
    REQUIRE(layout->fingerprint == 0xabc);
    REQUIRE(layout->points.size() == 8);
    REQUIRE(service.poll() == std::nullopt); // delivered once

    // Second start for the same fingerprint is a cache hit: the answer is ready without a worker.
    service.start(smallInput(0xabc));
    REQUIRE_FALSE(service.isRunning());
    auto cached = service.poll();
    REQUIRE(cached.has_value());
    REQUIRE(cached->fingerprint == 0xabc);
}

TEST_CASE("A cancelled or superseded build never publishes", "[cloud]")
{
    TestDirectory t;
    CloudBuildService service(t.dir.getChildFile("cache"));
    service.start(smallInput(0x1));
    service.cancel();
    REQUIRE(service.poll() == std::nullopt);

    service.start(smallInput(0x2));
    service.start(smallInput(0x3)); // replaces the first
    REQUIRE(waitFor([&service] { return ! service.isRunning(); }));
    auto layout = service.poll();
    REQUIRE(layout.has_value());
    REQUIRE(layout->fingerprint == 0x3);
}
