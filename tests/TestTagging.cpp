#include <catch2/catch_test_macros.hpp>
#include "core/tagging/TaggingService.h"
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
