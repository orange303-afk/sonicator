#include <catch2/catch_test_macros.hpp>
#include "core/disc/AkaiFilesystem.h"
#include "core/disc/RawDiscSource.h"
#include "TestHelpers.h"
#include <catch2/catch_approx.hpp>
#include <cstring>
#include <vector>
using namespace sonicator;

namespace
{
constexpr size_t block = 8192;
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
void putLe24(std::vector<unsigned char>& b, size_t at, uint32_t v)
{
    for (int i = 0; i < 3; ++i) b[at + i] = (unsigned char) ((v >> (8 * i)) & 0xff);
}
void putLe32(std::vector<unsigned char>& b, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; ++i) b[at + i] = (unsigned char) ((v >> (8 * i)) & 0xff);
}
unsigned char akaiChar(char c)
{
    if (c >= '0' && c <= '9') return (unsigned char) (c - '0');
    if (c >= 'A' && c <= 'Z') return (unsigned char) (0x0b + c - 'A');
    if (c == ' ') return 0x0a;
    if (c == '.') return 0x28;
    if (c == '-') return 0x27;
    return 0x0a;
}
void putAkaiName(std::vector<unsigned char>& b, size_t at, const char* name)
{
    std::memset(b.data() + at, 0x0a, 12);
    for (size_t i = 0; name[i] != 0 && i < 12; ++i) b[at + i] = akaiChar(name[i]);
}
void putVolumeAt(std::vector<unsigned char>& b, size_t tableBase, int slot, const char* name,
                 uint16_t type, uint16_t start)
{
    const size_t at = tableBase + 0xca + (size_t) slot * 16;
    putAkaiName(b, at, name);
    putLe16(b, at + 12, type);
    putLe16(b, at + 14, start);
}
void putFileRecord(std::vector<unsigned char>& b, size_t directoryAt, int index, const char* name,
                   uint8_t type, uint32_t bytes, uint16_t start)
{
    const size_t at = directoryAt + (size_t) index * 24;
    putAkaiName(b, at, name);
    b[at + 0x10] = type;
    putLe24(b, at + 0x11, bytes);
    putLe16(b, at + 0x14, start);
}
void putFatAt(std::vector<unsigned char>& b, size_t tableBase, uint16_t blockIndex, uint16_t value)
{
    putLe16(b, tableBase + 0x70a + (size_t) blockIndex * 2, value);
}
void putFatMarkers(std::vector<unsigned char>& b, size_t tableBase)
{
    for (int i = 0; i < 3; ++i) putLe16(b, tableBase + 0x70a + i * 2, 0x40);
}
std::vector<unsigned char> akaiImage()
{
    std::vector<unsigned char> image(20 * block, 0);
    putFatMarkers(image, 0);
    putFatMarkers(image, 8 * block);
    image[0x4500] = 2; image[0x4501] = 1;
    putLe16(image, 0x4502, 8); putLe16(image, 0x4504, 12);
    putVolumeAt(image, 0, 0, "VOL ONE", 1, 4);       // partition 0 table at block 0
    putVolumeAt(image, 8 * block, 0, "VOL TWO", 3, 1); // partition 1 table at block 8
    putFileRecord(image, 4 * block, 0, "KICK", 0x73, 9000, 6);
    putFileRecord(image, 4 * block, 1, "PROG", 0x70, 0, 0);
    putFileRecord(image, 9 * block, 0, "SNARE", 0xf3, 100, 4);
    putFileRecord(image, 9 * block, 1, "PAIR .L", 0x73, 158, 5);
    putFileRecord(image, 9 * block, 2, "PAIR .R", 0x73, 158, 6);
    putFatAt(image, 0, 6, 7);
    putFatAt(image, 0, 7, 0x00c0);
    putFatAt(image, 8 * block, 4, 0x00c0);
    putFatAt(image, 8 * block, 5, 0x00c0);
    putFatAt(image, 8 * block, 6, 0x00c0);
    std::memset(image.data() + 6 * block, 0, block);
    std::memset(image.data() + 7 * block, 0, block);
    image[6 * block] = 3; // sample header id
    putLe32(image, 6 * block + 0x1a, 4425); // (9000 - 150) / 2
    putLe16(image, 6 * block + 0x8a, 44100);
    putLe16(image, 6 * block + 150, 0x7fff);
    putLe16(image, 6 * block + 152, 0x8000);
    std::memset(image.data() + 12 * block, 'Z', 100);
    for (int i = 0; i < 2; ++i)
    {
        const uint64_t at = (uint64_t) (13 + i) * block;
        image[at] = 3;
        putLe32(image, at + 0x1a, 4);
        putLe16(image, at + 0x8a, 44100);
        const short values[4] = { (short) (i == 0 ? 1000 : -1000), (short) (i == 0 ? 2000 : -2000),
                                  (short) (i == 0 ? 3000 : -3000), (short) (i == 0 ? 4000 : -4000) };
        for (int f = 0; f < 4; ++f)
            putLe16(image, at + 150 + (size_t) f * 2, (uint16_t) values[f]);
    }
    return image;
}
DiscResult<std::unique_ptr<AkaiFilesystem>> openAkai(const juce::File& file)
{
    const auto raw = openRawDisc(file, {});
    if (! raw.ok()) return { raw.status, {}, raw.error };
    return AkaiFilesystem::open(raw.value, {}, {});
}
} // namespace

TEST_CASE("An Akai image lists partitions, volumes and sample records", "[disc-akai-fs]")
{
    TestDirectory t;
    auto file = writeBytes(t, "akai.iso", akaiImage());
    auto opened = openAkai(file);
    INFO(opened.error);
    REQUIRE(opened.ok());
    auto& fs = *opened.value;

    const auto root = fs.list(fs.root(), {});
    REQUIRE(root.ok());
    REQUIRE(root.value.entries.size() == 2);
    REQUIRE(root.value.entries[0].displayName == "Partition 1");
    REQUIRE(root.value.entries[1].displayName == "Partition 2");

    const auto first = fs.list(root.value.entries[0].id, {});
    REQUIRE(first.ok());
    REQUIRE(first.value.entries.size() == 1);
    REQUIRE(first.value.entries[0].rawName == "VOL ONE");
    const auto second = fs.list(root.value.entries[1].id, {});
    REQUIRE(second.ok());
    REQUIRE(second.value.entries.size() == 1);
    REQUIRE(second.value.entries[0].rawName == "VOL TWO");

    const auto volume = fs.list(first.value.entries[0].id, {});
    REQUIRE(volume.ok());
    REQUIRE(volume.value.entries.size() == 2); // padding is not an entry
    REQUIRE(volume.value.entries[0].displayName == "KICK");
    REQUIRE(volume.value.entries[0].kind == DiscEntryKind::file);
    REQUIRE(volume.value.entries[0].capability.isEmpty());
    REQUIRE(volume.value.entries[0].declaredBytes == 9000);
    REQUIRE(volume.value.entries[1].displayName == "PROG");
    REQUIRE(volume.value.entries[1].kind == DiscEntryKind::container);
    REQUIRE_FALSE(volume.value.entries[1].capability.isEmpty());

    const auto sample = fs.openFile(volume.value.entries[0].id, {});
    REQUIRE(sample.ok());
    REQUIRE(sample.value->size() == 9000);
    const auto decoded = fs.decodeSample(volume.value.entries[0].id, {});
    INFO(decoded.error);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value->frames == 4425);
    REQUIRE(decoded.value->sampleRate == 44100);
    REQUIRE(decoded.value->channels == 1);
    REQUIRE(decoded.value->left[0] == Catch::Approx(32767.0f / 32768.0f));
    REQUIRE(decoded.value->left[1] == Catch::Approx(-1.0f));

    const auto s3000 = fs.list(second.value.entries[0].id, {});
    REQUIRE(s3000.ok());
    REQUIRE(s3000.value.entries.size() == 2); // SNARE + one merged PAIR
    const auto snare = fs.openFile(s3000.value.entries[0].id, {});
    REQUIRE(snare.ok());
    REQUIRE(snare.value->size() == 100);
    const auto& pair = s3000.value.entries[1];
    REQUIRE(pair.stereo);
    REQUIRE(pair.displayName == "PAIR");
    REQUIRE(pair.partner.value != 0);
    const auto stereo = fs.decodeSample(pair.id, {});
    INFO(stereo.error);
    REQUIRE(stereo.ok());
    REQUIRE(stereo.value->channels == 2);
    REQUIRE(stereo.value->frames == 4);
    REQUIRE(stereo.value->left[0] == Catch::Approx(1000.0f / 32768.0f));
    REQUIRE(stereo.value->right[0] == Catch::Approx(-1000.0f / 32768.0f));
}

TEST_CASE("Akai FAT cycles and early ends are rejected locally", "[disc-akai-fs]")
{
    TestDirectory t;
    auto cyclic = akaiImage();
    putLe24(cyclic, 4 * block + 0x11, 20000); // three blocks are required, so the cycle is inside the file
    putFatAt(cyclic, 0, 6, 7); putFatAt(cyclic, 0, 7, 6);
    auto cyclicFile = writeBytes(t, "cycle.iso", cyclic);
    auto cycleFs = openAkai(cyclicFile);
    REQUIRE(cycleFs.ok());
    const auto partitions = cycleFs.value->list(cycleFs.value->root(), {});
    REQUIRE(partitions.ok());
    REQUIRE(partitions.value.entries.size() == 2);
    const auto volumes = cycleFs.value->list(partitions.value.entries[0].id, {});
    REQUIRE(volumes.ok());
    REQUIRE(volumes.value.entries.size() == 1);
    const auto entries = cycleFs.value->list(volumes.value.entries[0].id, {});
    REQUIRE(entries.ok());
    const auto cyclicSample = cycleFs.value->openFile(entries.value.entries[0].id, {});
    REQUIRE_FALSE(cyclicSample.ok());
    REQUIRE(cyclicSample.status == DiscStatus::malformed);

    auto shortChain = akaiImage();
    putFatAt(shortChain, 0, 6, 0x00c0);
    auto shortFile = writeBytes(t, "short.iso", shortChain);
    auto shortFs = openAkai(shortFile);
    REQUIRE(shortFs.ok());
    const auto shortParts = shortFs.value->list(shortFs.value->root(), {});
    REQUIRE(shortParts.ok());
    REQUIRE(shortParts.value.entries.size() == 2);
    const auto shortVolumes = shortFs.value->list(shortParts.value.entries[0].id, {});
    REQUIRE(shortVolumes.ok());
    REQUIRE(shortVolumes.value.entries.size() == 1);
    const auto shortEntries = shortFs.value->list(shortVolumes.value.entries[0].id, {});
    REQUIRE_FALSE(shortFs.value->openFile(shortEntries.value.entries[0].id, {}).ok());
}

TEST_CASE("An unvalidated Akai partition table is reported, not invented", "[disc-akai-fs]")
{
    TestDirectory t;
    auto image = akaiImage();
    image[0x4500] = 0; image[0x4501] = 0;
    auto file = writeBytes(t, "noparts.iso", image);
    auto opened = openAkai(file);
    REQUIRE_FALSE(opened.ok());
    REQUIRE(opened.status == DiscStatus::unsupported);
}
