#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/audio/AudioEngine.h"
#include "core/audio/ContainerValidation.h"
#include "core/audio/SonicatorFormats.h"
#include "core/caf/CafAudioFormat.h"
#include "core/scanner/FileScanner.h"
#include "ui/preview/PreviewPanel.h"
#include "TestHelpers.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using namespace sonicator;
using Catch::Approx;

namespace
{
struct CafBytes
{
    std::vector<juce::uint8> v;
    void u8(int x) { v.push_back((juce::uint8) (x & 0xff)); }
    void u16be(int x) { u8(x >> 8); u8(x); }
    void u32be(juce::uint32 x)
    {
        u8((int) (x >> 24)); u8((int) (x >> 16)); u8((int) (x >> 8)); u8((int) x);
    }
    void u64be(juce::int64 x) { for (int i = 7; i >= 0; --i) u8((int) ((x >> (8 * i)) & 0xff)); }
    void f64be(double value)
    {
        juce::uint64 bits = 0;
        std::memcpy(&bits, &value, 8);
        u64be((juce::int64) bits);
    }
    void raw(const void* data, size_t count)
    {
        const auto* p = (const juce::uint8*) data;
        v.insert(v.end(), p, p + count);
    }
};

void chunk(CafBytes& b, const char* tag, const CafBytes& payload)
{
    b.raw(tag, 4);
    b.u64be((juce::int64) payload.v.size());
    b.v.insert(b.v.end(), payload.v.begin(), payload.v.end());
}

// Writes a caff header plus a desc chunk. `flags` follows CAF's own linear PCM convention:
// bit 0 float, bit 1 little-endian.
void header(CafBytes& b, double rate, const char* format, juce::uint32 flags,
            juce::uint32 bytesPerPacket, juce::uint32 framesPerPacket,
            juce::uint32 channels, juce::uint32 bits)
{
    b.raw("caff", 4); b.u16be(1); b.u16be(0);
    CafBytes desc;
    desc.f64be(rate);
    desc.raw(format, 4);
    desc.u32be(flags); desc.u32be(bytesPerPacket); desc.u32be(framesPerPacket);
    desc.u32be(channels); desc.u32be(bits);
    chunk(b, "desc", desc);
}

void data(CafBytes& b, const std::vector<juce::uint8>& audio)
{
    CafBytes payload;
    payload.u32be(0); // edit count
    payload.v.insert(payload.v.end(), audio.begin(), audio.end());
    chunk(b, "data", payload);
}

void packetTable(CafBytes& b, juce::int64 packets, juce::int64 validFrames)
{
    CafBytes payload;
    payload.u64be(packets); payload.u64be(validFrames);
    payload.u32be(0); payload.u32be(0); // priming, remainder
    chunk(b, "pakt", payload);
}

std::vector<std::byte> bytesOf(const CafBytes& b)
{
    std::vector<std::byte> out(b.v.size());
    for (size_t i = 0; i < b.v.size(); ++i) out[i] = (std::byte) b.v[i];
    return out;
}

juce::File writeCaf(TestDirectory& t, const juce::String& name, const CafBytes& b)
{
    auto file = t.dir.getChildFile(name);
    file.deleteFile();
    file.replaceWithData(b.v.data(), b.v.size());
    return file;
}

CafAudio decode(const CafBytes& b, juce::String& error)
{
    CafAudio audio;
    REQUIRE(decodeCaf(bytesOf(b), audio, error));
    return audio;
}

// Apple IMA4 tables, mirrored here so a packet can be encoded the way the decoder expects.
const int stepTable[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
    73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
    494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272,
    2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};
const int indexTable[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

struct ImaEncoder
{
    int predictor = 0;
    int index = 0;
    int nibbleFor(int target)
    {
        const int step = stepTable[index];
        int difference = target - predictor;
        int nibble = 0;
        if (difference < 0) { nibble = 8; difference = -difference; }
        if (difference >= step) { nibble |= 4; difference -= step; }
        if (difference >= step >> 1) { nibble |= 2; difference -= step >> 1; }
        if (difference >= step >> 2) { nibble |= 1; }
        int delta = step >> 3;
        if ((nibble & 4) != 0) delta += step;
        if ((nibble & 2) != 0) delta += step >> 1;
        if ((nibble & 1) != 0) delta += step >> 2;
        predictor += (nibble & 8) != 0 ? -delta : delta;
        predictor = juce::jlimit(-32768, 32767, predictor);
        index = juce::jlimit(0, 88, index + indexTable[nibble & 7]);
        return nibble;
    }
};

// Encodes 64 int16 samples into one 34-byte Apple IMA4 packet: a preamble packing the step index
// into its low seven bits, then 32 bytes of two nibbles each, low nibble first (the QuickTime
// layout, the opposite of the high-nibble-first IMA variants).
std::vector<juce::uint8> ima4Packet(const std::vector<int>& samples)
{
    REQUIRE(samples.size() == 64);
    ImaEncoder encoder;
    encoder.predictor = samples[0] & ~0x7F;
    encoder.index = 0;
    CafBytes packet;
    packet.u16be(encoder.predictor | encoder.index);
    std::vector<int> nibbles;
    for (int i = 0; i < 64; ++i) nibbles.push_back(encoder.nibbleFor(samples[(size_t) i]));
    for (size_t i = 0; i < nibbles.size(); i += 2)
        packet.u8((nibbles[i] & 0x0f) | ((nibbles[i + 1] & 0x0f) << 4));
    REQUIRE(packet.v.size() == 34);
    return packet.v;
}
} // namespace

TEST_CASE("CAF linear PCM decodes big-endian integers", "[caf][decode]")
{
    CafBytes b;
    header(b, 44100.0, "lpcm", 0u, 4u, 1u, 2u, 16u);
    // stereo 16-bit big-endian: (+0.5,-0.5) and (-1.0, 32767/32768)
    data(b, { 0x40, 0x00, 0xC0, 0x00, 0x80, 0x00, 0x7F, 0xFF });
    juce::String error;
    const auto audio = decode(b, error);
    CHECK(error.isEmpty());
    CHECK(audio.sampleRate == 44100);
    CHECK(audio.channels == 2);
    CHECK(audio.bitsPerSample == 16);
    CHECK(audio.frames == 2);
    CHECK(audio.left[0] == Approx(0.5f));
    CHECK(audio.right[0] == Approx(-0.5f));
    CHECK(audio.left[1] == Approx(-1.0f));
    CHECK(audio.right[1] == Approx(32767.0f / 32768.0f));
}

TEST_CASE("CAF linear PCM decodes little-endian 24-bit and floats", "[caf][decode]")
{
    juce::String error;
    {
        CafBytes b;
        header(b, 48000.0, "lpcm", 0x2u, 3u, 1u, 1u, 24u); // little-endian flag
        data(b, { 0x00, 0x00, 0x40, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00 });
        const auto audio = decode(b, error);
        CHECK(audio.sampleRate == 48000);
        CHECK(audio.channels == 1);
        CHECK(audio.bitsPerSample == 24);
        REQUIRE(audio.frames == 3);
        CHECK(audio.left[0] == Approx(0.5f));
        CHECK(audio.left[1] == Approx(-1.0f));
        CHECK(audio.left[2] == Approx(1.0f / 8388608.0f));
    }
    {
        // float32 little-endian: 0.25 and -0.5
        CafBytes b;
        header(b, 96000.0, "lpcm", 0x3u, 4u, 1u, 1u, 32u);
        data(b, { 0x00, 0x00, 0x80, 0x3E, 0x00, 0x00, 0x00, 0xBF });
        const auto audio = decode(b, error);
        CHECK(audio.sampleRate == 96000);
        CHECK(audio.bitsPerSample == 32);
        REQUIRE(audio.frames == 2);
        CHECK(audio.left[0] == Approx(0.25f));
        CHECK(audio.left[1] == Approx(-0.5f));
    }
}

TEST_CASE("CAF companded mu-law and A-law decode to full scale", "[caf][decode]")
{
    juce::String error;
    {
        CafBytes b;
        header(b, 8000.0, "ulaw", 0u, 1u, 1u, 1u, 8u);
        data(b, { 0xFF, 0x80, 0x00 });
        const auto audio = decode(b, error);
        CHECK(audio.bitsPerSample == 8);
        REQUIRE(audio.frames == 3);
        CHECK(audio.left[0] == Approx(0.0f).margin(1.0e-6));
        CHECK(audio.left[1] == Approx(32124.0f / 32768.0f).margin(1.0e-4));
        CHECK(audio.left[2] == Approx(-32124.0f / 32768.0f).margin(1.0e-4));
    }
    {
        CafBytes b;
        header(b, 8000.0, "alaw", 0u, 1u, 1u, 1u, 8u);
        // 0xD5/0x55 are the two zero codes, 0xAA/0x2A the peaks.
        data(b, { 0xD5, 0x55, 0xAA, 0x2A });
        const auto audio = decode(b, error);
        REQUIRE(audio.frames == 4);
        CHECK(audio.left[0] == Approx(8.0f / 32768.0f).margin(1.0e-5));
        CHECK(audio.left[1] == Approx(-8.0f / 32768.0f).margin(1.0e-5));
        CHECK(audio.left[2] == Approx(32256.0f / 32768.0f).margin(1.0e-4));
        CHECK(audio.left[3] == Approx(-32256.0f / 32768.0f).margin(1.0e-4));
    }
}

TEST_CASE("CAF Apple IMA4 packets decode and the packet table trims the tail", "[caf][decode][adpcm]")
{
    // A slow ramp stays well inside IMA's tracking, so a swapped nibble order would show up as a
    // large error instead of a plausible one.
    std::vector<int> samples(64);
    for (int i = 0; i < 64; ++i) samples[(size_t) i] = -4000 + i * 120;
    const auto packet = ima4Packet(samples);

    CafBytes b;
    header(b, 22050.0, "ima4", 0u, 34u, 64u, 1u, 0u);
    data(b, packet);
    juce::String error;
    const auto audio = decode(b, error);
    CHECK(audio.bitsPerSample == 16);
    REQUIRE(audio.frames == 64);
    double worst = 0.0;
    for (int i = 0; i < 64; ++i)
        worst = juce::jmax(worst, std::abs((double) audio.left[(size_t) i] - (double) samples[(size_t) i] / 32768.0));
    INFO("worst IMA4 error " << worst);
    CHECK(worst < 0.02);

    // Two packets, but the table says only 100 frames are valid.
    CafBytes trimmed;
    header(trimmed, 22050.0, "ima4", 0u, 34u, 64u, 1u, 0u);
    packetTable(trimmed, 2, 100);
    std::vector<juce::uint8> twoPackets = packet;
    twoPackets.insert(twoPackets.end(), packet.begin(), packet.end());
    data(trimmed, twoPackets);
    const auto cut = decode(trimmed, error);
    CHECK(cut.frames == 100);
}

TEST_CASE("Malformed or unsupported CAF files are rejected with a reason", "[caf][decode][malformed]")
{
    juce::String error;
    CafAudio audio;
    {
        CafBytes b;
        b.raw("RIFF", 4);
        CHECK_FALSE(decodeCaf(bytesOf(b), audio, error));
        CHECK(error.containsIgnoreCase("Core Audio Format"));
    }
    {
        CafBytes b;
        header(b, 44100.0, "lpcm", 0u, 2u, 1u, 1u, 16u);
        b.raw("data", 4); b.u64be(4096); b.u32be(0); // claims far more than the file holds
        CHECK_FALSE(decodeCaf(bytesOf(b), audio, error));
        CHECK(error.containsIgnoreCase("truncated"));
    }
    {
        CafBytes b;
        header(b, 44100.0, "alac", 0u, 0u, 4096u, 2u, 0u);
        data(b, { 0, 0, 0, 0 });
        CHECK_FALSE(decodeCaf(bytesOf(b), audio, error));
        CHECK(error.containsIgnoreCase("ALAC"));
    }
    {
        CafBytes b;
        header(b, 44100.0, "aac ", 0u, 0u, 1024u, 2u, 0u);
        data(b, { 0, 0, 0, 0 });
        CHECK_FALSE(decodeCaf(bytesOf(b), audio, error));
        CHECK(error.containsIgnoreCase("AAC"));
    }
    {
        CafBytes b;
        header(b, 44100.0, "lpcm", 0u, 6u, 1u, 3u, 16u);
        data(b, { 0, 0, 0, 0, 0, 0 });
        CHECK_FALSE(decodeCaf(bytesOf(b), audio, error));
        CHECK(error.containsIgnoreCase("channels"));
    }
    {
        // 24-bit samples stored in 32-bit words: refused rather than misread.
        CafBytes b;
        header(b, 44100.0, "lpcm", 0x2u, 8u, 1u, 2u, 24u);
        data(b, { 0, 0, 0, 0, 0, 0, 0, 0 });
        CHECK_FALSE(decodeCaf(bytesOf(b), audio, error));
        CHECK(error.containsIgnoreCase("Unpacked"));
    }
    {
        CafBytes b;
        header(b, 44100.0, "lpcm", 0u, 2u, 1u, 1u, 16u);
        CHECK_FALSE(decodeCaf(bytesOf(b), audio, error));
        CHECK(error.containsIgnoreCase("no audio data"));
    }
}

TEST_CASE("CAF registers with the shared format manager, scanner and preview", "[caf][format]")
{
    TestDirectory t;
    CafBytes b;
    header(b, 22050.0, "lpcm", 0x2u, 2u, 1u, 1u, 16u); // little-endian mono
    std::vector<juce::uint8> audio;
    for (int i = 0; i < 2205; ++i) // 0.1 s of a low tone
    {
        const auto value = (int) std::lround(0.5 * 32767.0
            * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * i / 22050.0));
        audio.push_back((juce::uint8) (value & 0xff));
        audio.push_back((juce::uint8) ((value >> 8) & 0xff));
    }
    data(b, audio);
    const auto file = writeCaf(t, "tone.caf", b);

    REQUIRE(FileScanner::isSupportedFormat(file));
    CHECK_FALSE(FileScanner::isMetadataOnlyFormat(file));
    CHECK(validateContainer(file).isEmpty());

    juce::AudioFormatManager formats;
    registerSonicatorFormats(formats);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    REQUIRE(reader != nullptr);
    CHECK(reader->sampleRate == Approx(22050.0));
    CHECK(reader->numChannels == 1);
    CHECK(reader->lengthInSamples == 2205);
    juce::AudioBuffer<float> buffer(1, 512);
    REQUIRE(reader->read(&buffer, 0, 512, 0, true, false));
    CHECK(buffer.getMagnitude(0, 512) > 0.1f);

    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"));
    REQUIRE(preview.loadFile(file, false));
    CHECK(preview.waveformLength() == Approx(0.1).margin(1.0e-3));
    CHECK(engine.length() > 0.0);
}

TEST_CASE("The CAF reader rejects a foreign stream before reading it as audio", "[caf][format]")
{
    // The manager offers every registered format the same stream, and this one is registered
    // first so macOS CoreAudio cannot claim `.caf`; the four-byte check keeps that order free.
    juce::MemoryInputStream stream("RIFF\x24\x00\x00\x00WAVE", 12, false);
    CafAudioFormat format;
    CHECK(format.createReaderFor(&stream, false) == nullptr);
    CHECK(format.getFileExtensions().contains(".caf"));
}

TEST_CASE("A physically truncated CAF is reported as invalid, not as a decode failure", "[caf][format]")
{
    TestDirectory t;
    CafBytes b;
    header(b, 44100.0, "lpcm", 0u, 2u, 1u, 1u, 16u);
    data(b, { 0x00, 0x40, 0x00, 0xC0 });
    auto file = writeCaf(t, "cut.caf", b);
    REQUIRE(validateContainer(file).isEmpty());
    // Drop the last two payload bytes: the header still claims them.
    file.replaceWithData(b.v.data(), b.v.size() - 2);
    const auto verdict = validateContainer(file);
    INFO("verdict: " << verdict);
    CHECK(verdict.containsIgnoreCase("CAF"));
}
