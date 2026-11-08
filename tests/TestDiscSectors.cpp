#include <catch2/catch_test_macros.hpp>
#include "core/disc/SectorLayout.h"
#include "core/disc/RawDiscSource.h"
#include "TestHelpers.h"
#include <array>
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
void putMode1Header(std::vector<unsigned char>& bytes, size_t sector)
{
    auto* p = bytes.data() + sector * 2352;
    p[0] = 0x00;
    std::memset(p + 1, 0xff, 10);
    p[11] = 0x00;
    p[15] = 0x01;
}
void putXaHeader(std::vector<unsigned char>& bytes, size_t sector, unsigned char submode)
{
    auto* p = bytes.data() + sector * 2352;
    p[0] = 0x00;
    std::memset(p + 1, 0xff, 10);
    p[11] = 0x00;
    p[15] = 0x02;
    for (int copy = 0; copy < 2; ++copy)
    {
        p[16 + copy * 4] = 0; p[17 + copy * 4] = 0;
        p[18 + copy * 4] = submode; p[19 + copy * 4] = 0;
    }
}
} // namespace

TEST_CASE("A cooked 2048 track maps logical blocks one to one", "[disc-sector]")
{
    TestDirectory t;
    std::vector<unsigned char> bytes(2048 * 4);
    for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = (unsigned char) (i % 251);
    auto file = writeBytes(t, "cooked.iso", bytes);
    const auto raw = openRawDisc(file, {});
    REQUIRE(raw.ok());
    TrackLayout layout;
    layout.sectorCount = 4;
    layout.profile = SectorProfile::cooked2048;
    const auto track = makeTrackSource(raw.value, layout, {});
    REQUIRE(track.ok());
    REQUIRE(track.value->logicalBlockSize() == 2048);
    REQUIRE(track.value->logicalBlockCount() == 4);
    std::array<std::byte, 2048> block {};
    REQUIRE(track.value->readBlocks(2, 1, block, {}).ok());
    REQUIRE(std::memcmp(block.data(), bytes.data() + 2 * 2048, 2048) == 0);
    REQUIRE_FALSE(track.value->readBlocks(4, 1, block, {}).ok());
}

TEST_CASE("A raw Mode1 track extracts user data and validates each header", "[disc-sector]")
{
    TestDirectory t;
    std::vector<unsigned char> bytes(2352 * 3, 0);
    for (size_t sector = 0; sector < 3; ++sector)
    {
        putMode1Header(bytes, sector);
        for (size_t i = 0; i < 2048; ++i) bytes[sector * 2352 + 16 + i] = (unsigned char) (sector + 1);
    }
    auto file = writeBytes(t, "mode1.bin", bytes);
    const auto raw = openRawDisc(file, {});
    REQUIRE(raw.ok());
    TrackLayout layout; layout.sectorCount = 3; layout.profile = SectorProfile::mode1_2352;
    const auto track = makeTrackSource(raw.value, layout, {});
    REQUIRE(track.ok());
    std::array<std::byte, 2048> block {};
    REQUIRE(track.value->readBlocks(1, 1, block, {}).ok());
    REQUIRE((int) block[0] == 2);
    REQUIRE((int) block[2047] == 2);

    // A corrupted sync pattern makes only that sector unreadable, not silently wrong.
    auto damaged = bytes; damaged[2352 + 3] = 0x00;
    auto damagedFile = writeBytes(t, "mode1-bad.bin", damaged);
    const auto damagedRaw = openRawDisc(damagedFile, {});
    REQUIRE(damagedRaw.ok());
    const auto damagedTrack = makeTrackSource(damagedRaw.value, layout, {});
    REQUIRE(damagedTrack.ok());
    REQUIRE_FALSE(damagedTrack.value->readBlocks(1, 1, block, {}).ok());
    REQUIRE(damagedTrack.value->readBlocks(0, 1, block, {}).ok());
}

TEST_CASE("An XA Form1 track reads data at offset 24 and refuses Form2", "[disc-sector]")
{
    TestDirectory t;
    std::vector<unsigned char> bytes(2352 * 2, 0);
    putXaHeader(bytes, 0, 0x08); // data, form 1
    putXaHeader(bytes, 1, 0x28); // data, form 2
    for (size_t i = 0; i < 2048; ++i) bytes[24 + i] = 0x7e;
    auto file = writeBytes(t, "xa.bin", bytes);
    const auto raw = openRawDisc(file, {});
    REQUIRE(raw.ok());
    TrackLayout layout; layout.sectorCount = 2; layout.profile = SectorProfile::xaForm1_2352;
    const auto track = makeTrackSource(raw.value, layout, {});
    REQUIRE(track.ok());
    std::array<std::byte, 2048> block {};
    REQUIRE(track.value->readBlocks(0, 1, block, {}).ok());
    REQUIRE((int) block[0] == 0x7e);
    const auto form2 = track.value->readBlocks(1, 1, block, {});
    REQUIRE_FALSE(form2.ok());
    REQUIRE(form2.status == DiscStatus::unsupported);
}

TEST_CASE("A track layout cannot escape its source and rejects bad requests", "[disc-sector]")
{
    TestDirectory t;
    std::vector<unsigned char> bytes(2048 * 2, 0x11);
    auto file = writeBytes(t, "short.bin", bytes);
    const auto raw = openRawDisc(file, {});
    REQUIRE(raw.ok());
    TrackLayout layout; layout.sectorCount = 3; layout.profile = SectorProfile::cooked2048;
    REQUIRE_FALSE(makeTrackSource(raw.value, layout, {}).ok());
    layout.sectorCount = 2;
    const auto track = makeTrackSource(raw.value, layout, {});
    REQUIRE(track.ok());
    std::array<std::byte, 4> tooSmall {};
    REQUIRE_FALSE(track.value->readBlocks(0, 1, tooSmall, {}).ok());
    REQUIRE(track.value->readBlocks(2, 0, tooSmall, {}).ok());
    REQUIRE(track.value->readBlocks(0, 1, std::span<std::byte>(tooSmall), [] { return true; }).status
            == DiscStatus::cancelled);
}
