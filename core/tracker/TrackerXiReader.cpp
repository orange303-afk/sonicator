#include "TrackerModuleReaders.h"
#include <cmath>
#include <cstring>
namespace sonicator {
namespace {
constexpr int xiHeaderSize = 298;
constexpr int xiSampleHeaderSize = 40;
constexpr int xiDataOffset = xiHeaderSize + xiSampleHeaderSize;

int xiRate(int relativeNote, int finetune)
{
    const double semitones = (double) relativeNote + (double) finetune / 128.0;
    const double rate = 8363.0 * std::pow(2.0, semitones / 12.0);
    return (int) juce::jlimit(100.0, 384000.0, std::round(rate));
}
} // namespace

bool parseXi(const std::vector<uint8_t>& bytes, TrackerDocumentData& out, juce::String& error)
{
    if (bytes.size() < (size_t) xiDataOffset
        || std::memcmp(bytes.data(), "Extended Instrument: ", 21) != 0)
    {
        error = "Not a recognised XI instrument";
        return false;
    }
    if (bytes[43] != 0x1A)
    {
        error = "XI header marker is missing";
        return false;
    }
    TrackerBytes in(bytes.data(), bytes.size());
    in.seek(64);
    const int version = in.u16le();
    if (in.failed) { error = "XI header is truncated"; return false; }
    if (version < 0x0102 || version > 0x0104)
        out.info.warnings.add("XI version 0x" + juce::String::toHexString(version)
                              + " is outside the documented 0x0102-0x0104 range");

    in.seek(xiHeaderSize);
    const int64_t lengthBytes = (int64_t) in.u32le();
    const int64_t loopStartBytes = (int64_t) in.u32le();
    const int64_t loopLengthBytes = (int64_t) in.u32le();
    in.u8();  // volume
    const int finetune = in.i8();
    const int type = in.u8();
    in.u8();  // panning
    const int relativeNote = in.i8();
    in.u8();  // reserved
    const auto displayText = in.text(22, true);
    const auto name = displayText.trim();
    if (in.failed)
    {
        error = "XI sample header is truncated";
        return false;
    }
    if (lengthBytes <= 0)
    {
        error = "XI instrument has no sample data";
        return false;
    }
    if ((type & 0x20) != 0)
    {
        error = "Stereo XI samples are not supported";
        return false;
    }
    const bool sixteenBit = (type & 0x10) != 0;
    if (sixteenBit && (lengthBytes & 1) != 0)
    {
        error = "16-bit XI sample length is not even";
        return false;
    }
    if (lengthBytes > (int64_t) bytes.size() - xiDataOffset)
    {
        error = "XI sample data is truncated";
        return false;
    }

    TrackerSampleSlot slot;
    slot.slot = 0;
    slot.name = name;
    slot.displayText = displayText;
    slot.empty = false;
    slot.channels = 1;
    slot.bitsPerSample = sixteenBit ? 16 : 8;
    slot.encoding = sixteenBit ? TrackerPcmEncoding::deltaInt16LE : TrackerPcmEncoding::deltaInt8;
    slot.frames = sixteenBit ? lengthBytes / 2 : lengthBytes;
    slot.sampleRate = xiRate(relativeNote, finetune);
    // The note-60 rate above is transposed by the sample's relative note, so the note that plays
    // the 8363 Hz tracker reference is lower by exactly that offset.
    slot.rootKey = juce::jlimit(0, 127, 60 - relativeNote);
    slot.pcmOffset = xiDataOffset;

    const int loopKind = type & 0x03;
    const int64_t loopStart = sixteenBit ? loopStartBytes / 2 : loopStartBytes;
    const int64_t loopLength = sixteenBit ? loopLengthBytes / 2 : loopLengthBytes;
    if (loopKind != 0 && loopLength > 1 && loopStart >= 0 && loopStart + loopLength <= slot.frames)
    {
        slot.looping = true;
        slot.pingPong = loopKind == 2;
        slot.loopStart = loopStart;
        slot.loopEnd = loopStart + loopLength;
    }
    else if (loopKind != 0)
        out.info.warnings.add("Sample has an out-of-range XI loop; the loop is disabled");

    out.info.format = TrackerFormat::xi;
    out.info.formatName = "XI";
    out.info.title = name;
    out.info.channels = 0;  // a standalone instrument has no song channels
    out.info.patterns = 0;
    out.info.orders = 0;
    out.info.sampleSlots = 1;
    out.info.instruments = 1;
    out.slots.push_back(std::move(slot));
    out.bytes = bytes;
    return true;
}
} // namespace sonicator
