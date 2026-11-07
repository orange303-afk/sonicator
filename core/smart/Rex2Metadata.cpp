#include "Rex2Metadata.h"
#include "BinaryReader.h"
#include <cmath>
#include <cstring>
namespace sonicator {
namespace {
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
struct WalkState
{
    Rex2MetadataResult& result;
    bool first = true;
};
} // namespace
Rex2MetadataResult extractRex2Metadata(const juce::File& file)
{
    Rex2MetadataResult result;
    juce::FileInputStream stream(file);
    if (! stream.openedOk()) { result.error = "Cannot open file"; return result; }
    BinaryReader reader(stream, true);
    char header[12] {};
    if (! reader.read(header, 12)) { result.error = "File is shorter than a CAT header"; return result; }
    if (! idIs(header, "CAT ")) { result.error = "Not an IFF CAT container"; return result; }
    if (! idIs(header + 8, "REX2")) { result.error = "CAT container is not REX2"; return result; }
    const auto total = reader.length();
    result.info.fileSize = total;
    struct Walker
    {
        BinaryReader& reader;
        Rex2MetadataResult& result;
        bool walk(int64_t start, int64_t end, int depth)
        {
            if (depth > 4) return true;
            int64_t position = start;
            int guard = 0;
            while (position + 8 <= end && ++guard < 100000)
            {
                if (! reader.seek(position)) { result.error = "Seek failed"; return false; }
                char id[4] {};
                if (! reader.read(id, 4)) { result.error = "Truncated chunk header"; return false; }
                const auto size = reader.u32();
                const int64_t body = position + 8;
                if ((int64_t) size > end - body)
                {
                    result.error = "Chunk '" + chunkName(id) + "' exceeds its container";
                    result.truncated = true;
                    return false;
                }
                if (idIs(id, "CAT ") || idIs(id, "FORM"))
                {
                    if (! walk(body + 4, body + (int64_t) size, depth + 1)) return false;
                }
                else if (idIs(id, "GLOB") && size >= 22)
                {
                    std::vector<juce::uint8> data((size_t) size);
                    if (! reader.read(data.data(), (int) size)) return false;
                    auto be16 = [&](size_t o) { return (int) ((data[o] << 8) | data[o + 1]); };
                    auto be32 = [&](size_t o) { return ((juce::uint32) data[o] << 24) | ((juce::uint32) data[o + 1] << 16)
                                                      | ((juce::uint32) data[o + 2] << 8) | data[o + 3]; };
                    result.info.declaredSlices = (int) be32(0);
                    result.info.previewTempo = be32(16) / 1000.0;
                    (void) be16;
                }
                else if (idIs(id, "RECY") && size >= 12)
                {
                    std::vector<juce::uint8> data((size_t) size);
                    if (! reader.read(data.data(), (int) size)) return false;
                    const double tempo = (((juce::uint32) data[8] << 24) | ((juce::uint32) data[9] << 16)
                                        | ((juce::uint32) data[10] << 8) | data[11]) / 1000.0;
                    if (std::isfinite(tempo) && tempo >= 20.0 && tempo <= 400.0)
                        result.info.originalTempo = tempo;
                }
                else if (idIs(id, "SINF") && size >= 18)
                {
                    std::vector<juce::uint8> data((size_t) size);
                    if (! reader.read(data.data(), (int) size)) return false;
                    auto be32 = [&](size_t o) { return ((juce::uint32) data[o] << 24) | ((juce::uint32) data[o + 1] << 16)
                                                      | ((juce::uint32) data[o + 2] << 8) | data[o + 3]; };
                    result.info.channels = data[0];
                    result.info.format = data[1];
                    result.info.sampleRate = (int) be32(2);
                    result.info.frames = (int64_t) be32(6);
                    result.info.loopStart = (int64_t) be32(10);
                    result.info.loopEnd = (int64_t) be32(14);
                }
                else if ((idIs(id, "SDAT") || idIs(id, "DWOP")) && size > 0)
                {
                    result.info.sdatOffset = body;
                    result.info.sdatSize = (int64_t) size;
                }
                else if (idIs(id, "SLCE") && size >= 11)
                {
                    std::vector<juce::uint8> data((size_t) size);
                    if (! reader.read(data.data(), (int) size)) return false;
                    auto be32 = [&](size_t o) { return ((juce::uint32) data[o] << 24) | ((juce::uint32) data[o + 1] << 16)
                                                      | ((juce::uint32) data[o + 2] << 8) | data[o + 3]; };
                    Rex2Slice slice;
                    slice.start = (int64_t) be32(0);
                    slice.length = (int64_t) be32(4);
                    slice.flags = data[10];
                    result.info.slices.push_back(slice);
                }
                position = body + (int64_t) size + (size & 1);
            }
            return true;
        }
    } walker { reader, result };
    walker.walk(12, total, 0);

    const auto frames = result.info.frames;
    for (const auto& slice : result.info.slices)
    {
        if (slice.flags & 1) { ++result.mutedSlices; continue; }
        if (slice.length <= 1) continue;
        if (frames > 0 && (slice.start < 0 || slice.start + slice.length > frames))
        {
            ++result.badSlices;
            continue;
        }
        ++result.playableSlices;
    }
    auto addTempo = [&](const juce::String& source, double tempo, const juce::String& detail)
    {
        SmartCandidate c; c.field = SmartField::Bpm; c.source = source; c.numericValue = tempo;
        c.status = std::isfinite(tempo) && tempo >= 20.0 && tempo <= 400.0 ? CandidateStatus::Valid
                                                                          : CandidateStatus::Invalid;
        c.detail = detail;
        result.candidates.push_back(c);
    };
    if (result.info.previewTempo != 0)
        addTempo("rx2_glob", result.info.previewTempo,
                 "rx2:GLOB preview tempo=" + juce::String(result.info.previewTempo, 3));
    if (result.info.originalTempo != 0)
        addTempo("rx2_recy", result.info.originalTempo,
                 "rx2:RECY original tempo=" + juce::String(result.info.originalTempo, 3));
    return result;
}
} // namespace sonicator
