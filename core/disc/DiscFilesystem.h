#pragma once
#include "DiscTypes.h"
#include "ReadOnlyByteSource.h"
#include <map>
#include <memory>
#include <vector>
namespace sonicator {
struct DiscEntryId
{
    uint64_t value = 0;
    bool operator==(const DiscEntryId& other) const { return value == other.value; }
    bool operator!=(const DiscEntryId& other) const { return value != other.value; }
    bool operator<(const DiscEntryId& other) const { return value < other.value; }
};
enum class DiscEntryKind { directory, file, container, unknown };
struct DiscEntry
{
    DiscEntryId id, parent;
    DiscEntryKind kind = DiscEntryKind::unknown;
    juce::String rawName, displayName;
    uint64_t declaredBytes = 0;
    DiscEntryId partner; // second channel of a verified stereo pair, invalid for mono
    bool stereo = false;
    juce::String capability; // empty when the entry can be opened by this build
};
struct DiscListing
{
    std::vector<DiscEntry> entries;
    bool partial = false;
    juce::String warning;
};
class DiscFilesystem
{
public:
    virtual ~DiscFilesystem() = default;
    virtual DiscEntryId root() const = 0;
    // Entries are ordered by the on-disc record order, not sorted by a display name.
    virtual DiscResult<DiscListing> list(DiscEntryId directory, CancelCheck cancel) = 0;
    virtual DiscResult<ByteSourcePtr> openFile(DiscEntryId file, CancelCheck cancel) = 0;
};

// Lazy byte view over logical blocks; no whole-file copy and no fake file system path.
class BlockRangeByteSource final : public ReadOnlyByteSource
{
public:
    BlockRangeByteSource(BlockSourcePtr blocks, uint64_t firstBlock, uint64_t bytes);
    uint64_t size() const override { return total; }
    DiscResult<size_t> readAt(uint64_t offset, std::span<std::byte> output, CancelCheck cancel) const override;
private:
    BlockSourcePtr source;
    uint64_t first = 0, total = 0;
};

// Byte view over a logical-block source, used to probe audio/user-data extracted from raw sectors.
class BlockByteSource final : public ReadOnlyByteSource
{
public:
    explicit BlockByteSource(BlockSourcePtr blocks)
        : source(std::move(blocks)) { total = source->logicalBlockCount() * source->logicalBlockSize(); }
    uint64_t size() const override { return total; }
    DiscResult<size_t> readAt(uint64_t offset, std::span<std::byte> output, CancelCheck cancel) const override;
private:
    BlockSourcePtr source;
    uint64_t total = 0;
};

class Iso9660Filesystem final : public DiscFilesystem
{
public:
    static DiscResult<std::unique_ptr<Iso9660Filesystem>> open(BlockSourcePtr blocks,
                                                               const DiscLimits& limits,
                                                               CancelCheck cancel);
    DiscEntryId root() const override { return rootId; }
    DiscResult<DiscListing> list(DiscEntryId directory, CancelCheck cancel) override;
    DiscResult<ByteSourcePtr> openFile(DiscEntryId file, CancelCheck cancel) override;
private:
    struct DirectoryData { uint32_t extent = 0; uint32_t bytes = 0; };
    struct FileData { uint32_t extent = 0; uint32_t bytes = 0; };
    Iso9660Filesystem(BlockSourcePtr blocks, DiscLimits limits) : source(std::move(blocks)), budget(limits) {}
    BlockSourcePtr source;
    DiscLimits budget;
    DiscEntryId rootId;
    bool joliet = false;
    std::map<uint64_t, DirectoryData> directories;
    std::map<uint64_t, FileData> files;
    DiscResult<DiscListing> fail(DiscStatus status, const juce::String& message) const;
    juce::String decodeName(const std::byte* bytes, size_t count) const;
    juce::String displayName(const juce::String& raw) const;
};
} // namespace sonicator
