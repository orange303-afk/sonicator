#include <catch2/catch_test_macros.hpp>
#include "core/scanner/FileScanner.h"
#include "TestHelpers.h"
using namespace sonicator;
TEST_CASE("Scanner recurses, filters case-insensitively and handles empty folders", "[scanner]")
{
    TestDirectory t;
    REQUIRE(FileScanner::scanDirectory(t.dir).empty());
    t.wave("tone.WAV"); t.wave("sub/тон.wav");
    t.dir.getChildFile("readme.txt").replaceWithText("not audio");
    REQUIRE(FileScanner::scanDirectory(t.dir).size() == 2);
    REQUIRE(FileScanner::isSupportedFormat(t.dir.getChildFile("x.FLAC")));
    REQUIRE(FileScanner::isSupportedFormat(t.dir.getChildFile("x.aif")));
    REQUIRE(FileScanner::isSupportedFormat(t.dir.getChildFile("x.mp3")));
    REQUIRE_FALSE(FileScanner::isSupportedFormat(t.dir.getChildFile("x.txt")));
    REQUIRE(FileScanner::scanDirectory(t.dir, [] { return true; }).empty());
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
