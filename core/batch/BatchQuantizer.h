#pragma once
#include "BatchPipeline.h"
#include <cstdint>
#include <memory>

namespace sonicator::batch
{
// Seed for the dither generator: stable for one source/format/settings combination, independent
// of the output name and block size.
uint64_t ditherSeed(const SourceSnapshot&, Format, Encoding);

// Encoding-aware quantization. Integer writers receive already quantized left-aligned int32 words,
// so no second dither or sign shift happens inside JUCE.
class Quantizer
{
public:
    void reset(uint64_t seed, bool dither, int targetBits);
    void quantize(const float* const* input, int channels, int frames, int32_t* const* output);
    bool dithering() const { return useDither; }
    // One TPDF dither sample in target LSB units, before rounding. Public so the numeric contract
    // (mean 0, variance 1/6 LSB^2) is testable without a test-only switch.
    double ditherSample(int channel);
private:
    uint64_t next(int channel);
    double uniform(int channel);
    std::array<uint64_t, 2> state { 0x9e3779b97f4a7c15ULL, 0xbf58476d1ce4e5b9ULL };
    bool useDither = false;
    int bits = 24;
};

// Auto dither is off for untouched PCM at the same or a wider depth and on for float/lossy
// sources, reduced target depth and any real amplitude change (chain steps, SRC, channels).
bool automaticDither(Encoding target, const SourceSnapshot&, bool amplitudeChanged);
} // namespace sonicator::batch
