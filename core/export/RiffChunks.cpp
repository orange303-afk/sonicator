#include "RiffChunks.h"
#include <cstring>
#include <vector>

namespace sonicator::riff
{
namespace
{
void put16(juce::uint8* data, size_t offset, juce::uint16 value)
{
    data[offset] = (juce::uint8) (value & 0xff);
    data[offset + 1] = (juce::uint8) ((value >> 8) & 0xff);
}

void put32(juce::uint8* data, size_t offset, juce::uint32 value)
{
    data[offset] = (juce::uint8) (value & 0xff);
    data[offset + 1] = (juce::uint8) ((value >> 8) & 0xff);
    data[offset + 2] = (juce::uint8) ((value >> 16) & 0xff);
    data[offset + 3] = (juce::uint8) ((value >> 24) & 0xff);
}

juce::uint32 read32(const juce::uint8* data, size_t offset)
{
    return (juce::uint32) data[offset] | ((juce::uint32) data[offset + 1] << 8)
        | ((juce::uint32) data[offset + 2] << 16) | ((juce::uint32) data[offset + 3] << 24);
}

bool rewrite(const juce::File& wav, const char* id, const juce::MemoryBlock* payload)
{
    juce::MemoryBlock data;
    if (! wav.loadFileAsData(data)) return false;
    const auto* bytes = static_cast<const juce::uint8*>(data.getData());
    if (data.getSize() < 12) return false;
    if (juce::String::fromUTF8((const char*) bytes, 4) != "RIFF") return false;
    if (juce::String::fromUTF8((const char*) bytes + 8, 4) != "WAVE") return false;
    const juce::String target(id, 4);

    juce::MemoryBlock out;
    out.append("RIFF", 4);
    const juce::uint32 placeholder = 0;
    out.append(&placeholder, 4);
    out.append("WAVE", 4);
    size_t offset = 12;
    while (offset + 8 <= data.getSize())
    {
        const juce::String chunkId((const char*) bytes + offset, 4);
        const auto size = (size_t) read32(bytes, offset + 4);
        if (offset + 8 + size > data.getSize()) return false;
        const auto padded = size + (size & 1u);
        if (offset + 8 + padded > data.getSize()) return false;
        if (chunkId != target)
        {
            out.append(bytes + offset, 8 + padded);
        }
        offset += 8 + padded;
    }
    if (offset != data.getSize()) return false;
    if (payload != nullptr)
    {
        char header[8] {};
        std::memcpy(header, id, 4);
        const auto size = (juce::uint32) payload->getSize();
        std::memcpy(header + 4, &size, 4);
        out.append(header, 8);
        out.append(payload->getData(), payload->getSize());
        if (payload->getSize() & 1u)
        {
            const char pad = 0;
            out.append(&pad, 1);
        }
    }
    const auto riffSize = (juce::uint32) (out.getSize() - 8);
    std::memcpy(static_cast<juce::uint8*>(out.getData()) + 4, &riffSize, 4);
    return wav.replaceWithData(out.getData(), out.getSize());
}
} // namespace

bool insertOrReplaceChunk(const juce::File& wav, const char* id, const juce::MemoryBlock& payload)
{
    return rewrite(wav, id, &payload);
}

bool removeChunk(const juce::File& wav, const char* id)
{
    return rewrite(wav, id, nullptr);
}

juce::MemoryBlock buildAcid(const double bpm, const int beats, const int rootMidi)
{
    juce::MemoryBlock payload(24, true);
    auto* bytes = static_cast<juce::uint8*>(payload.getData());
    put32(bytes, 0, rootMidi >= 0 ? 0x02u : 0x00u);   // root bit
    put16(bytes, 4, (juce::uint16) juce::jmax(0, rootMidi));
    put32(bytes, 12, (juce::uint32) juce::jmax(0, beats));
    put16(bytes, 16, 4);
    put16(bytes, 18, 4);
    const auto tempo = (float) bpm;
    juce::uint32 bits = 0;
    std::memcpy(&bits, &tempo, sizeof(bits));
    put32(bytes, 20, bits);
    return payload;
}

juce::MemoryBlock buildSmplRoot(const int rootMidi, const juce::MemoryBlock& existing)
{
    juce::MemoryBlock payload;
    if (existing.getSize() >= 36) payload = existing;
    else payload.setSize(36, true);
    put32(static_cast<juce::uint8*>(payload.getData()), 12, (juce::uint32) juce::jmax(0, rootMidi));
    return payload;
}
} // namespace sonicator::riff
