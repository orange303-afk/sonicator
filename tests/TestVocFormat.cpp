#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/audio/AudioEngine.h"
#include "core/audio/SonicatorFormats.h"
#include "core/scanner/FileScanner.h"
#include "core/voc/VocAudioFormat.h"
#include "ui/preview/PreviewPanel.h"
#include "TestHelpers.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
using namespace sonicator;

namespace {
struct VocBytes
{
    std::vector<juce::uint8> v;
    void u8(int x) { v.push_back((juce::uint8) (x & 0xff)); }
    void u16le(int x) { u8(x); u8(x >> 8); }
    void u32le(juce::uint32 x) { for (int i = 0; i < 4; ++i) u8((int) ((x >> (8 * i)) & 0xff)); }
    void bytes(std::initializer_list<int> data) { for (int x : data) u8(x); }
};
void header(VocBytes& b)
{
    const char* magic = "Creative Voice File";
    b.v.insert(b.v.end(), magic, magic + 19);
    b.u8(0x1A);
    b.u16le(26);
    b.u16le(0x010A);
    b.u16le(0x1129);
}
void block(VocBytes& b, int type, const std::vector<juce::uint8>& data)
{
    b.u8(type);
    b.u8((int) data.size());
    b.u8((int) (data.size() >> 8));
    b.u8((int) (data.size() >> 16));
    b.v.insert(b.v.end(), data.begin(), data.end());
}
void sound1(VocBytes& b, int rateByte, int codec, std::initializer_list<int> pcm)
{
    std::vector<juce::uint8> data { (juce::uint8) rateByte, (juce::uint8) codec };
    for (int x : pcm) data.push_back((juce::uint8) x);
    block(b, 1, data);
}
void sound9(VocBytes& b, int rate, int bits, int channels, int codec,
            std::initializer_list<int> pcm)
{
    std::vector<juce::uint8> data;
    const auto add = [&data] (int x, int count = 1)
    { for (int i = 0; i < count; ++i) data.push_back((juce::uint8) ((x >> (8 * i)) & 0xff)); };
    add(rate, 4); add(bits); add(channels); add(codec, 2); add(0, 4);
    for (int x : pcm) data.push_back((juce::uint8) x);
    block(b, 9, data);
}
void terminate(VocBytes& b) { b.u8(0); }
std::vector<std::byte> bytesOf(const VocBytes& b)
{
    std::vector<std::byte> out(b.v.size());
    for (size_t i = 0; i < b.v.size(); ++i) out[i] = (std::byte) b.v[i];
    return out;
}
juce::File writeVoc(TestDirectory& t, const juce::String& name, const VocBytes& b)
{
    auto file = t.dir.getChildFile(name);
    file.deleteFile();
    file.replaceWithData(b.v.data(), b.v.size());
    return file;
}
VocAudio decode(const VocBytes& b, juce::String& error)
{
    VocAudio audio;
    const auto bytes = bytesOf(b);
    REQUIRE(decodeVoc(bytes, audio, error));
    return audio;
}
} // namespace

TEST_CASE("VOC block 1 decodes unsigned 8-bit PCM", "[voc][decode]")
{
    VocBytes b; header(b);
    sound1(b, 233, 0, { 128, 255, 0, 64 }); // 1e6 / (256 - 233) = 43478 Hz
    terminate(b);
    juce::String error;
    const auto audio = decode(b, error);
    CHECK(error.isEmpty());
    CHECK(audio.sampleRate == 43478);
    CHECK(audio.channels == 1);
    CHECK(audio.bitsPerSample == 8);
    CHECK(audio.frames == 4);
    CHECK(audio.left[0] == Catch::Approx(0.0f));
    CHECK(audio.left[1] == Catch::Approx(127.0f / 128.0f));
    CHECK(audio.left[2] == Catch::Approx(-1.0f));
    CHECK(audio.left[3] == Catch::Approx(-0.5f));
}

TEST_CASE("VOC block 9 decodes 16-bit signed PCM and 8-bit stereo", "[voc][decode]")
{
    VocBytes b; header(b);
    sound9(b, 22050, 16, 1, 4, { 0x00, 0x00, 0x00, 0x40, 0x00, 0xc0, 0xff, 0x7f });
    terminate(b);
    juce::String error;
    auto audio = decode(b, error);
    CHECK(audio.sampleRate == 22050);
    CHECK(audio.channels == 1);
    CHECK(audio.bitsPerSample == 16);
    CHECK(audio.frames == 4);
    CHECK(audio.left[0] == Catch::Approx(0.0f));
    CHECK(audio.left[1] == Catch::Approx(16384.0f / 32768.0f));
    CHECK(audio.left[2] == Catch::Approx(-16384.0f / 32768.0f));
    CHECK(audio.left[3] == Catch::Approx(32767.0f / 32768.0f));

    VocBytes stereo; header(stereo);
    sound9(stereo, 11025, 8, 2, 0, { 128, 64, 192, 0 });
    terminate(stereo);
    error.clear();
    audio = decode(stereo, error);
    CHECK(audio.channels == 2);
    CHECK(audio.frames == 2);
    CHECK(audio.left[0] == Catch::Approx(0.0f));
    CHECK(audio.right[0] == Catch::Approx(-0.5f));
    CHECK(audio.left[1] == Catch::Approx(0.5f));
    CHECK(audio.right[1] == Catch::Approx(-1.0f));
}

TEST_CASE("VOC block 8 enables stereo for the following sound block", "[voc][decode]")
{
    VocBytes b; header(b);
    std::vector<juce::uint8> extension;
    extension.push_back(0x54); extension.push_back(0xE9); // time constant, as in the library
    extension.push_back(0x00); extension.push_back(0x01); // pack, stereo mode
    block(b, 8, extension);
    sound1(b, 233, 0, { 128, 64, 192, 0 });
    terminate(b);
    juce::String error;
    const auto audio = decode(b, error);
    CHECK(audio.channels == 2);
    CHECK(audio.frames == 2);
    CHECK(audio.left[0] == Catch::Approx(0.0f));
    CHECK(audio.right[0] == Catch::Approx(-0.5f));
    CHECK(audio.left[1] == Catch::Approx(0.5f));
    CHECK(audio.right[1] == Catch::Approx(-1.0f));
}

TEST_CASE("VOC Creative ADPCM 4, 3 and 2-bit blocks decode", "[voc][decode][adpcm]")
{
    juce::String error;
    {
        VocBytes b; header(b); sound1(b, 233, 1, { 0x80, 0x10 }); terminate(b);
        const auto audio = decode(b, error);
        REQUIRE(audio.frames == 3);
        CHECK(audio.left[0] == Catch::Approx(0.0f));
        CHECK(audio.left[1] == Catch::Approx(128.0f / 32768.0f));
        CHECK(audio.left[2] == Catch::Approx(128.0f / 32768.0f));
    }
    {
        VocBytes b; header(b); sound1(b, 233, 2, { 0x80, 0x20 }); terminate(b);
        const auto audio = decode(b, error);
        REQUIRE(audio.frames == 4);
        CHECK(audio.left[0] == Catch::Approx(0.0f));
        CHECK(audio.left[1] == Catch::Approx(128.0f / 32768.0f));
        CHECK(audio.left[2] == Catch::Approx(128.0f / 32768.0f));
        CHECK(audio.left[3] == Catch::Approx(128.0f / 32768.0f));
    }
    {
        VocBytes b; header(b); sound1(b, 233, 3, { 0x80, 0x40 }); terminate(b);
        const auto audio = decode(b, error);
        REQUIRE(audio.frames == 5);
        CHECK(audio.left[0] == Catch::Approx(0.0f));
        for (int i = 1; i < 5; ++i)
            CHECK(audio.left[(size_t) i] == Catch::Approx(512.0f / 32768.0f));
    }
}

TEST_CASE("VOC silence and repeat blocks extend the decoded audio", "[voc][decode]")
{
    VocBytes b; header(b);
    std::vector<juce::uint8> silence { 9, 0, 233 }; // ten frames at 43478 Hz
    block(b, 3, silence);
    sound1(b, 233, 0, { 128, 128, 128, 128 });
    std::vector<juce::uint8> repeat { 2, 0 }; // one extra copy
    block(b, 6, repeat);
    sound1(b, 233, 0, { 255, 0 });
    block(b, 7, {});
    terminate(b);
    juce::String error;
    const auto audio = decode(b, error);
    REQUIRE(audio.frames == 18); // 10 silence + 4 + 2 + one 2-frame repeat
    for (int i = 0; i < 10; ++i) CHECK(audio.left[(size_t) i] == 0.0f);
    CHECK(audio.left[10] == Catch::Approx(0.0f));
    CHECK(audio.left[14] == Catch::Approx(127.0f / 128.0f));
    CHECK(audio.left[15] == Catch::Approx(-1.0f));
    CHECK(audio.left[16] == Catch::Approx(127.0f / 128.0f));
    CHECK(audio.left[17] == Catch::Approx(-1.0f));
}

TEST_CASE("Malformed or unsupported VOC files are rejected with a reason", "[voc][decode][malformed]")
{
    juce::String error;
    VocAudio audio;
    {
        VocBytes b; header(b); b.v[0] = 'X'; terminate(b);
        CHECK_FALSE(decodeVoc(bytesOf(b), audio, error));
        CHECK(error.containsIgnoreCase("Creative"));
    }
    {
        VocBytes b; header(b);
        b.u8(1); b.u8(100); b.u8(0); b.u8(0); // claims 100 bytes, file ends
        CHECK_FALSE(decodeVoc(bytesOf(b), audio, error));
        CHECK(error.containsIgnoreCase("truncated"));
    }
    {
        VocBytes b; header(b); sound9(b, 22050, 8, 1, 6, { 0, 1, 2 }); terminate(b);
        CHECK_FALSE(decodeVoc(bytesOf(b), audio, error));
        CHECK(error.containsIgnoreCase("law"));
    }
    {
        VocBytes b; header(b); sound9(b, 0, 8, 1, 0, { 0, 1 }); terminate(b);
        CHECK_FALSE(decodeVoc(bytesOf(b), audio, error));
        CHECK(error.containsIgnoreCase("rate"));
    }
    {
        VocBytes b; header(b); terminate(b);
        CHECK_FALSE(decodeVoc(bytesOf(b), audio, error));
        CHECK(error.containsIgnoreCase("audio"));
    }
}

TEST_CASE("VOC registers with the shared audio format manager and preview", "[voc][format][preview]")
{
    TestDirectory t;
    VocBytes b; header(b); sound1(b, 233, 0, { 128, 255, 0, 64 }); terminate(b);
    const auto file = writeVoc(t, "tone.voc", b);
    REQUIRE(FileScanner::isSupportedFormat(file));
    CHECK_FALSE(FileScanner::isMetadataOnlyFormat(file));

    juce::AudioFormatManager formats;
    registerSonicatorFormats(formats);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    REQUIRE(reader != nullptr);
    CHECK(reader->sampleRate == Catch::Approx(43478.0));
    CHECK(reader->numChannels == 1);
    CHECK(reader->lengthInSamples == 4);

    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"));
    REQUIRE(preview.loadFile(file, false));
    CHECK(preview.waveformLength() == Catch::Approx(4.0 / 43478.0).margin(1e-6));
    CHECK(engine.length() > 0.0);
}

// Opt-in corpus check. SONICATOR_VOC_DIR points at the read-only library; the summary and every
// rejection reason are printed and recorded in docs/VOC.md.
TEST_CASE("Real VOC corpus decodes read-only", "[.external][voc-corpus]")
{
    const auto directory = juce::SystemStats::getEnvironmentVariable("SONICATOR_VOC_DIR", {});
    REQUIRE(directory.isNotEmpty());
    const juce::File root(directory);
    REQUIRE(root.isDirectory());
    std::vector<juce::File> files;
    for (const auto& entry : juce::RangedDirectoryIterator(root, true, "*.voc;*.VOC",
                                                           juce::File::findFiles))
        files.push_back(entry.getFile());
    std::sort(files.begin(), files.end(), [] (const auto& a, const auto& b)
              { return a.getFullPathName() < b.getFullPathName(); });
    int decoded = 0, rejected = 0;
    int64_t frames = 0;
    juce::StringArray reasons;
    for (const auto& file : files)
    {
        juce::MemoryBlock block;
        if (! file.loadFileAsData(block)) { ++rejected; reasons.add(file.getFileName() + ": unreadable"); continue; }
        std::vector<std::byte> bytes(block.getSize());
        if (! bytes.empty()) std::memcpy(bytes.data(), block.getData(), bytes.size());
        VocAudio audio;
        juce::String error;
        if (decodeVoc(bytes, audio, error)) { ++decoded; frames += audio.frames; }
        else { ++rejected; reasons.add(file.getFileName() + ": " + error.substring(0, 140)); }
    }
    std::cout << "VOC corpus: files=" << files.size() << " decoded=" << decoded
              << " rejected=" << rejected << " frames=" << frames << "\n";
    for (int i = 0; i < juce::jmin(30, reasons.size()); ++i) std::cout << "  " << reasons[i] << "\n";
    INFO("files=" << files.size() << " decoded=" << decoded << " rejected=" << rejected
         << " frames=" << frames);
    INFO(reasons.joinIntoString("\n"));
    REQUIRE_FALSE(files.empty());
    CHECK(decoded > 0);
}
