#include "AkaiFilesystem.h"
#include <array>
#include <cstring>
#include <set>
namespace sonicator {
namespace {
constexpr uint64_t blockSize = 8192;
constexpr uint64_t volumeTableOffset = 0x00ca;
constexpr uint64_t fatOffset = 0x070a;
constexpr uint64_t partitionTableOffset = 0x4500;
constexpr uint32_t s1000Capacity = 341; // one 8192-byte block of 24-byte records
constexpr uint32_t s3000Capacity = 512;
uint16_t le16At(const std::byte* p) { return (uint16_t) (std::to_integer<unsigned>(p[0]) | (std::to_integer<unsigned>(p[1]) << 8)); }
uint16_t be16At(const std::byte* p) { return (uint16_t) ((std::to_integer<unsigned>(p[0]) << 8) | std::to_integer<unsigned>(p[1])); }
uint32_t le24At(const std::byte* p)
{
    return (uint32_t) std::to_integer<unsigned>(p[0])
         | ((uint32_t) std::to_integer<unsigned>(p[1]) << 8)
         | ((uint32_t) std::to_integer<unsigned>(p[2]) << 16);
}
bool fits(uint64_t offset, uint64_t length, uint64_t total) { return offset <= total && length <= total - offset; }
} // namespace
AkaiChainByteSource::AkaiChainByteSource(ByteSourcePtr bytes, std::vector<uint16_t> blocks,
                                         uint64_t blockBytes, uint64_t bytesInFile)
    : source(std::move(bytes)), chain(std::move(blocks)), blockSize(blockBytes), total(bytesInFile)
{
}
DiscResult<size_t> AkaiChainByteSource::readAt(uint64_t offset, std::span<std::byte> output,
                                               CancelCheck cancel) const
{
    DiscResult<size_t> result;
    if (cancel && cancel()) { result.status = DiscStatus::cancelled; return result; }
    if (! fits(offset, output.size(), total))
    {
        result.status = DiscStatus::ioError;
        result.error = "read range is outside the sample extent";
        return result;
    }
    size_t done = 0;
    uint64_t position = offset;
    while (done < output.size())
    {
        const auto blockIndex = (size_t) (position / blockSize);
        if (blockIndex >= chain.size())
        {
            result.status = DiscStatus::malformed;
            result.error = "sample FAT chain is shorter than the declared size";
            return result;
        }
        const size_t inBlock = (size_t) (position % blockSize);
        const size_t chunk = (size_t) juce::jmin<uint64_t>(blockSize - inBlock, output.size() - done);
        const auto read = source->readAt((uint64_t) chain[blockIndex] * blockSize + inBlock,
                                         output.subspan(done, chunk), cancel);
        if (! read.ok()) { result.status = read.status; result.error = read.error; return result; }
        if (read.value != chunk) { result.status = DiscStatus::ioError; result.error = "short read from sample block"; return result; }
        done += chunk;
        position += chunk;
    }
    result.status = DiscStatus::ready;
    result.value = done;
    return result;
}

namespace {
// "KICK.L" / "KICK-R" / "KICK .L" -> {"KICK", -1 for L, +1 for R, 0 for no marker}.
std::pair<juce::String, int> splitStereoMarker(juce::String name)
{
    name = name.trim();
    const auto lower = name.toLowerCase();
    struct Suffix { const char* text; int side; };
    const Suffix suffixes[] = { { ".l", -1 }, { ".r", 1 }, { " l", -1 }, { " r", 1 },
                                { "-l", -1 }, { "-r", 1 }, { "_l", -1 }, { "_r", 1 } };
    for (const auto& suffix : suffixes)
        if (lower.endsWith(suffix.text))
        {
            auto base = name.dropLastCharacters((int) std::strlen(suffix.text)).trim();
            return { base, base.isEmpty() ? 0 : suffix.side };
        }
    return { name, 0 };
}
} // namespace

juce::String AkaiFilesystem::decodeName(const std::byte* bytes, size_t count)
{
    juce::String name;
    for (size_t i = 0; i < count; ++i)
    {
        const auto value = std::to_integer<unsigned>(bytes[i]);
        if (value == 0) break;
        if (value <= 9) name += juce::String::charToString((juce::juce_wchar) ('0' + value));
        else if (value == 0x0a) name += ' ';
        else if (value >= 0x0b && value <= 0x24) name += juce::String::charToString((juce::juce_wchar) ('A' + value - 0x0b));
        else if (value == 0x25) name += '#';
        else if (value == 0x26) name += '+';
        else if (value == 0x27) name += '-';
        else if (value == 0x28) name += '.';
        else name += '?';
    }
    return name.trim();
}

DiscResult<std::unique_ptr<AkaiFilesystem>> AkaiFilesystem::open(ByteSourcePtr bytes, const DiscLimits& limits,
                                                                 CancelCheck cancel)
{
    DiscResult<std::unique_ptr<AkaiFilesystem>> result;
    if (bytes == nullptr) { result.status = DiscStatus::ioError; result.error = "no source"; return result; }
    if (cancel && cancel()) { result.status = DiscStatus::cancelled; return result; }
    std::unique_ptr<AkaiFilesystem> fs(new AkaiFilesystem(std::move(bytes), limits));
    fs->blockCount = fs->source->size() / blockSize;
    if (fs->blockCount == 0) { result.status = DiscStatus::malformed; result.error = "image has no complete 8192-byte blocks"; return result; }

    std::array<std::byte, 0x100> partitionHeader {};
    const auto partitionRead = fs->source->readAt(partitionTableOffset, partitionHeader, cancel);
    if (partitionRead.ok() && partitionRead.value == partitionHeader.size())
    {
        const auto count = std::to_integer<unsigned>(partitionHeader[0]);
        const auto fixed = std::to_integer<unsigned>(partitionHeader[1]);
        if (count >= 1 && count <= 64 && (fixed == 0 || fixed == 1) && 2 + count * 2 <= partitionHeader.size())
        {
            uint64_t start = 0, total = 0;
            bool valid = true;
            std::vector<uint64_t> sizes;
            for (unsigned i = 0; i < count; ++i)
            {
                const auto size = le16At(partitionHeader.data() + 2 + i * 2);
                if (size == 0) { valid = false; break; }
                sizes.push_back(size);
                if (total > UINT64_MAX - size) { valid = false; break; }
                total += size;
            }
            if (valid && total <= fs->blockCount)
                for (unsigned i = 0; i < count; ++i)
                {
                    fs->partitions.push_back({ i, start, sizes[i] });
                    start += sizes[i];
                }
        }
    }
    if (fs->partitions.empty())
    {
        result.status = DiscStatus::unsupported;
        result.error = "the partition table is not validated";
        return result;
    }

    std::array<std::byte, 6> fatProbe {};
    std::array<std::byte, 16 * 100> volumeTable {};
    for (auto& partition : fs->partitions)
    {
        if (cancel && cancel()) { result.status = DiscStatus::cancelled; return result; }
        const uint64_t base = partition.startBlock * blockSize;
        const auto fatRead = fs->source->readAt(base + fatOffset, fatProbe, cancel);
        const auto fatLe = le16At(fatProbe.data());
        const auto fatBe = be16At(fatProbe.data());
        // Reserved markers appear either as 0x0040 or as the byte-swapped 0x4000; the chain walk
        // below decides the exact interpretation per sample instead of trusting one byte.
        const bool markers = fatLe == 0x0040 || fatLe == 0x4000 || fatBe == 0x0040;
        if (! fatRead.ok() || fatRead.value != 6 || ! markers)
        {
            result.status = DiscStatus::malformed;
            result.error = "partition FAT reserved markers are missing";
            return result;
        }
        if (! fits(base + volumeTableOffset, volumeTable.size(), fs->source->size())) continue;
        const auto volumeRead = fs->source->readAt(base + volumeTableOffset, volumeTable, cancel);
        if (! volumeRead.ok() || volumeRead.value != volumeTable.size()) continue;
        for (uint32_t slot = 0; slot < 100; ++slot)
        {
            const auto* entry = volumeTable.data() + slot * 16;
            const auto type = le16At(entry + 12);
            const auto startBlock = le16At(entry + 14);
            if ((type != 1 && type != 3) || startBlock == 0) continue;
            if ((uint64_t) partition.startBlock + startBlock >= fs->blockCount) continue;
            Volume volume;
            volume.partition = partition.index;
            volume.slot = slot;
            volume.type = type;
            volume.startBlock = startBlock;
            volume.name = decodeName(entry, 12);
            fs->volumes.push_back(volume);
            fs->volumeById[volumeId(partition.index, slot).value] = volume;
        }
    }
    result.status = DiscStatus::ready;
    result.value = std::move(fs);
    return result;
}

DiscResult<DiscListing> AkaiFilesystem::list(DiscEntryId directory, CancelCheck cancel)
{
    DiscResult<DiscListing> result;
    if (cancel && cancel()) { result.status = DiscStatus::cancelled; return result; }
    if (directory.value == 0)
    {
        for (const auto& partition : partitions)
        {
            DiscEntry entry;
            entry.id = partitionId(partition.index);
            entry.parent = directory;
            entry.kind = DiscEntryKind::container;
            entry.rawName = "Partition " + juce::String(partition.index + 1);
            entry.displayName = entry.rawName;
            entry.declaredBytes = partition.blockCount * blockSize;
            result.value.entries.push_back(std::move(entry));
        }
        result.status = DiscStatus::ready;
        return result;
    }
    if (directory.value < 0x100000000ull)
    {
        const auto index = directory.value - 1;
        if (index >= partitions.size()) { result.status = DiscStatus::unknown; result.error = "unknown partition"; return result; }
        for (const auto& volume : volumes)
        {
            if (volume.partition != index) continue;
            DiscEntry entry;
            entry.id = volumeId(volume.partition, volume.slot);
            entry.parent = directory;
            entry.kind = DiscEntryKind::container;
            entry.rawName = volume.name.isNotEmpty() ? volume.name : ("Volume " + juce::String(volume.slot + 1));
            entry.displayName = entry.rawName;
            result.value.entries.push_back(std::move(entry));
        }
        result.status = DiscStatus::ready;
        return result;
    }
    const auto found = volumeById.find(directory.value);
    if (found == volumeById.end()) { result.status = DiscStatus::unknown; result.error = "unknown volume"; return result; }
    const auto volume = found->second;
    const auto& partition = partitions[volume.partition];
    const uint32_t capacity = volume.type == 3 ? s3000Capacity : s1000Capacity;
    const uint64_t directoryBytes = (uint64_t) capacity * 24;
    const uint64_t absoluteBlock = partition.startBlock + volume.startBlock;
    const uint64_t offset = absoluteBlock * blockSize;
    if (! fits(offset, directoryBytes, source->size()))
    {
        result.status = DiscStatus::malformed;
        result.error = "volume directory is outside the image";
        return result;
    }
    std::vector<std::byte> records((size_t) directoryBytes);
    const auto read = source->readAt(offset, records, cancel);
    if (! read.ok() || read.value != records.size())
    {
        result.status = DiscStatus::malformed; result.error = "volume directory cannot be read"; return result;
    }
    for (uint32_t recordIndex = 0; recordIndex < capacity; ++recordIndex)
    {
        const auto* record = records.data() + (size_t) recordIndex * 24;
        const auto type = std::to_integer<unsigned>(record[0x10]);
        const auto bytes = le24At(record + 0x11);
        const auto startBlock = le16At(record + 0x14);
        const auto name = decodeName(record, 12);
        if (type == 0 && bytes == 0 && startBlock == 0) continue; // empty slot or padding
        if (type == 0 || name.isEmpty()) continue;
        FileRecord file;
        file.partition = volume.partition;
        file.slot = volume.slot;
        file.recordIndex = recordIndex;
        file.type = (uint8_t) type;
        file.bytes = bytes;
        file.startBlock = startBlock;
        file.name = name;
        DiscEntry entry;
        entry.id = fileId(volume.partition, volume.slot, recordIndex);
        entry.parent = directory;
        entry.rawName = name;
        entry.displayName = name;
        entry.declaredBytes = bytes;
        if (type == 0x70 || type == 0xf0)
        {
            entry.kind = DiscEntryKind::container;
            entry.capability = "program metadata only; keygroups and filters are not emulated";
        }
        else if (type == 0x73 || type == 0xf3)
        {
            entry.kind = DiscEntryKind::file;
            const uint64_t absolute = partition.startBlock + startBlock;
            const uint64_t needed = ((uint64_t) bytes + blockSize - 1) / blockSize;
            if (startBlock == 0 || bytes == 0 || absolute >= blockCount || needed > blockCount - absolute)
            {
                entry.kind = DiscEntryKind::unknown;
                entry.capability = "sample record has an invalid start or size";
            }

        }
        else
        {
            entry.kind = DiscEntryKind::unknown;
            entry.capability = "file type is not supported";
        }
        if (entry.kind == DiscEntryKind::file) files[entry.id.value] = file;
        result.value.entries.push_back(std::move(entry));
    }
    // Merge a verified L/R pair into one stereo entry. Only equal declared sizes and a unique
    // one-left/one-right base are accepted; a lone suffix or a size mismatch stays separate.
    std::map<juce::String, size_t> lefts, rights;
    for (size_t i = 0; i < result.value.entries.size(); ++i)
    {
        auto& entry = result.value.entries[i];
        if (entry.kind != DiscEntryKind::file || entry.capability.isNotEmpty()) continue;
        const auto [base, side] = splitStereoMarker(entry.displayName);
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
        const auto [leftBase, leftSide] = splitStereoMarker(left.displayName);
        const auto [rightBase, rightSide] = splitStereoMarker(right.displayName);
        if (leftSide != -1 || rightSide != 1 || leftBase.compareIgnoreCase(rightBase) != 0) continue;
        left.partner = right.id;
        left.stereo = true;
        left.displayName = leftBase;
        left.rawName = leftBase;
        files[left.id.value].partnerId = right.id.value;
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

bool AkaiFilesystem::readFat(uint32_t partitionIndex, uint16_t relativeBlock, CancelCheck cancel,
                             uint16_t& little, uint16_t& big, juce::String& error) const
{
    if (partitionIndex >= partitions.size()) { error = "FAT partition is unknown"; return false; }
    const auto& partition = partitions[partitionIndex];
    if ((uint64_t) partition.startBlock + relativeBlock >= blockCount)
    {
        error = "FAT block is outside the image";
        return false;
    }
    std::array<std::byte, 2> value {};
    const auto read = source->readAt(partition.startBlock * blockSize + fatOffset
                                     + (uint64_t) relativeBlock * 2, value, cancel);
    if (! read.ok() || read.value != 2) { error = "FAT entry cannot be read"; return false; }
    little = le16At(value.data());
    big = be16At(value.data());
    return true;
}

DiscResult<ByteSourcePtr> AkaiFilesystem::openChain(const FileRecord& file, CancelCheck cancel)
{
    DiscResult<ByteSourcePtr> result;
    const auto& partition = partitions[file.partition];
    const uint64_t needed = ((uint64_t) file.bytes + blockSize - 1) / blockSize;
    struct Variant { bool bigValue; uint16_t end, reserved40, reserved80; };
    // The reserved markers may be stored as 0x0040/0x00c0 or byte-swapped as 0x4000/0xc000.
    // Pointers are normally little-endian; the big-value variant covers the documented layout.
    const Variant variants[] = { { false, 0x00c0, 0x0040, 0x0080 },
                                 { false, 0xc000, 0x4000, 0x8000 },
                                 { true,  0x00c0, 0x0040, 0x0080 } };
    for (const auto& variant : variants)
    {
        std::vector<uint16_t> blocks;
        std::set<uint16_t> visited;
        uint16_t current = file.startBlock;
        juce::String error;
        bool valid = true;
        while (blocks.size() < needed)
        {
            if (cancel && cancel()) { result.status = DiscStatus::cancelled; return result; }
            if (blocks.size() >= budget.maxChainSteps)
            {
                result.status = DiscStatus::limitExceeded; result.error = "FAT chain limit reached"; return result;
            }
            if (! visited.insert(current).second) { valid = false; error = "FAT chain contains a cycle"; break; }
            const uint64_t absolute = (uint64_t) partition.startBlock + current;
            if (absolute >= blockCount) { valid = false; error = "FAT chain leaves the image"; break; }
            blocks.push_back((uint16_t) absolute);
            uint16_t little = 0, big = 0;
            if (! readFat(file.partition, current, cancel, little, big, error)) { valid = false; break; }
            const auto next = variant.bigValue ? big : little;
            if (blocks.size() == needed)
            {
                // A complete chain must end in an end marker; otherwise this interpretation is
                // not the one that stored the file.
                if (next != variant.end && next != variant.reserved40 && next != variant.reserved80)
                {
                    valid = false; error = "FAT chain continues past the declared size";
                }
                break;
            }
            if (next == variant.end) { valid = false; error = "FAT chain ends before the declared sample size"; break; }
            if (next == variant.reserved40 || next == variant.reserved80 || next == 0)
            {
                valid = false; error = "FAT chain reaches a reserved or free block"; break;
            }
            current = next;
        }
        if (valid && blocks.size() == needed)
        {
            result.status = DiscStatus::ready;
            result.value = std::make_shared<AkaiChainByteSource>(source, std::move(blocks), blockSize, file.bytes);
            return result;
        }
        if (! error.isEmpty()) result.error = error;
    }
    result.status = DiscStatus::malformed;
    if (result.error.isEmpty()) result.error = "no FAT interpretation matches the declared sample size";
    return result;
}

DiscResult<ByteSourcePtr> AkaiFilesystem::openFile(DiscEntryId file, CancelCheck cancel)
{
    DiscResult<ByteSourcePtr> result;
    const auto found = files.find(file.value);
    if (found == files.end()) { result.status = DiscStatus::unknown; result.error = "not a known sample extent"; return result; }
    return openChain(found->second, cancel);
}

DiscResult<SampleAudioPtr> AkaiFilesystem::decodeSample(DiscEntryId file, CancelCheck cancel)
{
    DiscResult<SampleAudioPtr> result;
    const auto found = files.find(file.value);
    if (found == files.end())
    {
        result.status = DiscStatus::unknown;
        result.error = "not a known sample extent";
        return result;
    }
    auto decodeOne = [this, cancel](const FileRecord& record)
    {
        const auto extent = openChain(record, cancel);
        if (! extent.ok()) return DiscResult<SampleAudioPtr> { extent.status, {}, extent.error };
        return decodeAkaiSample(extent.value, record.bytes, budget, cancel);
    };
    auto left = decodeOne(found->second);
    if (! left.ok()) return left;
    if (found->second.partnerId == 0) return left;
    const auto partner = files.find(found->second.partnerId);
    if (partner == files.end())
    {
        result.status = DiscStatus::malformed;
        result.error = "stereo partner extent is missing";
        return result;
    }
    auto right = decodeOne(partner->second);
    if (! right.ok()) return right;
    if (left.value->frames != right.value->frames || left.value->sampleRate != right.value->sampleRate)
    {
        result.status = DiscStatus::malformed;
        result.error = "stereo pair differs in length or sample rate";
        return result;
    }
    auto stereo = std::make_shared<SampleAudio>();
    stereo->sampleRate = left.value->sampleRate;
    stereo->channels = 2;
    stereo->bitsPerSample = 16;
    stereo->frames = left.value->frames;
    stereo->left = std::move(left.value->left);
    stereo->right = std::move(right.value->left);
    result.status = DiscStatus::ready;
    result.value = std::move(stereo);
    return result;
}
} // namespace sonicator
