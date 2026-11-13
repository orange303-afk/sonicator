#include "WavMetadata.h"
#include "BinaryReader.h"
#include "KeyNames.h"
#include <cmath>
#include <cstring>
namespace sonicator {
namespace {
constexpr int64_t metadataCap = 8 * 1024 * 1024;
bool idIs(const char* bytes, const char* id) { return std::memcmp(bytes, id, 4) == 0; }
juce::String chunkName(const char* bytes)
{
    juce::String name;
    for (int i = 0; i < 4; ++i)
    {
        const auto c = (unsigned char) bytes[i];
        name += (c >= 32 && c < 127) ? juce::String::charToString((juce::juce_wchar) c)
                                     : juce::String::charToString('?');
    }
    return name;
}
void addRootNote(std::vector<SmartCandidate>& out, const juce::String& source,
                 int note, const juce::String& detail, bool defaultValue)
{
    SmartCandidate c;
    c.field = SmartField::RootNote; c.source = source; c.numericValue = note;
    c.status = defaultValue ? CandidateStatus::DefaultValue : CandidateStatus::Valid;
    c.detail = detail;
    out.push_back(c);
}
void addBpm(std::vector<SmartCandidate>& out, const juce::String& source,
            double bpm, const juce::String& detail)
{
    SmartCandidate c;
    c.field = SmartField::Bpm; c.source = source; c.numericValue = bpm; c.detail = detail;
    c.status = std::isfinite(bpm) && bpm >= 20.0 && bpm <= 400.0 ? CandidateStatus::Valid
                                                                 : CandidateStatus::Invalid;
    out.push_back(c);
}
juce::String decodeText(const juce::uint8* data, int size)
{
    while (size > 0 && data[size - 1] == 0) --size;
    return juce::String::fromUTF8((const char*) data, size).trim();
}
void parseInfo(const std::vector<juce::uint8>& payload, std::vector<SmartCandidate>& out)
{
    if (payload.size() < 4 || std::memcmp(payload.data(), "INFO", 4) != 0) return;
    size_t p = 4;
    while (p + 8 <= payload.size())
    {
        const char* id = (const char*) payload.data() + p;
        const auto size = (juce::uint32) payload[p + 4] | ((juce::uint32) payload[p + 5] << 8)
                        | ((juce::uint32) payload[p + 6] << 16) | ((juce::uint32) payload[p + 7] << 24);
        const size_t body = p + 8;
        if (body + size > payload.size()) break;
        if (idIs(id, "IKEY"))
        {
            const auto raw = decodeText(payload.data() + body, (int) size);
            SmartCandidate c; c.field = SmartField::Key; c.source = "info";
            c.detail = "IKEY=" + raw;
            if (auto key = parseKeyValue(raw))
            {
                c.textValue = key->canonical;
                c.status = CandidateStatus::Valid;
            }
            else
            {
                // Keywords are common in IKEY; keep the raw value as provenance, never as a key.
                c.status = CandidateStatus::Invalid;
            }
            out.push_back(c);
        }
        p = body + size + (size & 1);
    }
}
juce::String utf16(const juce::uint8* data, int size, bool bigEndian)
{
    juce::String text;
    for (int i = 0; i + 1 < size; i += 2)
    {
        juce::uint32 unit = bigEndian ? (juce::uint32) ((data[i] << 8) | data[i + 1])
                                      : (juce::uint32) (data[i] | (data[i + 1] << 8));
        if (unit == 0) break;
        if (unit >= 0xd800 && unit <= 0xdbff && i + 3 < size)
        {
            const juce::uint32 low = bigEndian ? (juce::uint32) ((data[i + 2] << 8) | data[i + 3])
                                               : (juce::uint32) (data[i + 2] | (data[i + 3] << 8));
            if (low >= 0xdc00 && low <= 0xdfff)
            {
                unit = 0x10000 + ((unit - 0xd800) << 10) + (low - 0xdc00);
                i += 2;
            }
        }
        text += juce::String::charToString((juce::juce_wchar) unit);
    }
    return text;
}
juce::String id3Text(const juce::uint8* frame, int size)
{
    if (size < 2) return {};
    const int encoding = frame[0];
    const auto* body = frame + 1;
    int length = size - 1;
    while (length > 0 && body[length - 1] == 0) --length;
    juce::String text;
    if (encoding == 1) text = utf16(body, length, false);
    else if (encoding == 2) text = utf16(body, length, true);
    else if (encoding == 3) text = juce::String::fromUTF8((const char*) body, length);
    else text = juce::String::fromUTF8((const char*) body, length);
    return text.trim();
}
void parseId3(const std::vector<juce::uint8>& payload, std::vector<SmartCandidate>& out)
{
    if (payload.size() < 10 || std::memcmp(payload.data(), "ID3", 3) != 0) return;
    const int version = payload[3];
    if (version < 3) return; // 2.2 uses 3-byte frames; out of scope, never guessed.
    const size_t tagSize = ((payload[6] & 0x7f) << 21) | ((payload[7] & 0x7f) << 14)
                         | ((payload[8] & 0x7f) << 7) | (payload[9] & 0x7f);
    size_t p = 10;
    const size_t end = juce::jmin(payload.size(), 10 + tagSize);
    while (p + 10 <= end)
    {
        const char* id = (const char*) payload.data() + p;
        size_t frameSize = 0;
        if (version == 4)
            frameSize = ((payload[p + 4] << 21) | (payload[p + 5] << 14)
                       | (payload[p + 6] << 7) | payload[p + 7]);
        else
            frameSize = (payload[p + 4] << 24) | (payload[p + 5] << 16)
                      | (payload[p + 6] << 8) | payload[p + 7];
        p += 10;
        if (frameSize == 0 || p + frameSize > end) break;
        const auto text = id3Text(payload.data() + p, (int) frameSize);
        p += frameSize;
        if (idIs(id, "TBPM"))
        {
            addBpm(out, "id3", text.getDoubleValue(), "TBPM=" + text);
        }
        else if (idIs(id, "TKEY"))
        {
            SmartCandidate c; c.field = SmartField::Key; c.source = "id3"; c.detail = "TKEY=" + text;
            if (auto key = parseKeyValue(text)) { c.textValue = key->canonical; c.status = CandidateStatus::Valid; }
            else c.status = CandidateStatus::Invalid;
            out.push_back(c);
        }
    }
}
} // namespace
WavMetadataResult extractWavMetadata(const juce::File& file)
{
    WavMetadataResult result;
    juce::FileInputStream stream(file);
    if (! stream.openedOk()) { result.error = "Cannot open file"; return result; }
    BinaryReader reader(stream, false);
    char header[12] {};
    if (! reader.read(header, 12)) { result.error = "File is shorter than a RIFF header"; return result; }
    const bool isRiff = idIs(header, "RIFF"), isRf64 = idIs(header, "RF64");
    if (! isRiff && ! isRf64) { result.error = "Not a RIFF/WAVE file"; return result; }
    if (! idIs(header + 8, "WAVE")) { result.error = "RIFF container is not WAVE"; return result; }
    if (isRf64)
    {
        result.info.rf64 = true;
        result.error = "RF64 is not supported";
        return result;
    }
    const auto total = reader.length();
    int64_t position = 12;
    int guard = 0;
    while (position + 8 <= total && ++guard < 100000)
    {
        if (! reader.seek(position)) { result.error = "Seek failed"; break; }
        char id[4] {};
        if (! reader.read(id, 4)) { result.error = "Truncated chunk header"; break; }
        const auto size = reader.u32();
        const int64_t body = position + 8;
        if ((int64_t) size > total - body)
        {
            result.error = "Chunk '" + chunkName(id) + "' claims more bytes than the file contains";
            break;
        }
        const int64_t padded = body + (int64_t) size + (size & 1);
        if (idIs(id, "data"))
        {
            if (result.info.channels > 0 && result.info.bitsPerSample > 0)
            {
                const auto block = (int64_t) result.info.channels
                                 * juce::jmax(1, (result.info.bitsPerSample + 7) / 8);
                if (block > 0) result.info.frames = (int64_t) size / block;
            }
            position = padded;
            continue;
        }
        if ((int64_t) size > metadataCap) { position = padded; continue; }
        std::vector<juce::uint8> payload((size_t) size);
        if (size > 0 && ! reader.read(payload.data(), (int) size))
        {
            result.error = "Truncated metadata chunk '" + chunkName(id) + "'";
            break;
        }
        if (idIs(id, "fmt ") && payload.size() >= 16)
        {
            auto le16 = [&](size_t o) { return (int) (payload[o] | (payload[o + 1] << 8)); };
            auto le32 = [&](size_t o) { return (juce::uint32) payload[o] | ((juce::uint32) payload[o + 1] << 8)
                                              | ((juce::uint32) payload[o + 2] << 16) | ((juce::uint32) payload[o + 3] << 24); };
            result.info.channels = le16(2);
            result.info.sampleRate = (double) le32(4);
            result.info.bitsPerSample = le16(14);
        }
        else if (idIs(id, "smpl") && payload.size() >= 36)
        {
            auto le32 = [&](size_t o) { return (juce::uint32) payload[o] | ((juce::uint32) payload[o + 1] << 8)
                                              | ((juce::uint32) payload[o + 2] << 16) | ((juce::uint32) payload[o + 3] << 24); };
            const int unity = (int) le32(12);
            const auto loops = le32(28);
            if (unity >= 0 && unity <= 127)
                addRootNote(result.candidates, "wav_smpl", unity,
                            "smpl:unity=" + juce::String(unity) + ",loops=" + juce::String((int) loops),
                            unity == 60);
            else
            {
                SmartCandidate c; c.field = SmartField::RootNote; c.source = "wav_smpl";
                c.status = CandidateStatus::Invalid;
                c.detail = "smpl:unity=" + juce::String(unity);
                result.candidates.push_back(c);
            }
        }
        else if (idIs(id, "inst") && payload.size() >= 7)
        {
            addRootNote(result.candidates, "wav_inst", (int) payload[0],
                        "inst:baseNote=" + juce::String((int) payload[0]), payload[0] == 60);
        }
        else if (idIs(id, "acid") && payload.size() >= 24)
        {
            auto le16 = [&](size_t o) { return (int) (payload[o] | (payload[o + 1] << 8)); };
            auto le32 = [&](size_t o) { return (juce::uint32) payload[o] | ((juce::uint32) payload[o + 1] << 8)
                                              | ((juce::uint32) payload[o + 2] << 16) | ((juce::uint32) payload[o + 3] << 24); };
            const juce::uint32 tempoBits = le32(20);
            float tempo = 0;
            std::memcpy(&tempo, &tempoBits, sizeof(tempo));
            const int root = le16(4);
            addBpm(result.candidates, "wav_acid", tempo,
                   "acid:tempo=" + juce::String(tempo, 2) + ",beats=" + juce::String((int) le32(12))
                   + "," + juce::String(le16(16)) + "/" + juce::String(le16(18)));
            // The root only means something when the root-set flag is on.
            if ((le16(0) & 0x02) != 0)
                addRootNote(result.candidates, "wav_acid", root, "acid:root=" + juce::String(root),
                            root == 60);
        }
        else if (idIs(id, "LIST"))
        {
            parseInfo(payload, result.candidates);
        }
        else if (idIs(id, "id3 ") || idIs(id, "ID3 "))
        {
            parseId3(payload, result.candidates);
        }
        position = padded;
    }
    return result;
}
} // namespace sonicator
