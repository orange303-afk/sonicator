#include "CafAudioFormat.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace sonicator
{
namespace
{
// The same caps the VOC reader uses: a bounded, pure parse, so a malformed file can never make the
// library hold more than this.
constexpr int64_t maxFileBytes = 256 * 1024 * 1024;
constexpr int64_t maxDecodedFrames = 128LL * 1024 * 1024;
constexpr const char* cafMagic = "caff";

juce::uint64 readBe(const juce::uint8* p, int count)
{
    juce::uint64 value = 0;
    for (int i = 0; i < count; ++i) value = (value << 8) | (juce::uint64) p[i];
    return value;
}

juce::int64 readSigned(const juce::uint8* p, int bytes, bool littleEndian)
{
    juce::uint64 raw = 0;
    if (littleEndian)
        for (int i = bytes - 1; i >= 0; --i) raw = (raw << 8) | (juce::uint64) p[i];
    else
        for (int i = 0; i < bytes; ++i) raw = (raw << 8) | (juce::uint64) p[i];
    const int shift = 64 - bytes * 8;
    return (juce::int64) (raw << shift) >> shift;
}

bool sameTag(const juce::uint8* bytes, const char* tag)
{
    return std::memcmp(bytes, tag, 4) == 0;
}

struct Description
{
    double sampleRate = 0;
    char format[5] {};
    juce::uint32 flags = 0, bytesPerPacket = 0, framesPerPacket = 0, channels = 0, bits = 0;
    bool isFormat(const char* id) const { return std::memcmp(format, id, 4) == 0; }
};

struct Output
{
    int rate = 0;
    int channels = 1;
    int bits = 16;
    std::vector<float> left, right;
    int64_t frames() const { return (int64_t) left.size(); }
};

// G.711 mu-law, 16-bit output scale. 0xFF is silence, 0x00 the negative peak.
float muLawToFloat(int byte)
{
    const int value = ~byte & 0xff;
    const int sign = (value & 0x80) != 0 ? -1 : 1;
    const int exponent = (value >> 4) & 0x07;
    const int mantissa = value & 0x0f;
    const int magnitude = (((mantissa << 3) + 0x84) << exponent) - 0x84;
    return (float) (sign * magnitude) / 32768.0f;
}

// G.711 A-law after the 0x55 byte inversion. 0xD5 is the positive zero code.
float aLawToFloat(int byte)
{
    const int value = byte ^ 0x55;
    const int sign = (value & 0x80) != 0 ? 1 : -1;
    const int exponent = (value >> 4) & 0x07;
    const int mantissa = value & 0x0f;
    const int magnitude = exponent == 0
        ? (mantissa << 4) + 8
        : (((mantissa << 4) + 0x108) << (exponent - 1));
    return (float) (sign * magnitude) / 32768.0f;
}

const int imaStepTable[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
    73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
    494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272,
    2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};
const int imaIndexTable[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

struct ImaState
{
    int predictor = 0;
    int index = 0;
};

float imaNibble(ImaState& state, int nibble)
{
    const int step = imaStepTable[state.index];
    int difference = step >> 3;
    if ((nibble & 4) != 0) difference += step;
    if ((nibble & 2) != 0) difference += step >> 1;
    if ((nibble & 1) != 0) difference += step >> 2;
    state.predictor += (nibble & 8) != 0 ? -difference : difference;
    state.predictor = juce::jlimit(-32768, 32767, state.predictor);
    state.index = juce::jlimit(0, 88, state.index + imaIndexTable[nibble & 7]);
    return (float) state.predictor / 32768.0f;
}
} // namespace

bool decodeCaf(const std::vector<std::byte>& input, CafAudio& out, juce::String& error)
{
    error.clear();
    out = {};
    if (input.size() < 8 || (int64_t) input.size() > maxFileBytes)
    {
        error = "Not a Core Audio Format file or the file is too large";
        return false;
    }
    const auto* bytes = (const juce::uint8*) input.data();
    const auto size = (int64_t) input.size();
    if (! sameTag(bytes, cafMagic))
    {
        error = "Not a Core Audio Format file (missing caff header)";
        return false;
    }
    // Bytes 4..7 are the version (1) and flags (0). They are informational: the chunks below are
    // what decides whether the file can be read, so an unknown version is still parsed.

    auto fail = [&] (const juce::String& reason) { error = reason; return false; };

    Description desc;
    bool haveDesc = false;
    const juce::uint8* audio = nullptr;
    int64_t audioBytes = 0;
    int64_t validFrames = -1;

    int64_t position = 8;
    while (position + 12 <= size)
    {
        const auto* header = bytes + position;
        const auto* payload = header + 12;
        const auto available = size - position - 12;
        auto chunkSize = (int64_t) readBe(header + 4, 8);
        // -1 means "to the end of the file" and is only valid for the last chunk, normally data.
        if (chunkSize < 0) chunkSize = available;
        if (chunkSize > available) return fail("CAF chunk is truncated");

        if (sameTag(header, "desc"))
        {
            if (chunkSize < 32) return fail("CAF audio description is truncated");
            const auto rateBits = (juce::uint64) readBe(payload, 8);
            std::memcpy(&desc.sampleRate, &rateBits, 8);
            std::memcpy(desc.format, payload + 8, 4);
            desc.flags = (juce::uint32) readBe(payload + 12, 4);
            desc.bytesPerPacket = (juce::uint32) readBe(payload + 16, 4);
            desc.framesPerPacket = (juce::uint32) readBe(payload + 20, 4);
            desc.channels = (juce::uint32) readBe(payload + 24, 4);
            desc.bits = (juce::uint32) readBe(payload + 28, 4);
            haveDesc = true;
        }
        else if (sameTag(header, "data"))
        {
            if (chunkSize < 4) return fail("CAF audio chunk is truncated");
            // The first four bytes are an edit count, not audio.
            audio = payload + 4;
            audioBytes = chunkSize - 4;
        }
        else if (sameTag(header, "pakt"))
        {
            if (chunkSize >= 16) validFrames = (int64_t) readBe(payload + 8, 8);
        }
        // free, uuid, strg, chan, info, kuki, peak and mark carry no audio for the codecs read
        // here; ALAC's cookie lives in kuki and is refused by the codec check below.

        position += 12 + chunkSize;
    }

    if (! haveDesc) return fail("CAF file has no audio description");
    if (audio == nullptr) return fail("CAF file has no audio data");

    Output output;
    output.rate = (int) std::lround(desc.sampleRate);
    if (output.rate <= 0) return fail("CAF audio description has an invalid sample rate");
    if (desc.channels < 1 || desc.channels > 2)
        return fail("CAF with " + juce::String((int) desc.channels)
                    + " channels is not supported (mono and stereo only)");
    output.channels = (int) desc.channels;

    auto reserve = [&] (int64_t extra)
    {
        if (extra < 0 || output.frames() + extra > maxDecodedFrames)
            return fail("CAF exceeds the 128 MiB decoded audio limit");
        output.left.reserve(output.left.size() + (size_t) extra);
        if (output.channels > 1) output.right.reserve(output.right.size() + (size_t) extra);
        return true;
    };
    auto append = [&] (float left, float right)
    {
        if (output.frames() >= maxDecodedFrames) return false;
        output.left.push_back(left);
        if (output.channels > 1) output.right.push_back(right);
        return true;
    };

    if (desc.isFormat("lpcm"))
    {
        // CAF's own flags: bit 0 float, bit 1 little-endian. Integers without the little-endian
        // bit are big-endian, the opposite default from WAV and the usual source of bad readers.
        const bool isFloat = (desc.flags & 0x1u) != 0;
        const bool littleEndian = (desc.flags & 0x2u) != 0;
        const int bits = (int) desc.bits;
        if (isFloat)
        {
            if (bits != 32 && bits != 64)
                return fail("CAF floating-point audio must be 32 or 64 bits per sample");
        }
        else if (bits != 8 && bits != 16 && bits != 24 && bits != 32)
            return fail("CAF linear PCM must be 8, 16, 24 or 32 bits per sample");
        const int bytesPerSample = bits / 8;
        const int frameBytes = bytesPerSample * output.channels;
        if (desc.bytesPerPacket != 0 && (int) desc.bytesPerPacket != frameBytes)
            return fail("Unpacked CAF frames are not supported");
        const int64_t frames = audioBytes / frameBytes;
        if (! reserve(frames)) return false;
        const double scale = (double) ((juce::int64) 1 << (bits - 1));
        for (int64_t frame = 0; frame < frames; ++frame)
        {
            const auto* p = audio + frame * frameBytes;
            float values[2] { 0.0f, 0.0f };
            for (int channel = 0; channel < output.channels; ++channel)
            {
                const auto* sample = p + channel * bytesPerSample;
                if (isFloat)
                {
                    if (bits == 32)
                    {
                        juce::uint32 bits32 = 0;
                        if (littleEndian)
                            bits32 = (juce::uint32) ((juce::uint32) sample[0]
                                | ((juce::uint32) sample[1] << 8) | ((juce::uint32) sample[2] << 16)
                                | ((juce::uint32) sample[3] << 24));
                        else
                            bits32 = (juce::uint32) readBe(sample, 4);
                        float value = 0.0f;
                        std::memcpy(&value, &bits32, 4);
                        values[channel] = value;
                    }
                    else
                    {
                        juce::uint64 bits64 = 0;
                        if (littleEndian)
                            for (int i = 7; i >= 0; --i) bits64 = (bits64 << 8) | (juce::uint64) sample[i];
                        else
                            for (int i = 0; i < 8; ++i) bits64 = (bits64 << 8) | (juce::uint64) sample[i];
                        double value = 0.0;
                        std::memcpy(&value, &bits64, 8);
                        values[channel] = (float) value;
                    }
                }
                else
                    values[channel] = (float) ((double) readSigned(sample, bytesPerSample, littleEndian)
                                               / scale);
            }
            if (! append(values[0], values[1]))
                return fail("CAF exceeds the decoded audio limit");
        }
        output.bits = bits;
    }
    else if (desc.isFormat("ulaw") || desc.isFormat("alaw"))
    {
        const bool muLaw = desc.isFormat("ulaw");
        if (desc.bytesPerPacket != 0 && (int) desc.bytesPerPacket != output.channels)
            return fail("Unsupported CAF companded packet layout");
        const int64_t frames = audioBytes / output.channels;
        if (! reserve(frames)) return false;
        for (int64_t frame = 0; frame < frames; ++frame)
        {
            const auto* p = audio + frame * output.channels;
            const float left = muLaw ? muLawToFloat(p[0]) : aLawToFloat(p[0]);
            const float right = output.channels > 1
                ? (muLaw ? muLawToFloat(p[1]) : aLawToFloat(p[1])) : 0.0f;
            if (! append(left, right)) return fail("CAF exceeds the decoded audio limit");
        }
        output.bits = 8;
    }
    else if (desc.isFormat("ima4"))
    {
        // Apple IMA4 (the QuickTime 4:1 codec): 34 bytes per channel per packet and 64 frames per
        // packet. The first 16-bit big-endian word packs the step index in its low seven bits and
        // the predictor in the rest; the 32 bytes that follow hold two samples each, the low
        // nibble first. That order is the opposite of the more common high-nibble-first IMA
        // variants, so it is spelled out here.
        const int framesPerPacket = desc.framesPerPacket > 0 ? (int) desc.framesPerPacket : 64;
        const int packetBytes = 34 * output.channels;
        if ((int) desc.bytesPerPacket != packetBytes || framesPerPacket != 64)
            return fail("Unsupported CAF IMA4 packet layout");
        const int64_t packets = audioBytes / packetBytes;
        int64_t frames = packets * 64;
        if (validFrames > 0) frames = juce::jmin(frames, validFrames);
        if (! reserve(frames)) return false;
        for (int64_t packet = 0; packet < packets && output.frames() < frames; ++packet)
        {
            const auto* base = audio + packet * packetBytes;
            ImaState states[2];
            for (int channel = 0; channel < output.channels; ++channel)
            {
                const auto preamble = (int) readSigned(base + channel * 34, 2, false);
                states[channel].index = juce::jlimit(0, 88, preamble & 0x7F);
                states[channel].predictor = preamble & ~0x7F;
            }
            for (int i = 0; i < 64 && output.frames() < frames; ++i)
            {
                float values[2] { 0.0f, 0.0f };
                for (int channel = 0; channel < output.channels; ++channel)
                {
                    const auto byte = base[channel * 34 + 2 + i / 2];
                    const int nibble = (i % 2 == 0) ? (byte & 0x0f) : (byte >> 4);
                    values[channel] = imaNibble(states[channel], nibble);
                }
                if (! append(values[0], values[1]))
                    return fail("CAF exceeds the decoded audio limit");
            }
        }
        output.bits = 16;
    }
    else if (desc.isFormat("alac"))
        return fail("CAF ALAC compression is not supported");
    else if (desc.isFormat("aac "))
        return fail("CAF AAC compression is not supported");
    else
        return fail("Unsupported CAF codec '" + juce::String(desc.format, 4) + "'");

    if (output.frames() <= 0) return fail("CAF file has no audio frames");
    out.sampleRate = output.rate;
    out.channels = output.channels;
    out.bitsPerSample = output.bits;
    out.frames = output.frames();
    out.left = std::move(output.left);
    out.right = std::move(output.right);
    if (out.channels > 1 && out.right.size() != out.left.size())
    {
        error = "CAF stereo channels have different lengths";
        return false;
    }
    return true;
}

namespace
{
class CafAudioReader final : public juce::AudioFormatReader
{
public:
    CafAudioReader(CafAudio audio, juce::InputStream* stream)
        : juce::AudioFormatReader(stream, "Core Audio Format"), audio(std::move(audio))
    {
        sampleRate = this->audio.sampleRate;
        bitsPerSample = this->audio.bitsPerSample;
        usesFloatingPointData = false; // samples are normalised to full scale, like VOC
        lengthInSamples = this->audio.frames;
        numChannels = (unsigned int) this->audio.channels;
    }
    bool readSamples(int* const* destChannels, int numDestChannels, int startOffsetInDestBuffer,
                     juce::int64 startSampleInFile, int numSamples) override
    {
        const auto available = lengthInSamples - startSampleInFile;
        const int toRead = (int) juce::jlimit<juce::int64>(0, numSamples, available);
        for (int channel = 0; channel < numDestChannels; ++channel)
        {
            if (destChannels[channel] == nullptr) continue;
            const auto& source = channel == 1 && audio.right.size() == audio.left.size()
                ? audio.right : audio.left;
            for (int i = 0; i < toRead; ++i)
            {
                const float value = source[(size_t) (startSampleInFile + i)];
                const float scaled = juce::jlimit(-1.0f, 1.0f, value) * 2147483647.0f;
                destChannels[channel][startOffsetInDestBuffer + i] = (int) std::lround(scaled);
            }
            juce::zeromem(destChannels[channel] + startOffsetInDestBuffer + toRead,
                          (size_t) (numSamples - toRead) * sizeof(int));
        }
        return true;
    }
private:
    CafAudio audio;
};
} // namespace

CafAudioFormat::CafAudioFormat() : juce::AudioFormat("Core Audio Format (CAF)", ".caf") {}
juce::Array<int> CafAudioFormat::getPossibleSampleRates() { return {}; }
juce::Array<int> CafAudioFormat::getPossibleBitDepths() { return { 8, 16, 24, 32 }; }

juce::AudioFormatReader* CafAudioFormat::createReaderFor(juce::InputStream* sourceStream,
                                                         bool deleteStreamIfOpeningFails)
{
    auto refuse = [sourceStream, deleteStreamIfOpeningFails]() -> juce::AudioFormatReader*
    {
        if (deleteStreamIfOpeningFails) delete sourceStream;
        return nullptr;
    };
    if (sourceStream == nullptr) return nullptr;
    // The manager offers every registered format the same stream in registration order. This
    // format is registered first so macOS CoreAudio cannot claim `.caf`, so anything else must be
    // rejected after four bytes instead of after reading a whole file into memory.
    sourceStream->setPosition(0);
    char magic[4] {};
    if (sourceStream->read(magic, 4) != 4 || std::memcmp(magic, cafMagic, 4) != 0) return refuse();
    const auto total = sourceStream->getTotalLength();
    if (total <= 0 || total > maxFileBytes) return refuse();
    sourceStream->setPosition(0);
    std::vector<std::byte> bytes((size_t) total);
    size_t done = 0;
    while (done < bytes.size())
    {
        const int n = sourceStream->read(bytes.data() + done,
                                         (int) std::min<size_t>(bytes.size() - done, 1u << 20));
        if (n <= 0) break;
        done += (size_t) n;
    }
    if (done != bytes.size()) return refuse();
    CafAudio audio;
    juce::String error;
    if (! decodeCaf(bytes, audio, error)) return refuse();
    return new CafAudioReader(std::move(audio), sourceStream);
}

std::unique_ptr<juce::AudioFormatWriter> CafAudioFormat::createWriterFor(
    std::unique_ptr<juce::OutputStream>&, const juce::AudioFormatWriterOptions&)
{
    return nullptr; // CAF is read-only in Sonicator; the batch converter keeps its own outputs.
}
} // namespace sonicator
