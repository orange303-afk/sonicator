#include "DiscFilesystem.h"
#include <array>
#include <cstring>
namespace sonicator {
namespace {
constexpr uint64_t blockBytes = 2048;
uint32_t le32(const std::byte* p)
{
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value |= (uint32_t) std::to_integer<unsigned>(p[i]) << (8 * i);
    return value;
}
uint32_t be32(const std::byte* p)
{
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value = (value << 8) | (uint32_t) std::to_integer<unsigned>(p[i]);
    return value;
}
std::pair<juce::String, int> splitStereoDisplay(juce::String name)
{
    const auto lower = name.toLowerCase();
    for (const auto* extension : { ".wav", ".aif", ".aiff" })
        if (lower.endsWith(extension)) { name = name.dropLastCharacters((int) std::strlen(extension)); break; }
    name = name.trim();
    const auto stemLower = name.toLowerCase();
    struct Marker { const char* text; int side; };
    const Marker markers[] = { { "-l", -1 }, { "-r", 1 }, { " l", -1 }, { " r", 1 },
                               { ".l", -1 }, { ".r", 1 }, { "_l", -1 }, { "_r", 1 } };
    for (const auto& marker : markers)
        if (stemLower.endsWith(marker.text))
        {
            auto base = name.dropLastCharacters((int) std::strlen(marker.text)).trim();
            return { base, base.isEmpty() ? 0 : marker.side };
        }
    return { name, 0 };
}
bool fits(uint64_t offset, uint64_t length, uint64_t total)
{
    return offset <= total && length <= total - offset;
}
struct Record
{
    uint32_t extent = 0, bytes = 0;
    uint8_t flags = 0, extendedAttributes = 0, unitSize = 0, gap = 0, nameLength = 0;
    const std::byte* name = nullptr;
    size_t length = 0;
};
bool parseRecord(const std::byte* p, size_t available, Record& record, juce::String& error)
{
    if (available < 33) { error = "directory record is shorter than its fixed fields"; return false; }
    record.length = std::to_integer<unsigned>(p[0]);
    if (record.length == 0) { error.clear(); return true; }
    if (record.length < 34 || record.length > available)
    {
        error = "directory record length is invalid";
        return false;
    }
    record.extendedAttributes = std::to_integer<unsigned>(p[1]);
    record.extent = le32(p + 2);
    record.bytes = le32(p + 10);
    if (be32(p + 6) != record.extent || be32(p + 14) != record.bytes)
    {
        error = "directory record extent copies disagree";
        return false;
    }
    record.flags = std::to_integer<unsigned>(p[25]);
    record.unitSize = std::to_integer<unsigned>(p[26]);
    record.gap = std::to_integer<unsigned>(p[27]);
    record.nameLength = std::to_integer<unsigned>(p[32]);
    if (33u + record.nameLength > record.length)
    {
        error = "directory record name exceeds its length";
        return false;
    }
    record.name = p + 33;
    return true;
}
} // namespace

BlockRangeByteSource::BlockRangeByteSource(BlockSourcePtr blocks, uint64_t firstBlock, uint64_t bytes)
    : source(std::move(blocks)), first(firstBlock), total(bytes)
{
}
DiscResult<size_t> BlockRangeByteSource::readAt(uint64_t offset, std::span<std::byte> output,
                                                CancelCheck cancel) const
{
    DiscResult<size_t> result;
    if (cancel && cancel()) { result.status = DiscStatus::cancelled; return result; }
    if (! fits(offset, output.size(), total))
    {
        result.status = DiscStatus::ioError;
        result.error = "read range is outside the file extent";
        return result;
    }
    const auto blockSize = source->logicalBlockSize();
    if (blockSize == 0) { result.status = DiscStatus::malformed; result.error = "zero block size"; return result; }
    size_t done = 0;
    uint64_t position = offset;
    std::array<std::byte, 2048> one {};
    while (done < output.size())
    {
        const uint64_t block = first + position / blockSize;
        const size_t inBlock = (size_t) (position % blockSize);
        const size_t remaining = output.size() - done;
        if (inBlock == 0 && remaining >= blockSize)
        {
            const uint32_t blocks = (uint32_t) juce::jmin<uint64_t>(remaining / blockSize, 64);
            std::vector<std::byte> chunk((size_t) blocks * blockSize);
            const auto read = source->readBlocks(block, blocks, chunk, cancel);
            if (! read.ok()) { result.status = read.status; result.error = read.error; return result; }
            std::memcpy(output.data() + done, chunk.data(), (size_t) blocks * blockSize);
            done += (size_t) blocks * blockSize;
            position += (uint64_t) blocks * blockSize;
            continue;
        }
        const auto read = source->readBlocks(block, 1, one, cancel);
        if (! read.ok()) { result.status = read.status; result.error = read.error; return result; }
        const size_t chunk = juce::jmin(blockSize - inBlock, remaining);
        std::memcpy(output.data() + done, one.data() + inBlock, chunk);
        done += chunk;
        position += chunk;
    }
    result.status = DiscStatus::ready;
    result.value = done;
    return result;
}

DiscResult<size_t> BlockByteSource::readAt(uint64_t offset, std::span<std::byte> output,
                                           CancelCheck cancel) const
{
    DiscResult<size_t> result;
    if (cancel && cancel()) { result.status = DiscStatus::cancelled; return result; }
    if (! fits(offset, output.size(), total))
    {
        result.status = DiscStatus::ioError;
        result.error = "read range is outside the block source";
        return result;
    }
    const auto blockSize = source->logicalBlockSize();
    size_t done = 0;
    uint64_t position = offset;
    std::array<std::byte, 2048> one {};
    while (done < output.size())
    {
        const uint64_t block = position / blockSize;
        const size_t inBlock = (size_t) (position % blockSize);
        const size_t remaining = output.size() - done;
        if (inBlock == 0 && remaining >= blockSize)
        {
            const uint32_t blocks = (uint32_t) juce::jmin<uint64_t>(remaining / blockSize, 64);
            std::vector<std::byte> chunk((size_t) blocks * blockSize);
            const auto read = source->readBlocks(block, blocks, chunk, cancel);
            if (! read.ok()) { result.status = read.status; result.error = read.error; return result; }
            std::memcpy(output.data() + done, chunk.data(), (size_t) blocks * blockSize);
            done += (size_t) blocks * blockSize;
            position += (uint64_t) blocks * blockSize;
            continue;
        }
        const auto read = source->readBlocks(block, 1, one, cancel);
        if (! read.ok()) { result.status = read.status; result.error = read.error; return result; }
        const size_t chunk = juce::jmin(blockSize - inBlock, remaining);
        std::memcpy(output.data() + done, one.data() + inBlock, chunk);
        done += chunk;
        position += chunk;
    }
    result.status = DiscStatus::ready;
    result.value = done;
    return result;
}

DiscResult<std::unique_ptr<Iso9660Filesystem>> Iso9660Filesystem::open(BlockSourcePtr blocks,
                                                                       const DiscLimits& limits,
                                                                       CancelCheck cancel)
{
    DiscResult<std::unique_ptr<Iso9660Filesystem>> result;
    if (blocks == nullptr) { result.status = DiscStatus::ioError; result.error = "no block source"; return result; }
    std::unique_ptr<Iso9660Filesystem> filesystem(new Iso9660Filesystem(std::move(blocks), limits));
    std::array<std::byte, 2048> descriptor {};
    std::array<std::byte, 2048> primary {}, joliet {};
    bool havePrimary = false, haveJoliet = false, terminated = false;
    for (uint32_t i = 0; i < juce::jlimit(1u, 256u, limits.maxVolumeDescriptors); ++i)
    {
        if (cancel && cancel()) { result.status = DiscStatus::cancelled; return result; }
        const auto read = filesystem->source->readBlocks(16 + i, 1, descriptor, cancel);
        if (! read.ok()) { result.status = read.status; result.error = read.error; return result; }
        const auto type = std::to_integer<unsigned>(descriptor[0]);
        if (std::memcmp(descriptor.data() + 1, "CD001", 5) != 0) break;
        if (type == 255) { terminated = true; break; }
        if (type == 1 && ! havePrimary) { primary = descriptor; havePrimary = true; }
        if (type == 2 && ! haveJoliet)
        {
            const auto escape = descriptor.data() + 88;
            if (std::memcmp(escape, "%/@", 3) == 0 || std::memcmp(escape, "%/C", 3) == 0
                || std::memcmp(escape, "%/E", 3) == 0)
            {
                joliet = descriptor; haveJoliet = true;
            }
        }
    }
    if (! havePrimary || ! terminated)
    {
        result.status = DiscStatus::malformed;
        result.error = "no primary volume descriptor or terminator";
        return result;
    }
    const std::byte* chosen = haveJoliet ? joliet.data() : primary.data();
    filesystem->joliet = haveJoliet;
    Record rootRecord;
    juce::String parseError;
    if (! parseRecord(chosen + 156, 34, rootRecord, parseError) || rootRecord.length == 0
        || (rootRecord.flags & 0x02) == 0)
    {
        result.status = DiscStatus::malformed;
        result.error = parseError.isNotEmpty() ? parseError : "root directory record is invalid";
        return result;
    }
    const uint64_t blocksNeeded = ((uint64_t) rootRecord.bytes + blockBytes - 1) / blockBytes;
    if (rootRecord.extent > filesystem->source->logicalBlockCount()
        || blocksNeeded > filesystem->source->logicalBlockCount() - rootRecord.extent)
    {
        result.status = DiscStatus::malformed;
        result.error = "root directory exceeds the volume";
        return result;
    }
    filesystem->rootId = { ((uint64_t) rootRecord.extent << 32) | 0xFFFFFFFFu };
    filesystem->directories[filesystem->rootId.value] = { rootRecord.extent, rootRecord.bytes };
    result.status = DiscStatus::ready;
    result.value = std::move(filesystem);
    return result;
}

juce::String Iso9660Filesystem::decodeName(const std::byte* bytes, size_t count) const
{
    if (! joliet) return juce::String::fromUTF8(reinterpret_cast<const char*>(bytes), (int) count);
    juce::String name;
    for (size_t i = 0; i + 1 < count; i += 2)
    {
        const auto code = (uint32_t) (std::to_integer<unsigned>(bytes[i]) << 8)
                        | std::to_integer<unsigned>(bytes[i + 1]);
        if (code == 0) break;
        name += juce::String::charToString((juce::juce_wchar) code);
    }
    return name;
}

juce::String Iso9660Filesystem::displayName(const juce::String& raw) const
{
    auto name = raw;
    const auto semicolon = name.lastIndexOfChar(';');
    if (semicolon >= 0)
    {
        const auto version = name.substring(semicolon + 1);
        if (version.isNotEmpty() && version.containsOnly("0123456789"))
            name = name.substring(0, semicolon);
    }
    if (name.endsWithChar('.')) name = name.dropLastCharacters(1);
    return name;
}

DiscResult<DiscListing> Iso9660Filesystem::fail(DiscStatus status, const juce::String& message) const
{
    DiscResult<DiscListing> result;
    result.status = status;
    result.error = message;
    return result;
}

DiscResult<DiscListing> Iso9660Filesystem::list(DiscEntryId directory, CancelCheck cancel)
{
    const auto known = directories.find(directory.value);
    if (known == directories.end())
        return fail(DiscStatus::unknown, "directory is not known in this session; reopen from the root");
    DiscResult<DiscListing> result;
    std::array<std::byte, 2048> block {};
    const uint64_t blockCount = ((uint64_t) known->second.bytes + blockBytes - 1) / blockBytes;
    uint32_t records = 0;
    for (uint64_t blockIndex = 0; blockIndex < blockCount; ++blockIndex)
    {
        if (cancel && cancel()) { result.status = DiscStatus::cancelled; return result; }
        const auto read = source->readBlocks(known->second.extent + blockIndex, 1, block, cancel);
        if (! read.ok())
        {
            result.status = read.status; result.error = read.error;
            result.value.partial = ! result.value.entries.empty();
            return result;
        }
        size_t offset = 0;
        const size_t limit = (size_t) juce::jmin<uint64_t>(blockBytes, known->second.bytes - blockIndex * blockBytes);
        while (offset < limit)
        {
            Record record;
            juce::String parseError;
            if (! parseRecord(block.data() + offset, limit - offset, record, parseError))
                return fail(DiscStatus::malformed, parseError);
            if (record.length == 0) break; // padding to the next logical block
            offset += record.length;
            if (record.nameLength == 1 && (std::to_integer<unsigned>(record.name[0]) == 0
                                           || std::to_integer<unsigned>(record.name[0]) == 1))
                continue; // "." and ".."
            if (++records > budget.maxDirectoryRecords)
            {
                result.value.partial = true;
                result.value.warning = "directory record limit reached";
                result.status = DiscStatus::ready;
                return result;
            }
            const auto raw = decodeName(record.name, record.nameLength);
            DiscEntry entry;
            entry.parent = directory;
            entry.rawName = raw;
            entry.displayName = displayName(raw);
            entry.id = { (((uint64_t) (known->second.extent + blockIndex)) << 32) | (uint32_t) offset };
            const bool isDirectory = (record.flags & 0x02) != 0;
            const uint64_t payloadBlocks = ((uint64_t) record.bytes + blockBytes - 1) / blockBytes;
            const bool extentValid = record.extent <= source->logicalBlockCount()
                && payloadBlocks <= source->logicalBlockCount() - record.extent;
            if ((record.flags & 0x80) != 0)
                entry.capability = "multi-extent file is not supported";
            else if (record.extendedAttributes != 0)
                entry.capability = "extended attribute records are not supported";
            else if (record.unitSize != 0 || record.gap != 0)
                entry.capability = "interleaved files are not supported";
            else if (! extentValid)
                entry.capability = "extent is outside the volume";
            if (isDirectory) entry.kind = DiscEntryKind::directory;
            else if (entry.capability.isEmpty()) entry.kind = DiscEntryKind::file;
            entry.declaredBytes = record.bytes;
            if (entry.kind == DiscEntryKind::directory && entry.capability.isEmpty())
                directories[entry.id.value] = { record.extent, record.bytes };
            if (entry.kind == DiscEntryKind::file)
                files[entry.id.value] = { record.extent, record.bytes };
            result.value.entries.push_back(std::move(entry));
        }
    }
    // Merge a verified mono L/R pair into one stereo entry, exactly like the Akai catalogue.
    std::map<juce::String, size_t> lefts, rights;
    for (size_t i = 0; i < result.value.entries.size(); ++i)
    {
        auto& entry = result.value.entries[i];
        if (entry.kind != DiscEntryKind::file || entry.capability.isNotEmpty()) continue;
        const auto [base, side] = splitStereoDisplay(entry.displayName);
        if (side < 0) lefts[base.toUpperCase()] = i;
        else if (side > 0) rights[base.toUpperCase()] = i;
    }
    std::vector<bool> merged(result.value.entries.size(), false);
    for (const auto& [base, leftIndex] : lefts)
    {
        const auto rightIt = rights.find(base);
        if (rightIt == rights.end()) continue;
        auto& left = result.value.entries[leftIndex];
        auto& right = result.value.entries[rightIt->second];
        if (left.declaredBytes == 0 || left.declaredBytes != right.declaredBytes) continue;
        left.partner = right.id;
        left.stereo = true;
        left.displayName = splitStereoDisplay(left.displayName).first;
        left.rawName = left.displayName;
        merged[rightIt->second] = true;
    }
    std::vector<DiscEntry> kept;
    kept.reserve(result.value.entries.size());
    for (size_t i = 0; i < result.value.entries.size(); ++i)
        if (! merged[i]) kept.push_back(std::move(result.value.entries[i]));
    result.value.entries = std::move(kept);
    result.status = DiscStatus::ready;
    return result;
}

DiscResult<ByteSourcePtr> Iso9660Filesystem::openFile(DiscEntryId file, CancelCheck cancel)
{
    DiscResult<ByteSourcePtr> result;
    if (cancel && cancel()) { result.status = DiscStatus::cancelled; return result; }
    const auto known = files.find(file.value);
    if (known == files.end()) { result.status = DiscStatus::unknown; result.error = "file is not a known ISO file extent"; return result; }
    result.status = DiscStatus::ready;
    result.value = std::make_shared<BlockRangeByteSource>(source, known->second.extent, known->second.bytes);
    return result;
}
} // namespace sonicator
