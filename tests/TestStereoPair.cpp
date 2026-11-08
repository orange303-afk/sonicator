#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/audio/StereoPair.h"
#include "TestHelpers.h"
#include <cstring>
#include <vector>
using namespace sonicator;

namespace
{
juce::File writeMono(TestDirectory& t, const juce::String& name, float value, int frames,
                     int rate = 44100, int channels = 1)
{
    auto file = t.dir.getChildFile(name);
    file.deleteFile();
    auto out = file.createOutputStream();
    const int bytes = frames * channels * 2;
    out->write("RIFF", 4); out->writeInt(bytes + 36); out->write("WAVEfmt ", 8);
    out->writeInt(16); out->writeShort(1); out->writeShort((short) channels);
    out->writeInt(rate); out->writeInt(rate * channels * 2);
    out->writeShort((short) (channels * 2)); out->writeShort(16);
    out->write("data", 4); out->writeInt(bytes);
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c)
            out->writeShort((short) std::lround(value * 32767));
    return file;
}
} // namespace

TEST_CASE("Stereo markers are parsed from common L/R file names", "[stereo-pair]")
{
    auto check = [](const char* name, const char* base, int side)
    {
        const auto parsed = parseStereoName(name);
        REQUIRE(parsed.base == base);
        REQUIRE(parsed.side == side);
    };
    check("KICK-L.wav", "KICK", -1);
    check("KICK-R.wav", "KICK", 1);
    check("KICK R.wav", "KICK", 1);
    check("KICK.L.wav", "KICK", -1);
    check("KICK_R.wav", "KICK", 1);
    check("KICK.wav", "KICK.wav", 0);
    check("CRASH 1-L.wav", "CRASH 1", -1);
}

TEST_CASE("A mono L/R pair loads as one stereo snapshot", "[stereo-pair]")
{
    TestDirectory t;
    auto left = writeMono(t, "PAD-L.wav", 0.25f, 100);
    auto right = writeMono(t, "PAD-R.wav", -0.5f, 100);
    const auto pair = loadStereoPair(left);
    INFO(pair.error);
    REQUIRE(pair.paired);
    REQUIRE(pair.ok);
    REQUIRE(pair.audio != nullptr);
    REQUIRE(pair.audio->channels == 2);
    REQUIRE(pair.audio->frames == 100);
    REQUIRE(pair.audio->sampleRate == 44100);
    REQUIRE(pair.audio->left[0] == Catch::Approx(0.25f).margin(0.0001f));
    REQUIRE(pair.audio->right[0] == Catch::Approx(-0.5f).margin(0.0001f));

    // Selecting the right channel resolves the same pair with L still on the left.
    const auto fromRight = loadStereoPair(right);
    REQUIRE(fromRight.ok);
    REQUIRE(fromRight.audio->left[0] == Catch::Approx(0.25f).margin(0.0001f));
    REQUIRE(fromRight.audio->right[0] == Catch::Approx(-0.5f).margin(0.0001f));
}

TEST_CASE("Mismatched, missing and already-stereo pairs fall back", "[stereo-pair]")
{
    TestDirectory t;
    auto left = writeMono(t, "BAD-L.wav", 0.1f, 100);
    writeMono(t, "BAD-R.wav", 0.2f, 90);
    auto mismatch = loadStereoPair(left);
    REQUIRE(mismatch.paired);
    REQUIRE_FALSE(mismatch.ok);
    REQUIRE(mismatch.error.containsIgnoreCase("differs"));
    mismatch = loadStereoPair(left);
    REQUIRE_FALSE(mismatch.ok);

    auto lonely = writeMono(t, "LONELY-L.wav", 0.1f, 10);
    const auto missing = loadStereoPair(lonely);
    REQUIRE(missing.paired);
    REQUIRE_FALSE(missing.ok);
    REQUIRE(missing.error.containsIgnoreCase("complementary"));

    auto plain = writeMono(t, "PLAIN.wav", 0.1f, 10);
    REQUIRE_FALSE(loadStereoPair(plain).paired);

    auto stereoFile = writeMono(t, "WIDE-L.wav", 0.1f, 100, 44100, 2);
    writeMono(t, "WIDE-R.wav", 0.1f, 100, 44100, 2);
    const auto alreadyStereo = loadStereoPair(stereoFile);
    REQUIRE(alreadyStereo.paired);
    REQUIRE_FALSE(alreadyStereo.ok);
    REQUIRE(alreadyStereo.error.containsIgnoreCase("mono"));
}
