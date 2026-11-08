#include "TrackerModuleReaders.h"
#include <cstring>
namespace sonicator {
namespace {
bool validPatternPointer(uint32_t paragraph, size_t size)
{
    if (paragraph == 0) return true; // empty pattern slot
    const auto offset = (uint64_t) paragraph * 16u;
    return offset + 2 <= (uint64_t) size;
}
} // namespace

bool parseS3m(const std::vector<uint8_t>& bytes, TrackerDocumentData& out, juce::String& error)
{
    if (bytes.size() < 96 || bytes[28] != 0x1A || std::memcmp(bytes.data() + 44, "SCRM", 4) != 0)
    { error = "Not a recognised S3M module"; return false; }
    TrackerBytes in(bytes.data(), bytes.size());
    const auto title = in.text(28);
    in.skip(4); // 0x1A, file type, reserved
    const int ordNum = in.u16le();
    const int insNum = in.u16le();
    const int patNum = in.u16le();
    in.u16le(); // flags
    in.u16le(); // tracker version
    const int ffi = in.u16le();
    if (in.failed || ordNum < 1 || ordNum > 256 || insNum > 256 || patNum > 256)
    { error = "S3M song header has out-of-range counts"; return false; }
    if (96 + (int64_t) ordNum + 2 * ((int64_t) insNum + patNum) > (int64_t) bytes.size())
    { error = "S3M order and pointer tables are truncated"; return false; }
    if (ffi != 1 && ffi != 2)
        out.info.warnings.add("S3M file-format information is not 1 or 2; samples are read as unsigned");

    in.seek(96 + (size_t) ordNum);
    std::vector<uint16_t> instrumentParas((size_t) insNum);
    for (auto& para : instrumentParas) para = in.u16le();
    for (int i = 0; i < patNum; ++i)
    {
        const int para = in.u16le();
        if (in.failed || ! validPatternPointer((uint32_t) para, bytes.size()))
        { error = "S3M pattern pointer is outside the file"; return false; }
    }
    if (in.failed) { error = "S3M pointer table is truncated"; return false; }

    for (int i = 0; i < insNum; ++i)
    {
        TrackerSampleSlot slot;
        slot.slot = i;
        const auto para = instrumentParas[(size_t) i];
        if (para == 0) { slot.empty = true; out.slots.push_back(std::move(slot)); continue; }
        const int64_t offset = (int64_t) para * 16;
        if (offset + 80 > (int64_t) bytes.size())
        { error = "S3M instrument header is outside the file"; return false; }
        TrackerBytes s(bytes.data() + offset, 80);
        const int type = s.u8();
        s.text(12); // DOS file name
        const int memsegHigh = s.u8();
        const int memsegLow = s.u16le();
        const int64_t length = s.u32le();
        const int64_t loopBegin = s.u32le();
        const int64_t loopEnd = s.u32le();
        s.u8();  // volume
        s.u8();  // reserved
        const int pack = s.u8();
        const int flags = s.u8();
        const auto c2spd = s.u32le();
        s.skip(12); // reserved
        slot.name = s.text(28);
        const auto magic = s.u32be();
        slot.frames = length;
        slot.sampleRate = c2spd > 0 ? (int) juce::jlimit((uint32_t) 100, (uint32_t) 384000, c2spd) : 8363;
        slot.channels = 1;
        if (type == 0)
        {
            slot.empty = true;
        }
        else if (type == 2)
        {
            slot.empty = false;
            slot.fmInstrument = true;
            slot.error = "AdLib instrument has no PCM sample";
        }
        else if (type != 1)
        {
            slot.empty = false;
            slot.error = "Unknown S3M instrument type " + juce::String(type);
        }
        else if (slot.frames <= 0)
        {
            slot.empty = true;
        }
        else if (magic != 0x53435253u) // "SCRS"
        {
            slot.empty = false;
            slot.error = "S3M instrument header is not SCRS";
        }
        else if ((flags & 0x02) != 0)
        {
            slot.empty = false;
            slot.error = "Stereo S3M samples are not supported";
        }
        else if (pack != 0)
        {
            slot.empty = false;
            slot.error = "Packed S3M samples are not supported for extraction";
        }
        else
        {
            slot.empty = false;
            slot.bitsPerSample = (flags & 0x04) != 0 ? 16 : 8;
            slot.encoding = ffi == 1
                ? (slot.bitsPerSample == 16 ? TrackerPcmEncoding::signedInt16LE
                                            : TrackerPcmEncoding::signedInt8)
                : (slot.bitsPerSample == 16 ? TrackerPcmEncoding::unsignedInt16LE
                                            : TrackerPcmEncoding::unsignedInt8);
            const uint32_t memseg = ((uint32_t) memsegHigh << 16) | (uint32_t) memsegLow;
            const int64_t bytesPerFrame = slot.bitsPerSample / 8;
            const int64_t sampleBytes = slot.frames * bytesPerFrame;
            slot.pcmOffset = (int64_t) memseg * 16;
            if (slot.pcmOffset + sampleBytes > (int64_t) bytes.size())
                slot.error = "S3M sample data exceeds the file size";
            else if ((flags & 0x01) != 0 && loopEnd > loopBegin && loopEnd <= slot.frames)
            {
                slot.looping = true;
                slot.loopStart = loopBegin;
                slot.loopEnd = loopEnd;
            }
            else if ((flags & 0x01) != 0)
                out.info.warnings.add("Sample " + juce::String(i + 1)
                                      + " has an out-of-range loop; the loop is disabled");
        }
        out.slots.push_back(std::move(slot));
    }

    out.info.format = TrackerFormat::s3m;
    out.info.formatName = "S3M";
    out.info.title = title;
    out.info.channels = 0; // the container does not declare a channel count
    out.info.patterns = patNum;
    out.info.orders = ordNum;
    out.info.sampleSlots = insNum;
    out.info.instruments = insNum;
    out.bytes = bytes;
    return true;
}
} // namespace sonicator
