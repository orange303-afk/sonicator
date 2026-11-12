#include <catch2/catch_test_macros.hpp>
#include "core/emu/EblReader.h"
#include "TestHelpers.h"
#include <optional>
#include <span>
#include <vector>
namespace {
using sonicator::EblLoop;
using sonicator::EblResult;

// A test-only writer for the EBL layout: FORM/TOC2, the first E5S1 section
// (with the absolute offset of the second), the 14-byte second section, the
// 176-byte data-description block, a pad, the PCM, and an optional EXLZ trailer.
struct EblOptions
{
    juce::String name = "Sample";
    uint32_t rate = 47360;
    int channelByte = 1;
    int pad = 8;
    std::vector<int16_t> left;
    std::vector<int16_t> right;
    std::optional<std::pair<uint32_t, uint32_t>> loop;
    // Writes two extra pad bytes, so the front anchor lands two bytes into the
    // first left sample while the true audio start is anchored from the end.
    bool stereoFrontSkew = false;
    // Keeps only this many bytes of the audio, as a truncated rip would.
    int truncateAfterAudioBytes = -1;
};

std::vector<std::byte> makeEbl(const EblOptions& o)
{
    std::vector<std::byte> b;
    auto u8 = [&b] (int v) { b.push_back((std::byte) (v & 0xff)); };
    auto u16le = [&u8] (int v) { u8(v); u8(v >> 8); };
    auto u32le = [&u8] (uint32_t v) { for (int i = 0; i < 4; ++i) u8((int) ((v >> (8 * i)) & 0xff)); };
    auto u32be = [&u8] (uint32_t v) { for (int i = 3; i >= 0; --i) u8((int) ((v >> (8 * i)) & 0xff)); };
    auto ascii = [&u8] (const char* s) { for (const char* p = s; *p != 0; ++p) u8(*p); };
    auto name64 = [&] {
        const size_t start = b.size();
        for (auto c : o.name) u16le((int) ((uint32_t) c & 0xffffu));
        while (b.size() < start + 64) { u8(' '); u8(0); }
    };
    ascii("FORM"); u32be(0);
    ascii("E5B0TOC2"); u32be(78);
    ascii("E5S1"); u32be(0); u32be(98);
    u8(0); u8(0);
    name64();
    while (b.size() < 98) u8(0);
    ascii("E5S1"); u32be(0);
    for (int i = 0; i < 6; ++i) u8(0);
    name64();
    u32le(0);                             // V1
    const uint32_t perChannel = (uint32_t) (o.left.size() * 2);
    u32le(180u + (uint32_t) o.pad);       // V2, audio anchor
    u32le(180u + (uint32_t) o.pad + perChannel); // V3, per-channel length
    for (int i = 0; i < 6; ++i) u32le(0); // V4..V9
    u32le(o.rate);                        // V10
    u32le(0);                             // V11
    u32le(((uint32_t) o.channelByte << 16) | (0x02u << 8) | 1u); // V12
    for (int i = 0; i < 64; ++i) u8(0);   // comment
    const size_t padWritten = (size_t) o.pad + (o.stereoFrontSkew && o.channelByte == 3 ? 2u : 0u);
    const size_t audio = b.size() + padWritten;
    for (size_t i = 0; i < padWritten; ++i) u8(0);
    for (auto v : o.left) u16le(v);
    if (o.channelByte == 3)
        for (auto v : o.right) u16le(v);
    if (o.loop)
    {
        ascii("EXLZ"); u32le(0x20);
        ascii("INFO"); u32le(8); u32le(1); u32le(1);
        ascii("MARK"); u32le(8); u32le(o.loop->first); u32le(o.loop->second);
    }
    if (o.truncateAfterAudioBytes >= 0)
        b.resize(audio + (size_t) o.truncateAfterAudioBytes);
    const uint32_t formSize = (uint32_t) (b.size() - 8);
    for (int i = 0; i < 4; ++i) b[4 + i] = (std::byte) ((formSize >> (8 * (3 - i))) & 0xff);
    return b;
}

std::vector<int16_t> interleaved(const std::vector<int16_t>& left, const std::vector<int16_t>& right)
{
    std::vector<int16_t> out;
    for (size_t i = 0; i < left.size(); ++i) { out.push_back(left[i]); out.push_back(right[i]); }
    return out;
}

EblOptions monoOptions()
{
    EblOptions o;
    o.name = "org nod C +36";
    o.channelByte = 1;
    o.pad = 8;
    o.left = { 0, 1, -1, 32767, -32768, 100, -100, 1234 };
    return o;
}

EblOptions stereoOptions()
{
    EblOptions o;
    o.name = "Stereo Pair";
    o.channelByte = 3;
    o.pad = 8;
    o.left = { 10, 20, 30, 40 };
    o.right = { -10, -20, -30, -40 };
    return o;
}
} // namespace

TEST_CASE("EBL mono with an 8-byte pad and a loop", "[ebl]")
{
    auto o = monoOptions();
    o.loop = std::pair<uint32_t, uint32_t> { 2, 6 };
    auto r = sonicator::parseEbl(makeEbl(o));
    REQUIRE(r.ok());
    CHECK(r.sample->name == "org nod C +36");
    CHECK(r.sample->sampleRate == 47360);
    CHECK(r.sample->channels == 1);
    CHECK(r.sample->frames == 8);
    CHECK(r.sample->pcm == o.left);
    REQUIRE(r.sample->loop.has_value());
    CHECK(r.sample->loop->start == 2);
    CHECK(r.sample->loop->end == 6);
}

TEST_CASE("EBL mono with a 4-byte pad and no loop runs to the end of the file", "[ebl]")
{
    auto o = monoOptions();
    o.pad = 4;
    auto r = sonicator::parseEbl(makeEbl(o));
    REQUIRE(r.ok());
    CHECK(r.sample->channels == 1);
    CHECK(r.sample->frames == 8);
    CHECK(r.sample->pcm == o.left);
    CHECK_FALSE(r.sample->loop.has_value());
}

TEST_CASE("EBL channel byte 0x02 is mono", "[ebl]")
{
    auto o = monoOptions();
    o.channelByte = 2;
    auto r = sonicator::parseEbl(makeEbl(o));
    REQUIRE(r.ok());
    CHECK(r.sample->channels == 1);
    CHECK(r.sample->frames == 8);
    CHECK(r.sample->pcm == o.left);
}

TEST_CASE("EBL stereo is anchored from the end", "[ebl]")
{
    auto o = stereoOptions();
    o.stereoFrontSkew = true;
    auto r = sonicator::parseEbl(makeEbl(o));
    REQUIRE(r.ok());
    CHECK(r.sample->channels == 2);
    CHECK(r.sample->frames == 4);
    CHECK(r.sample->pcm == interleaved(o.left, o.right));
}

TEST_CASE("EBL stereo split at the front", "[ebl]")
{
    auto o = stereoOptions();
    auto r = sonicator::parseEbl(makeEbl(o));
    REQUIRE(r.ok());
    CHECK(r.sample->channels == 2);
    CHECK(r.sample->frames == 4);
    CHECK(r.sample->pcm == interleaved(o.left, o.right));
}

TEST_CASE("EBL truncated stereo falls back to a front split", "[ebl]")
{
    auto o = stereoOptions();
    o.truncateAfterAudioBytes = 4; // two left frames; the tail is gone
    auto r = sonicator::parseEbl(makeEbl(o));
    REQUIRE(r.ok());
    CHECK(r.sample->channels == 2);
    CHECK(r.sample->frames == 1);
    // The span is split in half and interleaved: the first left frame and the
    // next value on the disc, rather than an out-of-range from-the-end anchor.
    CHECK(r.sample->pcm == std::vector<int16_t> { 10, 20 });
}

TEST_CASE("EBL loop outside the audio is dropped", "[ebl]")
{
    auto o = monoOptions();
    o.loop = std::pair<uint32_t, uint32_t> { 2, 20 };
    auto r = sonicator::parseEbl(makeEbl(o));
    REQUIRE(r.ok());
    CHECK_FALSE(r.sample->loop.has_value());
    o.loop = std::pair<uint32_t, uint32_t> { 6, 2 };
    r = sonicator::parseEbl(makeEbl(o));
    REQUIRE(r.ok());
    CHECK_FALSE(r.sample->loop.has_value());
}

TEST_CASE("EBL refuses a file that is not a FORM", "[ebl]")
{
    auto bytes = makeEbl(monoOptions());
    bytes[0] = std::byte { 'X' };
    auto r = sonicator::parseEbl(bytes);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error == "not a FORM");
    CHECK(sonicator::parseEbl(std::vector<std::byte> { std::byte { 'F' }, std::byte { 'O' } }).error == "not a FORM");
}

TEST_CASE("EBL refuses a missing E5B0TOC2 table of contents", "[ebl]")
{
    auto bytes = makeEbl(monoOptions());
    bytes[8] = std::byte { 'X' };
    auto r = sonicator::parseEbl(bytes);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error == "not an EBL (no E5B0TOC2)");
}

TEST_CASE("EBL refuses a first section that is not E5S1", "[ebl]")
{
    auto bytes = makeEbl(monoOptions());
    bytes[0x14] = std::byte { 'X' };
    auto r = sonicator::parseEbl(bytes);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error == "first section is not E5S1");
}

TEST_CASE("EBL refuses a second section that is not where the header says", "[ebl]")
{
    auto bytes = makeEbl(monoOptions());
    bytes[0x1c] = std::byte { 0xff };
    auto r = sonicator::parseEbl(bytes);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error == "second section is not where the header says");
}

TEST_CASE("EBL refuses a data block that runs past the end", "[ebl]")
{
    auto bytes = makeEbl(monoOptions());
    const size_t second = 98;
    bytes.resize(second + 14 + 40);
    auto r = sonicator::parseEbl(bytes);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error == "data block runs past the end");
}

TEST_CASE("EBL refuses a zero sample rate", "[ebl]")
{
    auto o = monoOptions();
    o.rate = 0;
    auto r = sonicator::parseEbl(makeEbl(o));
    REQUIRE_FALSE(r.ok());
    CHECK(r.error == "sample rate is zero");
}

TEST_CASE("EBL refuses audio that is not there", "[ebl]")
{
    auto o = monoOptions();
    o.truncateAfterAudioBytes = 0;
    auto r = sonicator::parseEbl(makeEbl(o));
    REQUIRE_FALSE(r.ok());
    CHECK(r.error == "no audio");
}

TEST_CASE("EBL refuses a decoded size over the limit", "[ebl]")
{
    auto bytes = makeEbl(monoOptions());
    auto r = sonicator::parseEbl(bytes, 4);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error == "decoded size exceeds the limit");
}

TEST_CASE("EBL every truncation is refused or decoded, never out of range", "[ebl]")
{
    auto o = stereoOptions();
    o.loop = std::pair<uint32_t, uint32_t> { 1, 3 };
    const auto bytes = makeEbl(o);
    for (size_t n = 0; n <= bytes.size(); ++n)
    {
        const auto r = sonicator::parseEbl(std::span<const std::byte>(bytes.data(), n));
        if (r.ok())
        {
            CHECK(r.sample->channels >= 1);
            CHECK(r.sample->channels <= 2);
            CHECK(r.sample->frames > 0);
            CHECK(r.sample->pcm.size() == (size_t) r.sample->frames * (size_t) r.sample->channels);
            if (r.sample->loop)
                CHECK(r.sample->loop->end <= (uint32_t) r.sample->frames);
        }
        else
        {
            CHECK(r.error.isNotEmpty());
        }
    }
}

TEST_CASE("EBL reads a file and recognises its extension", "[ebl]")
{
    TestDirectory t;
    auto o = monoOptions();
    o.loop = std::pair<uint32_t, uint32_t> { 2, 6 };
    const auto bytes = makeEbl(o);
    auto file = t.dir.getChildFile("named.ebl");
    file.replaceWithData(bytes.data(), bytes.size());
    auto r = sonicator::readEblFile(file);
    REQUIRE(r.ok());
    CHECK(r.sample->name == "org nod C +36");
    CHECK(r.sample->pcm == o.left);
    CHECK(sonicator::isEblFile(file));
    CHECK_FALSE(sonicator::isEblFile(t.dir.getChildFile("named.wav")));
    CHECK(sonicator::isEblFile(t.dir.getChildFile("named.EBL"))); // extension is case-insensitive
}

TEST_CASE("EBL refuses a file too large to decode before reading it", "[ebl]")
{
    TestDirectory t;
    auto file = t.dir.getChildFile("huge.ebl");
    REQUIRE(file.replaceWithText(juce::String::repeatedString("x", 70 * 1024)));
    auto r = sonicator::readEblFile(file, 0);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error == "decoded size exceeds the limit");
}
