#pragma once
#include "DiscTypes.h"
#include <memory>
namespace sonicator {
// Random-access, read-only bytes. Implementations are confined to a worker; readAt must be safe
// to call repeatedly and must not use a mutable shared seek cursor.
class ReadOnlyByteSource
{
public:
    virtual ~ReadOnlyByteSource() = default;
    virtual uint64_t size() const = 0;
    virtual DiscResult<size_t> readAt(uint64_t offset, std::span<std::byte> output,
                                      CancelCheck cancel) const = 0;
};
using ByteSourcePtr = std::shared_ptr<const ReadOnlyByteSource>;

// Bounded block access for filesystem/sector adapters. `output` must hold at least
// count * logicalBlockSize() bytes; the result is the number of bytes actually written.
class ReadOnlyBlockSource
{
public:
    virtual ~ReadOnlyBlockSource() = default;
    virtual uint32_t logicalBlockSize() const = 0;
    virtual uint64_t logicalBlockCount() const = 0;
    virtual DiscResult<size_t> readBlocks(uint64_t firstBlock, uint32_t blockCount,
                                          std::span<std::byte> output,
                                          CancelCheck cancel) const = 0;
};
using BlockSourcePtr = std::shared_ptr<const ReadOnlyBlockSource>;
} // namespace sonicator
