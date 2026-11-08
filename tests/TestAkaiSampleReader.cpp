#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/disc/AkaiSampleReader.h"
#include "core/disc/RawDiscSource.h"
#include "TestHelpers.h"
#include <cstring>
#include <vector>
using namespace sonicator;

namespace
{
juce::File makeSample(TestDirectory& t, const juce::String& name, uint32_t frames,
                      uint16_t rate, uint64_t headerBytes, bool includeData = true)
{
    std::vector<unsigned char> bytes((size_t) (headerBytes + (includeData ? frames * 2 : 0)), 0);
    bytes[0] = 3;
    bytes[0x1a] = (unsigned char) (frames & 0xff);
    bytes[0x1b] = (unsigned char) ((frames >> 8) & 0xff);
    bytes[0x1c] = (unsigned char) ((frames >> 16) & 0xff);
    bytes[0x1d] = (unsigned char) ((frames >> 24) & 0xff);
    bytes[0x8a] = (unsigned char) (rate & 0xff);
    bytes[0x8b] = (unsigned char) (rate >> 8);
    if (includeData)
    {
        const short values[] = { 0, 32767, -32768, -1, 16384, -16384 };
        for (int i = 0; i < 6 && (uint32_t) i < frames; ++i)
        {
            const auto raw = (unsigned short) values[i];
            bytes[(size_t) headerBytes + (size_t) i * 2] = (unsigned char) (raw & 0xff);
            bytes[(size_t) headerBytes + (size_t) i * 2 + 1] = (unsigned char) (raw >> 8);
        }
    }
    auto file = t.dir.getChildFile(name);
    file.deleteFile();
    if (auto out = file.createOutputStream()) out->write(bytes.data(), (int) bytes.size());
    return file;
}
} // namespace

TEST_CASE("The verified Akai sample variant decodes signed 16-bit LE exactly", "[disc-akai-pcm]")
{
    TestDirectory t;
    auto file = makeSample(t, "kick.aki", 6, 44100, 150);
    const auto source = openRawDisc(file, {});
    REQUIRE(source.ok());
    const auto decoded = decodeAkaiSample(source.value, file.getSize(), {}, {});
    INFO(decoded.error);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value->frames == 6);
    REQUIRE(decoded.value->sampleRate == 44100);
    REQUIRE(decoded.value->channels == 1);
    REQUIRE(decoded.value->bitsPerSample == 16);
    REQUIRE(decoded.value->left[0] == Catch::Approx(0.0f));
    REQUIRE(decoded.value->left[1] == Catch::Approx(32767.0f / 32768.0f));
    REQUIRE(decoded.value->left[2] == Catch::Approx(-1.0f));
    REQUIRE(decoded.value->left[3] == Catch::Approx(-1.0f / 32768.0f));
    REQUIRE(decoded.value->left[4] == Catch::Approx(0.5f));
    REQUIRE(decoded.value->left[5] == Catch::Approx(-0.5f));
}

TEST_CASE("An unverified header variant is refused, not guessed", "[disc-akai-pcm]")
{
    TestDirectory t;
    auto file = makeSample(t, "doc190.aki", 10, 44100, 190);
    const auto source = openRawDisc(file, {});
    REQUIRE(source.ok());
    const auto decoded = decodeAkaiSample(source.value, file.getSize(), {}, {});
    REQUIRE_FALSE(decoded.ok());
    REQUIRE(decoded.status == DiscStatus::unsupported);
    REQUIRE(decoded.error.containsIgnoreCase("unverified"));
}

TEST_CASE("A wrong rate, truncated data and cancellation fail locally", "[disc-akai-pcm]")
{
    TestDirectory t;
    auto badRate = makeSample(t, "bad-rate.aki", 6, 100, 150);
    const auto rateSource = openRawDisc(badRate, {});
    REQUIRE(decodeAkaiSample(rateSource.value, badRate.getSize(), {}, {}).status == DiscStatus::malformed);

    auto truncated = makeSample(t, "truncated.aki", 100, 44100, 150, false);
    const auto truncatedSource = openRawDisc(truncated, {});
    REQUIRE(decodeAkaiSample(truncatedSource.value, 150 + 100 * 2, {}, {}).status == DiscStatus::malformed);

    auto file = makeSample(t, "cancel.aki", 6, 44100, 150);
    const auto source = openRawDisc(file, {});
    REQUIRE(decodeAkaiSample(source.value, file.getSize(), {}, [] { return true; }).status
            == DiscStatus::cancelled);
}
