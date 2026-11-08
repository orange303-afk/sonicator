#include <catch2/catch_test_macros.hpp>
#include "core/disc/DiscImageProbe.h"
#include "core/disc/RawDiscSource.h"
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
std::vector<unsigned char> isoImage(bool joliet)
{
    std::vector<unsigned char> bytes(24 * 2048, 0);
    auto* pvd = bytes.data() + 16 * 2048;
    pvd[0] = 1; std::memcpy(pvd + 1, "CD001", 5); pvd[6] = 1;
    putLe32(bytes, 16 * 2048 + 80, 24);
    putBe32(bytes, 16 * 2048 + 84, 24);
    putLe16(bytes, 16 * 2048 + 128, 2048);
    bytes[16 * 2048 + 130] = 0x08; bytes[16 * 2048 + 131] = 0x00;
    if (joliet)
    {
        auto* svd = bytes.data() + 17 * 2048;
        svd[0] = 2; std::memcpy(svd + 1, "CD001", 5); svd[6] = 1;
        std::memcpy(svd + 88, "%/E", 3);
        auto* term = bytes.data() + 18 * 2048;
        term[0] = 255; std::memcpy(term + 1, "CD001", 5); term[6] = 1;
    }
    else
    {
        auto* term = bytes.data() + 17 * 2048;
        term[0] = 255; std::memcpy(term + 1, "CD001", 5); term[6] = 1;
    }
    return bytes;
}
} // namespace

TEST_CASE("An ISO-9660 image is recognised by its descriptors, not its file name", "[disc-probe]")
{
    TestDirectory t;
    auto file = writeBytes(t, "anything.img", isoImage(true));
    const auto raw = openRawDisc(file, {});
    REQUIRE(raw.ok());
    const auto probe = probeDisc(*raw.value, {}, {});
    REQUIRE(probe.ok());
    REQUIRE(probe.value.kind == DiscImageKind::iso9660);
    REQUIRE(probe.value.joliet);
    REQUIRE(probe.value.logicalBlockSize == 2048);
    REQUIRE(probe.value.volumeSpaceBlocks == 24);
}

TEST_CASE("A truncated descriptor area is malformed, not guessed", "[disc-probe]")
{
    TestDirectory t;
    auto bytes = isoImage(false);
    bytes.resize(17 * 2048); // one primary descriptor, then EOF before its terminator
    auto file = writeBytes(t, "truncated.iso", bytes);
    const auto raw = openRawDisc(file, {});
    REQUIRE(raw.ok());
    const auto probe = probeDisc(*raw.value, {}, {});
    REQUIRE_FALSE(probe.ok());
    REQUIRE(probe.status == DiscStatus::malformed);
    REQUIRE(probe.error.isNotEmpty());
}

TEST_CASE("UDF and MDX are reported as unsupported, never as ISO", "[disc-probe]")
{
    TestDirectory t;
    std::vector<unsigned char> udf(20 * 2048, 0);
    std::memcpy(udf.data() + 16 * 2048 + 1, "BEA01", 5); // structure type 0, then the identifier
    auto udfFile = writeBytes(t, "udf.bin", udf);
    const auto udfRaw = openRawDisc(udfFile, {});
    REQUIRE(udfRaw.ok());
    const auto udfProbe = probeDisc(*udfRaw.value, {}, {});
    REQUIRE_FALSE(udfProbe.ok());
    REQUIRE(udfProbe.status == DiscStatus::unsupported);
    REQUIRE(udfProbe.value.kind == DiscImageKind::unknown);

    std::vector<unsigned char> mdx(64, 0);
    std::memcpy(mdx.data(), "MEDIA DESCRIPTOR", 16);
    auto mdxFile = writeBytes(t, "library.mdx", mdx);
    const auto mdxRaw = openRawDisc(mdxFile, {});
    REQUIRE(mdxRaw.ok());
    const auto mdxProbe = probeDisc(*mdxRaw.value, {}, {});
    REQUIRE_FALSE(mdxProbe.ok());
    REQUIRE(mdxProbe.status == DiscStatus::unsupported);
    REQUIRE(mdxProbe.value.kind == DiscImageKind::mdx);
}

TEST_CASE("A misaligned CD001 string is not an ISO descriptor", "[disc-probe]")
{
    TestDirectory t;
    std::vector<unsigned char> bytes(20 * 2048, 0);
    std::memcpy(bytes.data() + 16 * 2048 + 7, "CD001", 5);
    auto file = writeBytes(t, "notiso.bin", bytes);
    const auto raw = openRawDisc(file, {});
    REQUIRE(raw.ok());
    const auto probe = probeDisc(*raw.value, {}, {});
    REQUIRE_FALSE(probe.ok());
    REQUIRE(probe.status != DiscStatus::unsupported); // unknown, not a false positive
}

TEST_CASE("An Akai raw image is recognised from volume and FAT structure", "[disc-probe]")
{
    TestDirectory t;
    std::vector<unsigned char> bytes(8192 * 8, 0);
    // Volume entry 1: twelve Akai-encoded spaces (0x0a), type 1, start block 6.
    std::memset(bytes.data() + 0xca, 0x0a, 12);
    putLe16(bytes, 0xca + 12, 1);
    putLe16(bytes, 0xca + 14, 6);
    // First three FAT entries are the reserved markers 0x0040.
    for (int i = 0; i < 3; ++i) putLe16(bytes, 0x70a + i * 2, 0x40);
    auto file = writeBytes(t, "akai.iso", bytes);
    const auto raw = openRawDisc(file, {});
    REQUIRE(raw.ok());
    const auto probe = probeDisc(*raw.value, {}, {});
    REQUIRE(probe.ok());
    REQUIRE(probe.value.kind == DiscImageKind::akaiRaw);
    REQUIRE(probe.value.akaiVolumeType == 1);
}
