#include <catch2/catch_test_macros.hpp>
#include "core/database/PathRelocation.h"
#include "core/database/SampleRepository.h"
#include "TestHelpers.h"
using namespace sonicator::pathrelocation;
using namespace sonicator;

TEST_CASE("Path relocation is separator-aware in both directions", "[relocation][paths]")
{
    REQUIRE(isUnder("/Users/me/Drums/Kick.wav", "/Users/me/Drums"));
    REQUIRE_FALSE(isUnder("/Users/me/Drums2/Kick.wav", "/Users/me/Drums"));
    REQUIRE(isUnder("C:\\Samples\\Drums\\Kick.wav", "C:\\Samples\\Drums"));
    REQUIRE(isUnder("C:/Samples/Drums/Kick.wav", "C:\\Samples\\Drums"));
    REQUIRE(isUnder("C:\\a\\b.wav", "C:\\"));
    REQUIRE(isUnder("/a/b.wav", "/"));
    REQUIRE_FALSE(isUnder("/other/b.wav", "/a"));

    REQUIRE(remap("C:\\Samples\\Drums\\Kick\\01.wav", "C:\\Samples\\Drums",
                  "/Users/me/Drums", '/') == "/Users/me/Drums/Kick/01.wav");
    REQUIRE(remap("/Users/me/Drums/Kick/01.wav", "/Users/me/Drums",
                  "D:\\Samples\\Drums", '\\') == "D:\\Samples\\Drums\\Kick\\01.wav");
    REQUIRE(remap("C:\\a\\b.wav", "C:\\", "D:\\Library", '\\') == "D:\\Library\\a\\b.wav");
    REQUIRE(remap("/a/b.wav", "/", "/mnt/library", '/') == "/mnt/library/a/b.wav");
    REQUIRE(remap("/Users/me/Drums/Kick.wav", "/Users/me/Drums/",
                  "/new/Drums", '/') == "/new/Drums/Kick.wav");
    REQUIRE(remap("/other/b.wav", "/a", "/mnt/library", '/') == "/other/b.wav");
    REQUIRE(join("/new", "Kick\\01.wav", '/') == "/new/Kick/01.wav");
    REQUIRE(trimTrailingSeparators("/Users/me/Drums/") == "/Users/me/Drums");
    REQUIRE(trimTrailingSeparators("C:\\") == "C:");
    REQUIRE(trimTrailingSeparators("/") == "/");
}

TEST_CASE("Root relocation moves ignore entries with the catalog", "[relocate][ignore]")
{
    TestDirectory t;
    const auto oldRoot = t.dir.getChildFile("old");
    const auto newRoot = t.dir.getChildFile("new");
    oldRoot.createDirectory(); newRoot.createDirectory();
    const auto hidden = oldRoot.getChildFile("Drums/kick.wav").getFullPathName();
    Database db(t.dir.getChildFile("relocate.sqlite")); SampleRepository repo(db); repo.createTables();
    SampleRecord r; r.filePath = hidden; r.fileName = "kick.wav"; repo.upsert(r);
    repo.ignorePath(oldRoot.getChildFile("Drums").getFullPathName(), true);
    REQUIRE(repo.relocateRoots({ { oldRoot.getFullPathName(), newRoot.getFullPathName() } }).movedFiles == 1);
    const auto moved = newRoot.getChildFile("Drums/kick.wav").getFullPathName();
    REQUIRE(repo.findByPath(moved).has_value());
    REQUIRE(repo.isIgnored(moved));
    REQUIRE_FALSE(repo.isIgnored(hidden));
}
