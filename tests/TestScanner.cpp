#include <catch2/catch_test_macros.hpp>
#include "core/scanner/FileScanner.h"
#include "TestHelpers.h"
using namespace sonicator;
TEST_CASE("Scanner recurses, filters case-insensitively and handles empty folders", "[scanner]")
{
    TestDirectory t;
    REQUIRE(FileScanner::scanDirectory(t.dir).empty());
    t.wave("tone.WAV"); t.wave(juce::String::fromUTF8("sub/тон.wav"));
    t.dir.getChildFile("readme.txt").replaceWithText("not audio");
    REQUIRE(FileScanner::scanDirectory(t.dir).size() == 2);
    REQUIRE(FileScanner::isSupportedFormat(t.dir.getChildFile("x.FLAC")));
    REQUIRE(FileScanner::isSupportedFormat(t.dir.getChildFile("x.aif")));
    REQUIRE(FileScanner::isSupportedFormat(t.dir.getChildFile("x.mp3")));
    REQUIRE_FALSE(FileScanner::isSupportedFormat(t.dir.getChildFile("x.txt")));
    REQUIRE(FileScanner::scanDirectory(t.dir, [] { return true; }).empty());
}
TEST_CASE("Cancellation checkpoints include directory-only traversal", "[scanner]")
{
    TestDirectory t;
    for (auto name : {"a/b/c", "d/e/f", "g/h/i"}) t.dir.getChildFile(name).createDirectory();
    int checkpoints = 0;
    auto files = FileScanner::scanDirectory(t.dir,[&] { ++checkpoints; return checkpoints >= 3; });
    REQUIRE(files.empty());
    REQUIRE(checkpoints >= 3);
}
TEST_CASE("AppleDouble resource forks are not mistaken for audio but real dot-underscore audio is kept", "[scanner]")
{
    TestDirectory t; t.wave("._real.wav"); t.wave("regular.wav");
    auto fork = t.dir.getChildFile("._metadata.wav");
    {
        auto output = fork.createOutputStream(); output->writeIntBigEndian(0x00051607); output->writeInt(0);
    }
    REQUIRE_FALSE(FileScanner::isSupportedFormat(fork));
    REQUIRE(FileScanner::isSupportedFormat(t.dir.getChildFile("._real.wav")));
    auto files = FileScanner::scanDirectory(t.dir);
    bool regularFound = false;
    for (auto& entry : files)
    {
        REQUIRE(entry.file != fork);
        if (entry.file.getFileName() == "regular.wav") regularFound = true;
    }
    REQUIRE(regularFound); // macOS native enumeration itself may hide ._ names.
}
TEST_CASE("Full file hash distinguishes files with identical prefixes and lengths", "[scanner]")
{
    TestDirectory t;
    auto a = t.dir.getChildFile("a.wav"), b = t.dir.getChildFile("b.wav");
    juce::MemoryBlock bytes(70000, true);
    a.replaceWithData(bytes.getData(), bytes.getSize());
    static_cast<char*>(bytes.getData())[69999] = 1;
    b.replaceWithData(bytes.getData(), bytes.getSize());
    auto hash = FileScanner::computeFileHash(a);
    REQUIRE(hash.length() == 64);
    REQUIRE(hash == FileScanner::computeFileHash(a));
    REQUIRE(hash != FileScanner::computeFileHash(b));
}
