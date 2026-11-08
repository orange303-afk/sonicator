#include <catch2/catch_test_macros.hpp>
#include "core/database/AssetCompatibility.h"
#include "core/dsp/BasicAnalyzer.h"
#include "core/smart/AssetAnalyzer.h"
#include "TestHelpers.h"
using namespace sonicator;

TEST_CASE("Only unsupported and invalid assets count as incompatible", "[asset-compat]")
{
    REQUIRE_FALSE(isIncompatible(AssetCompatibility::unknown));
    REQUIRE_FALSE(isIncompatible(AssetCompatibility::supported));
    REQUIRE(isIncompatible(AssetCompatibility::unsupported));
    REQUIRE(isIncompatible(AssetCompatibility::invalid));
    REQUIRE(assetCompatibilityFromInt(0) == AssetCompatibility::unknown);
    REQUIRE(assetCompatibilityFromInt(1) == AssetCompatibility::supported);
    REQUIRE(assetCompatibilityFromInt(2) == AssetCompatibility::unsupported);
    REQUIRE(assetCompatibilityFromInt(3) == AssetCompatibility::invalid);
    REQUIRE(assetCompatibilityFromInt(99) == AssetCompatibility::unknown);
}

TEST_CASE("Audio analysis classifies supported, invalid and ambiguous files", "[asset-compat]")
{
    TestDirectory t;
    const auto good = BasicAnalyzer().analyzeAndHash(t.wave("good.wav"));
    REQUIRE(good.error.isEmpty());
    REQUIRE(good.compatibility == AssetCompatibility::supported);

    auto truncated = t.dir.getChildFile("truncated.wav");
    truncated.deleteFile();
    if (auto out = truncated.createOutputStream())
    {
        out->write("RIFF", 4); out->writeInt(1000); out->write("WAVEfmt ", 8); out->writeInt(16);
        out->writeShort(1); out->writeShort(1); out->writeInt(44100); out->writeInt(88200);
        out->writeShort(2); out->writeShort(16);
        out->write("data", 4); out->writeInt(4000); // claims more payload than the file holds
    }
    const auto bad = BasicAnalyzer().analyzeAndHash(truncated);
    REQUIRE_FALSE(bad.error.isEmpty());
    REQUIRE(bad.compatibility == AssetCompatibility::invalid);

    // An empty but structurally valid WAV is empty audio, not proof of an incompatible format.
    const auto empty = BasicAnalyzer().analyzeAndHash(t.wave("empty.wav", 440, 1, 0));
    REQUIRE_FALSE(empty.error.isEmpty());
    REQUIRE(empty.compatibility == AssetCompatibility::unknown);
}

TEST_CASE("Metadata analysis classifies corrupt containers as invalid", "[asset-compat]")
{
    TestDirectory t;
    auto text = t.dir.getChildFile("not-a-bank.sf2");
    text.replaceWithText("plain text");
    const auto result = analyzeMetadataAsset(text);
    REQUIRE_FALSE(result.error.isEmpty());
    REQUIRE(result.compatibility == AssetCompatibility::invalid);
}
