#pragma once
#include <juce_core/juce_core.h>
namespace sonicator {
// Bounds-checked cursor over a stream. Every read is validated against the declared length;
// a short read sets ok() to false and leaves the caller free to keep what was parsed already.
class BinaryReader
{
public:
    BinaryReader(juce::InputStream& input, bool bigEndian = false)
        : stream(input), be(bigEndian) {}
    bool ok() const { return good; }
    int64_t position() const { return stream.getPosition(); }
    int64_t length() const { return stream.getTotalLength(); }
    int64_t remaining() const { return juce::jmax<int64_t>(0, length() - position()); }
    bool seek(int64_t position)
    {
        if (position < 0 || position > length()) { good = false; return false; }
        stream.setPosition(position);
        return true;
    }
    bool skip(int64_t count)
    {
        if (count < 0 || count > remaining()) { good = false; return false; }
        stream.setPosition(position() + count);
        return true;
    }
    bool read(void* destination, int count)
    {
        if (count < 0 || count > remaining()) { good = false; return false; }
        if (stream.read(destination, count) != count) { good = false; return false; }
        return true;
    }
    juce::uint8 u8()
    {
        juce::uint8 value = 0;
        return read(&value, 1) ? value : 0;
    }
    juce::uint16 u16()
    {
        juce::uint8 bytes[2] {};
        if (! read(bytes, 2)) return 0;
        return be ? (juce::uint16) ((bytes[0] << 8) | bytes[1])
                  : (juce::uint16) (bytes[0] | (bytes[1] << 8));
    }
    juce::uint32 u32()
    {
        juce::uint8 bytes[4] {};
        if (! read(bytes, 4)) return 0;
        return be ? ((juce::uint32) bytes[0] << 24) | ((juce::uint32) bytes[1] << 16)
                  | ((juce::uint32) bytes[2] << 8) | bytes[3]
                  : ((juce::uint32) bytes[3] << 24) | ((juce::uint32) bytes[2] << 16)
                  | ((juce::uint32) bytes[1] << 8) | bytes[0];
    }
    juce::uint64 u64()
    {
        juce::uint8 bytes[8] {};
        if (! read(bytes, 8)) return 0;
        juce::uint64 value = 0;
        if (be) for (int i = 0; i < 8; ++i) value = (value << 8) | bytes[i];
        else for (int i = 7; i >= 0; --i) value = (value << 8) | bytes[i];
        return value;
    }
    float f32()
    {
        const auto bits = u32();
        float value = 0;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    bool atEnd() const { return remaining() <= 0; }
private:
    juce::InputStream& stream;
    bool be = false;
    bool good = true;
};
} // namespace sonicator
