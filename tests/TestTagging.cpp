#include <catch2/catch_test_macros.hpp>
#include "core/tagging/TaggingService.h"
#include "core/ml/AdaptiveClassifier.h"
#include "TestHelpers.h"
using namespace sonicator;
namespace {
struct ScopeFixture
{
    TestDirectory t;
    std::unique_ptr<Database> db;
    std::unique_ptr<SampleRepository> repo;
    juce::String folder;
    std::vector<int64_t> ids;
    int64_t tagId = 0;
    ScopeFixture()
    {
        db = std::make_unique<Database>(t.dir.getChildFile("tagging.sqlite"));
        repo = std::make_unique<SampleRepository>(*db);
        repo->createTables();
        folder = t.dir.getChildFile("lib/Drums").getFullPathName();
        for (int i = 0; i < 6; ++i)
        {
            SampleRecord r; r.filePath = folder + "/s" + juce::String(i) + ".wav";
            r.fileName = "s" + juce::String(i) + ".wav"; r.fileHash = "h" + juce::String(i);
            ids.push_back(repo->upsert(r));
        }
        SampleRecord sibling; sibling.filePath = t.dir.getChildFile("lib/DrumsExtra/x.wav").getFullPathName();
        sibling.fileName = "x.wav"; sibling.fileHash = "sibling"; repo->upsert(sibling);
        tagId = repo->createTag("Kick", 0xff00ff00);
    }
};
}
TEST_CASE("Folder tag job is recursive, idempotent and records one recent tag", "[tagging][folders]")
{
    ScopeFixture f;
    TaggingService service(*f.repo);
    REQUIRE(service.startTagFolder(f.folder, f.tagId));
    REQUIRE(service.waitUntilFinished(15000));
    auto p = service.progress();
    REQUIRE_FALSE(p.cancelled);
    REQUIRE(p.applied == 6);
    REQUIRE(p.errors == 0);
    for (auto id : f.ids) REQUIRE(f.repo->tagsForSample(id).size() == 1);
    REQUIRE(f.repo->recentTags().size() == 1);
    REQUIRE(f.repo->recentTags()[0].id == f.tagId);
    // Out-of-scope sibling prefix is untouched.
    auto sibling = f.repo->findByPath(f.t.dir.getChildFile("lib/DrumsExtra/x.wav").getFullPathName());
    REQUIRE(sibling.has_value());
    REQUIRE(f.repo->tagsForSample(sibling->id).empty());
    // Repeating the same assignment creates no links and does not duplicate history.
    REQUIRE(service.startTagFolder(f.folder, f.tagId));
    REQUIRE(service.waitUntilFinished(15000));
    REQUIRE(service.progress().applied == 0);
    REQUIRE(f.repo->recentTags().size() == 1);
}
TEST_CASE("Tag batch cancellation rolls back the unfinished batch and keeps earlier ones", "[tagging][cancel]")
{
    ScopeFixture f;
    TaggingService service(*f.repo, 1);
    service.onBatch = [&service](const TaggingProgress& p)
    {
        if (p.applied >= 2) service.cancel();
    };
    REQUIRE(service.startTagSelected(f.ids, f.tagId));
    REQUIRE(service.waitUntilFinished(15000));
    const auto p = service.progress();
    REQUIRE(p.cancelled);
    REQUIRE(p.applied >= 2);
    REQUIRE(p.applied < (int) f.ids.size());
    int tagged = 0;
    for (auto id : f.ids) tagged += f.repo->tagsForSample(id).empty() ? 0 : 1;
    REQUIRE(tagged == p.applied);
}
TEST_CASE("Missing sample ids do not abort a batch", "[tagging][errors]")
{
    ScopeFixture f;
    TaggingService service(*f.repo);
    auto ids = f.ids;
    ids.push_back(99999999);
    REQUIRE(service.startTagSelected(ids, f.tagId));
    REQUIRE(service.waitUntilFinished(15000));
    REQUIRE(service.progress().applied == 6);
    REQUIRE(service.progress().errors == 0);
}
TEST_CASE("Empty and unavailable folders complete a tag job without assignments", "[tagging][folders][empty]")
{
    ScopeFixture f;
    TaggingService service(*f.repo);
    REQUIRE(service.startTagFolder(f.t.dir.getChildFile("empty").getFullPathName(), f.tagId));
    REQUIRE(service.waitUntilFinished(15000));
    REQUIRE(service.progress().applied == 0);
    REQUIRE_FALSE(service.progress().cancelled);
    REQUIRE(f.repo->recentTags().empty());
    REQUIRE_FALSE(service.startTagFolder({}, f.tagId));
}
TEST_CASE("Clearing tags removes links recursively and is idempotent", "[tagging][reset]")
{
    ScopeFixture f;
    const auto other = f.repo->createTag("Other", 0xff222222);
    REQUIRE(f.repo->assignTag({ f.ids[0], f.ids[1], f.ids[2] }, f.tagId) == 3);
    REQUIRE(f.repo->assignTag({ f.ids[0] }, other) == 1);
    REQUIRE(f.repo->countTaggedInFolder(f.folder) == 3);
    REQUIRE(f.repo->countTaggedSamples({ f.ids[0], f.ids[2], 99999999 }) == 2);
    const auto outcome = f.repo->clearTagsBatch({ f.ids[0], f.ids[1], 99999999 }, {});
    REQUIRE(outcome.added == 3);
    REQUIRE_FALSE(outcome.cancelled);
    REQUIRE(f.repo->tagsForSample(f.ids[0]).empty());
    REQUIRE(f.repo->tagsForSample(f.ids[1]).empty());
    REQUIRE(f.repo->tagsForSample(f.ids[2]).size() == 1);
    REQUIRE(f.repo->countTaggedInFolder(f.folder) == 1); // ids[2]
    REQUIRE(f.repo->tags().size() == 27); // tag definitions survive
    REQUIRE(f.repo->clearTagsBatch({ f.ids[0], f.ids[1] }, {}).added == 0);
    REQUIRE(f.repo->recentTags().empty());
}
TEST_CASE("Clearing tags rolls back the unfinished transaction when cancelled", "[tagging][reset][cancel]")
{
    ScopeFixture f;
    REQUIRE(f.repo->assignTag({ f.ids[0], f.ids[1], f.ids[2] }, f.tagId) == 3);
    int calls = 0;
    const auto outcome = f.repo->clearTagsBatch({ f.ids[0], f.ids[1], f.ids[2] },
        [&calls] { return ++calls > 1; });
    REQUIRE(outcome.cancelled);
    REQUIRE(outcome.added == 0);
    for (int i = 0; i < 3; ++i)
        REQUIRE(f.repo->tagsForSample(f.ids[(size_t) i]).size() == 1);
}
TEST_CASE("Folder reset removes every tag link and preserves stars, overrides and history", "[tagging][reset][folders]")
{
    ScopeFixture f;
    const auto other = f.repo->createTag("Other", 0xff222222);
    REQUIRE(f.repo->assignTag(f.ids, f.tagId) == 6);
    REQUIRE(f.repo->assignTag({ f.ids[0] }, other) == 1);
    f.repo->setStarred({ f.ids[0] }, true);
    SmartOverride overrideValue; overrideValue.textValue = "Kick";
    f.repo->setSmartOverride(f.ids[0], SmartField::Type, overrideValue);
    f.repo->recordRecentTag(f.tagId);
    TaggingService service(*f.repo);
    REQUIRE(service.startResetFolder(f.folder));
    REQUIRE(service.waitUntilFinished(15000));
    const auto p = service.progress();
    REQUIRE(p.clearing);
    REQUIRE_FALSE(p.cancelled);
    REQUIRE(p.applied == 7);
    for (auto id : f.ids) REQUIRE(f.repo->tagsForSample(id).empty());
    REQUIRE(f.repo->tags().size() == 27);
    REQUIRE(f.repo->findById(f.ids[0])->starred);
    REQUIRE(f.repo->smartOverrides(f.ids[0]).size() == 1);
    REQUIRE(f.repo->recentTags().size() == 1);
    REQUIRE(service.startResetFolder(f.folder));
    REQUIRE(service.waitUntilFinished(15000));
    REQUIRE(service.progress().applied == 0);
}
TEST_CASE("Reset cancellation keeps completed batches and reports the real removed count", "[tagging][reset][cancel]")
{
    ScopeFixture f;
    REQUIRE(f.repo->assignTag(f.ids, f.tagId) == 6);
    TaggingService service(*f.repo, 1);
    service.onBatch = [&service](const TaggingProgress& p) { if (p.applied >= 2) service.cancel(); };
    REQUIRE(service.startResetSelected(f.ids));
    REQUIRE(service.waitUntilFinished(15000));
    const auto p = service.progress();
    REQUIRE(p.cancelled);
    REQUIRE(p.applied >= 2);
    REQUIRE(p.applied < 6);
    int remaining = 0;
    for (auto id : f.ids) remaining += f.repo->tagsForSample(id).empty() ? 0 : 1;
    REQUIRE(remaining == 6 - p.applied);
}

TEST_CASE("Selected tag jobs skip ignored rows", "[tagging][ignore]")
{
    TestDirectory t; Database db(t.dir.getChildFile("tag-ignore.sqlite"));
    SampleRepository repo(db); repo.createTables();
    SampleRecord a, b; a.filePath = "/lib/a.wav"; a.fileName = "a.wav";
    b.filePath = "/lib/skip/b.wav"; b.fileName = "b.wav";
    const auto keep = repo.upsert(a);
    const auto hidden = repo.upsert(b);
    repo.ignorePath("/lib/skip", true);
    const auto tag = repo.createTag("Kick", 0xff00ff00);
    TaggingService service(repo);
    REQUIRE(service.startTagSelected({ keep, hidden }, tag));
    REQUIRE(service.waitUntilFinished(20000));
    REQUIRE(repo.tagsForSample(keep).size() == 1);
    REQUIRE(repo.tagsForSample(hidden).empty());
}

TEST_CASE("AutoTag incorporates learned tags from adaptive prototypes", "[tagging][adaptive]")
{
    AdaptiveClassifier classifier;
    std::vector<float> features(14, 0.3f);
    classifier.updatePrototype(50, "Lead", {"vintage", "analog"}, features);

    auto suggested = classifier.predictTags(features, 2);
    REQUIRE(suggested.size() >= 2);
    juce::StringArray labels;
    for (const auto& s : suggested) labels.add(s.label);
    CHECK(labels.contains("vintage"));
    CHECK(labels.contains("analog"));
    CHECK(suggested[0].score > 0.9f);
}

