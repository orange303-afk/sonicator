#pragma once
#include <juce_core/juce_core.h>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>
// A test-only writer for the EBL layout: FORM/TOC2, the first E5S1 section
// (with the absolute offset of the second), the 14-byte second section, the
// 176-byte data-description block, a pad, the PCM, and an optional EXLZ trailer.
namespace ebltest {
struct EblOptions
{
    juce::String name = "Sample";
    uint32_t rate = 47360;
    int channelByte = 1;
    int pad = 8;
    std::vector<int16_t> left;
    std::vector<int16_t> right;
    std::optional<std::pair<uint32_t, uint32_t>> loop;
    // Writes two extra pad bytes, so the front anchor lands two bytes into the
    // first left sample while the true audio start is anchored from the end.
    bool stereoFrontSkew = false;
    // Keeps only this many bytes of the audio, as a truncated rip would.
    int truncateAfterAudioBytes = -1;
};

inline std::vector<std::byte> makeEbl(const EblOptions& o)
{
    std::vector<std::byte> b;
    auto u8 = [&b] (int v) { b.push_back((std::byte) (v & 0xff)); };
    auto u16le = [&u8] (int v) { u8(v); u8(v >> 8); };
    auto u32le = [&u8] (uint32_t v) { for (int i = 0; i < 4; ++i) u8((int) ((v >> (8 * i)) & 0xff)); };
    auto u32be = [&u8] (uint32_t v) { for (int i = 3; i >= 0; --i) u8((int) ((v >> (8 * i)) & 0xff)); };
    auto ascii = [&u8] (const char* s) { for (const char* p = s; *p != 0; ++p) u8(*p); };
    auto name64 = [&] {
        const size_t start = b.size();
        for (auto c : o.name) u16le((int) ((uint32_t) c & 0xffffu));
        while (b.size() < start + 64) { u8(' '); u8(0); }
    };
    ascii("FORM"); u32be(0);
    ascii("E5B0TOC2"); u32be(78);
    ascii("E5S1"); u32be(0); u32be(98);
    u8(0); u8(0);
    name64();
    while (b.size() < 98) u8(0);
    ascii("E5S1"); u32be(0);
    for (int i = 0; i < 6; ++i) u8(0);
    name64();
    u32le(0);                             // V1
    const uint32_t perChannel = (uint32_t) (o.left.size() * 2);
    u32le(180u + (uint32_t) o.pad);       // V2, audio anchor
    u32le(180u + (uint32_t) o.pad + perChannel); // V3, per-channel length
    for (int i = 0; i < 6; ++i) u32le(0); // V4..V9
    u32le(o.rate);                        // V10
    u32le(0);                             // V11
    u32le(((uint32_t) o.channelByte << 16) | (0x02u << 8) | 1u); // V12
    for (int i = 0; i < 64; ++i) u8(0);   // comment
    const size_t padWritten = (size_t) o.pad + (o.stereoFrontSkew && o.channelByte == 3 ? 2u : 0u);
    const size_t audio = b.size() + padWritten;
    for (size_t i = 0; i < padWritten; ++i) u8(0);
    for (auto v : o.left) u16le(v);
    if (o.channelByte == 3)
        for (auto v : o.right) u16le(v);
    if (o.loop)
    {
        ascii("EXLZ"); u32le(0x20);
        ascii("INFO"); u32le(8); u32le(1); u32le(1);
        ascii("MARK"); u32le(8); u32le(o.loop->first); u32le(o.loop->second);
    }
    if (o.truncateAfterAudioBytes >= 0)
        b.resize(audio + (size_t) o.truncateAfterAudioBytes);
    const uint32_t formSize = (uint32_t) (b.size() - 8);
    for (int i = 0; i < 4; ++i) b[4 + i] = (std::byte) ((formSize >> (8 * (3 - i))) & 0xff);
    return b;
}

inline std::vector<int16_t> interleaved(const std::vector<int16_t>& left, const std::vector<int16_t>& right)
{
    std::vector<int16_t> out;
    for (size_t i = 0; i < left.size(); ++i) { out.push_back(left[i]); out.push_back(right[i]); }
    return out;
}

inline EblOptions monoOptions()
{
    EblOptions o;
    o.name = "org nod C +36";
    o.channelByte = 1;
    o.pad = 8;
    o.left = { 0, 1, -1, 32767, -32768, 100, -100, 1234 };
    return o;
}

inline EblOptions stereoOptions()
{
    EblOptions o;
    o.name = "Stereo Pair";
    o.channelByte = 3;
    o.pad = 8;
    o.left = { 10, 20, 30, 40 };
    o.right = { -10, -20, -30, -40 };
    return o;
}
} // namespace ebltest
