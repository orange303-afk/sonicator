#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <thread>
#include <unordered_set>
#include "core/indexing/IndexingService.h"
#include "TestHelpers.h"
#include <sqlite3.h>
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
    LibraryFilter duplicateFilter; duplicateFilter.hideDuplicates = true;
    REQUIRE(repo.query(duplicateFilter).total == 2); // copy.wav hidden, tone + corrupt remain
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
        { 4, "/elsewhere/outside.wav", false },
        { 5, "/Library/image.iso::/KICK.WAV", false, "/Library/image.iso" }
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
TEST_CASE("New-file scan analyzes only paths absent from the catalog and leaves missing records", "[indexing][new-files]")
{
    TestDirectory t;
    auto audio = t.dir.getChildFile("audio"); audio.createDirectory();
    t.wave("audio/known.wav", 440); t.wave("audio/gone.wav", 550);
    auto dbPath = t.dir.getChildFile("catalog.sqlite");
    Database db(dbPath); SampleRepository repo(db); repo.createTables();
    IndexingService index(repo);
    juce::StringArray roots { audio.getFullPathName() };
    REQUIRE(index.start(roots)); REQUIRE(index.waitUntilFinished(10000));
    REQUIRE(repo.count() == 2);
    const auto knownPath = audio.getChildFile("known.wav").getFullPathName();
    const auto gonePath = audio.getChildFile("gone.wav").getFullPathName();
    const auto knownBefore = *repo.findByPath(knownPath);

    REQUIRE(audio.getChildFile("gone.wav").deleteFile());
    t.wave("audio/new.wav", 660);
    REQUIRE(index.startNewOnly(roots)); REQUIRE(index.waitUntilFinished(10000));
    auto p = index.progress();
    REQUIRE(p.analyzed == 1); REQUIRE(p.skipped == 1); REQUIRE(p.processed == 2);
    REQUIRE(p.message == "New-file scan complete");
    REQUIRE(repo.count() == 3);
    REQUIRE(repo.findByPath(audio.getChildFile("new.wav").getFullPathName()).has_value());
    // The additive mode never reconciles missing paths.
    REQUIRE_FALSE(repo.findByPath(gonePath)->missing);
    REQUIRE(repo.findByPath(knownPath)->id == knownBefore.id);
    REQUIRE(repo.findByPath(knownPath)->fileHash == knownBefore.fileHash);

    // A known path is skipped even when the file changed on disk.
    t.wave("audio/new.wav", 220);
    REQUIRE(index.startNewOnly(roots)); REQUIRE(index.waitUntilFinished(10000));
    p = index.progress();
    REQUIRE(p.analyzed == 0); REQUIRE(p.skipped == 2);
    REQUIRE(index.progress().message == "New-file scan complete");
}
TEST_CASE("Cancelled reanalysis keeps the previous successful row", "[indexing][cancel]")
{
    TestDirectory t;
    auto file = t.wave("audio/long.wav", 440);
    auto dbPath = t.dir.getChildFile("catalog.sqlite");
    Database db(dbPath); SampleRepository repo(db); repo.createTables();
    IndexingService index(repo);
    juce::StringArray roots { file.getParentDirectory().getFullPathName() };
    REQUIRE(index.start(roots)); REQUIRE(index.waitUntilFinished(10000));
    auto original = repo.findByPath(file.getFullPathName()); REQUIRE(original);

    // A 600-second sparse WAV at the same path forces a reanalysis that is slow enough to cancel.
    REQUIRE(file.deleteFile());
    constexpr int frames = 44100 * 600;
    const int bytes = frames * 2;
    {
        auto stream = file.createOutputStream(); REQUIRE(stream != nullptr);
        stream->write("RIFF",4); stream->writeInt(bytes+36); stream->write("WAVEfmt ",8);
        stream->writeInt(16); stream->writeShort(1); stream->writeShort(1);
        stream->writeInt(44100); stream->writeInt(88200); stream->writeShort(2); stream->writeShort(16);
        stream->write("data",4); stream->writeInt(bytes);
        REQUIRE(stream->setPosition((int64_t) bytes+43)); stream->writeByte(0);
    }
    REQUIRE(index.start(roots, true));
    index.cancel();
    REQUIRE(index.waitUntilFinished(10000));
    REQUIRE(index.progress().cancelled);
    auto after = repo.findByPath(file.getFullPathName()); REQUIRE(after);
    REQUIRE(after->id == original->id);
    REQUIRE(after->fileHash == original->fileHash);
    REQUIRE(after->duration == original->duration);
    REQUIRE(after->error.isEmpty());
    for (const auto& row : repo.page()) REQUIRE(row.error != "Cancelled");
}
TEST_CASE("Subtree scan stays inside the chosen folder and never registers a new root", "[indexing][folders]")
{
    TestDirectory t;
    const auto lib = t.dir.getChildFile("lib");
    lib.getChildFile("Other").createDirectory();
    const auto drums = lib.getChildFile("Drums");
    const auto a = t.wave("lib/Drums/a.wav");
    const auto b = t.wave("lib/Other/b.wav");
    Database db(t.dir.getChildFile("subtree.sqlite")); SampleRepository repo(db); repo.createTables();
    IndexingService index(repo);
    REQUIRE(index.start({ lib.getFullPathName() })); REQUIRE(index.waitUntilFinished(10000));
    REQUIRE(repo.roots().size() == 1);
    REQUIRE(repo.count() == 2);
    const auto aId = repo.findByPath(a.getFullPathName())->id;
    const auto bId = repo.findByPath(b.getFullPathName())->id;
    const auto keep = repo.createTag("Keep", 0xff445566);
    REQUIRE(repo.assignTag({ aId }, keep) == 1);
    repo.setStarred({ aId }, true);
    REQUIRE(b.deleteFile());
    const auto c = t.wave("lib/Drums/new/c.wav");
    REQUIRE(index.startSubtree(drums.getFullPathName())); REQUIRE(index.waitUntilFinished(10000));
    REQUIRE(repo.roots().size() == 1);
    REQUIRE(repo.roots()[0] == lib.getFullPathName());
    REQUIRE(repo.findByPath(c.getFullPathName()).has_value());
    REQUIRE(repo.count() == 3);
    // The sibling file outside the scanned subtree is not reconciled as missing.
    REQUIRE_FALSE(repo.findById(bId)->missing);
    REQUIRE(repo.findByPath(a.getFullPathName()).has_value());
    REQUIRE(repo.tagsForSample(aId).size() == 1);
    REQUIRE(repo.findById(aId)->starred);
}
TEST_CASE("Scanning an unavailable folder does not mark catalogued siblings missing", "[indexing][folders][empty]")
{
    TestDirectory t;
    const auto file = t.wave("lib/Other/tone.wav");
    Database db(t.dir.getChildFile("unavailable.sqlite")); SampleRepository repo(db); repo.createTables();
    IndexingService index(repo);
    REQUIRE(index.start({ t.dir.getChildFile("lib").getFullPathName() })); REQUIRE(index.waitUntilFinished(10000));
    const auto id = repo.findByPath(file.getFullPathName())->id;
    REQUIRE(index.startSubtree(t.dir.getChildFile("lib/NotThere").getFullPathName()));
    REQUIRE(index.waitUntilFinished(10000));
    REQUIRE_FALSE(repo.findById(id)->missing);
    REQUIRE(repo.roots().size() == 1);
}

TEST_CASE("Indexing does not discover ignored files or folders", "[indexing][ignore]")
{
    TestDirectory t;
    auto keep = t.wave("keep.wav");
    t.wave("skip/hide.wav");
    Database db(t.dir.getChildFile("ignored.sqlite")); SampleRepository repo(db); repo.createTables();
    repo.ignorePath(t.dir.getChildFile("skip").getFullPathName(), true);
    IndexingService index(repo);
    REQUIRE(index.start({ t.dir.getFullPathName() })); REQUIRE(index.waitUntilFinished(20000));
    REQUIRE(repo.count() == 1);
    REQUIRE(repo.findByPath(keep.getFullPathName()).has_value());
    REQUIRE_FALSE(repo.findByPath(t.dir.getChildFile("skip/hide.wav").getFullPathName()).has_value());
}

TEST_CASE("An ignored row is not marked missing when its file disappears", "[indexing][ignore][missing]")
{
    TestDirectory t;
    auto gone = t.wave("gone.wav");
    Database db(t.dir.getChildFile("missing-ignore.sqlite")); SampleRepository repo(db); repo.createTables();
    SampleRecord r; r.filePath = gone.getFullPathName(); r.fileName = "gone.wav"; repo.upsert(r);
    repo.ignorePath(gone.getFullPathName(), false);
    REQUIRE(gone.deleteFile());
    IndexingService index(repo);
    REQUIRE(index.start({ t.dir.getFullPathName() })); REQUIRE(index.waitUntilFinished(20000));
    REQUIRE_FALSE(repo.findByPath(gone.getFullPathName())->missing);
}

TEST_CASE("startFolders indexes several folders without registering roots", "[indexing][split]")
{
    TestDirectory t;
    t.wave("a/one.wav"); t.wave("b/two.wav");
    Database db(t.dir.getChildFile("folders.sqlite")); SampleRepository repo(db); repo.createTables();
    IndexingService index(repo);
    REQUIRE(index.startFolders({ t.dir.getChildFile("a").getFullPathName(),
                                 t.dir.getChildFile("b").getFullPathName() }));
    REQUIRE(index.waitUntilFinished(20000));
    REQUIRE(repo.count() == 2);
    REQUIRE(repo.roots().isEmpty());
}

TEST_CASE("startFoldersNewOnly analyzes only unknown paths in the given folders", "[indexing][split][new-only]")
{
    TestDirectory t;
    t.wave("a/one.wav"); t.wave("a/two.wav");
    Database db(t.dir.getChildFile("new-only.sqlite")); SampleRepository repo(db); repo.createTables();
    SampleRecord known; known.filePath = t.dir.getChildFile("a/one.wav").getFullPathName();
    known.fileName = "one.wav"; known.error = "bad";
    repo.upsert(known);
    IndexingService index(repo);
    REQUIRE(index.startFoldersNewOnly({ t.dir.getChildFile("a").getFullPathName() }));
    REQUIRE(index.waitUntilFinished(20000));
    REQUIRE(repo.count() == 2);
    REQUIRE(repo.findByPath(known.filePath)->error == "bad");
    REQUIRE(repo.findByPath(t.dir.getChildFile("a/two.wav").getFullPathName())->error.isEmpty());
    REQUIRE(repo.roots().isEmpty());
}

TEST_CASE("A scan reports its analysis workers", "[indexing][parallel]")
{
    TestDirectory t;
    for (int i = 0; i < 4; ++i) t.wave("parallel/a" + juce::String(i) + ".wav", 200 + i * 40);
    Database db(t.dir.getChildFile("parallel.sqlite")); SampleRepository repo(db); repo.createTables();
    IndexingService index(repo);
    REQUIRE(index.start({ t.dir.getChildFile("parallel").getFullPathName() }));
    REQUIRE(index.waitUntilFinished(30000));
    const int expected = juce::jlimit(1, 4,
        (int) juce::jmax(1u, std::thread::hardware_concurrency()));
    REQUIRE(index.progress().analyzed == 4);
    REQUIRE(index.progress().workers == expected);
    if (std::thread::hardware_concurrency() >= 2) REQUIRE(index.progress().workers >= 2);
}

TEST_CASE("Parallel analysis stores the same rows as sequential analysis", "[indexing][parallel]")
{
    TestDirectory t;
    const auto folder = t.dir.getChildFile("parallel-rows");
    folder.createDirectory();
    std::vector<juce::File> files;
    for (int i = 0; i < 24; ++i)
        files.push_back(t.wave("parallel-rows/sample" + juce::String::formatted("%02d", i) + ".wav",
                               220 + i * 17, i % 3 == 0 ? 2 : 1));
    files.push_back(t.wave("parallel-rows/dup1.wav", 330));
    REQUIRE(files.back().copyFileTo(folder.getChildFile("dup2.wav")));
    files.push_back(folder.getChildFile("dup2.wav"));

    std::vector<SampleRecord> expected;
    for (const auto& file : files) expected.push_back(BasicAnalyzer().analyzeAndHash(file));

    Database db(t.dir.getChildFile("parallel-rows.sqlite")); SampleRepository repo(db); repo.createTables();
    IndexingService index(repo);
    REQUIRE(index.start({ folder.getFullPathName() }));
    REQUIRE(index.waitUntilFinished(60000));
    const auto progress = index.progress();
    REQUIRE(progress.analyzed == (int) files.size());
    REQUIRE(progress.failed == 0);
    REQUIRE(progress.duplicates == 1);
    REQUIRE(repo.count() == (int) files.size());
    for (size_t i = 0; i < files.size(); ++i)
    {
        INFO(files[i].getFileName());
        const auto stored = repo.findByPath(files[i].getFullPathName());
        REQUIRE(stored.has_value());
        REQUIRE(stored->fileHash == expected[i].fileHash);
        REQUIRE(stored->format == expected[i].format);
        REQUIRE(stored->duration == Catch::Approx(expected[i].duration));
        REQUIRE(stored->sampleRate == Catch::Approx(expected[i].sampleRate));
        REQUIRE(stored->numChannels == expected[i].numChannels);
        REQUIRE(stored->bitsPerSample == expected[i].bitsPerSample);
        REQUIRE(stored->error == expected[i].error);
    }

    // The additive mode still skips every known path after a parallel scan.
    REQUIRE(index.startNewOnly({ folder.getFullPathName() }));
    REQUIRE(index.waitUntilFinished(30000));
    REQUIRE(index.progress().analyzed == 0);
    REQUIRE(index.progress().skipped == (int) files.size());
}

TEST_CASE("A catalog write failure ends the scan instead of stranding its workers", "[indexing][parallel][abort]")
{
    TestDirectory t;
    const auto folder = t.dir.getChildFile("abort");
    folder.createDirectory();
    for (int i = 0; i < 40; ++i)
        t.wave("abort/take" + juce::String::formatted("%02d", i) + ".wav", 220 + i * 5);
    const auto dbFile = t.dir.getChildFile("abort.sqlite");
    Database db(dbFile); SampleRepository repo(db); repo.createTables();
    {
        sqlite3* raw = nullptr;
        REQUIRE(sqlite3_open(dbFile.getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
        REQUIRE(sqlite3_exec(raw,
            "CREATE TRIGGER fail_insert BEFORE INSERT ON samples"
            " WHEN NEW.path LIKE '%/abort/%' BEGIN SELECT RAISE(ABORT,'boom'); END;",
            nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(raw);
    }
    IndexingService index(repo);
    REQUIRE(index.start({ folder.getFullPathName() }));
    // Before the writer was exception-safe, this unwound while workers were still parked in the
    // pipeline and the service thread never finished.
    REQUIRE(index.waitUntilFinished(15000));
    REQUIRE_FALSE(index.progress().running);
    REQUIRE(index.progress().message.containsIgnoreCase("boom"));
}
