#include <catch2/catch_test_macros.hpp>
#include "core/scanner/FileScanner.h"
#include "core/smart/AssetAnalyzer.h"
#include "core/database/SampleRepository.h"
#include "TestHelpers.h"
#include <cstring>
#include <vector>
using namespace sonicator;

namespace
{
juce::File writeBytes(TestDirectory& t, const juce::String& name, const std::vector<unsigned char>& bytes)
{
    auto file = t.dir.getChildFile(name);
    file.deleteFile();
    if (auto out = file.createOutputStream())
    {
        if (! bytes.empty()) out->write(bytes.data(), (int) bytes.size());
        out->flush();
    }
    return file;
}
void putLe16(std::vector<unsigned char>& b, size_t at, uint16_t v)
{
    b[at] = (unsigned char) (v & 0xff); b[at + 1] = (unsigned char) (v >> 8);
}
void putBe32(std::vector<unsigned char>& b, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; ++i) b[at + i] = (unsigned char) ((v >> (8 * (3 - i))) & 0xff);
}
void putLe32(std::vector<unsigned char>& b, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; ++i) b[at + i] = (unsigned char) ((v >> (8 * i)) & 0xff);
}
std::vector<unsigned char> tinyIso()
{
    std::vector<unsigned char> bytes(20 * 2048, 0);
    auto* pvd = bytes.data() + 16 * 2048;
    pvd[0] = 1; std::memcpy(pvd + 1, "CD001", 5); pvd[6] = 1;
    putLe32(bytes, 16 * 2048 + 80, 20); putBe32(bytes, 16 * 2048 + 84, 20);
    putLe16(bytes, 16 * 2048 + 128, 2048);
    bytes[16 * 2048 + 130] = 0x08; bytes[16 * 2048 + 131] = 0x00;
    auto* term = bytes.data() + 17 * 2048;
    term[0] = 255; std::memcpy(term + 1, "CD001", 5); term[6] = 1;
    auto at = 16 * 2048 + 156;
    bytes[at] = 34;
    putLe32(bytes, at + 2, 18); putBe32(bytes, at + 6, 18);
    putLe32(bytes, at + 10, 2048); putBe32(bytes, at + 14, 2048);
    bytes[at + 25] = 0x02; bytes[at + 32] = 1;
    return bytes;
}
std::vector<unsigned char> tinyAkai()
{
    std::vector<unsigned char> bytes(16 * 8192, 0);
    std::memset(bytes.data() + 0xca, 0x0a, 12);
    putLe16(bytes, 0xca + 12, 1); putLe16(bytes, 0xca + 14, 4);
    for (int i = 0; i < 3; ++i) putLe16(bytes, 0x70a + i * 2, 0x40);
    bytes[0x4500] = 1; bytes[0x4501] = 1; putLe16(bytes, 0x4502, 16);
    return bytes;
}
} // namespace

TEST_CASE("Disc images are indexed as metadata-only assets by content", "[disc-index]")
{
    TestDirectory t;
    auto isoFile = writeBytes(t, "library.iso", tinyIso());
    const auto iso = analyzeMetadataAsset(isoFile);
    INFO(iso.error);
    REQUIRE(iso.error.isEmpty());
    REQUIRE(iso.format == "ISO");
    REQUIRE(iso.version == metadataOnlyVersion);
    REQUIRE(iso.compatibility == AssetCompatibility::supported);
    REQUIRE(iso.fileHash.isNotEmpty());

    auto akaiFile = writeBytes(t, "volume.bin", tinyAkai());
    const auto akai = analyzeMetadataAsset(akaiFile);
    INFO(akai.error);
    REQUIRE(akai.error.isEmpty());
    REQUIRE(akai.format == "AKAI");
    REQUIRE(akai.compatibility == AssetCompatibility::supported);
}

TEST_CASE("An unrecognised image stays visible as unknown", "[disc-index]")
{
    TestDirectory t;
    auto file = writeBytes(t, "mystery.img", std::vector<unsigned char>(4096, 0x5a));
    const auto result = analyzeMetadataAsset(file);
    REQUIRE_FALSE(result.error.isEmpty());
    REQUIRE(result.compatibility == AssetCompatibility::unknown);
    REQUIRE(result.format == "IMG");
}

TEST_CASE("MDX is catalogued as unsupported, never renamed to ISO", "[disc-index]")
{
    TestDirectory t;
    std::vector<unsigned char> mdx(64, 0);
    std::memcpy(mdx.data(), "MEDIA DESCRIPTOR", 16);
    auto file = writeBytes(t, "library.mdx", mdx);
    const auto result = analyzeMetadataAsset(file);
    REQUIRE_FALSE(result.error.isEmpty());
    REQUIRE(result.compatibility == AssetCompatibility::unsupported);
    REQUIRE(result.format == "MDX");
}

TEST_CASE("The scanner discovers image extensions as assets", "[disc-index]")
{
    TestDirectory t;
    writeBytes(t, "one.iso", tinyIso());
    writeBytes(t, "two.img", tinyIso());
    writeBytes(t, "three.bin", tinyAkai());
    const auto found = FileScanner::scanDirectory(t.dir);
    REQUIRE(found.size() == 3);
}

TEST_CASE("A cue sheet is indexed by its resolved data track", "[disc-index][disc-cue]")
{
    TestDirectory t;
    auto bin = writeBytes(t, "disk.bin", std::vector<unsigned char>(8 * 2048, 0));
    REQUIRE(bin.existsAsFile());
    auto cue = t.dir.getChildFile("disk.cue");
    cue.replaceWithText("FILE \"disk.bin\" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n");
    const auto result = analyzeMetadataAsset(cue);
    INFO(result.error);
    REQUIRE(result.error.isEmpty());
    REQUIRE(result.format == "CUE");
    REQUIRE(result.compatibility == AssetCompatibility::supported);
    REQUIRE(result.version == metadataOnlyVersion);

    auto bad = t.dir.getChildFile("bad.cue");
    bad.replaceWithText("FILE \"disk.bin\" BINARY\n  TRACK 01 MODE1/2048\n");
    const auto failed = analyzeMetadataAsset(bad);
    REQUIRE_FALSE(failed.error.isEmpty());
    REQUIRE(failed.compatibility == AssetCompatibility::invalid);
}
