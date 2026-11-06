#include <catch2/catch_test_macros.hpp>
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
