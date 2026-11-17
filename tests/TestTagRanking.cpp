#include <catch2/catch_test_macros.hpp>
#include "core/database/CatalogSchema.h"
#include "core/database/SampleRepository.h"
#include "TestHelpers.h"
#include <algorithm>
#include <memory>
#include <sqlite3.h>

using namespace sonicator;

namespace
{
constexpr int64_t dayMs = 86'400'000;
constexpr int64_t testNow = 1'800'000'000'000; // a fixed clock keeps the ranking deterministic

struct UsageFixture
{
    TestDirectory t;
    std::unique_ptr<Database> db;
    std::unique_ptr<SampleRepository> repo;
    UsageFixture()
    {
        db = std::make_unique<Database>(file());
        repo = std::make_unique<SampleRepository>(*db);
        repo->createTables();
    }
    juce::File file() const { return t.dir.getChildFile("usage.sqlite"); }
};

int indexOf(const std::vector<Tag>& tags, int64_t id)
{
    for (size_t i = 0; i < tags.size(); ++i)
        if (tags[i].id == id) return (int) i;
    return -1;
}
}

TEST_CASE("Tag usage counts explicit assignments and survives a reopen", "[tags][ranking]")
{
    UsageFixture f;
    const auto clap = f.repo->createTag("Clap", 0xffe0b84a);
    const auto kick = f.repo->createTag("Kick", 0xffe05a5a);
    f.repo->recordTagUse(clap, testNow);
    f.repo->recordTagUse(clap, testNow);
    f.repo->recordTagUse(kick, testNow);

    auto ranked = f.repo->rankedTags(15, testNow);
    REQUIRE(indexOf(ranked, clap) < indexOf(ranked, kick));

    f.repo.reset();
    f.db.reset();
    Database db(f.file());
    SampleRepository repo(db);
    repo.createTables();
    ranked = repo.rankedTags(15, testNow);
    REQUIRE(indexOf(ranked, clap) < indexOf(ranked, kick));
}

TEST_CASE("Ranked tags put recent frequent tags first", "[tags][ranking]")
{
    UsageFixture f;
    const auto fresh = f.repo->createTag("Fresh", 0xff111111);
    const auto old = f.repo->createTag("Old", 0xff222222);
    const auto unused = f.repo->createTag("Aardvark", 0xff333333);

    f.repo->recordTagUse(fresh, testNow);
    for (int i = 0; i < 10; ++i)
        f.repo->recordTagUse(old, testNow - 90 * dayMs);

    const auto ranked = f.repo->rankedTags(15, testNow);
    REQUIRE(ranked.front().id == fresh);
    // A tag that was never assigned sinks below every used tag.
    REQUIRE(indexOf(ranked, unused) > indexOf(ranked, fresh));
    REQUIRE(indexOf(ranked, unused) > indexOf(ranked, old));
}

TEST_CASE("Ranked tags respect the limit", "[tags][ranking]")
{
    UsageFixture f;
    for (int i = 0; i < 20; ++i)
        f.repo->createTag("Tag " + juce::String(i), 0xff000000);
    REQUIRE(f.repo->rankedTags(8, testNow).size() == 8);
    REQUIRE(f.repo->rankedTags(15, testNow).size() == 15);
}

TEST_CASE("Schema 20 seeds usage from recent tags", "[tags][ranking][migration]")
{
    UsageFixture f;
    const auto older = f.repo->createTag("Older", 0xff111111);
    const auto newer = f.repo->createTag("Newer", 0xff222222);
    f.repo->recordRecentTag(older);
    f.repo->recordRecentTag(newer);
    f.repo.reset();
    f.db.reset();

    {
        // A catalog written before tag_usage existed, with recent-tag history to inherit.
        sqlite3* raw = nullptr;
        REQUIRE(sqlite3_open(f.file().getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
        REQUIRE(sqlite3_exec(raw, "DROP TABLE tag_usage; PRAGMA user_version=19;",
                             nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(raw);
    }

    Database db(f.file());
    SampleRepository repo(db);
    repo.createTables();
    const auto ranked = repo.rankedTags(15, testNow);
    // The newest recent tag receives the heavier seed weight, so an upgraded catalog opens with
    // the same order the recent list had.
    REQUIRE(indexOf(ranked, newer) < indexOf(ranked, older));
}
