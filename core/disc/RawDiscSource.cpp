#include "RawDiscSource.h"
#include <juce_core/juce_core.h>
#include <mutex>
namespace sonicator {
namespace {
bool checkedRange(uint64_t offset, uint64_t length, uint64_t total)
{
    return offset <= total && length <= total - offset;
}
class FileByteSource final : public ReadOnlyByteSource
{
public:
    FileByteSource(juce::File file, uint64_t length, uint64_t maxRequest)
        : stream(file), total(length), maxRead(maxRequest)
    {
    }
    uint64_t size() const override { return total; }
    DiscResult<size_t> readAt(uint64_t offset, std::span<std::byte> output, CancelCheck cancel) const override
    {
        DiscResult<size_t> result;
        if (cancel && cancel()) { result.status = DiscStatus::cancelled; return result; }
        if (output.size() > maxRead)
        {
            result.status = DiscStatus::limitExceeded;
            result.error = "read request exceeds the configured limit";
            return result;
        }
        if (output.empty())
        {
            if (offset > total) { result.status = DiscStatus::ioError; result.error = "read offset is past the end"; return result; }
            result.status = DiscStatus::ready; result.value = 0; return result;
        }
        if (! checkedRange(offset, output.size(), total))
        {
            result.status = DiscStatus::ioError;
            result.error = "read range is outside the image";
            return result;
        }
        std::lock_guard lock(mutex);
        if (! stream.setPosition((juce::int64) offset)) { result.status = DiscStatus::ioError; result.error = "cannot seek"; return result; }
        const auto wanted = (int) output.size();
        int read = 0;
        while (read < wanted)
        {
            if (cancel && cancel()) { result.status = DiscStatus::cancelled; return result; }
            const int got = stream.read(output.data() + read, wanted - read);
            if (got <= 0) break;
            read += got;
        }
        if (read != wanted || stream.getStatus().failed())
        {
            result.status = DiscStatus::ioError;
            result.error = "short read from image";
            return result;
        }
        result.status = DiscStatus::ready;
        result.value = (size_t) read;
        return result;
    }
private:
    mutable juce::FileInputStream stream;
    mutable std::mutex mutex;
    uint64_t total = 0, maxRead = 0;
};
class SubrangeByteSource final : public ReadOnlyByteSource
{
public:
    SubrangeByteSource(ByteSourcePtr parentSource, uint64_t begin, uint64_t length)
        : parent(std::move(parentSource)), start(begin), total(length)
    {
    }
    uint64_t size() const override { return total; }
    DiscResult<size_t> readAt(uint64_t offset, std::span<std::byte> output, CancelCheck cancel) const override
    {
        DiscResult<size_t> result;
        if (! checkedRange(offset, output.size(), total))
        {
            result.status = DiscStatus::ioError;
            result.error = "read range is outside the subrange";
            return result;
        }
        return parent->readAt(start + offset, output, cancel);
    }
private:
    ByteSourcePtr parent;
    uint64_t start = 0, total = 0;
};
} // namespace
DiscResult<ByteSourcePtr> openRawDisc(const juce::File& file, const DiscLimits& limits)
{
    DiscResult<ByteSourcePtr> result;
    if (! file.existsAsFile())
    {
        result.status = DiscStatus::ioError;
        result.error = "image file does not exist";
        return result;
    }
    const auto size = file.getSize();
    if (size < 0)
    {
        result.status = DiscStatus::ioError;
        result.error = "image size is unavailable";
        return result;
    }
    juce::FileInputStream probe(file);
    if (! probe.openedOk())
    {
        result.status = DiscStatus::ioError;
        result.error = "image cannot be opened read-only";
        return result;
    }
    result.status = DiscStatus::ready;
    result.value = std::make_shared<FileByteSource>(file, (uint64_t) size,
                                                    juce::jmax<uint64_t>(1, limits.maxReadBytes));
    return result;
}
DiscResult<ByteSourcePtr> subrange(ByteSourcePtr parent, uint64_t offset, uint64_t length)
{
    DiscResult<ByteSourcePtr> result;
    if (parent == nullptr)
    {
        result.status = DiscStatus::ioError;
        result.error = "no parent source";
        return result;
    }
    if (! checkedRange(offset, length, parent->size()))
    {
        result.status = DiscStatus::ioError;
        result.error = "subrange is outside its parent";
        return result;
    }
    result.status = DiscStatus::ready;
    result.value = std::make_shared<SubrangeByteSource>(std::move(parent), offset, length);
    return result;
}
} // namespace sonicator
