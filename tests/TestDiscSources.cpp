#include <catch2/catch_test_macros.hpp>
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
std::vector<unsigned char> readInto(const ReadOnlyByteSource& source, uint64_t offset, size_t count)
{
    std::vector<std::byte> buffer(count);
    const auto result = source.readAt(offset, buffer, {});
    REQUIRE(result.ok());
    REQUIRE(result.value == count);
    std::vector<unsigned char> bytes(count);
    std::memcpy(bytes.data(), buffer.data(), count);
    return bytes;
}
DiscLimits looseLimits()
{
    DiscLimits limits;
    limits.maxReadBytes = 1u << 20;
    return limits;
}
} // namespace

TEST_CASE("A raw source reads exact first and last bytes", "[disc-source]")
{
    TestDirectory t;
    std::vector<unsigned char> bytes(4096);
    for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = (unsigned char) (i * 31 + 7);
    auto file = writeBytes(t, "raw.bin", bytes);
    const auto source = openRawDisc(file, looseLimits());
    REQUIRE(source.ok());
    REQUIRE(source.value->size() == bytes.size());
    auto first = readInto(*source.value, 0, 16);
    REQUIRE(std::memcmp(first.data(), bytes.data(), 16) == 0);
    auto last = readInto(*source.value, bytes.size() - 16, 16);
    REQUIRE(std::memcmp(last.data(), bytes.data() + bytes.size() - 16, 16) == 0);
    auto tail = readInto(*source.value, bytes.size() - 1, 1);
    REQUIRE(tail[0] == bytes.back());
}

TEST_CASE("A raw source rejects out-of-range, overflowing and short reads", "[disc-source]")
{
    TestDirectory t;
    std::vector<unsigned char> bytes(64, 0x5a);
    auto file = writeBytes(t, "small.bin", bytes);
    const auto source = openRawDisc(file, looseLimits());
    REQUIRE(source.ok());
    std::array<std::byte, 16> buffer {};
    REQUIRE_FALSE(source.value->readAt(65, buffer, {}).ok());
    REQUIRE_FALSE(source.value->readAt(60, buffer, {}).ok());          // short read
    REQUIRE_FALSE(source.value->readAt(UINT64_MAX - 4, buffer, {}).ok()); // offset + size overflow
    REQUIRE(source.value->readAt(64, std::span<std::byte>(), {}).ok());   // empty read at EOF
    REQUIRE(source.value->readAt(0, buffer, [] { return true; }).status == DiscStatus::cancelled);
}

TEST_CASE("A missing or unreadable raw source fails without side effects", "[disc-source]")
{
    TestDirectory t;
    const auto missing = openRawDisc(t.dir.getChildFile("absent.iso"), looseLimits());
    REQUIRE_FALSE(missing.ok());
    REQUIRE(missing.status == DiscStatus::ioError);
    REQUIRE(missing.error.isNotEmpty());
}

TEST_CASE("A subrange source maps reads and cannot escape its parent", "[disc-source]")
{
    TestDirectory t;
    std::vector<unsigned char> bytes(128);
    for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = (unsigned char) i;
    std::vector<unsigned char> copy = bytes;
    auto file = writeBytes(t, "range.bin", bytes);
    const auto source = openRawDisc(file, looseLimits());
    REQUIRE(source.ok());
    const auto slice = subrange(source.value, 32, 16);
    REQUIRE(slice.ok());
    REQUIRE(slice.value->size() == 16);
    auto mapped = readInto(*slice.value, 4, 8);
    REQUIRE(std::memcmp(mapped.data(), copy.data() + 36, 8) == 0);
    std::array<std::byte, 8> buffer {};
    REQUIRE_FALSE(slice.value->readAt(9, buffer, {}).ok());
    REQUIRE_FALSE(subrange(source.value, 120, 16).ok());
    REQUIRE_FALSE(subrange(source.value, UINT64_MAX, 2).ok());
    REQUIRE(subrange(source.value, 128, 0).ok());
}

TEST_CASE("A raw source reads a byte beyond 4 GiB without loading the file", "[disc-source]")
{
    TestDirectory t;
    auto file = t.dir.getChildFile("sparse.img");
    file.deleteFile();
    {
        auto out = file.createOutputStream();
        REQUIRE(out != nullptr);
        REQUIRE(out->setPosition(4'500'000'000LL));
        out->write("Z", 1);
        out->flush();
    }
    const auto source = openRawDisc(file, looseLimits());
    REQUIRE(source.ok());
    REQUIRE(source.value->size() == 4'500'000'001LL);
    const auto last = readInto(*source.value, 4'500'000'000LL, 1);
    REQUIRE(last[0] == 'Z');
}

TEST_CASE("Opening a raw source never changes the original", "[disc-source]")
{
    TestDirectory t;
    std::vector<unsigned char> bytes(1024, 0x11);
    auto file = writeBytes(t, "keep.bin", bytes);
    const auto size = file.getSize();
    const auto modified = file.getLastModificationTime();
    const auto hash = FileScanner::computeFileHash(file);
    REQUIRE(openRawDisc(file, looseLimits()).ok());
    REQUIRE(file.getSize() == size);
    REQUIRE(file.getLastModificationTime() == modified);
    REQUIRE(FileScanner::computeFileHash(file) == hash);
}
