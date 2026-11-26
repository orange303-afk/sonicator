#include "SpectrogramCache.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace sonicator
{
namespace
{
constexpr int cacheVersion = 1;
constexpr const char* magic = "SPS1";

juce::File cacheBase(const juce::File& source, const juce::File& folder)
{
    const auto hash = juce::String::toHexString(
        (juce::int64) spectrumCacheKey(source).hashCode64()).toLowerCase();
    return folder.getChildFile(hash.substring(0, 2)).getChildFile(hash + "-v" + juce::String(cacheVersion));
}

struct Entry
{
    juce::File gridFile, imageFile;
    juce::int64 bytes = 0;
    juce::Time modified;
};
} // namespace

namespace
{
juce::File& testCacheFolder()
{
    static juce::File folder;
    return folder;
}
} // namespace

void setSpectrumCacheFolderForTest(const juce::File& folder)
{
    testCacheFolder() = folder;
}

juce::File defaultSpectrumCacheFolder()
{
    if (testCacheFolder() != juce::File()) return testCacheFolder();
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Sonicator/spectra");
}

juce::String spectrumCacheKey(const juce::File& source)
{
    return source.getFullPathName() + "|" + juce::String(source.getSize()) + "|"
        + juce::String((juce::int64) source.getLastModificationTime().toMilliseconds()) + "|v"
        + juce::String(cacheVersion);
}

juce::File spectrumCacheFile(const juce::File& source, const juce::String& extension,
                             const juce::File& folder)
{
    return cacheBase(source, folder).withFileExtension(extension);
}

bool spectrumCacheIsFresh(const juce::File& source, const juce::File& folder)
{
    return spectrumCacheFile(source, ".sps", folder).existsAsFile()
        && spectrumCacheFile(source, ".png", folder).existsAsFile();
}

juce::Colour spectrumPngColour(float level)
{
    const auto t = juce::jlimit(0.0f, 1.0f, level);
    const juce::Colour low { 0xff0c0c10 }, mid { 0xffa83d16 }, high { 0xffffe3b8 };
    return t < 0.6f ? low.interpolatedWith(mid, t / 0.6f)
                    : mid.interpolatedWith(high, (t - 0.6f) / 0.4f);
}

juce::Image renderSpectrumPng(const SpectrogramGrid& grid)
{
    if (! grid.valid()) return {};
    juce::Image image(juce::Image::ARGB, grid.columns, grid.rows, true);
    juce::Image::BitmapData data(image, juce::Image::BitmapData::writeOnly);
    const auto span = (float) (0.0 - grid.floorDb);
    for (int row = 0; row < grid.rows; ++row)
    {
        auto* line = reinterpret_cast<juce::PixelARGB*>(data.getLinePointer(grid.rows - 1 - row));
        for (int column = 0; column < grid.columns; ++column)
        {
            const auto level = span > 0.0f
                ? (grid.at(row, column) - (float) grid.floorDb) / span : 0.0f;
            line[column] = spectrumPngColour(level).getPixelARGB();
        }
    }
    return image;
}

bool writeSpectrumCache(const juce::File& source, const SpectrogramGrid& grid,
                        const juce::File& folder)
{
    if (! source.existsAsFile() || ! grid.valid()) return false;
    const auto sps = spectrumCacheFile(source, ".sps", folder);
    if (! sps.getParentDirectory().createDirectory()) return false;

    juce::MemoryOutputStream out;
    out.write(magic, 4);
    out.writeInt(cacheVersion);
    out.writeString(spectrumCacheKey(source));
    out.writeInt(grid.rows);
    out.writeInt(grid.columns);
    out.writeInt(2048);
    out.writeDouble(grid.startSeconds);
    out.writeDouble(grid.endSeconds);
    out.writeDouble(grid.minFrequency);
    out.writeDouble(grid.maxFrequency);
    out.writeDouble(grid.floorDb);
    out.writeBool(grid.logFrequency);
    out.writeBool(grid.overview);
    // One byte per cell over floor..0 dB; a 0.35 dB step is finer than a screen can show.
    const auto span = (float) (0.0 - grid.floorDb);
    std::vector<uint8_t> quantized(grid.db.size());
    for (size_t i = 0; i < grid.db.size(); ++i)
    {
        const auto level = span > 0.0f ? (grid.db[i] - (float) grid.floorDb) / span : 0.0f;
        quantized[i] = (uint8_t) juce::jlimit(0, 255, (int) std::lround(level * 255.0f));
    }
    out.write(quantized.data(), (int) quantized.size());

    const auto part = juce::File(sps.getFullPathName() + ".part");
    part.deleteFile();
    {
        auto stream = part.createOutputStream();
        if (stream == nullptr) return false;
        const bool wrote = stream->write(out.getData(), out.getDataSize());
        stream->flush();
        if (! wrote)
        {
            stream.reset();
            part.deleteFile();
            return false;
        }
    }
    if (! part.moveFileTo(sps)) { part.deleteFile(); return false; }

    const auto png = spectrumCacheFile(source, ".png", folder);
    const auto image = renderSpectrumPng(grid);
    if (! image.isValid()) return true; // the grid still helps; the batch action can fill the PNG
    const auto pngPart = juce::File(png.getFullPathName() + ".part");
    pngPart.deleteFile();
    {
        auto stream = pngPart.createOutputStream();
        if (stream == nullptr) return true;
        const bool wrote = juce::PNGImageFormat().writeImageToStream(image, *stream);
        stream->flush();
        if (! wrote)
        {
            stream.reset();
            pngPart.deleteFile();
            return true;
        }
    }
    pngPart.moveFileTo(png);
    return true;
}

std::optional<SpectralOverview> readSpectrumCache(const juce::File& source, const juce::File& folder)
{
    const auto sps = spectrumCacheFile(source, ".sps", folder);
    if (! sps.existsAsFile() || sps.getSize() < 32) return std::nullopt;
    juce::MemoryBlock block;
    if (! sps.loadFileAsData(block) || block.getSize() < 32) return std::nullopt;
    juce::MemoryInputStream in(block.getData(), block.getSize(), false);
    char header[4] {};
    in.read(header, 4);
    if (std::memcmp(header, magic, 4) != 0) return std::nullopt;
    if (in.readInt() != cacheVersion) return std::nullopt;
    if (in.readString() != spectrumCacheKey(source)) return std::nullopt;

    SpectralOverview overview;
    auto& grid = overview.grid;
    grid.rows = in.readInt();
    grid.columns = in.readInt();
    const int fftSize = in.readInt();
    grid.startSeconds = in.readDouble();
    grid.endSeconds = in.readDouble();
    grid.minFrequency = in.readDouble();
    grid.maxFrequency = in.readDouble();
    grid.floorDb = in.readDouble();
    grid.logFrequency = in.readBool();
    grid.overview = in.readBool();
    if (fftSize <= 0 || grid.rows <= 0 || grid.columns <= 0 || grid.rows > 1024 || grid.columns > 8192)
        return std::nullopt;
    const size_t cells = (size_t) grid.rows * (size_t) grid.columns;
    if ((size_t) in.getNumBytesRemaining() < cells * sizeof(uint8_t)) return std::nullopt;
    grid.db.resize(cells);
    const auto span = (float) (0.0 - grid.floorDb);
    for (size_t i = 0; i < cells; ++i)
    {
        // InputStream::readByte returns the signed char widened to int; mask it back to 0..255.
        const auto quantized = (float) ((uint8_t) (in.readByte() & 0xff)) / 255.0f;
        grid.db[i] = (float) grid.floorDb + quantized * span;
    }
    if (! grid.valid()) return std::nullopt;

    const auto png = spectrumCacheFile(source, ".png", folder);
    if (png.existsAsFile())
        overview.image = juce::ImageFileFormat::loadFrom(png);
    if (! overview.image.isValid()) overview.image = renderSpectrumPng(grid);
    return overview;
}

int spectrumCacheFileCount(const juce::File& folder)
{
    if (! folder.isDirectory()) return 0;
    int count = 0;
    for (juce::RangedDirectoryIterator it(folder, true, "*.sps", juce::File::findFiles); it != juce::RangedDirectoryIterator(); ++it)
        ++count;
    return count;
}

juce::int64 spectrumCacheBytes(const juce::File& folder)
{
    if (! folder.isDirectory()) return 0;
    juce::int64 bytes = 0;
    for (juce::RangedDirectoryIterator it(folder, true, "*", juce::File::findFiles); it != juce::RangedDirectoryIterator(); ++it)
        bytes += it->getFile().getSize();
    return bytes;
}

void pruneSpectrumCache(const juce::File& folder, int keep, juce::int64 maxBytes)
{
    if (! folder.isDirectory()) return;
    std::vector<Entry> entries;
    for (juce::RangedDirectoryIterator it(folder, true, "*.sps", juce::File::findFiles); it != juce::RangedDirectoryIterator(); ++it)
    {
        Entry entry;
        entry.gridFile = it->getFile();
        entry.imageFile = entry.gridFile.withFileExtension(".png");
        entry.bytes = entry.gridFile.getSize()
            + (entry.imageFile.existsAsFile() ? entry.imageFile.getSize() : 0);
        entry.modified = entry.gridFile.getLastModificationTime();
        entries.push_back(std::move(entry));
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b)
    {
        return a.modified.toMilliseconds() > b.modified.toMilliseconds();
    });
    juce::int64 total = 0;
    int kept = 0;
    for (const auto& entry : entries)
    {
        const bool withinCount = kept < keep;
        const bool withinBytes = total + entry.bytes <= maxBytes;
        if (withinCount && (withinBytes || kept == 0))
        {
            total += entry.bytes;
            ++kept;
            continue;
        }
        entry.gridFile.deleteFile();
        if (entry.imageFile.existsAsFile()) entry.imageFile.deleteFile();
    }
}
} // namespace sonicator
