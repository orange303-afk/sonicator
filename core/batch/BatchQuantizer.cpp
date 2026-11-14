#include "BatchQuantizer.h"
#include <cmath>

namespace sonicator::batch
{
namespace
{
uint64_t mix(uint64_t value)
{
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}
} // namespace

uint64_t ditherSeed(const SourceSnapshot& source, Format format, Encoding encoding)
{
    juce::String text;
    text << source.identity << "|" << source.path.getFullPathName() << "|" << (int) format << "|"
         << (int) encoding;
    const auto hash = juce::SHA256(text.toRawUTF8(), text.getNumBytesAsUTF8()).toHexString();
    return (uint64_t) hash.substring(0, 16).getHexValue64();
}

void Quantizer::reset(uint64_t seed, bool dither, int targetBits)
{
    state[0] = mix(seed);
    state[1] = mix(seed ^ 0xd1b54a32d192ed03ULL);
    useDither = dither;
    bits = juce::jlimit(1, 24, targetBits);
}

uint64_t Quantizer::next(int channel)
{
    auto& value = state[(size_t) juce::jlimit(0, 1, channel)];
    value ^= value >> 12;
    value ^= value << 25;
    value ^= value >> 27;
    return value * 0x2545f4914f6cdd1dULL;
}

double Quantizer::uniform(int channel)
{
    // 53 random bits fill the mantissa of a double in [0, 1).
    return (double) (next(channel) >> 11) * (1.0 / 9007199254740992.0);
}

double Quantizer::ditherSample(int channel)
{
    return uniform(channel) - uniform(channel);
}

void Quantizer::quantize(const float* const* input, int channels, int frames,
                         int32_t* const* output)
{
    const double scale = std::ldexp(1.0, bits - 1);
    const double minimum = -scale;
    const double maximum = scale - 1.0;
    const int shift = 32 - bits;
    for (int channel = 0; channel < channels; ++channel)
    {
        auto* destination = output[channel];
        const auto* source = input[channel];
        for (int n = 0; n < frames; ++n)
        {
            double value = (double) source[n] * scale;
            if (useDither)
            {
                // TPDF: U1 - U2, independent in [0, 1) per channel and per sample.
                value += ditherSample(channel);
            }
            value = std::floor(value + 0.5);
            value = juce::jlimit(minimum, maximum, value);
            const auto integer = (int64_t) value;
            destination[n] = (int32_t) ((uint32_t) integer << shift);
        }
    }
}

bool automaticDither(Encoding target, const SourceSnapshot& source, bool amplitudeChanged)
{
    if (target == Encoding::float32) return false;
    if (source.floating || source.bits == 0) return true;
    if (target == Encoding::int16 && source.bits > 16) return true;
    return amplitudeChanged;
}
} // namespace sonicator::batch
