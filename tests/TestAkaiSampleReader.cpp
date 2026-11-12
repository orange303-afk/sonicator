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
void putLe32(std::vector<unsigned char>& bytes, size_t at, uint32_t value)
{
    for (int i = 0; i < 4; ++i) bytes[at + (size_t) i] = (unsigned char) ((value >> (8 * i)) & 0xff);
}

juce::File makeSample(TestDirectory& t, const juce::String& name, uint32_t frames,
                      uint16_t rate, uint64_t headerBytes, bool includeData = true,
                      uint32_t startFrame = 0, uint32_t endFrame = 0, uint8_t root = 60,
                      int32_t headerFrames = -1)
{
    std::vector<unsigned char> bytes((size_t) (headerBytes + (includeData ? frames * 2 : 0)), 0);
    bytes[0] = 3;
    bytes[2] = root;
    putLe32(bytes, 0x1a, (uint32_t) (headerFrames >= 0 ? headerFrames : (int32_t) frames));
    putLe32(bytes, 0x1e, startFrame);
    putLe32(bytes, 0x22, endFrame > 0 ? endFrame : frames);
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

TEST_CASE("An S3000 192-byte header decodes from offset 192", "[disc-akai-pcm]")
{
    TestDirectory t;
    auto file = makeSample(t, "s3000.aki", 6, 44100, 192);
    const auto source = openRawDisc(file, {});
    REQUIRE(source.ok());
    const auto decoded = decodeAkaiSample(source.value, file.getSize(), {}, {});
    INFO(decoded.error);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value->frames == 6);
    REQUIRE(decoded.value->sampleRate == 44100);
    REQUIRE(decoded.value->channels == 1);
    REQUIRE(decoded.value->left[0] == Catch::Approx(0.0f));
    REQUIRE(decoded.value->left[1] == Catch::Approx(32767.0f / 32768.0f));
    REQUIRE(decoded.value->left[2] == Catch::Approx(-1.0f));
    REQUIRE(decoded.value->left[5] == Catch::Approx(-0.5f));
}

TEST_CASE("Akai header fields are parsed", "[disc-akai-pcm]")
{
    TestDirectory t;
    auto s3000 = makeSample(t, "fields.aki", 10, 48000, 192, true, 2, 8, 60);
    const auto source = openRawDisc(s3000, {});
    REQUIRE(source.ok());
    const auto header = readAkaiSampleHeader(*source.value, s3000.getSize(), {});
    INFO(header.error);
    REQUIRE(header.ok());
    REQUIRE(header.value.id == 3);
    REQUIRE(header.value.headerBytes == 192);
    REQUIRE(header.value.frames == 10);
    REQUIRE(header.value.startFrame == 2);
    REQUIRE(header.value.endFrame == 8);
    REQUIRE(header.value.rootNote == 60);
    REQUIRE(header.value.sampleRate == 48000);

    auto s1000 = makeSample(t, "fields150.aki", 10, 44100, 150);
    const auto flat = openRawDisc(s1000, {});
    REQUIRE(flat.ok());
    const auto flatHeader = readAkaiSampleHeader(*flat.value, s1000.getSize(), {});
    REQUIRE(flatHeader.ok());
    REQUIRE(flatHeader.value.headerBytes == 150);
    REQUIRE(flatHeader.value.frames == 10);
}

TEST_CASE("An S3000 header with inconsistent frames is refused", "[disc-akai-pcm]")
{
    TestDirectory t;
    auto file = makeSample(t, "badframes.aki", 10, 44100, 192, true, 0, 10, 60, 11);
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
