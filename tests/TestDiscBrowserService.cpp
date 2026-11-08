#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/disc/DiscBrowserService.h"
#include "TestHelpers.h"
#include <cstring>
#include <vector>
using namespace sonicator;

namespace
{
void putLe16(std::vector<unsigned char>& b, size_t at, uint16_t v)
{
    b[at] = (unsigned char) (v & 0xff); b[at + 1] = (unsigned char) (v >> 8);
}
void putLe32(std::vector<unsigned char>& b, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; ++i) b[at + i] = (unsigned char) ((v >> (8 * i)) & 0xff);
}
void putBe32(std::vector<unsigned char>& b, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; ++i) b[at + i] = (unsigned char) ((v >> (8 * (3 - i))) & 0xff);
}
size_t appendRecord(std::vector<unsigned char>& image, size_t at, const std::vector<unsigned char>& name,
                    uint32_t extent, uint32_t bytes, uint8_t flags)
{
    const size_t length = 33 + name.size();
    const size_t padded = length + (length & 1);
    image[at] = (unsigned char) padded;
    putLe32(image, at + 2, extent); putBe32(image, at + 6, extent);
    putLe32(image, at + 10, bytes); putBe32(image, at + 14, bytes);
    image[at + 25] = flags;
    image[at + 32] = (unsigned char) name.size();
    std::memcpy(image.data() + at + 33, name.data(), name.size());
    return at + padded;
}
std::vector<unsigned char> isoWithWavBytes(const juce::File& wav, const juce::String& entryName)
{
    std::vector<unsigned char> wavBytes((size_t) wav.getSize());
    {
        auto in = wav.createInputStream();
        REQUIRE(in != nullptr);
        REQUIRE(in->read(wavBytes.data(), (int) wavBytes.size()) == (int) wavBytes.size());
    }
    const size_t blocksForWav = (wavBytes.size() + 2047) / 2048;
    std::vector<unsigned char> image((20 + blocksForWav) * 2048, 0);
    auto* pvd = image.data() + 16 * 2048;
    pvd[0] = 1; std::memcpy(pvd + 1, "CD001", 5); pvd[6] = 1;
    putLe32(image, 16 * 2048 + 80, (uint32_t) (image.size() / 2048));
    putBe32(image, 16 * 2048 + 84, (uint32_t) (image.size() / 2048));
    putLe16(image, 16 * 2048 + 128, 2048);
    image[16 * 2048 + 130] = 0x08;
    auto at = 16 * 2048 + 156;
    image[at] = 34;
    putLe32(image, at + 2, 19); putBe32(image, at + 6, 19);
    putLe32(image, at + 10, 2048); putBe32(image, at + 14, 2048);
    image[at + 25] = 0x02; image[at + 32] = 1;
    auto* term = image.data() + 17 * 2048;
    term[0] = 255; std::memcpy(term + 1, "CD001", 5); term[6] = 1;
    size_t dir = 19 * 2048;
    dir = appendRecord(image, dir, { 0x00 }, 19, 2048, 0x02);
    dir = appendRecord(image, dir, { 0x01 }, 19, 2048, 0x02);
    appendRecord(image, dir, std::vector<unsigned char>(entryName.toRawUTF8(), entryName.toRawUTF8() + entryName.getNumBytesAsUTF8()),
                 20, (uint32_t) wavBytes.size(), 0);
    std::memcpy(image.data() + 20 * 2048, wavBytes.data(), wavBytes.size());
    return image;
}
juce::File buildIsoWithWav(TestDirectory& t, int frames)
{
    auto wav = t.wave("tone.wav", 440.0, 1, frames);
    auto image = isoWithWavBytes(wav, "TONE.WAV");
    auto file = t.dir.getChildFile("service.iso");
    file.deleteFile();
    if (auto out = file.createOutputStream()) out->write(image.data(), (int) image.size());
    return file;
}
std::vector<unsigned char> rawMode1(const std::vector<unsigned char>& cooked)
{
    const size_t blocks = cooked.size() / 2048;
    std::vector<unsigned char> raw(blocks * 2352, 0);
    for (size_t b = 0; b < blocks; ++b)
    {
        auto* sector = raw.data() + b * 2352;
        sector[0] = 0x00;
        std::memset(sector + 1, 0xff, 10);
        sector[11] = 0x00;
        sector[15] = 0x01;
        std::memcpy(sector + 16, cooked.data() + b * 2048, 2048);
    }
    return raw;
}
juce::File writeConstantWav(TestDirectory& t, const juce::String& name, float value, int frames)
{
    auto file = t.dir.getChildFile(name);
    file.deleteFile();
    auto out = file.createOutputStream();
    const int bytes = frames * 2;
    out->write("RIFF", 4); out->writeInt(bytes + 36); out->write("WAVEfmt ", 8);
    out->writeInt(16); out->writeShort(1); out->writeShort(1);
    out->writeInt(44100); out->writeInt(44100 * 2); out->writeShort(2); out->writeShort(16);
    out->write("data", 4); out->writeInt(bytes);
    for (int i = 0; i < frames; ++i) out->writeShort((short) std::lround(value * 32767));
    return file;
}
juce::File isoWithPair(TestDirectory& t, const juce::File& left, const juce::File& right)
{
    auto read = [](const juce::File& file)
    {
        std::vector<unsigned char> bytes((size_t) file.getSize());
        auto in = file.createInputStream();
        REQUIRE(in != nullptr);
        REQUIRE(in->read(bytes.data(), (int) bytes.size()) == (int) bytes.size());
        return bytes;
    };
    const auto a = read(left), b = read(right);
    const size_t blocksA = (a.size() + 2047) / 2048;
    const size_t blocksB = (b.size() + 2047) / 2048;
    std::vector<unsigned char> image((20 + blocksA + blocksB) * 2048, 0);
    auto* pvd = image.data() + 16 * 2048;
    pvd[0] = 1; std::memcpy(pvd + 1, "CD001", 5); pvd[6] = 1;
    putLe32(image, 16 * 2048 + 80, (uint32_t) (image.size() / 2048));
    putBe32(image, 16 * 2048 + 84, (uint32_t) (image.size() / 2048));
    putLe16(image, 16 * 2048 + 128, 2048);
    image[16 * 2048 + 130] = 0x08;
    auto at = 16 * 2048 + 156;
    image[at] = 34;
    putLe32(image, at + 2, 19); putBe32(image, at + 6, 19);
    putLe32(image, at + 10, 2048); putBe32(image, at + 14, 2048);
    image[at + 25] = 0x02; image[at + 32] = 1;
    auto* term = image.data() + 17 * 2048;
    term[0] = 255; std::memcpy(term + 1, "CD001", 5); term[6] = 1;
    size_t dir = 19 * 2048;
    dir = appendRecord(image, dir, { 0x00 }, 19, 2048, 0x02);
    dir = appendRecord(image, dir, { 0x01 }, 19, 2048, 0x02);
    const auto name = [](const char* text) { return std::vector<unsigned char>(text, text + std::strlen(text)); };
    dir = appendRecord(image, dir, name("PAIR-L.WAV;1"), 20, (uint32_t) a.size(), 0);
    appendRecord(image, dir, name("PAIR-R.WAV;1"), (uint32_t) (20 + blocksA), (uint32_t) b.size(), 0);
    std::memcpy(image.data() + 20 * 2048, a.data(), a.size());
    std::memcpy(image.data() + (20 + blocksA) * 2048, b.data(), b.size());
    auto file = t.dir.getChildFile("pair.iso");
    file.deleteFile();
    if (auto out = file.createOutputStream()) out->write(image.data(), (int) image.size());
    return file;
}
DiscBrowserService::Completion waitFor(DiscBrowserService& service, DiscBrowserService::Kind kind,
                                       int timeoutMs = 10000)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    for (;;)
    {
        DiscBrowserService::Completion completion;
        while (service.poll(completion))
            if (completion.kind == kind) return completion;
        if (juce::Time::getMillisecondCounter() > deadline) return {};
        juce::Thread::sleep(5);
    }
}
} // namespace

TEST_CASE("The disc service opens, lists and decodes off the message thread", "[disc-service]")
{
    TestDirectory t;
    auto image = buildIsoWithWav(t, 512);
    DiscBrowserService service;
    service.requestOpen(image);
    const auto opened = waitFor(service, DiscBrowserService::Kind::open);
    INFO(opened.error);
    REQUIRE(opened.status == DiscStatus::ready);
    REQUIRE(opened.root.value != 0);

    service.requestList(opened.root);
    const auto listed = waitFor(service, DiscBrowserService::Kind::list);
    REQUIRE(listed.status == DiscStatus::ready);
    REQUIRE(listed.listing.entries.size() == 1);
    REQUIRE(listed.listing.entries[0].displayName == "TONE.WAV");
    REQUIRE(listed.listing.entries[0].kind == DiscEntryKind::file);

    service.requestDecode(listed.listing.entries[0].id, listed.listing.entries[0].displayName);
    const auto decoded = waitFor(service, DiscBrowserService::Kind::decode);
    INFO(decoded.error);
    REQUIRE(decoded.status == DiscStatus::ready);
    REQUIRE(decoded.audio != nullptr);
    REQUIRE(decoded.audio->frames == 512);
    REQUIRE(decoded.audio->sampleRate == 44100);
    REQUIRE(decoded.audio->channels == 1);
    REQUIRE(decoded.audio->bitsPerSample == 16);

    service.close();
    service.requestList(opened.root);
    const auto closed = waitFor(service, DiscBrowserService::Kind::list);
    REQUIRE(closed.status != DiscStatus::ready);
}

namespace
{
juce::String framesToTime(uint64_t frames)
{
    const auto minutes = frames / (75 * 60);
    const auto seconds = (frames / 75) % 60;
    const auto ff = frames % 75;
    return juce::String(minutes).paddedLeft('0', 2) + ":" + juce::String(seconds).paddedLeft('0', 2)
        + ":" + juce::String(ff).paddedLeft('0', 2);
}
}

TEST_CASE("An ISO L/R pair decodes as one stereo source", "[disc-service][stereo-pair]")
{
    TestDirectory t;
    auto left = writeConstantWav(t, "PAIR-L.wav", 0.25f, 100);
    auto right = writeConstantWav(t, "PAIR-R.wav", -0.5f, 100);
    auto image = isoWithPair(t, left, right);
    DiscBrowserService service;
    service.requestOpen(image);
    const auto opened = waitFor(service, DiscBrowserService::Kind::open);
    INFO(opened.error);
    REQUIRE(opened.status == DiscStatus::ready);
    service.requestList(opened.root);
    const auto listed = waitFor(service, DiscBrowserService::Kind::list);
    REQUIRE(listed.status == DiscStatus::ready);
    REQUIRE(listed.listing.entries.size() == 1);
    const auto& entry = listed.listing.entries[0];
    REQUIRE(entry.stereo);
    REQUIRE(entry.displayName == "PAIR");
    REQUIRE(entry.partner.value != 0);
    service.requestDecode(entry.id, entry.displayName, entry.partner);
    const auto decoded = waitFor(service, DiscBrowserService::Kind::decode);
    INFO(decoded.error);
    REQUIRE(decoded.status == DiscStatus::ready);
    REQUIRE(decoded.audio != nullptr);
    REQUIRE(decoded.audio->channels == 2);
    REQUIRE(decoded.audio->frames == 100);
    REQUIRE(decoded.audio->left[0] == Catch::Approx(0.25f).margin(0.001f));
    REQUIRE(decoded.audio->right[0] == Catch::Approx(-0.5f).margin(0.001f));
}

TEST_CASE("A BIN with a sibling CUE opens its data track after the audio track", "[disc-cue][disc-service]")
{
    TestDirectory t;
    auto wav = t.wave("cue-tone.wav", 440.0, 1, 512);
    const auto iso = isoWithWavBytes(wav, "TONE.WAV");
    const auto data = rawMode1(iso);
    constexpr size_t audioSectors = 75;
    std::vector<unsigned char> image(audioSectors * 2352, 0);
    image.insert(image.end(), data.begin(), data.end());
    auto bin = t.dir.getChildFile("cue-disc.bin");
    bin.deleteFile();
    {
        auto out = bin.createOutputStream();
        REQUIRE(out != nullptr);
        out->write(image.data(), (int) image.size());
    }
    t.dir.getChildFile("cue-disc.cue").replaceWithText(
        "FILE \"cue-disc.bin\" BINARY\n"
        "  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n"
        "  TRACK 02 MODE1/2352\n    INDEX 01 00:01:00\n");

    DiscBrowserService service;
    service.requestOpen(bin); // sibling CUE is found automatically
    const auto opened = waitFor(service, DiscBrowserService::Kind::open);
    INFO(opened.error);
    REQUIRE(opened.status == DiscStatus::ready);
    service.requestList(opened.root);
    const auto listed = waitFor(service, DiscBrowserService::Kind::list);
    REQUIRE(listed.status == DiscStatus::ready);
    REQUIRE(listed.listing.entries.size() == 1);
    REQUIRE(listed.listing.entries[0].displayName == "TONE.WAV");
    service.requestDecode(listed.listing.entries[0].id, listed.listing.entries[0].displayName);
    const auto decoded = waitFor(service, DiscBrowserService::Kind::decode);
    INFO(decoded.error);
    REQUIRE(decoded.status == DiscStatus::ready);
    REQUIRE(decoded.audio != nullptr);
    REQUIRE(decoded.audio->frames == 512);
}

TEST_CASE("Multiple CUE data tracks are listed and one is selected explicitly", "[disc-cue][disc-service]")
{
    TestDirectory t;
    auto wavA = t.wave("a.wav", 220.0, 1, 256);
    auto wavB = t.wave("b.wav", 330.0, 1, 256);
    const auto isoA = isoWithWavBytes(wavA, "A.WAV");
    const auto isoB = isoWithWavBytes(wavB, "B.WAV");
    const auto rawA = rawMode1(isoA);
    const auto rawB = rawMode1(isoB);
    constexpr size_t audioSectors = 75;
    const auto secondStart = (uint64_t) audioSectors + isoA.size() / 2048;
    std::vector<unsigned char> image(audioSectors * 2352, 0);
    image.insert(image.end(), rawA.begin(), rawA.end());
    image.insert(image.end(), rawB.begin(), rawB.end());
    auto bin = t.dir.getChildFile("multi.bin");
    bin.deleteFile();
    {
        auto out = bin.createOutputStream();
        REQUIRE(out != nullptr);
        out->write(image.data(), (int) image.size());
    }
    auto cue = t.dir.getChildFile("multi.cue");
    cue.replaceWithText("FILE \"multi.bin\" BINARY\n"
        "  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n"
        "  TRACK 02 MODE1/2352\n    INDEX 01 " + framesToTime(audioSectors) + "\n"
        "  TRACK 03 MODE1/2352\n    INDEX 01 " + framesToTime(secondStart) + "\n");

    DiscBrowserService service;
    service.requestOpen(cue);
    const auto ambiguous = waitFor(service, DiscBrowserService::Kind::open);
    REQUIRE(ambiguous.status == DiscStatus::ambiguous);
    REQUIRE(ambiguous.cueTracks.size() == 2);
    REQUIRE(ambiguous.cueTracks[0].number == 2);
    REQUIRE(ambiguous.cueTracks[1].number == 3);

    service.requestOpen(cue, 3);
    const auto opened = waitFor(service, DiscBrowserService::Kind::open);
    INFO(opened.error);
    REQUIRE(opened.status == DiscStatus::ready);
    service.requestList(opened.root);
    const auto listed = waitFor(service, DiscBrowserService::Kind::list);
    REQUIRE(listed.status == DiscStatus::ready);
    REQUIRE(listed.listing.entries.size() == 1);
    REQUIRE(listed.listing.entries[0].displayName == "B.WAV");
}
