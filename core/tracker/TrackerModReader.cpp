#include "TrackerModReader.h"
#include <algorithm>
#include <cstring>
#include <cmath>
namespace sonicator {
namespace {
// Channel count declared by the four-byte MOD signature at the end of the sample-header block.
// Returns 0 for a tag this reader does not claim to support.
int channelsFromModTag(const uint8_t* p)
{
    const auto is = [p] (const char* tag) { return std::memcmp(p, tag, 4) == 0; };
    if (is("M.K.") || is("M!K!") || is("M&K!") || is("N.T.") || is("FLT4") || is("4CHN")
        || is("FA04"))
        return 4;
    if (is("FLT8") || is("OCTA") || is("OKTA") || is("CD81") || is("8CHN") || is("FA08"))
        return 8;
    if (is("6CHN") || is("FA06")) return 6;
    const auto digits = [] (const uint8_t* q, int count)
    {
        for (int i = 0; i < count; ++i) if (q[i] < '0' || q[i] > '9') return false;
        return true;
    };
    const auto number = [] (const uint8_t* q, int count)
    {
        int value = 0;
        for (int i = 0; i < count; ++i) value = value * 10 + (q[i] - '0');
        return value;
    };
    if (std::memcmp(p, "TDZ", 3) == 0 && p[3] >= '1' && p[3] <= '9') return p[3] - '0';
    // "10CH" .. "32CH" and "16CN"/"32CN" style multichannel tags.
    if (p[2] == 'C' && p[3] == 'H' && digits(p, 2))
    {
        const int n = number(p, 2);
        return n >= 1 && n <= 32 ? n : 0;
    }
    if (p[2] == 'C' && p[3] == 'N' && digits(p, 2))
    {
        const int n = number(p, 2);
        return n >= 1 && n <= 32 ? n : 0;
    }
    return 0;
}
bool hasTagAt(const uint8_t* head, size_t headSize, size_t offset, int& channels)
{
    if (headSize < offset + 4) return false;
    channels = channelsFromModTag(head + offset);
    return channels > 0;
}} // namespace

bool readModHeader(const uint8_t* head, size_t headSize, int64_t fileSize, ModHeader& out)
{
    out = {};
    int slots = 0, channels = 0;
    bool hasSignature = false;
    if (headSize >= 1084 && hasTagAt(head, headSize, 1080, channels))
    { slots = 31; hasSignature = true; }

    // A 15-sample module has no signature: songLength/restart/orders sit directly at offset 470
    // and pattern data starts at 600. Accept it only when several independent structural checks
    // agree, never because the extension or a random word matched.
    if (slots == 0)
    {
        if (headSize < 600) return false;
        slots = 15; channels = 4;
    }

    TrackerBytes in(head, headSize);
    out.title = in.text(20);
    if (! in.seek(20)) return false;
    std::vector<TrackerSampleSlot> samples((size_t) slots);
    std::vector<int> loopStartWords((size_t) slots, 0), loopLengthWords((size_t) slots, 0);
    int64_t totalBytes = 0;
    for (int i = 0; i < slots; ++i)
    {
        auto& slot = samples[(size_t) i];
        slot.slot = i;
        slot.name = in.text(22);
        const int lengthWords = in.u16be();
        const int finetuneRaw = in.u8();
        const int volume = in.u8();
        loopStartWords[(size_t) i] = in.u16be();
        loopLengthWords[(size_t) i] = in.u16be();
        if (in.failed) return false;
        (void) volume;
        const int finetune = finetuneRaw > 7 ? finetuneRaw - 16 : finetuneRaw;
        slot.frames = (int64_t) lengthWords * 2;
        slot.empty = slot.frames == 0;
        slot.channels = 1;
        slot.bitsPerSample = 8;
        slot.encoding = TrackerPcmEncoding::signedInt8;
        slot.sampleRate = (int) std::lround(8363.0 * std::pow(2.0, finetune / 96.0));
        totalBytes += slot.frames;
    }

    const size_t orderOffset = 20 + (size_t) slots * 30;
    const int songLength = in.u8();
    (void) in.u8(); // restart position, unused for extraction
    if (in.failed || songLength < 1 || songLength > 128) return false;
    int patternCount = 0;
    for (int i = 0; i < songLength; ++i)
    {
        const int order = in.u8();
        if (order > 127) return false;
        patternCount = juce::jmax(patternCount, order + 1);
    }
    if (in.failed) return false;

    // The four-byte signature sits between the order list and the pattern data in a 31-sample
    // module. A 15-sample module has no signature and pattern data starts immediately.
    const size_t headerEnd = orderOffset + 2 + 128 + (slots == 31 ? 4 : 0);
    if (slots == 31 && ! hasSignature) return false;
    const int64_t dataOffset = (int64_t) headerEnd
        + (int64_t) patternCount * 64 * (int64_t) channels * 4;
    if (dataOffset > fileSize) return false;
    if (totalBytes <= 0) return false;
    const int64_t available = fileSize - dataOffset;
    if (totalBytes > available)
    {
        // Real MOD exporters often miscount the payload by a handful of bytes. Clamp only a small
        // shortfall; a genuinely truncated module is still rejected with a precise error.
        const int64_t missing = totalBytes - available;
        const int64_t tolerance = juce::jmin<int64_t>(4096, juce::jmax<int64_t>(16, totalBytes / 100));
        if (missing > tolerance) return false;
        out.warnings.add("Sample data is " + juce::String(missing)
                         + " bytes shorter than declared; the last samples are truncated");
        int64_t remaining = available;
        for (auto& slot : samples)
        {
            if (slot.frames > remaining)
            {
                slot.frames = juce::jmax<int64_t>(0, remaining);
                if (slot.frames == 0) slot.empty = true;
            }
            remaining -= slot.frames;
        }
    }

    // Loop metadata is validated against the final, possibly clamped, sample length.
    for (int i = 0; i < slots; ++i)
    {
        auto& slot = samples[(size_t) i];
        if (slot.empty) continue;
        const int64_t loopStart = (int64_t) loopStartWords[(size_t) i] * 2;
        const int64_t loopLength = (int64_t) loopLengthWords[(size_t) i] * 2;
        if (loopLengthWords[(size_t) i] > 1 && loopStart >= 0
            && loopStart + loopLength <= slot.frames)
        {
            slot.looping = true;
            slot.loopStart = loopStart;
            slot.loopEnd = loopStart + loopLength;
        }
        else if (loopLengthWords[(size_t) i] > 1)
            out.warnings.add("Sample " + juce::String(i + 1)
                             + " has an out-of-range loop; the loop is disabled");
    }

    int64_t offset = dataOffset;
    for (auto& slot : samples)
    {
        slot.pcmOffset = offset;
        offset += slot.frames;
    }

    out.sampleSlots = slots;
    out.channels = channels;
    out.songLength = songLength;
    out.patternCount = patternCount;
    out.dataOffset = dataOffset;
    out.variant = "MOD " + juce::String(slots) + "-sample";
    out.slots = std::move(samples);
    return true;
}

bool parseMod(const std::vector<uint8_t>& bytes, TrackerDocumentData& out, juce::String& error)
{
    ModHeader header;
    if (! readModHeader(bytes.data(), bytes.size(), (int64_t) bytes.size(), header))
    {
        error = "Not a recognised MOD module";
        return false;
    }
    out.info.format = TrackerFormat::mod;
    out.info.formatName = header.variant;
    out.info.title = header.title;
    out.info.channels = header.channels;
    out.info.patterns = header.patternCount;
    out.info.orders = header.songLength;
    out.info.sampleSlots = (int) header.slots.size();
    out.info.instruments = 0;
    out.info.warnings = header.warnings;
    // Verify the payload extent again with the real byte array: the probe only had a prefix.
    int64_t end = header.dataOffset;
    for (const auto& slot : header.slots) end += slot.frames;
    if (end > (int64_t) bytes.size())
    {
        error = "MOD sample data exceeds the file size";
        return false;
    }
    out.slots = std::move(header.slots);
    out.bytes = bytes;
    return true;
}
} // namespace sonicator
