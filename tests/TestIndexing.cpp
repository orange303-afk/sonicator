#include <catch2/catch_test_macros.hpp>
#include <unordered_set>
#include "core/indexing/IndexingService.h"
#include "TestHelpers.h"
using namespace sonicator;
TEST_CASE("Indexer persists, skips unchanged files, detects duplicates and retries changes", "[indexing]")
{
    TestDirectory t;
    auto audio = t.dir.getChildFile("audio"); audio.createDirectory();
    t.wave("audio/tone.wav", 440);
    t.dir.getChildFile("audio/tone.wav").copyFileTo(audio.getChildFile("copy.wav"));
    audio.getChildFile("corrupt.wav").replaceWithText("bad");
    Database db(t.dir.getChildFile("catalog.sqlite")); SampleRepository repo(db); repo.createTables();
    IndexingService index(repo);
    juce::StringArray roots { audio.getFullPathName() };
    REQUIRE(index.start(roots)); REQUIRE(index.waitUntilFinished(10000));
    auto p = index.progress();
    REQUIRE(p.processed == 3); REQUIRE(p.analyzed == 2); REQUIRE(p.failed == 1);
    REQUIRE(p.duplicates == 1); REQUIRE(repo.count() == 3);
    REQUIRE(repo.roots().size() == 1);
    auto old = repo.findByPath(audio.getChildFile("tone.wav").getFullPathName()); REQUIRE(old);
    REQUIRE(index.start(roots)); REQUIRE(index.waitUntilFinished(10000));
    p = index.progress(); REQUIRE(p.skipped == 2); REQUIRE(p.failed == 1); // bad files can be retried
    t.wave("audio/tone.wav", 220, 1, 22050);
    REQUIRE(index.start(roots)); REQUIRE(index.waitUntilFinished(10000));
    auto updated = repo.findByPath(audio.getChildFile("tone.wav").getFullPathName()); REQUIRE(updated);
    REQUIRE(updated->id == old->id); REQUIRE(updated->duration == 0.5);
    REQUIRE(updated->fileHash != old->fileHash);
    REQUIRE(repo.count() == 3);
    REQUIRE(index.start(roots, true)); REQUIRE(index.waitUntilFinished(10000));
    REQUIRE(index.progress().skipped == 0);
}
TEST_CASE("Missing-file restoration and analyzer-version invalidation keep stable identities", "[indexing]")
{
    TestDirectory t; auto file = t.wave("audio/tone.wav");
    Database db(t.dir.getChildFile("catalog.sqlite")); SampleRepository repo(db); repo.createTables();
    IndexingService index(repo); juce::StringArray roots { file.getParentDirectory().getFullPathName() };
    REQUIRE(index.start(roots)); REQUIRE(index.waitUntilFinished(10000));
    auto original = repo.findByPath(file.getFullPathName()); REQUIRE(original);
    auto obsolete = *original; obsolete.version = basicFeaturesVersion-1; repo.upsert(obsolete);
    REQUIRE(index.start(roots)); REQUIRE(index.waitUntilFinished(10000));
    REQUIRE(index.progress().analyzed == 1); REQUIRE(index.progress().skipped == 0);
    REQUIRE(file.deleteFile());
    REQUIRE(index.start(roots)); REQUIRE(index.waitUntilFinished(10000));
    REQUIRE(repo.findByPath(file.getFullPathName())->missing);
    t.wave("audio/tone.wav");
    REQUIRE(index.start(roots)); REQUIRE(index.waitUntilFinished(10000));
    auto restored = repo.findByPath(file.getFullPathName()); REQUIRE(restored);
    REQUIRE_FALSE(restored->missing); REQUIRE(restored->id == original->id);
}
TEST_CASE("Missing files are detected from the discovery set without touching other roots", "[indexing]")
{
    TestDirectory t;
    auto audio = t.dir.getChildFile("audio"); audio.createDirectory();
    t.wave("audio/keep.wav", 440); t.wave("audio/gone.wav", 550);
    Database db(t.dir.getChildFile("catalog.sqlite")); SampleRepository repo(db); repo.createTables();
    IndexingService index(repo);
    REQUIRE(index.start({audio.getFullPathName()})); REQUIRE(index.waitUntilFinished(10000));
    REQUIRE(repo.count() == 2);
    SampleRecord outside; outside.filePath = "/elsewhere/not-scanned.wav"; outside.fileName = "not-scanned.wav";
    outside.fileHash = "h"; outside.version = basicFeaturesVersion; repo.upsert(outside);
    REQUIRE(audio.getChildFile("gone.wav").deleteFile());
    REQUIRE(index.start({audio.getFullPathName()})); REQUIRE(index.waitUntilFinished(10000));
    REQUIRE(repo.findByPath(audio.getChildFile("gone.wav").getFullPathName())->missing);
    REQUIRE_FALSE(repo.findByPath(audio.getChildFile("keep.wav").getFullPathName())->missing);
    REQUIRE_FALSE(repo.findByPath("/elsewhere/not-scanned.wav")->missing);
    REQUIRE_FALSE(index.progress().finalizing);
}
TEST_CASE("Rows written with the current feature version are skipped, not reanalyzed", "[indexing][version]")
{
    TestDirectory t; auto file = t.wave("audio/tone.wav");
    auto dbPath = t.dir.getChildFile("catalog.sqlite");
    Database db(dbPath); SampleRepository repo(db); repo.createTables();
    auto analyzed = BasicAnalyzer().analyzeAndHash(file);
    analyzed.fileSize = file.getSize(); analyzed.dateModified = file.getLastModificationTime().toMilliseconds();
    analyzed.version = 1; repo.upsert(analyzed);
    REQUIRE(basicFeaturesVersion == 1);
    IndexingService index(repo);
    REQUIRE(index.start({file.getParentDirectory().getFullPathName()})); REQUIRE(index.waitUntilFinished(10000));
    REQUIRE(index.progress().skipped == 1);
    REQUIRE(index.progress().analyzed == 0);
}
TEST_CASE("A record absent from discovery is only marked missing when the file is gone", "[indexing]")
{
    TestDirectory t; auto present = t.wave("audio/present.wav", 440);
    juce::StringArray roots { present.getParentDirectory().getFullPathName() };
    std::unordered_set<std::string> discovered;
    std::vector<PathEntry> rows {
        { 1, present.getFullPathName(), false },
        { 2, present.getParentDirectory().getChildFile("gone.wav").getFullPathName(), false },
        { 3, present.getFullPathName(), true },
        { 4, "/elsewhere/outside.wav", false }
    };
    REQUIRE(findMissingIds(rows, discovered, roots) == std::vector<int64_t>{ 2 });
}
TEST_CASE("Indexer cancellation and unavailable folders are explicit", "[indexing]")
{
    TestDirectory t; Database db(t.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db); repo.createTables(); IndexingService index(repo);
    REQUIRE(index.start({t.dir.getChildFile("absent").getFullPathName()}));
    REQUIRE(index.waitUntilFinished(10000));
    REQUIRE(index.progress().message.isNotEmpty()); REQUIRE_FALSE(index.progress().running);
    t.wave("large.wav", 440, 1, 44100 * 5);
    REQUIRE(index.start({t.dir.getFullPathName()}));
    index.setPaused(true); index.cancel();
    REQUIRE(index.waitUntilFinished(10000));
    REQUIRE(index.progress().cancelled);
}
