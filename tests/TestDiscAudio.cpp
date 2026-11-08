#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/disc/DiscAudioReader.h"
#include "core/disc/DiscFilesystem.h"
#include "core/disc/RawDiscSource.h"
#include "core/disc/SectorLayout.h"
#include "TestHelpers.h"
#include <cmath>
#include <cstring>
#include <vector>
using namespace sonicator;

namespace
{
void writeLe16(std::vector<unsigned char>& b, size_t at, uint16_t v)
{
    b[at] = (unsigned char) (v & 0xff); b[at + 1] = (unsigned char) (v >> 8);
}
void writeLe32(std::vector<unsigned char>& b, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; ++i) b[at + i] = (unsigned char) ((v >> (8 * i)) & 0xff);
}
std::vector<unsigned char> floatWav(int frames)
{
    const uint32_t payload = (uint32_t) frames * 4;
    std::vector<unsigned char> bytes(44 + payload, 0);
    std::memcpy(bytes.data(), "RIFF", 4); writeLe32(bytes, 4, 36 + payload);
    std::memcpy(bytes.data() + 8, "WAVEfmt ", 8);
    writeLe32(bytes, 16, 16); writeLe16(bytes, 20, 3); writeLe16(bytes, 22, 1);
    writeLe32(bytes, 24, 44100); writeLe32(bytes, 28, 44100 * 4);
    writeLe16(bytes, 32, 4); writeLe16(bytes, 34, 32);
    std::memcpy(bytes.data() + 36, "data", 4); writeLe32(bytes, 40, payload);
    for (int i = 0; i < frames; ++i)
    {
        const float value = 0.5f;
        std::memcpy(bytes.data() + 44 + (size_t) i * 4, &value, 4);
    }
    return bytes;
}
std::vector<unsigned char> channelWav(int channels, int frames)
{
    const uint32_t payload = (uint32_t) frames * channels * 2;
    std::vector<unsigned char> bytes(44 + payload, 0);
    std::memcpy(bytes.data(), "RIFF", 4); writeLe32(bytes, 4, 36 + payload);
    std::memcpy(bytes.data() + 8, "WAVEfmt ", 8);
    writeLe32(bytes, 16, 16); writeLe16(bytes, 20, 1); writeLe16(bytes, 22, (uint16_t) channels);
    writeLe32(bytes, 24, 44100); writeLe32(bytes, 28, 44100 * channels * 2);
    writeLe16(bytes, 32, (uint16_t) (channels * 2)); writeLe16(bytes, 34, 16);
    std::memcpy(bytes.data() + 36, "data", 4); writeLe32(bytes, 40, payload);
    return bytes;
}
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
} // namespace

TEST_CASE("A PCM WAV from a byte source decodes to one immutable snapshot", "[disc-audio]")
{
    TestDirectory t;
    auto file = t.wave("plain.wav", 440.0, 1, 1000);
    const auto source = openRawDisc(file, {});
    REQUIRE(source.ok());
    const auto decoded = decodeDiscAudio(source.value, file.getFileName(), {}, {});
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value->frames == 1000);
    REQUIRE(decoded.value->sampleRate == 44100);
    REQUIRE(decoded.value->channels == 1);
    REQUIRE(decoded.value->bitsPerSample == 16);
    REQUIRE_FALSE(decoded.value->floatingPoint);
    REQUIRE(decoded.value->right.empty());
    REQUIRE(std::isfinite(decoded.value->left[10]));
}

TEST_CASE("A float32 WAV keeps its precision policy", "[disc-audio]")
{
    TestDirectory t;
    auto file = writeBytes(t, "float.wav", floatWav(64));
    const auto source = openRawDisc(file, {});
    REQUIRE(source.ok());
    const auto decoded = decodeDiscAudio(source.value, "float.wav", {}, {});
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value->frames == 64);
    REQUIRE(decoded.value->bitsPerSample == 32);
    REQUIRE(decoded.value->floatingPoint);
    REQUIRE(decoded.value->left[0] == Catch::Approx(0.5f));
}

TEST_CASE("Compressed fixtures decode from a stream, not a file path", "[disc-audio]")
{
    TestDirectory t;
    for (const auto* name : { "tone.flac", "tone.mp3" })
    {
        const auto file = juce::File(SONICATOR_FIXTURE_DIR).getChildFile(name);
        REQUIRE(file.existsAsFile());
        const auto source = openRawDisc(file, {});
        REQUIRE(source.ok());
        const auto decoded = decodeDiscAudio(source.value, file.getFileName(), {}, {});
        INFO(name);
        REQUIRE(decoded.ok());
        REQUIRE(decoded.value->frames > 0);
        REQUIRE(decoded.value->channels >= 1);
        // FLAC reports its true lossless precision; a lossy decoder reports none (0) and the
        // snapshot declares the 24-bit decoded precision policy.
        REQUIRE(decoded.value->bitsPerSample == 24);
    }
}

TEST_CASE("More than two channels is unsupported rather than mixed down", "[disc-audio]")
{
    TestDirectory t;
    auto file = writeBytes(t, "three.wav", channelWav(3, 32));
    const auto source = openRawDisc(file, {});
    REQUIRE(source.ok());
    const auto decoded = decodeDiscAudio(source.value, "three.wav", {}, {});
    REQUIRE_FALSE(decoded.ok());
    REQUIRE(decoded.status == DiscStatus::unsupported);
}

TEST_CASE("A truncated audio payload is invalid and cancellation is honoured", "[disc-audio]")
{
    TestDirectory t;
    auto valid = t.wave("trunc-source.wav", 440.0, 1, 10000);
    auto bytes = std::vector<unsigned char>((size_t) valid.getSize());
    {
        auto in = valid.createInputStream();
        REQUIRE(in != nullptr);
        REQUIRE(in->read(bytes.data(), (int) bytes.size()) == (int) bytes.size());
    }
    bytes.resize(200);
    auto truncated = writeBytes(t, "truncated.wav", bytes);
    const auto source = openRawDisc(truncated, {});
    REQUIRE(source.ok());
    const auto decoded = decodeDiscAudio(source.value, "truncated.wav", {}, {});
    REQUIRE_FALSE(decoded.ok());
    REQUIRE(decoded.status == DiscStatus::malformed);

    const auto cancelled = decodeDiscAudio(source.value, "truncated.wav", {}, [] { return true; });
    REQUIRE(cancelled.status == DiscStatus::cancelled);
}

TEST_CASE("A block-range source decodes audio without a file path", "[disc-audio]")
{
    TestDirectory t;
    auto wav = t.wave("block.wav", 220.0, 2, 512);
    const auto wavBytes = (size_t) wav.getSize();
    std::vector<unsigned char> image(((wavBytes + 2047) / 2048 + 1) * 2048, 0);
    {
        auto in = wav.createInputStream();
        REQUIRE(in != nullptr);
        REQUIRE(in->read(image.data() + 2048, (int) wavBytes) == (int) wavBytes);
    }
    auto imageFile = writeBytes(t, "block.img", image);
    const auto raw = openRawDisc(imageFile, {});
    REQUIRE(raw.ok());
    TrackLayout layout; layout.sectorCount = image.size() / 2048; layout.profile = SectorProfile::cooked2048;
    const auto track = makeTrackSource(raw.value, layout, {});
    REQUIRE(track.ok());
    const auto extent = std::make_shared<BlockRangeByteSource>(track.value, 1, wavBytes);
    const auto decoded = decodeDiscAudio(extent, "block.wav", {}, {});
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value->frames == 512);
    REQUIRE(decoded.value->channels == 2);
}
