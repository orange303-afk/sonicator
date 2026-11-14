#include <catch2/catch_test_macros.hpp>
#include "core/database/Database.h"
#include "core/database/FolderIndex.h"
#include "core/database/SampleRepository.h"
#include "TestHelpers.h"

using namespace sonicator;

namespace
{
SampleRecord fileRecord(const juce::String& path)
{
    SampleRecord record;
    record.filePath = path;
    record.fileName = path.fromLastOccurrenceOf("/", false, false);
    record.format = juce::File(path).hasFileExtension("iso;img;bin;cue") ? "ISO" : "WAV";
    return record;
}
}

TEST_CASE("The folder index groups catalog paths by folder", "[folder-index]")
{
    TestDirectory t;
    const auto lib = t.dir.getChildFile("Library");
    const auto drums = lib.getChildFile("Drums");
    const auto kicks = drums.getChildFile("Kicks");
    const auto bass = lib.getChildFile("Bass");
    kicks.createDirectory();
    bass.createDirectory();

    Database db(t.dir.getChildFile("index.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    repo.addRoot(lib.getFullPathName());
    repo.upsert(fileRecord(kicks.getChildFile("kick.wav").getFullPathName()));
    repo.upsert(fileRecord(drums.getChildFile("snare.wav").getFullPathName()));
    repo.upsert(fileRecord(bass.getChildFile("sub.wav").getFullPathName()));

    FolderIndexRequest request;
    request.roots = repo.roots();
    const auto index = buildFolderIndex(repo, request);

    REQUIRE(index.roots == repo.roots());
    // The tree holds folders only: a folder appears under its parent when it has content.
    REQUIRE(index.children.at(lib.getFullPathName()).count(drums.getFullPathName()) == 1);
    REQUIRE(index.children.at(lib.getFullPathName()).count(bass.getFullPathName()) == 1);
    REQUIRE(index.children.at(drums.getFullPathName()).count(kicks.getFullPathName()) == 1);
    REQUIRE(index.children.count(kicks.getFullPathName()) == 0);   // no subfolders below it
    REQUIRE(index.children.count(bass.getFullPathName()) == 0);
    REQUIRE(index.images.empty());
}

TEST_CASE("An ignored folder keeps its files out of the index unless they are revealed", "[folder-index]")
{
    TestDirectory t;
    const auto lib = t.dir.getChildFile("Library");
    const auto drums = lib.getChildFile("Drums");
    const auto bass = lib.getChildFile("Bass");
    drums.createDirectory();
    bass.createDirectory();

    Database db(t.dir.getChildFile("ignore.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    repo.addRoot(lib.getFullPathName());
    repo.upsert(fileRecord(drums.getChildFile("kick.wav").getFullPathName()));
    repo.upsert(fileRecord(bass.getChildFile("sub.wav").getFullPathName()));
    repo.ignorePath(bass.getFullPathName(), true);

    FolderIndexRequest hidden;
    hidden.roots = repo.roots();
    hidden.ignored = repo.ignoredPaths();
    const auto withoutBass = buildFolderIndex(repo, hidden);
    REQUIRE(withoutBass.children.count(bass.getFullPathName()) == 0);
    REQUIRE(withoutBass.children.at(lib.getFullPathName()).count(bass.getFullPathName()) == 0);
    REQUIRE(withoutBass.children.at(lib.getFullPathName()).count(drums.getFullPathName()) == 1);

    FolderIndexRequest shown = hidden;
    shown.revealIgnored = true;
    const auto withBass = buildFolderIndex(repo, shown);
    REQUIRE(withBass.children.at(lib.getFullPathName()).count(bass.getFullPathName()) == 1);
}

TEST_CASE("Disc images and their failures are part of the index", "[folder-index]")
{
    TestDirectory t;
    const auto lib = t.dir.getChildFile("Library");
    lib.createDirectory();
    const auto good = lib.getChildFile("good.iso");
    const auto broken = lib.getChildFile("broken.iso");
    REQUIRE(good.replaceWithText("iso"));
    REQUIRE(broken.replaceWithText("iso"));

    Database db(t.dir.getChildFile("images.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    repo.addRoot(lib.getFullPathName());
    repo.upsert(fileRecord(good.getFullPathName()));
    auto failed = fileRecord(broken.getFullPathName());
    failed.error = "unreadable";
    repo.upsert(failed);

    FolderIndexRequest request;
    request.roots = repo.roots();
    const auto index = buildFolderIndex(repo, request);
    REQUIRE(index.images.count(good.getFullPathName()) == 1);
    REQUIRE(index.images.count(broken.getFullPathName()) == 1);
    REQUIRE(index.failedImages.count(broken.getFullPathName()) == 1);
    REQUIRE(index.children.at(lib.getFullPathName()).count(good.getFullPathName()) == 1);
}

TEST_CASE("A subtree of the index carries only the folders under its roots", "[folder-index]")
{
    TestDirectory t;
    const auto lib = t.dir.getChildFile("Library");
    const auto drums = lib.getChildFile("Drums");
    const auto kicks = drums.getChildFile("Kicks");
    const auto bass = lib.getChildFile("Bass");
    kicks.createDirectory();
    bass.createDirectory();

    Database db(t.dir.getChildFile("subset.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    repo.addRoot(lib.getFullPathName());
    repo.upsert(fileRecord(kicks.getChildFile("kick.wav").getFullPathName()));
    repo.upsert(fileRecord(bass.getChildFile("sub.wav").getFullPathName()));
    repo.upsert(fileRecord(lib.getChildFile("loop.iso").getFullPathName()));

    FolderIndexRequest request;
    request.roots = repo.roots();
    const auto index = buildFolderIndex(repo, request);
    const auto onlyDrums = folderIndexUnderRoots(index, juce::StringArray { drums.getFullPathName() });

    REQUIRE(onlyDrums.roots.size() == 1);
    REQUIRE(onlyDrums.children.count(drums.getFullPathName()) == 1);
    REQUIRE(onlyDrums.children.at(drums.getFullPathName()).count(kicks.getFullPathName()) == 1);
    REQUIRE(onlyDrums.children.count(bass.getFullPathName()) == 0);
    REQUIRE(onlyDrums.children.count(lib.getFullPathName()) == 0);
    REQUIRE(onlyDrums.images.empty());   // the image sits in the library root, not in Drums
}

TEST_CASE("An ignored path only covers its own subtree", "[folder-index]")
{
    const std::vector<IgnoredPath> entries { { "/a/b", true, 0 }, { "/a/file.wav", false, 0 } };
    REQUIRE(isPathIgnored("/a/b", entries));
    REQUIRE(isPathIgnored("/a/b/c/d.wav", entries));
    REQUIRE(isPathIgnored("/a/b/", entries));
    REQUIRE_FALSE(isPathIgnored("/a/bc/d.wav", entries));   // a prefix that is not a folder boundary
    REQUIRE(isPathIgnored("/a/file.wav", entries));
    REQUIRE_FALSE(isPathIgnored("/a/file.wav.bak", entries)); // a file entry covers only itself
    REQUIRE_FALSE(isPathIgnored("/other", entries));
    REQUIRE_FALSE(isPathIgnored("/a", {}));
}
