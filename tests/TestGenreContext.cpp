#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/smart/GenreContextExtractor.h"

using namespace sonicator;

TEST_CASE("GenreContextExtractor path extraction for top-level genre", "[genre]")
{
    juce::File file("/Samples/Drum and Bass/Breaks/break01.wav");
    auto res = GenreContextExtractor::extract(file);
    REQUIRE(res.genre == "Drum & Bass");
    REQUIRE(res.subgenre.isEmpty());
    REQUIRE(res.source == "path");
    REQUIRE(res.confidence >= 0.8);
}

TEST_CASE("GenreContextExtractor path extraction for subgenre and parent genre", "[genre]")
{
    juce::File file("/Packs/Trap Collection/808s/sub01.wav");
    auto res = GenreContextExtractor::extract(file);
    REQUIRE(res.subgenre == "Trap");
    REQUIRE(res.genre == "Hip-Hop");
    REQUIRE(res.source == "path");
    REQUIRE(res.confidence >= 0.8);
}

TEST_CASE("GenreContextExtractor existing metadata candidate precedence", "[genre]")
{
    juce::File file("/Packs/Whatever/file.wav");
    SmartCandidate cand;
    cand.source = "id3";
    cand.textValue = "Techno";
    auto res = GenreContextExtractor::extract(file, { cand });
    REQUIRE(res.genre == "Techno");
    REQUIRE(res.subgenre.isEmpty());
    REQUIRE(res.source == "id3");
    REQUIRE(res.confidence == Catch::Approx(0.9));
}

TEST_CASE("GenreContextExtractor canonical genres list", "[genre]")
{
    auto genres = GenreContextExtractor::canonicalGenres();
    REQUIRE(genres.contains("Electronic"));
    REQUIRE(genres.contains("House"));
    REQUIRE(genres.contains("Techno"));
    REQUIRE(genres.contains("Hip-Hop"));
    REQUIRE(genres.contains("Drum & Bass"));
    REQUIRE(genres.contains("Trap"));
    REQUIRE(genres.contains("Dubstep"));
    REQUIRE(genres.contains("Ambient"));
    REQUIRE(genres.contains("Lo-Fi"));
    REQUIRE(genres.contains("Synthwave"));
    REQUIRE(genres.contains("Rock"));
    REQUIRE(genres.contains("Pop"));
    REQUIRE(genres.contains("Funk"));
    REQUIRE(genres.contains("Jazz"));
    REQUIRE(genres.contains("Reggae"));
}

TEST_CASE("GenreContextExtractor normalizeGenreToken alias mapping", "[genre]")
{
    REQUIRE(GenreContextExtractor::normalizeGenreToken("dnb") == "Drum & Bass");
    REQUIRE(GenreContextExtractor::normalizeGenreToken("drum and bass") == "Drum & Bass");
    REQUIRE(GenreContextExtractor::normalizeGenreToken("drum & bass") == "Drum & Bass");
    REQUIRE(GenreContextExtractor::normalizeGenreToken("trap") == "Trap");
    REQUIRE(GenreContextExtractor::normalizeGenreToken("drill") == "Trap");
    REQUIRE(GenreContextExtractor::normalizeGenreToken("hiphop") == "Hip-Hop");
    REQUIRE(GenreContextExtractor::normalizeGenreToken("hip hop") == "Hip-Hop");
    REQUIRE(GenreContextExtractor::normalizeGenreToken("boombap") == "Hip-Hop");
    REQUIRE(GenreContextExtractor::normalizeGenreToken("techno") == "Techno");
    REQUIRE(GenreContextExtractor::normalizeGenreToken("house") == "House");
    REQUIRE(GenreContextExtractor::normalizeGenreToken("tech house") == "House");
    REQUIRE(GenreContextExtractor::normalizeGenreToken("deep house") == "House");
    REQUIRE(GenreContextExtractor::normalizeGenreToken("lofi") == "Lo-Fi");
    REQUIRE(GenreContextExtractor::normalizeGenreToken("lo-fi") == "Lo-Fi");
    REQUIRE(GenreContextExtractor::normalizeGenreToken("synthwave") == "Synthwave");
    REQUIRE(GenreContextExtractor::normalizeGenreToken("retrowave") == "Synthwave");
}

TEST_CASE("GenreContextExtractor path extraction with subfolder refinement", "[genre]")
{
    juce::File file("/Samples/House/Deep House/Loops/loop.wav");
    auto res = GenreContextExtractor::extract(file);
    REQUIRE(res.genre == "House");
    REQUIRE(res.subgenre == "Deep House");
    REQUIRE(res.source == "path");
    REQUIRE(res.confidence >= 0.8);

    juce::File winFile = juce::File::createFileWithoutCheckingPath("C:\\Packs\\Hip-Hop\\Drill Collection\\kick.wav");
    auto winRes = GenreContextExtractor::extract(winFile);
    REQUIRE(winRes.genre == "Hip-Hop");
    REQUIRE(winRes.subgenre == "Drill");
    REQUIRE(winRes.source == "path");
    REQUIRE(winRes.confidence >= 0.8);
}

TEST_CASE("GenreContextExtractor existing metadata candidate handling details", "[genre]")
{
    juce::File file("/Packs/Whatever/file.wav");

    // Subgenre in metadata
    SmartCandidate cand1;
    cand1.source = "id3";
    cand1.textValue = "Trap";
    auto res1 = GenreContextExtractor::extract(file, { cand1 });
    REQUIRE(res1.genre == "Hip-Hop");
    REQUIRE(res1.subgenre == "Trap");
    REQUIRE(res1.source == "id3");
    REQUIRE(res1.confidence == Catch::Approx(0.9));

    // Non-genre ID3 candidate (BPM) should be ignored, falling back to path or empty
    SmartCandidate candBpm;
    candBpm.source = "id3";
    candBpm.field = SmartField::Bpm;
    candBpm.numericValue = 128.0;
    candBpm.textValue = "128";
    auto resBpm = GenreContextExtractor::extract(file, { candBpm });
    REQUIRE(resBpm.genre.isEmpty());
    REQUIRE(resBpm.confidence == 0.0);

    // INFO / metadata candidate
    SmartCandidate candInfo;
    candInfo.source = "info";
    candInfo.detail = "genre";
    candInfo.textValue = "Drum & Bass";
    auto resInfo = GenreContextExtractor::extract(file, { candInfo });
    REQUIRE(resInfo.genre == "Drum & Bass");
    REQUIRE(resInfo.source == "metadata");
    REQUIRE(resInfo.confidence == Catch::Approx(0.9));
}

TEST_CASE("GenreContextExtractor returns empty for unrecognized path without candidates", "[genre]")
{
    juce::File file("/Music/Samples/Various/unknown01.wav");
    auto res = GenreContextExtractor::extract(file);
    REQUIRE(res.genre.isEmpty());
    REQUIRE(res.subgenre.isEmpty());
    REQUIRE(res.confidence == 0.0);
    REQUIRE(res.source.isEmpty());
}
