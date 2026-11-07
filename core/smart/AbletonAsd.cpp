#include "AbletonAsd.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace sonicator {
namespace {
constexpr int64_t maxFileSize = 64 * 1024 * 1024;
juce::uint16 le16(const juce::uint8* d) { return (juce::uint16) (d[0] | (d[1] << 8)); }
juce::uint32 le32(const juce::uint8* d)
{
    return (juce::uint32) d[0] | ((juce::uint32) d[1] << 8)
         | ((juce::uint32) d[2] << 16) | ((juce::uint32) d[3] << 24);
}
double le64(const juce::uint8* d)
{
    juce::uint64 bits = 0;
    for (int i = 7; i >= 0; --i) bits = (bits << 8) | d[i];
    double value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
void addTempo(std::vector<SmartCandidate>& out, const juce::String& source, double bpm,
              const juce::String& detail, CandidateStatus status)
{
    SmartCandidate c; c.field = SmartField::Bpm; c.source = source; c.numericValue = bpm;
    c.detail = detail; c.status = status;
    out.push_back(c);
}
const char* const warpName = "WarpMarker";
const juce::uint8 secTimePattern[] = { 7, 0, 0, 0, 'S', 0, 'e', 0, 'c', 0, 'T', 0, 'i', 0, 'm', 0, 'e', 0 };
} // namespace
AsdMetadataResult extractAsdMetadata(const juce::File& file)
{
    AsdMetadataResult result;
    if (! file.existsAsFile()) { result.error = "ASD sidecar is missing"; return result; }
    if (file.getSize() > maxFileSize) { result.error = "ASD exceeds 64 MiB limit"; return result; }
    juce::MemoryBlock block;
    if (! file.loadFileAsData(block)) { result.error = "Cannot read ASD"; return result; }
    const auto* data = (const juce::uint8*) block.getData();
    const auto size = (int64_t) block.getSize();
    if (size < 14 || data[1] != 'I') { result.error = "Unrecognised ASD header"; return result; }
    result.version = (int) data[0];
    const auto slots = le16(data + 2);
    if (slots < 3) { result.error = "Invalid ASD overview table"; return result; }
    const auto tableLength = (int64_t) slots - 2;
    const auto tableEnd = 14 + tableLength * 4;
    if (tableEnd > size) { result.error = "ASD overview table overruns the file"; return result; }
    result.frameCount = (int64_t) le32(data + tableEnd - 4);
    if (result.version != 6)
    {
        addTempo(result.candidates, "asd_warp", 0, "asd:version=" + juce::String(result.version),
                 CandidateStatus::Unsupported);
        return result;
    }
    result.supported = true;
    std::vector<std::pair<double, double>> markers; // seconds, beats
    const size_t nameLength = std::strlen(warpName);
    for (int64_t i = 0; i + (int64_t) nameLength + 20 <= size; ++i)
    {
        if (std::memcmp(data + i, warpName, nameLength) != 0) continue;
        const int64_t body = i + (int64_t) nameLength;
        if (body + 4 + (int64_t) sizeof(secTimePattern) <= size
            && std::memcmp(data + body + 4, secTimePattern, sizeof(secTimePattern)) == 0)
        {
            i = body + 4; // schema type definition, not an instance
            continue;
        }
        const double seconds = le64(data + body + 4);
        const double beats = le64(data + body + 12);
        if (std::isfinite(seconds) && std::isfinite(beats) && seconds >= 0 && seconds <= 86400
            && std::abs(beats) < 1e7)
            markers.emplace_back(seconds, beats);
        i = body;
    }
    std::sort(markers.begin(), markers.end());
    markers.erase(std::unique(markers.begin(), markers.end()), markers.end());
    result.warpMarkerCount = (int) markers.size();
    if (markers.size() >= 2)
    {
        const double seconds = markers.back().first - markers.front().first;
        const double beats = markers.back().second - markers.front().second;
        if (seconds > 0)
        {
            const double bpm = beats / seconds * 60.0;
            double spread = 0;
            for (size_t i = 1; i < markers.size(); ++i)
            {
                const double ds = markers[i].first - markers[i - 1].first;
                if (ds > 0)
                {
                    const double segment = (markers[i].second - markers[i - 1].second) / ds * 60.0;
                    spread = juce::jmax(spread, std::abs(segment - bpm));
                }
            }
            if (spread > 1.0)
                addTempo(result.candidates, "asd_warp", bpm,
                         "asd:warp markers imply variable tempo", CandidateStatus::Invalid);
            else if (std::isfinite(bpm) && bpm >= 20.0 && bpm <= 400.0)
                addTempo(result.candidates, "asd_warp", bpm,
                         "asd:warp markers=" + juce::String((int) markers.size()), CandidateStatus::Valid);
        }
    }
    // AufTaktData.UnbiasedTempoEstimate: only a self-describing instance carries a usable value.
    static const juce::uint8 aufPrefix[] = { 0x00, 0x0b, 'A', 'u', 'f', 'T', 'a', 'k', 't', 'D', 'a', 't', 'a' };
    for (int64_t i = 0; i + (int64_t) sizeof(aufPrefix) + 8 <= size; ++i)
    {
        if (std::memcmp(data + i, aufPrefix, sizeof(aufPrefix)) != 0) continue;
        int64_t q = i + (int64_t) sizeof(aufPrefix);
        const auto memberCount = (juce::uint32) le32(data + q); q += 4;
        if (memberCount == 0 || memberCount > 32) { i = q; continue; }
        bool plausible = true;
        juce::StringArray names;
        for (juce::uint32 member = 0; member < memberCount; ++member)
        {
            if (q + 4 > size) { plausible = false; break; }
            const auto chars = (juce::uint32) le32(data + q); q += 4;
            if (chars == 0 || chars > 128 || q + (int64_t) chars * 2 + 1 > size) { plausible = false; break; }
            juce::String name;
            for (juce::uint32 c = 0; c < chars; ++c)
                name += juce::String::charToString((juce::juce_wchar) (data[q + c * 2] | (data[q + c * 2 + 1] << 8)));
            names.add(name);
            q += (int64_t) chars * 2;
            ++q; // primitive type tag
        }
        if (! plausible) continue;
        if (names.size() < 2 || names[0] != "PreprocessedDataChunk" || names[1] != "UnbiasedTempoEstimate")
        {
            i = q;
            continue;
        }
        if (q + 4 > size) continue;
        const auto blobLength = (int64_t) le32(data + q); q += 4;
        if (blobLength < 0 || blobLength > size - q) { i = q; continue; }
        q += blobLength;
        if (q + 13 > size) continue;
        const double tempo = le64(data + q);
        const bool isSet = data[q + 8] != 0;
        if (! isSet) { i = q; continue; }
        if (std::isfinite(tempo) && tempo >= 20.0 && tempo <= 400.0)
            addTempo(result.candidates, "asd_tempo", tempo, "asd:AufTaktData", CandidateStatus::Valid);
        else
            addTempo(result.candidates, "asd_tempo", tempo,
                     "asd:AufTaktData IsSet but tempo=" + juce::String(tempo, 3),
                     CandidateStatus::Invalid);
        break;
    }
    return result;
}
} // namespace sonicator
