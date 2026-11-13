#include "LegacyFormatProbe.h"
#include "core/tracker/TrackerModReader.h"
#include <cstring>
namespace sonicator {
namespace {
constexpr int probeBytes = 64 * 1024;

bool probeXi(const std::vector<uint8_t>& head, int64_t fileSize, LegacyFormatProbe& out)
{
    if (head.size() < 338 || std::memcmp(head.data(), "Extended Instrument: ", 21) != 0
        || head[43] != 0x1A)
        return false;
    const int64_t length = (int64_t) ((uint32_t) head[298] | ((uint32_t) head[299] << 8)
                         | ((uint32_t) head[300] << 16) | ((uint32_t) head[301] << 24));
    if (length <= 0 || 338 + length > fileSize) return false;
    out.format = TrackerFormat::xi;
    out.variant = "XI";
    out.channels = 0;
    out.sampleSlots = 1;
    const int type = head[312];
    out.detail = "XI · 1 instrument · "
               + juce::String(type & 0x10 ? length / 2 : length) + " frames";
    return true;
}

bool probeXm(const std::vector<uint8_t>& head, LegacyFormatProbe& out)
{
    if (head.size() < 80 || std::memcmp(head.data(), "Extended Module: ", 17) != 0) return false;
    if (head[37] != 0x1A) return false;
    const int64_t headerSize = (int64_t) ((uint32_t) head[60] | ((uint32_t) head[61] << 8)
                         | ((uint32_t) head[62] << 16) | ((uint32_t) head[63] << 24));
    const int songLength = (int) head[64] | ((int) head[65] << 8);
    const int channels = (int) head[68] | ((int) head[69] << 8);
    const int instruments = (int) head[72] | ((int) head[73] << 8);
    if (headerSize < 20 || songLength < 1 || songLength > 256
        || channels < 1 || channels > 64 || instruments > 256)
        return false;
    if ((int64_t) 60 + headerSize > (int64_t) head.size()) return false;
    out.format = TrackerFormat::xm;
    out.variant = "XM";
    out.channels = channels;
    out.sampleSlots = -1; // determined by the full parser; XM counts instrument-local samples
    out.detail = "XM · " + juce::String(channels) + " channels · "
               + juce::String(instruments) + " instruments";
    return true;
}

bool probeS3m(const std::vector<uint8_t>& head, int64_t fileSize, LegacyFormatProbe& out)
{
    if (head.size() < 96 || head[28] != 0x1A || std::memcmp(head.data() + 44, "SCRM", 4) != 0)
        return false;
    const int ordNum = (int) head[32] | ((int) head[33] << 8);
    const int insNum = (int) head[34] | ((int) head[35] << 8);
    const int patNum = (int) head[36] | ((int) head[37] << 8);
    if (ordNum < 1 || ordNum > 256 || insNum > 256 || patNum > 256) return false;
    if (96 + (int64_t) ordNum + 2 * ((int64_t) insNum + patNum) > fileSize) return false;
    out.format = TrackerFormat::s3m;
    out.variant = "S3M";
    out.channels = 0;
    out.sampleSlots = insNum;
    out.detail = "S3M · " + juce::String(insNum) + " instrument slots · "
               + juce::String(patNum) + " patterns";
    return true;
}
} // namespace

bool isLegacyTrackerExtension(const juce::File& file)
{
    return file.hasFileExtension("mod;xm;s3m;xi");
}

LegacyFormatProbe probeLegacyTracker(const juce::File& file)
{
    LegacyFormatProbe out;
    if (! file.existsAsFile()) { out.error = "The file does not exist"; return out; }
    const auto size = file.getSize();
    if (size <= 0) { out.error = "The file is empty"; return out; }
    juce::FileInputStream stream(file);
    if (! stream.openedOk()) { out.error = "Cannot open the file"; return out; }
    std::vector<uint8_t> head((size_t) juce::jmin<int64_t>(size, probeBytes));
    const auto read = stream.read(head.data(), (int) head.size());
    if (read <= 0) { out.error = "Cannot read the file header"; return out; }
    head.resize((size_t) read);

    if (probeXi(head, size, out))
    {
        out.recognised = true;
        out.songPlayback = false; // a standalone instrument, played as a source
        out.sampleExtraction = true;
        return out;
    }
    if (probeXm(head, out) || probeS3m(head, size, out))
    {
        out.recognised = true;
        out.songPlayback = true;
        out.sampleExtraction = true;
        return out;
    }
    ModHeader mod;
    if (readModHeader(head.data(), head.size(), size, mod))
    {
        out.format = TrackerFormat::mod;
        out.variant = mod.variant;
        out.channels = mod.channels;
        out.sampleSlots = mod.sampleSlots;
        out.detail = mod.variant + " · " + juce::String(mod.channels) + " channels · "
                   + juce::String(mod.sampleSlots) + " sample slots";
        out.recognised = true;
        out.songPlayback = true;
        out.sampleExtraction = true;
        return out;
    }
    out.error = "Not a supported tracker module";
    return out;
}
} // namespace sonicator
