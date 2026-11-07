#include "Rex2Decoder.h"
#include "core/smart/Rex2Metadata.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
namespace sonicator {
namespace {
constexpr int64_t maxFrames = 64 * 1024 * 1024;
int32_t clamp32(int64_t value)
{
    if (value > std::numeric_limits<int32_t>::max()) return std::numeric_limits<int32_t>::max();
    if (value < std::numeric_limits<int32_t>::min()) return std::numeric_limits<int32_t>::min();
    return (int32_t) value;
}
int32_t add32(int32_t a, int32_t b) { return clamp32((int64_t) a + b); }
int32_t sub32(int32_t a, int32_t b) { return clamp32((int64_t) a - b); }
int32_t clampSample(int64_t value, int bitDepth)
{
    const int32_t maximum = bitDepth == 24 ? 8388607 : 32767;
    const int32_t minimum = bitDepth == 24 ? -8388608 : -32768;
    if (value > maximum) return maximum;
    if (value < minimum) return minimum;
    return (int32_t) value;
}
// Port of the DWOP predictive decoder described by the REX2 format documentation and the
// VelociLoops reference implementation (Unlicense). It writes signed PCM at the authored bit depth.
class DwopDecoder
{
public:
    bool init(const juce::uint8* data, size_t size)
    {
        if (data == nullptr || size < 4) return false;
        words.resize(size / 4);
        for (size_t i = 0; i < words.size(); ++i)
        {
            const size_t b = i * 4;
            words[i] = ((juce::uint32) data[b] << 24) | ((juce::uint32) data[b + 1] << 16)
                     | ((juce::uint32) data[b + 2] << 8) | data[b + 3];
        }
        input = 0; eof = false;
        for (auto& channel : channels)
        {
            std::fill(channel.deltas, channel.deltas + 5, 0);
            std::fill(channel.averages, channel.averages + 5, 2560u);
        }
        currentWord = 0; bitsLeft = 0;
        return true;
    }
    bool mono(int frames, int32_t* out, int bitDepth)
    {
        return decode(frames, out, bitDepth, false);
    }
    bool stereo(int frames, int32_t* out, int bitDepth)
    {
        return decode(frames, out, bitDepth, true);
    }
private:
    struct Channel { int32_t deltas[5] = { 0, 0, 0, 0, 0 }; juce::uint32 averages[5] = { 2560, 2560, 2560, 2560, 2560 }; };
    std::vector<juce::uint32> words;
    Channel channels[2];
    size_t input = 0;
    juce::uint32 currentWord = 0;
    int bitsLeft = 0;
    bool eof = false;
    bool readBit(bool& bit)
    {
        const int before = bitsLeft--;
        if (before - 1 < 0)
        {
            if (input >= words.size()) { eof = true; return false; }
            currentWord = words[input++];
            bitsLeft = 31;
        }
        bit = (int32_t) currentWord < 0;
        currentWord <<= 1;
        return true;
    }
    juce::uint32 readBits(int count)
    {
        if (count <= 0 || count > 31) { eof = true; return 0; }
        juce::uint32 result = currentWord >> (32 - count);
        currentWord <<= count;
        const int before = bitsLeft;
        bitsLeft -= count;
        if (before - count < 0)
        {
            if (input >= words.size()) { eof = true; return result; }
            const juce::uint32 next = words[input++];
            bitsLeft += 32;
            result |= next >> bitsLeft;
            currentWord = next << (32 - bitsLeft);
        }
        return result;
    }
    static void adjustJRbits(juce::uint32 step, juce::uint32& j, int& rbits)
    {
        if (step < j)
        {
            for (juce::uint32 candidate = j >> 1; step < candidate; candidate >>= 1)
            {
                j = candidate;
                --rbits;
            }
        }
        else
        {
            while (step >= j)
            {
                const juce::uint32 previous = j;
                j <<= 1;
                ++rbits;
                if (j <= previous) { j = previous; break; }
            }
        }
    }
    static int32_t predictor(int index, int32_t sample2x, int32_t d[5])
    {
        switch (index)
        {
            case 0:
            {
                const int32_t t0 = sub32(sample2x, d[0]), t1 = sub32(t0, d[1]), t2 = sub32(t1, d[2]);
                d[4] = sub32(t2, d[3]); d[3] = t2; d[2] = t1; d[1] = t0; d[0] = sample2x;
                return sample2x;
            }
            case 1:
            {
                const int32_t t1 = sub32(sample2x, d[1]), t2 = sub32(t1, d[2]), next0 = add32(d[0], sample2x);
                d[4] = sub32(t2, d[3]); d[3] = t2; d[2] = t1; d[1] = sample2x; d[0] = next0;
                return next0;
            }
            case 2:
            {
                const int32_t next1 = add32(d[1], sample2x), next0 = add32(d[0], next1), t = sub32(sample2x, d[2]);
                d[4] = sub32(t, d[3]); d[3] = t; d[2] = sample2x; d[1] = next1; d[0] = next0;
                return next0;
            }
            case 3:
            {
                const int32_t next2 = add32(d[2], sample2x), next1 = add32(d[1], next2), next0 = add32(d[0], next1);
                d[4] = sub32(sample2x, d[3]); d[3] = sample2x; d[2] = next2; d[1] = next1; d[0] = next0;
                return next0;
            }
            case 4:
            {
                const int32_t next3 = add32(d[3], sample2x), next2 = add32(d[2], next3);
                const int32_t next1 = add32(d[1], next2), next0 = add32(d[0], next1);
                d[4] = sample2x; d[3] = next3; d[2] = next2; d[1] = next1; d[0] = next0;
                return next0;
            }
            default: return d[0];
        }
    }
    static void updateAverages(juce::uint32 a[5], const int32_t d[5])
    {
        for (int i = 0; i < 5; ++i)
        {
            const juce::uint32 magnitude = (juce::uint32) (d[i] ^ (d[i] >> 31));
            a[i] = a[i] + magnitude - (a[i] >> 5);
        }
    }
    bool decode(int frameCount, int32_t* out, int bitDepth, bool stereoMode)
    {
        if (out == nullptr || frameCount <= 0) return false;
        struct State { int32_t d[5]; juce::uint32 a[5]; juce::uint32 j; int rbits; };
        State state[2];
        for (int c = 0; c < 2; ++c)
        {
            for (int i = 0; i < 5; ++i) { state[c].d[i] = channels[c].deltas[i] * 2; state[c].a[i] = channels[c].averages[i]; }
            state[c].j = 2; state[c].rbits = 0;
        }
        for (int frame = 0; frame < frameCount && ! eof; ++frame)
        {
            int32_t coded2x[2] = { 0, 0 };
            for (int c = 0; c < (stereoMode ? 2 : 1) && ! eof; ++c)
            {
                auto& s = state[c];
                juce::uint32 minimum = s.a[0];
                int index = 0;
                for (int i = 1; i < 5; ++i) if (s.a[i] < minimum) { minimum = s.a[i]; index = i; }
                juce::uint32 step = ((minimum * 3u) + 36u) >> 7;
                juce::uint32 prefix = 0;
                int zerosWindow = 7;
                for (;;)
                {
                    bool bit = false;
                    if (! readBit(bit) || eof) break;
                    if (bit) break;
                    if (step > 0 && prefix > 0xffffffffu - step) { eof = true; break; }
                    prefix += step;
                    if (--zerosWindow == 0) { step <<= 2; zerosWindow = 7; }
                }
                if (eof) break;
                adjustJRbits(step, s.j, s.rbits);
                juce::uint32 remainder = s.rbits > 0 ? readBits(s.rbits) : 0;
                if (eof) break;
                const int64_t threshold = (int64_t) s.j - (int64_t) step;
                if ((int64_t) remainder - threshold >= 0)
                {
                    const juce::uint32 extra = readBits(1);
                    if (eof) break;
                    remainder = remainder * 2u - (juce::uint32) threshold + extra;
                }
                const juce::uint32 code = remainder + prefix;
                const int32_t signed2x = -(int32_t) (code & 1u) ^ (int32_t) code;
                coded2x[c] = predictor(index, signed2x, s.d);
                updateAverages(s.a, s.d);
            }
            if (eof) break;
            if (stereoMode)
            {
                out[(size_t) frame * 2] = clampSample(coded2x[0] >> 1, bitDepth);
                out[(size_t) frame * 2 + 1] = clampSample(((int64_t) coded2x[0] + coded2x[1]) >> 1, bitDepth);
            }
            else
                out[frame] = clampSample(coded2x[0] >> 1, bitDepth);
        }
        for (int c = 0; c < 2; ++c)
        {
            for (int i = 0; i < 5; ++i)
            {
                channels[c].deltas[i] = state[c].d[i] >> 1;
                channels[c].averages[i] = state[c].a[i];
            }
        }
        return ! eof;
    }
};
} // namespace
bool decodeRex2Audio(const juce::File& file, Rex2Audio& audio, juce::String& error)
{
    audio = {};
    const auto metadata = extractRex2Metadata(file);
    if (metadata.error.isNotEmpty()) { error = metadata.error; return false; }
    const auto& info = metadata.info;
    if (info.channels < 1 || info.channels > 2) { error = "REX2 channel count is unsupported"; return false; }
    if (info.sampleRate <= 0) { error = "REX2 sample rate is invalid"; return false; }
    if (info.sdatOffset <= 0 || info.sdatSize <= 0) { error = "REX2 has no SDAT audio"; return false; }
    if (info.frames <= 0 || info.frames > maxFrames) { error = "REX2 frame count is out of range"; return false; }
    const int bitDepth = info.format == 3 ? 16 : (info.format == 5 ? 24 : 0);
    if (bitDepth == 0) { error = "REX2 bit depth is unsupported"; return false; }
    juce::FileInputStream stream(file);
    if (! stream.openedOk() || ! stream.setPosition(info.sdatOffset))
    {
        error = "Cannot read REX2 audio";
        return false;
    }
    std::vector<juce::uint8> compressed((size_t) info.sdatSize);
    if (stream.read(compressed.data(), (int) compressed.size()) != (int) compressed.size())
    {
        error = "Truncated REX2 audio chunk";
        return false;
    }
    DwopDecoder decoder;
    if (! decoder.init(compressed.data(), compressed.size())) { error = "Invalid DWOP stream"; return false; }
    const size_t channels = (size_t) info.channels;
    std::vector<int32_t> interleaved((size_t) info.frames * channels);
    constexpr int block = 0x100000;
    int64_t done = 0;
    while (done < info.frames)
    {
        const int count = (int) juce::jmin<int64_t>(block, info.frames - done);
        const bool ok = info.channels == 1
            ? decoder.mono(count, interleaved.data() + done, bitDepth)
            : decoder.stereo(count, interleaved.data() + (size_t) done * 2, bitDepth);
        if (! ok) { error = "DWOP decode failed"; return false; }
        done += count;
    }
    const float scale = bitDepth == 24 ? 1.0f / 8388608.0f : 1.0f / 32768.0f;
    audio.channels = info.channels;
    audio.sampleRate = info.sampleRate;
    audio.bitDepth = bitDepth;
    audio.frames = info.frames;
    audio.left.resize((size_t) info.frames);
    if (info.channels == 2) audio.right.resize((size_t) info.frames);
    for (size_t i = 0; i < (size_t) info.frames; ++i)
    {
        audio.left[i] = (float) interleaved[i * channels] * scale;
        if (info.channels == 2) audio.right[i] = (float) interleaved[i * channels + 1] * scale;
    }
    return true;
}
} // namespace sonicator
