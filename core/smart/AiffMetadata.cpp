#include "AiffMetadata.h"
#include "BinaryReader.h"
#include <cmath>
#include <cstring>
namespace sonicator {
namespace {
constexpr int64_t metadataCap = 4 * 1024 * 1024;
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
double extended80(const juce::uint8* data, int size)
{
    if (size < 10) return 0;
    const int exponent = (data[0] << 8) | data[1];
    juce::uint64 mantissa = 0;
    for (int i = 2; i < 10; ++i) mantissa = (mantissa << 8) | data[i];
    if (exponent == 0 && mantissa == 0) return 0;
    const bool negative = (exponent & 0x8000) != 0;
    const int exponentValue = exponent & 0x7fff;
    const double value = (double) mantissa / std::ldexp(1.0, 63) * std::ldexp(1.0, exponentValue - 16383);
    return negative ? -value : value;
}
std::vector<juce::String> cStrings(const std::vector<juce::uint8>& payload)
{
    std::vector<juce::String> out;
    size_t start = 0;
    for (size_t i = 0; i <= payload.size(); ++i)
    {
        if (i == payload.size() || payload[i] == 0)
        {
            if (i > start)
            {
                auto text = juce::String::fromUTF8((const char*) payload.data() + start, (int) (i - start)).trim();
                if (text.isNotEmpty()) out.push_back(text);
            }
            start = i + 1;
        }
    }
    return out;
}
juce::String normalizedCategory(const juce::String& value)
{
    const auto lower = value.toLowerCase().trim();
    if (lower.contains("drum") || lower.contains("beat")) return "percussion";
    if (lower.contains("bass")) return "bass";
    if (lower.contains("vocal") || lower.contains("voice")) return "vocal";
    if (lower.contains("guitar")) return "guitar";
    if (lower.contains("key") || lower.contains("piano") || lower.contains("organ")) return "keys";
    if (lower.contains("synth")) return "synth";
    if (lower.contains("string")) return "strings";
    if (lower.contains("brass")) return "brass";
    if (lower.contains("fx") || lower.contains("effect")) return "fx";
    return {};
}
} // namespace
AiffMetadataResult extractAiffMetadata(const juce::File& file)
{
    AiffMetadataResult result;
    juce::FileInputStream stream(file);
    if (! stream.openedOk()) { result.error = "Cannot open file"; return result; }
    BinaryReader reader(stream, true);
    char header[12] {};
    if (! reader.read(header, 12)) { result.error = "File is shorter than a FORM header"; return result; }
    if (! idIs(header, "FORM")) { result.error = "Not a FORM/AIFF file"; return result; }
    if (! idIs(header + 8, "AIFF") && ! idIs(header + 8, "AIFC"))
    {
        result.error = "FORM container is not AIFF/AIFC";
        return result;
    }
    const auto total = reader.length();
    int64_t position = 12;
    int guard = 0;
    struct Basc { bool present = false; juce::uint32 beats = 0; int root = 0; int scale = -1; };
    Basc basc;
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
        if (idIs(id, "SSND")) { position = padded; continue; }
        if ((int64_t) size > metadataCap) { position = padded; continue; }
        std::vector<juce::uint8> payload((size_t) size);
        if (size > 0 && ! reader.read(payload.data(), (int) size))
        {
            result.error = "Truncated metadata chunk '" + chunkName(id) + "'";
            break;
        }
        auto be16 = [&](size_t o) { return payload.size() >= o + 2 ? (int) ((payload[o] << 8) | payload[o + 1]) : 0; };
        auto be32 = [&](size_t o) { return payload.size() >= o + 4
            ? ((juce::uint32) payload[o] << 24) | ((juce::uint32) payload[o + 1] << 16)
            | ((juce::uint32) payload[o + 2] << 8) | payload[o + 3] : 0u; };
        if (idIs(id, "COMM") && payload.size() >= 18)
        {
            result.info.channels = be16(0);
            result.info.frames = (int64_t) be32(2);
            result.info.bitsPerSample = be16(6);
            result.info.sampleRate = extended80(payload.data() + 8, (int) payload.size() - 8);
            if (result.info.sampleRate > 0 && result.info.frames > 0)
                result.info.duration = (double) result.info.frames / result.info.sampleRate;
        }
        else if (idIs(id, "basc") && payload.size() >= 18)
        {
            const auto version = be32(0);
            if (version != 1)
            {
                SmartCandidate c; c.field = SmartField::Bpm; c.source = "aiff_basc";
                c.status = CandidateStatus::Unsupported;
                c.detail = "basc:version=" + juce::String(version);
                result.candidates.push_back(c);
            }
            else
            {
                basc.present = true;
                basc.beats = be32(4);
                basc.root = be16(8);
                basc.scale = be16(10);
                if (basc.root >= 1 && basc.root <= 127)
                {
                    SmartCandidate c; c.field = SmartField::RootNote; c.source = "aiff_basc";
                    c.numericValue = basc.root; c.status = CandidateStatus::Valid;
                    c.detail = "basc:root=" + juce::String(basc.root)
                             + ",scale=" + juce::String(basc.scale)
                             + "," + juce::String(be16(12)) + "/" + juce::String(be16(14));
                    result.candidates.push_back(c);
                }
            }
        }
        else if (idIs(id, "cate"))
        {
            const auto strings = cStrings(payload);
            if (! strings.empty())
            {
                auto type = normalizedCategory(strings.front());
                if (type.isNotEmpty())
                {
                    SmartCandidate c; c.field = SmartField::Type; c.source = "aiff_cate";
                    c.textValue = type; c.detail = strings.front();
                    result.candidates.push_back(c);
                }
            }
        }
        else if (idIs(id, "INST") && payload.size() >= 20)
        {
            const int baseNote = (int) payload[8];
            if (baseNote >= 0 && baseNote <= 127)
            {
                SmartCandidate c; c.field = SmartField::RootNote; c.source = "aiff_inst";
                c.numericValue = baseNote; c.status = baseNote == 60 ? CandidateStatus::DefaultValue
                                                                    : CandidateStatus::Valid;
                c.detail = "INST:baseNote=" + juce::String(baseNote);
                result.candidates.push_back(c);
            }
        }
        position = padded;
    }
    if (basc.present && basc.beats > 0 && result.info.duration > 0)
    {
        const double bpm = (double) basc.beats * 60.0 / result.info.duration;
        SmartCandidate c; c.field = SmartField::Bpm; c.source = "aiff_basc";
        c.numericValue = bpm;
        c.status = std::isfinite(bpm) && bpm >= 20.0 && bpm <= 400.0 ? CandidateStatus::Valid
                                                                    : CandidateStatus::Invalid;
        c.detail = "basc:beats=" + juce::String((int) basc.beats)
                 + ",duration=" + juce::String(result.info.duration, 3);
        result.candidates.push_back(c);
    }
    return result;
}
} // namespace sonicator
