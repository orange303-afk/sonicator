#pragma once
#include <juce_core/juce_core.h>
#include <cstdint>
#include <vector>
namespace sonicator {
enum class TrackerFormat { mod, xm, s3m, xi };
juce::String trackerFormatName(TrackerFormat);

// How one decoded source is stored inside the container. The encoding decides both the numeric
// conversion and whether a per-sample accumulate step is needed (XM delta encoding).
enum class TrackerPcmEncoding
{
    signedInt8, unsignedInt8, signedInt16LE, unsignedInt16LE, deltaInt8, deltaInt16LE
};

struct TrackerSampleSlot
{
    int slot = 0;                       // 0-based index in the document, stable for this generation
    juce::String name;
    juce::String displayText;            // original module text, including ASCII-art spacing
    bool empty = true;                  // slot exists but has no audio source
    bool fmInstrument = false;          // AdLib/OPL instrument: no PCM can be extracted
    TrackerPcmEncoding encoding = TrackerPcmEncoding::signedInt8;
    int64_t pcmOffset = 0;              // byte offset of the first source byte
    int64_t frames = 0;                 // decoded frames (never bytes)
    int channels = 1;
    int bitsPerSample = 8;
    int sampleRate = 0;                 // rate at MIDI note 60; the source has no other "true" rate
    int rootKey = 60;                   // MIDI note that plays the 8363 Hz reference rate
    int64_t loopStart = 0;              // frames
    int64_t loopEnd = 0;                // frames, exclusive
    bool looping = false, pingPong = false;
    juce::String error;                 // non-empty means this slot cannot be decoded
};

struct TrackerDocumentInfo
{
    TrackerFormat format = TrackerFormat::mod;
    juce::String formatName;            // "MOD 31-sample", "XM", "S3M"
    juce::String title;
    int channels = 0;                   // 0 when the container does not state it
    int patterns = 0, orders = 0;
    int sampleSlots = 0, instruments = 0;
    juce::StringArray warnings;
};

// All values the three readers need to publish one tracker document.
struct TrackerDocumentData
{
    TrackerDocumentInfo info;
    std::vector<TrackerSampleSlot> slots;
    std::vector<uint8_t> bytes;         // the unmodified source, kept for lazy PCM decoding
};

// Bounded, overflow-checked little/big endian cursor. A failed read never advances and sets
// failed, so a parser can return early without trusting partial values.
class TrackerBytes
{
public:
    TrackerBytes(const uint8_t* data, size_t size) : source(data), length(size) {}
    size_t size() const { return length; }
    size_t tell() const { return position; }
    bool failed = false;
    bool has(size_t n) const { return position <= length && n <= length - position; }
    bool seek(size_t p) { if (p > length) { failed = true; return false; } position = p; return true; }
    bool skip(size_t n) { if (! has(n)) { failed = true; return false; } position += n; return true; }
    const uint8_t* at(size_t p) const { return p <= length ? source + p : nullptr; }
    uint8_t u8()
    {
        if (! has(1)) { failed = true; return 0; }
        return source[position++];
    }
    int8_t i8() { return (int8_t) u8(); }
    uint16_t u16le()
    {
        if (! has(2)) { failed = true; return 0; }
        const auto v = (uint16_t) (source[position] | ((uint16_t) source[position + 1] << 8));
        position += 2;
        return v;
    }
    uint16_t u16be()
    {
        if (! has(2)) { failed = true; return 0; }
        const auto v = (uint16_t) (((uint16_t) source[position] << 8) | source[position + 1]);
        position += 2;
        return v;
    }
    uint32_t u32le()
    {
        if (! has(4)) { failed = true; return 0; }
        const auto v = (uint32_t) source[position] | ((uint32_t) source[position + 1] << 8)
                     | ((uint32_t) source[position + 2] << 16) | ((uint32_t) source[position + 3] << 24);
        position += 4;
        return v;
    }
    uint32_t u32be()
    {
        if (! has(4)) { failed = true; return 0; }
        const auto v = ((uint32_t) source[position] << 24) | ((uint32_t) source[position + 1] << 16)
                     | ((uint32_t) source[position + 2] << 8) | (uint32_t) source[position + 3];
        position += 4;
        return v;
    }
    // Reads a fixed-size, NUL-padded text field. Non-ASCII bytes are kept as Latin-1 so old
    // DOS/Amiga sample names stay readable instead of disappearing.
    juce::String text(size_t n, bool preserveWhitespace = false);
private:
    const uint8_t* source;
    size_t length, position = 0;
};

bool readWholeFile(const juce::File&, int64_t maxBytes, std::vector<uint8_t>& out,
                   juce::String& error);
} // namespace sonicator
