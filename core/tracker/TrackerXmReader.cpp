#include "TrackerModuleReaders.h"
#include <cmath>
#include <cstring>
namespace sonicator {
namespace {
// Standard XM instrument header: 29-byte prefix + sample-header-size + note map + two envelopes
// + counts/loops + types + vibrato + fadeout + 22 reserved bytes. Sample headers and sample data
// follow it and are not part of this size (confirmed against real 1.04 modules).
constexpr int xmInstrumentFixedSize = 263;
struct PendingXmSample
{
    int64_t lengthBytes = 0, loopStartBytes = 0, loopLengthBytes = 0;
    int type = 0, finetune = 0, relativeNote = 0;
    juce::String name;
    juce::String displayText;
    int64_t pcmOffset = 0;
};
int xmRate(int relativeNote, int finetune)
{
    const double semitones = (double) relativeNote + (double) finetune / 128.0;
    const double rate = 8363.0 * std::pow(2.0, semitones / 12.0);
    return (int) juce::jlimit(100.0, 384000.0, std::round(rate));
}
bool parseXmPatterns(TrackerBytes& in, int64_t& offset, int patterns, int version,
                     juce::String& error)
{
    const int headerSize = version > 0x0102 ? 9 : 8;
    for (int i = 0; i < patterns; ++i)
    {
        in.seek((size_t) offset);
        const auto patternHeaderSize = in.u32le();
        in.u8(); // packing type
        const int rows = version > 0x0102 ? in.u16le() : in.u8() + 1;
        const auto dataSize = in.u16le();
        if (in.failed || patternHeaderSize < (uint32_t) headerSize || rows < 1 || rows > 256
            || (int64_t) patternHeaderSize < (int64_t) headerSize
            || offset + (int64_t) patternHeaderSize + (int64_t) dataSize > (int64_t) in.size())
        { error = "XM pattern header is invalid"; return false; }
        offset += (int64_t) patternHeaderSize + (int64_t) dataSize;
    }
    return true;
}
// Reads one instrument header and its sample headers. For XM 1.04 the sample data follows
// immediately and is advanced over; for 1.02/1.03 only the headers exist here.
bool parseXmInstrument(TrackerBytes& in, int64_t& offset, int version, int index,
                       std::vector<PendingXmSample>& pending, int64_t& dataCursor,
                       juce::String& error)
{
    in.seek((size_t) offset);
    const int64_t start = offset;
    const auto instrumentSize = in.u32le();
    auto instrumentText = in.text(22, true);
    auto instrumentName = instrumentText.trim();
    in.u8(); // type
    const int numSamples = in.u16le();
    if (in.failed || instrumentSize < 29 || numSamples > 256
        || start + (int64_t) instrumentSize > (int64_t) in.size())
    { error = "XM instrument header is invalid"; return false; }
    if (numSamples == 0)
    {
        PendingXmSample empty;
        empty.name = instrumentName;
        empty.displayText = instrumentText;
        pending.push_back(std::move(empty)); // keep text-only instrument rows and ASCII art
        offset = start + (int64_t) instrumentSize;
        return true;
    }
    if (instrumentSize < 33) { error = "XM sampled instrument header is too short"; return false; }
    const auto sampleHeaderSize = in.u32le();
    if (in.failed || sampleHeaderSize < 40 || sampleHeaderSize > 0x100)
    { error = "XM sample header size is invalid"; return false; }
    in.seek((size_t) (start + (int64_t) instrumentSize));
    const int64_t headersEnd = start + (int64_t) instrumentSize
                             + (int64_t) numSamples * (int64_t) sampleHeaderSize;
    if (headersEnd > (int64_t) in.size())
    { error = "XM sample header table is truncated"; return false; }
    int64_t totalBytes = 0;
    for (int s = 0; s < numSamples; ++s)
    {
        in.seek((size_t) (start + (int64_t) instrumentSize
                          + (int64_t) s * (int64_t) sampleHeaderSize));
        PendingXmSample sample;
        sample.displayText = instrumentText;
        sample.lengthBytes = in.u32le();
        sample.loopStartBytes = in.u32le();
        sample.loopLengthBytes = in.u32le();
        in.u8();  // volume
        sample.finetune = in.i8();
        sample.type = in.u8();
        in.u8();  // panning
        sample.relativeNote = in.i8();
        in.u8();  // reserved
        sample.name = in.text(22);
        if (in.failed) { error = "XM sample header is truncated"; return false; }
        if (sample.name.isEmpty()) sample.name = instrumentName;
        if (version > 0x0103)
            sample.pcmOffset = headersEnd + totalBytes;
        totalBytes += sample.lengthBytes;
        pending.push_back(std::move(sample));
    }
    if (version > 0x0103)
    {
        if (headersEnd + totalBytes > (int64_t) in.size())
        { error = "XM sample data exceeds the file size"; return false; }
        offset = headersEnd + totalBytes;
    }
    else
    {
        offset = headersEnd;
        dataCursor = 0;
    }
    (void) index;
    return true;
}
} // namespace

bool parseXm(const std::vector<uint8_t>& bytes, TrackerDocumentData& out, juce::String& error)
{
    if (bytes.size() < 80 || std::memcmp(bytes.data(), "Extended Module: ", 17) != 0)
    { error = "Not a recognised XM module"; return false; }
    TrackerBytes in(bytes.data(), bytes.size());
    in.seek(17);
    auto title = in.text(20);
    const int marker = in.u8();
    in.text(20); // tracker name
    const int version = in.u16le();
    const auto headerSize = in.u32le();
    if (in.failed || marker != 0x1A) { error = "XM header is malformed"; return false; }
    if (version < 0x0102 || version > 0x0104)
        out.info.warnings.add("XM version 0x" + juce::String::toHexString(version)
                              + " is outside the documented 0x0102-0x0104 range");
    if (headerSize < 20 || (int64_t) 60 + (int64_t) headerSize > (int64_t) bytes.size())
    { error = "XM header size is invalid"; return false; }
    const int songLength = in.u16le();
    in.u16le(); // restart
    const int channels = in.u16le();
    const int patterns = in.u16le();
    const int instruments = in.u16le();
    in.u16le(); in.u16le(); in.u16le(); // flags, tempo, bpm
    if (in.failed || songLength < 1 || songLength > 256 || channels < 1 || channels > 64
        || patterns > 256 || instruments > 256
        || (int64_t) 80 + songLength > (int64_t) 60 + (int64_t) headerSize)
    { error = "XM song header has out-of-range counts"; return false; }

    std::vector<PendingXmSample> pending;
    int64_t offset = 60 + (int64_t) headerSize;
    int64_t dataCursor = 0;
    if (version <= 0x0103)
    {
        // XM 1.02/1.03 store instruments first, then patterns, then all sample data.
        for (int i = 0; i < instruments; ++i)
            if (! parseXmInstrument(in, offset, version, i, pending, dataCursor, error)) return false;
        if (! parseXmPatterns(in, offset, patterns, version, error)) return false;
        dataCursor = offset;
    }
    else
    {
        if (! parseXmPatterns(in, offset, patterns, version, error)) return false;
        for (int i = 0; i < instruments; ++i)
            if (! parseXmInstrument(in, offset, version, i, pending, dataCursor, error)) return false;
    }

    int64_t totalFrames = 0;
    for (size_t i = 0; i < pending.size(); ++i)
    {
        auto& sample = pending[i];
        TrackerSampleSlot slot;
        slot.slot = (int) i;
        slot.name = sample.name;
        slot.displayText = sample.displayText;
        slot.empty = sample.lengthBytes == 0;
        slot.channels = 1;
        const bool sixteenBit = (sample.type & 0x10) != 0;
        slot.bitsPerSample = sixteenBit ? 16 : 8;
        slot.encoding = sixteenBit ? TrackerPcmEncoding::deltaInt16LE : TrackerPcmEncoding::deltaInt8;
        slot.frames = sixteenBit ? sample.lengthBytes / 2 : sample.lengthBytes;
        slot.sampleRate = xmRate(sample.relativeNote, sample.finetune);
        if (sample.pcmOffset == 0 && version <= 0x0103)
        {
            sample.pcmOffset = dataCursor;
            dataCursor += sample.lengthBytes;
        }
        slot.pcmOffset = sample.pcmOffset;
        if ((sample.type & 0x20) != 0)
            slot.error = "Stereo XM samples are not supported";
        if (! slot.empty)
        {
            const int loopKind = sample.type & 0x03;
            const int64_t loopStart = sixteenBit ? sample.loopStartBytes / 2 : sample.loopStartBytes;
            const int64_t loopLength = sixteenBit ? sample.loopLengthBytes / 2 : sample.loopLengthBytes;
            if (loopKind != 0 && loopLength > 1 && loopStart + loopLength <= slot.frames)
            {
                slot.looping = true;
                slot.pingPong = loopKind == 2;
                slot.loopStart = loopStart;
                slot.loopEnd = loopStart + loopLength;
            }
            else if (loopKind != 0)
                out.info.warnings.add("Sample " + juce::String(slot.slot + 1)
                                      + " has an out-of-range loop; the loop is disabled");
        }
        if ((sample.lengthBytes & 1) != 0 && sixteenBit)
            slot.error = "16-bit XM sample length is not even";
        totalFrames += slot.frames;
        out.slots.push_back(std::move(slot));
    }
    if (version <= 0x0103 && dataCursor > (int64_t) bytes.size())
    { error = "XM sample data exceeds the file size"; return false; }
    if (totalFrames <= 0) { error = "XM module has no sample data"; return false; }

    out.info.format = TrackerFormat::xm;
    out.info.formatName = "XM";
    out.info.title = title;
    out.info.channels = channels;
    out.info.patterns = patterns;
    out.info.orders = songLength;
    out.info.sampleSlots = (int) pending.size();
    out.info.instruments = instruments;
    out.bytes = bytes;
    return true;
}
} // namespace sonicator
