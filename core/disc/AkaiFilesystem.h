#pragma once
#include "DiscFilesystem.h"
#include "AkaiSampleReader.h"
#include <map>
namespace sonicator {
// Byte view over a fragmented FAT chain. Bounds come from the declared file size; the chain is
// validated before this source is created.
class AkaiChainByteSource final : public ReadOnlyByteSource
{
public:
    AkaiChainByteSource(ByteSourcePtr bytes, std::vector<uint16_t> blocks, uint64_t blockBytes,
                        uint64_t bytesInFile);
    uint64_t size() const override { return total; }
    DiscResult<size_t> readAt(uint64_t offset, std::span<std::byte> output, CancelCheck cancel) const override;
private:
    ByteSourcePtr source;
    std::vector<uint16_t> chain;
    uint64_t blockSize = 0, total = 0;
};

// Raw Akai S1000/S3000-style volumes. Block size is 8192; each validated partition owns its
// volume table at +0x00ca and FAT at +0x070a, and volume/file start blocks are relative to that
// partition. Program records are metadata-only containers in this stage.
class AkaiFilesystem final : public DiscFilesystem
{
public:
    static DiscResult<std::unique_ptr<AkaiFilesystem>> open(ByteSourcePtr bytes, const DiscLimits& limits,
                                                            CancelCheck cancel);
    DiscEntryId root() const override { return {}; }
    DiscResult<DiscListing> list(DiscEntryId directory, CancelCheck cancel) override;
    DiscResult<ByteSourcePtr> openFile(DiscEntryId file, CancelCheck cancel) override;
    // Verified raw variant only: 150-byte header + signed 16-bit LE mono.
    DiscResult<SampleAudioPtr> decodeSample(DiscEntryId file, CancelCheck cancel);
private:
    struct Partition { uint32_t index = 0; uint64_t startBlock = 0; uint64_t blockCount = 0; };
    struct Volume { uint32_t partition = 0, slot = 0; uint16_t startBlock = 0, type = 0; juce::String name; };
    struct FileRecord { uint32_t partition = 0, slot = 0, recordIndex = 0; uint8_t type = 0;
                        uint32_t bytes = 0; uint16_t startBlock = 0; uint64_t partnerId = 0;
                        juce::String name; };
    explicit AkaiFilesystem(ByteSourcePtr bytes, DiscLimits limits)
        : source(std::move(bytes)), budget(limits) {}
    ByteSourcePtr source;
    DiscLimits budget;
    uint64_t blockCount = 0;
    std::vector<Partition> partitions;
    std::vector<Volume> volumes;
    std::map<uint64_t, FileRecord> files;
    std::map<uint64_t, Volume> volumeById;
    bool readFat(uint32_t partition, uint16_t relativeBlock, CancelCheck cancel, uint16_t& little,
                 uint16_t& big, juce::String& error) const;
    DiscResult<ByteSourcePtr> openChain(const FileRecord&, CancelCheck cancel);
    static juce::String decodeName(const std::byte* bytes, size_t count);
    static DiscEntryId volumeId(uint32_t partition, uint32_t slot)
    { return { (1ull << 32) | ((uint64_t) partition << 8) | slot }; }
    static DiscEntryId fileId(uint32_t partition, uint32_t slot, uint32_t record)
    { return { (2ull << 32) | ((uint64_t) partition << 24) | ((uint64_t) slot << 16) | record }; }
    static DiscEntryId partitionId(uint32_t index) { return { (uint64_t) index + 1 }; }
};
} // namespace sonicator
