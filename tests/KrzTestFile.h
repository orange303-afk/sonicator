#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <cstdint>
#include <vector>
// A test-only writer for the Kurzweil object-bank layout: the 32-byte PRAM header with the
// absolute PCM offset, a run of blocks with negative lengths, the object framing
// (hash, body size, name offset, padded name), type-specific bodies, and a big-endian PCM region.
namespace krztest {
struct HeaderSpec
{
    int rootKey = 60;
    bool looping = false;
    int volumeHalfDb = 0;
    uint32_t startWord = 0, loopWord = 0, endWord = 0;
    uint32_t period = 40000; // 25000 Hz
};
struct SampleSpec
{
    int id = 200;
    juce::String name = "Sample";
    bool stereo = false;
    std::vector<HeaderSpec> headers { HeaderSpec {} };
};
struct EntrySpec
{
    int sampleId = 0;
    int subSample = 1;
    int tuningCents = 0;
    int volumeHalfDb = 0;
};
struct KeymapSpec
{
    int id = 200;
    juce::String name = "Keymap";
    int method = 0x03;
    int entrySize = 3;
    int defaultSampleId = 0;
    // Index 0 is MIDI note 12 (C0); notes below 12 cannot be mapped.
    std::array<std::vector<EntrySpec>, 8> levels;
};
struct LayerSpec
{
    int keymap1 = 0, keymap2 = 0;
};
struct ProgramSpec
{
    int id = 200;
    juce::String name = "Program";
    std::vector<LayerSpec> layers { LayerSpec {} };
};
struct BankSpec
{
    std::vector<SampleSpec> samples;
    std::vector<KeymapSpec> keymaps;
    std::vector<ProgramSpec> programs;
    std::vector<int16_t> pcm;
};

namespace detail {
inline void u8(std::vector<std::byte>& b, int v) { b.push_back((std::byte) (v & 0xff)); }
inline void u16(std::vector<std::byte>& b, int v) { u8(b, v >> 8); u8(b, v); }
inline void u32(std::vector<std::byte>& b, uint32_t v)
{
    for (int i = 3; i >= 0; --i) u8(b, (int) ((v >> (8 * i)) & 0xff));
}
inline void s32(std::vector<std::byte>& b, int32_t v) { u32(b, (uint32_t) v); }
inline void patch32(std::vector<std::byte>& b, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; ++i) b[at + (size_t) i] = (std::byte) ((v >> (8 * (3 - i))) & 0xff);
}
inline void ascii(std::vector<std::byte>& b, const juce::String& s)
{
    for (auto c : s) u8(b, (int) (unsigned char) c);
}

// The object framing: hash, body size, name offset, name, pad. Body size is measured from the size
// field, so the next object starts at objectStart + 6 + bodySize.
inline void objectHeader(std::vector<std::byte>& out, int type, int id, const juce::String& name,
                         const std::vector<std::byte>& body)
{
    const int nameLength = name.length();
    // Content starts right after the block length: hash, body size, name offset, name, pad, body.
    const int bodyOffset = nameLength % 2 == 0 ? 8 + nameLength : 7 + nameLength;
    u16(out, (type << 10) | (id & 0x3ff));
    u16(out, bodyOffset + (int) body.size());
    u16(out, bodyOffset - 4);
    ascii(out, name);
    u8(out, 0);
    while ((int) out.size() < bodyOffset) u8(out, 0);
    out.insert(out.end(), body.begin(), body.end());
}

inline std::vector<std::byte> sampleBody(const SampleSpec& s)
{
    std::vector<std::byte> body;
    u16(body, 1);
    u16(body, (int) s.headers.size() - 1);
    u16(body, 8);
    u8(body, s.stereo ? 1 : 0);
    for (int i = 0; i < 5; ++i) u8(body, 0);
    for (const auto& h : s.headers)
    {
        u8(body, h.rootKey);
        u8(body, h.looping ? 0x70 : 0xf0);
        u8(body, h.volumeHalfDb);
        u8(body, 0);
        u16(body, 0);
        u16(body, 0);
        u32(body, h.startWord);
        u32(body, h.startWord);
        u32(body, h.loopWord);
        u32(body, h.endWord);
        u16(body, 0);
        u16(body, 0);
        u32(body, h.period);
    }
    return body;
}

inline int tuneWidth(int method)
{
    if (method & 0x10) return 2;
    if (method == 0x09 || method == 0x0b || method == 0x0d || method == 0x0f) return 1;
    return 0;
}
inline bool hasVolume(int method)
{
    return method == 0x05 || method == 0x07 || method == 0x0d || method == 0x0f
        || method == 0x15 || method == 0x17;
}
inline bool hasSampleId(int method)
{
    return method == 0x03 || method == 0x07 || method == 0x0b || method == 0x0f
        || method == 0x13 || method == 0x17;
}

inline std::vector<std::byte> encodeEntry(int method, int entrySize, const EntrySpec& e)
{
    std::vector<std::byte> out;
    const int tune = tuneWidth(method);
    if (tune == 1) u8(out, e.tuningCents & 0xff);
    else if (tune == 2) { u8(out, (e.tuningCents >> 8) & 0xff); u8(out, e.tuningCents & 0xff); }
    if (hasVolume(method)) u8(out, e.volumeHalfDb & 0xff);
    if (hasSampleId(method)) u16(out, e.sampleId);
    u8(out, e.subSample);
    while ((int) out.size() < entrySize) u8(out, 0);
    return out;
}

inline std::vector<std::byte> keymapBody(const KeymapSpec& k)
{
    const int entrySize = k.entrySize > 0 ? k.entrySize : 3;
    const int perLevel = 128 * entrySize;
    std::vector<std::byte> body;
    u16(body, k.defaultSampleId);
    u16(body, k.method);
    u16(body, 0);
    u16(body, 100);
    u16(body, 127);
    u16(body, entrySize);
    for (int level = 0; level < 8; ++level)
    {
        const int at = 28 + level * perLevel - (12 + 2 * level);
        u16(body, at);
    }
    for (int level = 0; level < 8; ++level)
    {
        const auto& entries = k.levels[(size_t) level];
        for (int slot = 0; slot < 128; ++slot)
        {
            EntrySpec e;
            if (slot < (int) entries.size()) e = entries[(size_t) slot];
            auto encoded = encodeEntry(k.method, entrySize, e);
            body.insert(body.end(), encoded.begin(), encoded.end());
        }
    }
    return body;
}

inline std::vector<std::byte> programBody(const ProgramSpec& p)
{
    std::vector<std::byte> body;
    u8(body, 0x08);
    u8(body, 2);
    u8(body, (int) p.layers.size());
    for (int i = 0; i < 13; ++i) u8(body, 0);
    u8(body, 0x0f);
    for (int i = 0; i < 7; ++i) u8(body, 0);
    for (const auto& layer : p.layers)
    {
        u8(body, 0x09);
        for (int i = 0; i < 15; ++i) u8(body, 0);
        u8(body, 0x40);
        u16(body, 0);                    // [0,1]
        for (int i = 0; i < 5; ++i) u8(body, 0); // [2..6]
        u16(body, layer.keymap2);        // [7,8]
        u16(body, 0);                    // [9,10]
        u16(body, layer.keymap1);        // [11,12]
        for (int i = 0; i < 18; ++i) u8(body, 0);
    }
    u16(body, 0);
    return body;
}

inline void appendBlock(std::vector<std::byte>& file, const std::vector<std::byte>& content)
{
    std::vector<std::byte> block;
    u32(block, 0);
    block.insert(block.end(), content.begin(), content.end());
    while (block.size() % 4 != 0) u8(block, 0);
    patch32(block, 0, (uint32_t) (int32_t) -((int32_t) block.size()));
    file.insert(file.end(), block.begin(), block.end());
}
} // namespace detail

// A complete bank. `pcm` is written big-endian from the declared PCM offset to the end of file.
inline std::vector<std::byte> makeBank(const BankSpec& spec)
{
    using namespace detail;
    std::vector<std::byte> objects;
    for (const auto& s : spec.samples)
    {
        std::vector<std::byte> body;
        objectHeader(body, 38, s.id, s.name, sampleBody(s));
        appendBlock(objects, body);
    }
    for (const auto& k : spec.keymaps)
    {
        std::vector<std::byte> body;
        objectHeader(body, 37, k.id, k.name, keymapBody(k));
        appendBlock(objects, body);
    }
    for (const auto& p : spec.programs)
    {
        std::vector<std::byte> body;
        objectHeader(body, 36, p.id, p.name, programBody(p));
        appendBlock(objects, body);
    }
    std::vector<std::byte> file;
    ascii(file, "PRAM");
    u32(file, 0);            // PCM offset, patched below
    u32(file, 0);
    u32(file, 208);
    for (int i = 0; i < 16; ++i) u8(file, 0);
    file.insert(file.end(), objects.begin(), objects.end());
    u32(file, 0);            // end of the object section
    patch32(file, 4, (uint32_t) file.size());
    for (auto word : spec.pcm) u16(file, (uint16_t) word);
    return file;
}

// A continuation part (`.kr2.krz`): a header that copies the first part's PCM offset, then sample
// data from byte 0x24.
inline std::vector<std::byte> makeContinuation(uint32_t copiedPcmOffset, const std::vector<int16_t>& pcm)
{
    using namespace detail;
    std::vector<std::byte> file;
    ascii(file, "PRAM");
    u32(file, copiedPcmOffset);
    u32(file, 0);
    u32(file, 208);
    for (int i = 0; i < 16; ++i) u8(file, 0);
    u32(file, 0);            // end of the object section, immediately: PCM starts at 0x24
    for (auto word : pcm) u16(file, (uint16_t) word);
    return file;
}

inline juce::File writeBytes(const juce::File& dir, const juce::String& name,
                             const std::vector<std::byte>& bytes)
{
    auto file = dir.getChildFile(name);
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    file.replaceWithData(bytes.data(), bytes.size());
    return file;
}

inline juce::File writeBank(const juce::File& dir, const juce::String& name, const BankSpec& spec)
{
    return writeBytes(dir, name, makeBank(spec));
}
} // namespace krztest
