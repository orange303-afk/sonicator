#include "CloudCache.h"
#include <algorithm>

namespace sonicator::cloud
{
namespace
{
constexpr char magic[4] = { 'S', 'C', 'L', 'D' };
constexpr int32_t formatVersion = 5; // 3: point spread and cloud fill changed; older layout files are stale
}

juce::File defaultCloudCacheFolder()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Sonicator/cache/cloud");
}

juce::File cloudCacheFile(uint64_t fingerprint, const juce::File& folder)
{
    return folder.getChildFile("layout-" + juce::String::toHexString((juce::int64) fingerprint).paddedLeft('0', 16) + ".bin");
}

bool writeCloudLayout(const CloudLayout& layout, const juce::File& file)
{
    if (! layout.valid()) return false;
    auto folder = file.getParentDirectory();
    if (! folder.createDirectory()) return false;
    juce::MemoryOutputStream out;
    out.write(magic, 4);
    out.writeInt(formatVersion);
    out.writeInt64((juce::int64) layout.fingerprint);
    out.writeInt(layout.algorithmVersion);
    out.writeFloat(layout.bounds.getX());
    out.writeFloat(layout.bounds.getY());
    out.writeFloat(layout.bounds.getWidth());
    out.writeFloat(layout.bounds.getHeight());
    out.writeInt((int) layout.islands.size());
    for (const auto& island : layout.islands)
    {
        out.writeInt64(island.tagId);
        out.writeInt((int) island.name.getNumBytesAsUTF8());
        out.write(island.name.toRawUTF8(), island.name.getNumBytesAsUTF8());
        out.writeInt((int) island.colour);
        out.writeFloat(island.x);
        out.writeFloat(island.y);
        out.writeFloat(island.radius);
        out.writeInt(island.count);
    }
    out.writeInt((int) layout.points.size());
    for (const auto& point : layout.points)
    {
        out.writeInt64(point.sampleId);
        out.writeFloat(point.x);
        out.writeFloat(point.y);
        out.writeFloat(point.radiusPx);
        out.writeInt((int) point.colour);
        out.writeInt(point.islandIndex);
        out.writeByte(point.flags);
    }
    auto temporary = file.getSiblingFile(file.getFileName() + ".tmp");
    temporary.deleteFile();
    if (! temporary.replaceWithData(out.getData(), out.getDataSize())) return false;
    return temporary.moveFileTo(file);
}

namespace
{
// A file on disk only carries its own fingerprint; the caller may also demand a specific one. The
// startup path asks for none, because the previous map is worth showing before the catalog is read.
std::optional<CloudLayout> parseCloudLayout(const juce::File& file, std::optional<uint64_t> expected)
{
    if (! file.existsAsFile()) return std::nullopt;
    juce::MemoryBlock block;
    if (! file.loadFileAsData(block) || block.getSize() < 24) return std::nullopt;
    juce::MemoryInputStream in(block, false);
    char readMagic[4] = {};
    if (in.read(readMagic, 4) != 4 || std::memcmp(readMagic, magic, 4) != 0) return std::nullopt;
    if (in.readInt() != formatVersion) return std::nullopt;
    const auto storedFingerprint = (uint64_t) in.readInt64();
    if (expected.has_value() && storedFingerprint != *expected) return std::nullopt;
    CloudLayout layout;
    layout.fingerprint = storedFingerprint;
    layout.algorithmVersion = in.readInt();
    const auto x = in.readFloat(), y = in.readFloat(), w = in.readFloat(), h = in.readFloat();
    if (! std::isfinite(x) || ! std::isfinite(y) || ! std::isfinite(w) || ! std::isfinite(h) || w <= 0 || h <= 0)
        return std::nullopt;
    layout.bounds = juce::Rectangle<float>(x, y, w, h);
    const auto islandCount = in.readInt();
    if (islandCount < 0 || islandCount > 10000) return std::nullopt;
    layout.islands.reserve((size_t) islandCount);
    for (int i = 0; i < islandCount; ++i)
    {
        CloudIsland island;
        island.tagId = in.readInt64();
        const auto nameBytes = in.readInt();
        if (nameBytes < 0 || nameBytes > 4096 || (size_t) nameBytes > block.getSize()) return std::nullopt;
        std::vector<char> name((size_t) nameBytes);
        if (nameBytes > 0 && in.read(name.data(), nameBytes) != nameBytes) return std::nullopt;
        island.name = juce::String::fromUTF8(name.data(), nameBytes);
        island.colour = (uint32_t) in.readInt();
        island.x = in.readFloat();
        island.y = in.readFloat();
        island.radius = in.readFloat();
        island.count = in.readInt();
        layout.islands.push_back(std::move(island));
    }
    const auto pointCount = in.readInt();
    if (pointCount < 0 || pointCount > 2000000) return std::nullopt;
    layout.points.reserve((size_t) pointCount);
    for (int i = 0; i < pointCount; ++i)
    {
        CloudPoint point;
        point.sampleId = in.readInt64();
        point.x = in.readFloat();
        point.y = in.readFloat();
        point.radiusPx = in.readFloat();
        point.colour = (uint32_t) in.readInt();
        point.islandIndex = in.readInt();
        point.flags = (uint8_t) in.readByte();
        if (! std::isfinite(point.x) || ! std::isfinite(point.y)) return std::nullopt;
        layout.points.push_back(point);
    }
    if (in.getNumBytesRemaining() != 0) return std::nullopt;
    return layout;
}
} // namespace

std::optional<CloudLayout> readCloudLayout(const juce::File& file, uint64_t expectedFingerprint)
{
    return parseCloudLayout(file, expectedFingerprint);
}

juce::File cloudLayoutCatalogFile(const juce::File& layoutFile)
{
    return layoutFile.getSiblingFile(layoutFile.getFileName() + ".catalog");
}

bool writeCloudLayoutCatalog(const juce::File& layoutFile, const juce::File& catalogFile)
{
    if (! layoutFile.existsAsFile()) return false;
    return cloudLayoutCatalogFile(layoutFile).replaceWithText(catalogFile.getFullPathName());
}

std::optional<CloudLayout> readNewestCloudLayout(const juce::File& folder, const juce::File& catalogFile)
{
    if (! folder.isDirectory() || catalogFile.getFullPathName().isEmpty()) return std::nullopt;
    auto files = folder.findChildFiles(juce::File::findFiles, false, "layout-*.bin");
    std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b)
    {
        return a.getLastModificationTime() > b.getLastModificationTime();
    });
    const auto wanted = catalogFile.getFullPathName();
    for (const auto& file : files)
    {
        // No tag, or a tag for another library: showing it would put files from one catalog on the
        // map while another is open. Skip it and let the build start from nothing, as before.
        const auto tag = cloudLayoutCatalogFile(file);
        if (! tag.existsAsFile()) continue;
        if (tag.loadFileAsString().trim() != wanted) continue;
        if (auto layout = parseCloudLayout(file, std::nullopt)) return layout;
    }
    return std::nullopt;
}

void pruneCloudCache(const juce::File& folder, int keep)
{
    if (! folder.isDirectory()) return;
    auto files = folder.findChildFiles(juce::File::findFiles, false, "layout-*.bin");
    std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b)
    {
        return a.getLastModificationTime() > b.getLastModificationTime();
    });
    for (int i = std::max(0, keep); i < files.size(); ++i)
    {
        cloudLayoutCatalogFile(files[i]).deleteFile();
        files[i].deleteFile();
    }
    // A tag left behind by a layout that was already gone would never be read again.
    auto tags = folder.findChildFiles(juce::File::findFiles, false, "layout-*.bin.catalog");
    for (const auto& tag : tags)
        if (! tag.getSiblingFile(tag.getFileName().dropLastCharacters(8)).existsAsFile()) tag.deleteFile();
}
}
