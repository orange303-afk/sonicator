#include "MelSpectrogram.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <algorithm>
#include <vector>

namespace sonicator {

namespace {

inline float hzToMel(float hz)
{
    return 2595.0f * std::log10(1.0f + std::max(0.0f, hz) / 700.0f);
}

inline float melToHz(float mel)
{
    return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f);
}

} // namespace

int MelSpectrogram::calculateNumFrames(int numSamples, int fftSize, int hopSize)
{
    if (numSamples < fftSize || hopSize <= 0 || fftSize <= 0)
        return 0;
    return (numSamples - fftSize) / hopSize + 1;
}

std::vector<float> MelSpectrogram::computeLogMel(const juce::AudioBuffer<float>& buffer,
                                                double sampleRate,
                                                int melBands,
                                                int fftSize,
                                                int hopSize)
{
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    if (numChannels == 0 || numSamples == 0 || sampleRate <= 40.0 || melBands <= 0 || fftSize <= 0 || hopSize <= 0)
        return {};

    const int numFrames = calculateNumFrames(numSamples, fftSize, hopSize);
    if (numFrames <= 0)
        return {};

    // Check that fftSize is a power of 2
    int fftOrder = 0;
    while ((1 << fftOrder) < fftSize)
        ++fftOrder;

    if ((1 << fftOrder) != fftSize)
        return {};

    // Downmix to mono
    std::vector<float> mono(static_cast<size_t>(numSamples), 0.0f);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* chData = buffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
        {
            float s = chData[i];
            if (std::isfinite(s))
                mono[static_cast<size_t>(i)] += s;
        }
    }
    const float invChannels = 1.0f / static_cast<float>(numChannels);
    for (int i = 0; i < numSamples; ++i)
        mono[static_cast<size_t>(i)] *= invChannels;

    // Build triangular mel filterbank matrix from 20 Hz to sampleRate / 2
    const float fMin = 20.0f;
    const float fMax = static_cast<float>(sampleRate / 2.0);
    if (fMax <= fMin)
        return {};

    const float melMin = hzToMel(fMin);
    const float melMax = hzToMel(fMax);

    std::vector<float> binPoints(static_cast<size_t>(melBands + 2));
    for (int i = 0; i < melBands + 2; ++i)
    {
        float mel = melMin + static_cast<float>(i) * (melMax - melMin) / static_cast<float>(melBands + 1);
        float hz = melToHz(mel);
        binPoints[static_cast<size_t>(i)] = hz * static_cast<float>(fftSize) / static_cast<float>(sampleRate);
    }

    const int numBins = fftSize / 2 + 1;
    // melWeights[band][bin]
    std::vector<std::vector<float>> melWeights(static_cast<size_t>(melBands), std::vector<float>(static_cast<size_t>(numBins), 0.0f));

    for (int b = 0; b < melBands; ++b)
    {
        float binL = binPoints[static_cast<size_t>(b)];
        float binC = binPoints[static_cast<size_t>(b + 1)];
        float binR = binPoints[static_cast<size_t>(b + 2)];

        for (int k = 0; k < numBins; ++k)
        {
            float fk = static_cast<float>(k);
            float weight = 0.0f;
            if (fk >= binL && fk <= binC)
            {
                float denom = binC - binL;
                weight = (denom > 1e-6f) ? (fk - binL) / denom : 1.0f;
            }
            else if (fk > binC && fk <= binR)
            {
                float denom = binR - binC;
                weight = (denom > 1e-6f) ? (binR - fk) / denom : 0.0f;
            }
            melWeights[static_cast<size_t>(b)][static_cast<size_t>(k)] = weight;
        }
    }

    juce::dsp::FFT fft(fftOrder);
    juce::dsp::WindowingFunction<float> window(static_cast<size_t>(fftSize), juce::dsp::WindowingFunction<float>::hann, false);

    std::vector<float> fftBuffer(static_cast<size_t>(fftSize * 2), 0.0f);
    // Flattened log-mel spectrogram (melBands x numFrames) in row-major order:
    // [band0_frame0, band0_frame1, ..., band1_frame0, ...]
    std::vector<float> result(static_cast<size_t>(melBands * numFrames), 0.0f);

    for (int f = 0; f < numFrames; ++f)
    {
        int offset = f * hopSize;
        std::fill(fftBuffer.begin(), fftBuffer.end(), 0.0f);
        std::copy_n(mono.data() + offset, fftSize, fftBuffer.begin());

        window.multiplyWithWindowingTable(fftBuffer.data(), static_cast<size_t>(fftSize));
        fft.performFrequencyOnlyForwardTransform(fftBuffer.data());

        for (int b = 0; b < melBands; ++b)
        {
            float melPower = 0.0f;
            const auto& weights = melWeights[static_cast<size_t>(b)];

            for (int k = 0; k < numBins; ++k)
            {
                float w = weights[static_cast<size_t>(k)];
                if (w > 0.0f)
                {
                    float mag = fftBuffer[static_cast<size_t>(k)];
                    melPower += w * (mag * mag);
                }
            }

            // Log compression: log(1.0f + 10.0f * melPower)
            float logVal = std::log(1.0f + 10.0f * melPower);
            result[static_cast<size_t>(b * numFrames + f)] = logVal;
        }
    }

    return result;
}

} // namespace sonicator
