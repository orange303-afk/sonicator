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

TEST_CASE("Catalog path normalisation and the folder range use the chosen separator",
          "[relocation][paths][folders]")
{
    // A Windows path written with forward slashes is stored in its native form.
    REQUIRE(normaliseSeparators("C:/Samples/Drums/Kick.wav", '\\')
            == "C:\\Samples\\Drums\\Kick.wav");
    // A POSIX separator leaves backslashes alone: they are legal filename characters there.
    REQUIRE(normaliseSeparators("C:\\Samples\\Drums", '/') == "C:\\Samples\\Drums");
    REQUIRE(normaliseSeparators("/Users/me/Drums", '/') == "/Users/me/Drums");

    const auto windows = folderRange("C:/Samples/Drums", '\\');
    CHECK(windows.first == "C:\\Samples\\Drums\\");
    CHECK(windows.second == "C:\\Samples\\Drums]");
    const auto inRange = [] (const juce::String& path, const std::pair<juce::String, juce::String>& range)
    {
        return path.compare(range.first) >= 0 && path.compare(range.second) < 0;
    };
    CHECK(inRange("C:\\Samples\\Drums\\Kick.wav", windows));
    CHECK_FALSE(inRange("C:\\Samples\\Drums2\\Kick.wav", windows));
    CHECK_FALSE(inRange("C:\\Samples\\Drums", windows)); // the folder alone is not a descendant

    const auto posix = folderRange("/Users/me/Drums", '/');
    CHECK(posix.first == "/Users/me/Drums/");
    CHECK(posix.second == "/Users/me/Drums0");
    CHECK(inRange("/Users/me/Drums/deep/Kick.wav", posix));
    CHECK_FALSE(inRange("/Users/me/Drums2/Kick.wav", posix));

    // A volume root keeps its single separator and still bounds everything below it.
    const auto root = folderRange("C:\\", '\\');
    CHECK(root.first == "C:\\");
    CHECK(inRange("C:\\a.wav", root));
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
