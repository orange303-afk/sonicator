#include <catch2/catch_test_macros.hpp>
#include "core/disc/DiscFilesystem.h"
#include "core/disc/RawDiscSource.h"
#include "core/disc/SectorLayout.h"
#include "TestHelpers.h"
#include <cstring>
#include <vector>
using namespace sonicator;

namespace
{
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
std::vector<unsigned char> ascii(const char* text) { return std::vector<unsigned char>(text, text + std::strlen(text)); }
void setPvd(std::vector<unsigned char>& image, unsigned block, unsigned char type, unsigned rootExtent,
            unsigned rootBytes, bool jolietEscape = false)
{
    auto* pvd = image.data() + (size_t) block * 2048;
    pvd[0] = type;
    std::memcpy(pvd + 1, "CD001", 5);
    pvd[6] = 1;
    putLe32(image, (size_t) block * 2048 + 80, 32); putBe32(image, (size_t) block * 2048 + 84, 32);
    image[(size_t) block * 2048 + 128] = 0x00; image[(size_t) block * 2048 + 129] = 0x08;
    image[(size_t) block * 2048 + 130] = 0x08; image[(size_t) block * 2048 + 131] = 0x00;
    if (jolietEscape) std::memcpy(pvd + 88, "%/E", 3);
    auto at = (size_t) block * 2048 + 156;
    image[at] = 34;
    putLe32(image, at + 2, rootExtent); putBe32(image, at + 6, rootExtent);
    putLe32(image, at + 10, rootBytes); putBe32(image, at + 14, rootBytes);
    image[at + 25] = 0x02;
    image[at + 32] = 1;
}
std::vector<unsigned char> isoWithTree()
{
    std::vector<unsigned char> image(32 * 2048, 0);
    setPvd(image, 16, 1, 20, 2048);
    auto* term = image.data() + 17 * 2048;
    term[0] = 255; std::memcpy(term + 1, "CD001", 5); term[6] = 1;
    size_t at = 20 * 2048;
    at = appendRecord(image, at, { 0x00 }, 20, 2048, 0x02);
    at = appendRecord(image, at, { 0x01 }, 20, 2048, 0x02);
    at = appendRecord(image, at, ascii("KICK.WAV;1"), 21, 2048, 0);
    at = appendRecord(image, at, ascii("BASS.WAV;2"), 22, 1024, 0);
    at = appendRecord(image, at, ascii("KICK.WAV;2"), 24, 512, 0);
    at = appendRecord(image, at, ascii("MULTI.BIN;1"), 25, 4096, 0x80);
    at = appendRecord(image, at, ascii("SAMPLES"), 23, 2048, 0x02);
    at = appendRecord(image, at, ascii("PAIR-L.WAV;1"), 28, 100, 0);
    at = appendRecord(image, at, ascii("PAIR-R.WAV;1"), 29, 100, 0);
    std::memset(image.data() + 21 * 2048, 'A', 2048);
    std::memset(image.data() + 22 * 2048, 'B', 1024);
    std::memset(image.data() + 24 * 2048, 'K', 512);
    size_t sub = 23 * 2048;
    sub = appendRecord(image, sub, { 0x00 }, 23, 2048, 0x02);
    sub = appendRecord(image, sub, { 0x01 }, 20, 2048, 0x02);
    sub = appendRecord(image, sub, ascii("SNARE.WAV;1"), 26, 100, 0);
    std::memset(image.data() + 26 * 2048, 'C', 100);
    return image;
}
DiscResult<std::unique_ptr<Iso9660Filesystem>> openIso(const juce::File& file)
{
    const auto raw = openRawDisc(file, {});
    if (! raw.ok()) return { raw.status, {}, raw.error };
    TrackLayout layout;
    layout.sectorCount = (uint64_t) file.getSize() / 2048;
    const auto track = makeTrackSource(raw.value, layout, {});
    if (! track.ok()) return { track.status, {}, track.error };
    return Iso9660Filesystem::open(track.value, {}, {});
}
} // namespace

TEST_CASE("An ISO directory lists entries by record identity and opens their bytes", "[disc-iso]")
{
    TestDirectory t;
    auto file = writeBytes(t, "tree.iso", isoWithTree());
    auto opened = openIso(file);
    INFO(opened.error);
    REQUIRE(opened.ok());
    auto& fs = *opened.value;
    const auto root = fs.list(fs.root(), {});
    REQUIRE(root.ok());
    REQUIRE(root.value.entries.size() == 6);
    REQUIRE(root.value.entries[0].displayName == "KICK.WAV");
    REQUIRE(root.value.entries[0].rawName == "KICK.WAV;1");
    REQUIRE(root.value.entries[0].kind == DiscEntryKind::file);
    REQUIRE(root.value.entries[1].displayName == "BASS.WAV");
    REQUIRE(root.value.entries[2].displayName == "KICK.WAV"); // same label, different identity
    REQUIRE(root.value.entries[0].id != root.value.entries[2].id);
    REQUIRE(root.value.entries[3].displayName == "MULTI.BIN");
    REQUIRE_FALSE(root.value.entries[3].capability.isEmpty());
    REQUIRE(root.value.entries[3].kind == DiscEntryKind::unknown);
    REQUIRE(root.value.entries[4].displayName == "SAMPLES");
    REQUIRE(root.value.entries[4].kind == DiscEntryKind::directory);
    REQUIRE(root.value.entries[5].displayName == "PAIR");
    REQUIRE(root.value.entries[5].stereo);
    REQUIRE(root.value.entries[5].partner.value != 0);

    const auto kick = fs.openFile(root.value.entries[0].id, {});
    REQUIRE(kick.ok());
    REQUIRE(kick.value->size() == 2048);
    std::vector<std::byte> bytes(2048);
    REQUIRE(kick.value->readAt(0, bytes, {}).ok());
    REQUIRE(std::to_integer<int>(bytes[0]) == 'A');

    const auto bass = fs.openFile(root.value.entries[1].id, {});
    REQUIRE(bass.ok());
    REQUIRE(bass.value->size() == 1024);
    REQUIRE(bass.value->readAt(1023, std::span<std::byte>(bytes).first(1), {}).ok());
    REQUIRE(std::to_integer<int>(bytes[0]) == 'B');

    const auto sub = fs.list(root.value.entries[4].id, {});
    REQUIRE(sub.ok());
    REQUIRE(sub.value.entries.size() == 1);
    REQUIRE(sub.value.entries[0].displayName == "SNARE.WAV");
    const auto snare = fs.openFile(sub.value.entries[0].id, {});
    REQUIRE(snare.ok());
    REQUIRE(snare.value->size() == 100);
    REQUIRE(snare.value->readAt(99, std::span<std::byte>(bytes).first(1), {}).ok());
    REQUIRE(std::to_integer<int>(bytes[0]) == 'C');

    REQUIRE_FALSE(fs.openFile({ 12345 }, {}).ok());
    REQUIRE_FALSE(fs.list({ 12345 }, {}).ok());
}

TEST_CASE("Joliet names are preferred and decoded as UCS-2", "[disc-iso]")
{
    TestDirectory t;
    auto image = isoWithTree();
    setPvd(image, 16, 1, 20, 2048);
    auto* svd = image.data() + 17 * 2048;
    svd[0] = 2; std::memcpy(svd + 1, "CD001", 5); svd[6] = 1;
    std::memcpy(svd + 88, "%/E", 3);
    image[17 * 2048 + 156] = 34;
    putLe32(image, 17 * 2048 + 158, 27); putBe32(image, 17 * 2048 + 162, 27);
    putLe32(image, 17 * 2048 + 166, 2048); putBe32(image, 17 * 2048 + 170, 2048);
    image[17 * 2048 + 181] = 0x02; image[17 * 2048 + 188] = 1;
    auto* term = image.data() + 18 * 2048;
    term[0] = 255; std::memcpy(term + 1, "CD001", 5); term[6] = 1;
    size_t at = 27 * 2048;
    at = appendRecord(image, at, { 0x00 }, 27, 2048, 0x02);
    at = appendRecord(image, at, { 0x01 }, 27, 2048, 0x02);
    // UTF-16BE "Тест"
    const std::vector<unsigned char> name { 0x04, 0x22, 0x04, 0x35, 0x04, 0x41, 0x04, 0x42 };
    at = appendRecord(image, at, name, 28, 2048, 0x02);
    auto file = writeBytes(t, "joliet.iso", image);
    auto opened = openIso(file);
    INFO(opened.error);
    REQUIRE(opened.ok());
    const auto root = opened.value->list(opened.value->root(), {});
    REQUIRE(root.ok());
    REQUIRE(root.value.entries.size() == 1);
    REQUIRE(root.value.entries[0].displayName == juce::String::fromUTF8("Тест"));
}

TEST_CASE("A directory record with an extent outside the volume is visible but not openable", "[disc-iso]")
{
    TestDirectory t;
    auto image = isoWithTree();
    // Rewrite the first file record extent to a block far beyond the image.
    const size_t record = 20 * 2048 + 68; // after "." and ".."
    putLe32(image, record + 2, 9000); putBe32(image, record + 6, 9000);
    auto file = writeBytes(t, "outside.iso", image);
    auto opened = openIso(file);
    INFO(opened.error);
    REQUIRE(opened.ok());
    const auto root = opened.value->list(opened.value->root(), {});
    REQUIRE(root.ok());
    REQUIRE_FALSE(root.value.entries[0].capability.isEmpty());
    REQUIRE(root.value.entries[0].kind == DiscEntryKind::unknown);
    REQUIRE_FALSE(opened.value->openFile(root.value.entries[0].id, {}).ok());
}
