#include "AcousticFeatures.h"
#include <cmath>
#include <algorithm>
#include <vector>

namespace sonicator {

AcousticDescriptors AcousticFeatures::extract(const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    if (sampleRate <= 0.0 || buffer.getNumSamples() == 0 || buffer.getNumChannels() == 0)
        return AcousticDescriptors{};

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    // Check for non-finite values and absolute peak across channels
    float peak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* data = buffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
        {
            float val = data[i];
            if (!std::isfinite(val))
                return AcousticDescriptors{};
            float absVal = std::abs(val);
            if (absVal > peak)
                peak = absVal;
        }
    }

    // Silence threshold
    if (peak < 1e-5f)
        return AcousticDescriptors{};

    // Downmixed mono representation and overall buffer RMS
    std::vector<float> mono(static_cast<size_t>(numSamples), 0.0f);
    double totalEnergySum = 0.0;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* data = buffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
        {
            mono[static_cast<size_t>(i)] += data[i];
            totalEnergySum += static_cast<double>(data[i]) * static_cast<double>(data[i]);
        }
    }
    const float invChannels = 1.0f / static_cast<float>(numChannels);
    for (int i = 0; i < numSamples; ++i)
        mono[static_cast<size_t>(i)] *= invChannels;

    const double totalSampleCount = static_cast<double>(numChannels) * static_cast<double>(numSamples);
    const double bufferRms = (totalSampleCount > 0.0) ? std::sqrt(totalEnergySum / totalSampleCount) : 0.0;
    const double totalDuration = static_cast<double>(numSamples) / sampleRate;

    AcousticDescriptors desc;
    desc.crestFactor = (bufferRms > 1e-9) ? (static_cast<double>(peak) / bufferRms) : 0.0;

    // Temporal Centroid: sum(t_i * energy_i) / (sum(energy_i) * totalDuration)
    double monoEnergySum = 0.0;
    double weightedTimeEnergy = 0.0;
    for (int i = 0; i < numSamples; ++i)
    {
        double e = static_cast<double>(mono[static_cast<size_t>(i)]) * static_cast<double>(mono[static_cast<size_t>(i)]);
        double t = static_cast<double>(i) / sampleRate;
        monoEnergySum += e;
        weightedTimeEnergy += t * e;
    }
    if (monoEnergySum > 1e-12 && totalDuration > 0.0)
        desc.temporalCentroid = juce::jlimit(0.0, 1.0, weightedTimeEnergy / (monoEnergySum * totalDuration));
    else
        desc.temporalCentroid = 0.0;

    // Envelope & Attack / Decay calculation
    // Frame size ~2.5 ms, hop ~1.0 ms
    const int frameSize = std::max(16, static_cast<int>(sampleRate * 0.0025));
    const int hopSize = std::max(8, static_cast<int>(sampleRate * 0.0010));

    std::vector<double> frameRms;
    std::vector<double> frameTime;

    if (numSamples <= frameSize)
    {
        double sumSq = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            double v = mono[static_cast<size_t>(i)];
            sumSq += v * v;
        }
        frameRms.push_back(std::sqrt(sumSq / numSamples));
        frameTime.push_back(totalDuration * 0.5);
    }
    else
    {
        const float* monoData = mono.data();
        for (int start = 0; start + frameSize <= numSamples; start += hopSize)
        {
            const float* p = monoData + start;
            float sumSq = 0.0f;
            for (int i = 0; i < frameSize; ++i)
            {
                float v = p[i];
                sumSq += v * v;
            }
            frameRms.push_back(std::sqrt(static_cast<double>(sumSq) / frameSize));
            frameTime.push_back((static_cast<double>(start) + frameSize * 0.5) / sampleRate);
        }
    }

    if (frameRms.empty())
        return desc;

    int peakFrameIdx = 0;
    double maxRms = 0.0;
    for (size_t i = 0; i < frameRms.size(); ++i)
    {
        if (frameRms[i] > maxRms)
        {
            maxRms = frameRms[i];
            peakFrameIdx = static_cast<int>(i);
        }
    }

    if (maxRms > 1e-9)
    {
        const double peakTime = frameTime[static_cast<size_t>(peakFrameIdx)];
        const double target10 = 0.10 * maxRms;
        const double target90 = 0.90 * maxRms;

        // Attack: 10% to 90% peak RMS preceding peak
        double t10 = 0.0;
        double t90 = peakTime;

        bool found10 = false;
        for (int i = 0; i <= peakFrameIdx; ++i)
        {
            if (frameRms[static_cast<size_t>(i)] >= target10)
            {
                if (i > 0 && frameRms[static_cast<size_t>(i)] > frameRms[static_cast<size_t>(i - 1)])
                {
                    double frac = (target10 - frameRms[static_cast<size_t>(i - 1)])
                                  / (frameRms[static_cast<size_t>(i)] - frameRms[static_cast<size_t>(i - 1)]);
                    t10 = frameTime[static_cast<size_t>(i - 1)]
                          + frac * (frameTime[static_cast<size_t>(i)] - frameTime[static_cast<size_t>(i - 1)]);
                }
                else
                {
                    t10 = (frameRms[0] > 0.0) ? (frameTime[0] * (target10 / frameRms[0])) : frameTime[0];
                }
                found10 = true;
                break;
            }
        }
        if (!found10)
            t10 = 0.0;

        bool found90 = false;
        for (int i = 0; i <= peakFrameIdx; ++i)
        {
            if (frameRms[static_cast<size_t>(i)] >= target90)
            {
                if (i > 0 && frameRms[static_cast<size_t>(i)] > frameRms[static_cast<size_t>(i - 1)])
                {
                    double frac = (target90 - frameRms[static_cast<size_t>(i - 1)])
                                  / (frameRms[static_cast<size_t>(i)] - frameRms[static_cast<size_t>(i - 1)]);
                    t90 = frameTime[static_cast<size_t>(i - 1)]
                          + frac * (frameTime[static_cast<size_t>(i)] - frameTime[static_cast<size_t>(i - 1)]);
                }
                else
                {
                    t90 = frameTime[static_cast<size_t>(i)];
                }
                found90 = true;
                break;
            }
        }
        if (!found90)
            t90 = peakTime;

        double attackDuration = std::max(0.0001, t90 - t10);
        desc.logAttackTime = std::log10(attackDuration);

        // Decay slope: dB/sec decay after peak
        const double targetMinus20Db = maxRms * 0.10; // -20 dB down
        bool reached20DbDown = false;
        double tDecay = peakTime;

        for (size_t i = static_cast<size_t>(peakFrameIdx + 1); i < frameRms.size(); ++i)
        {
            if (frameRms[i] <= targetMinus20Db)
            {
                if (frameRms[i - 1] > frameRms[i])
                {
                    double frac = (frameRms[i - 1] - targetMinus20Db) / (frameRms[i - 1] - frameRms[i]);
                    tDecay = frameTime[i - 1] + frac * (frameTime[i] - frameTime[i - 1]);
                }
                else
                {
                    tDecay = frameTime[i];
                }
                reached20DbDown = true;
                break;
            }
        }

        if (reached20DbDown)
        {
            double dt = tDecay - peakTime;
            desc.decayRate = (dt > 1e-4) ? (20.0 / dt) : 0.0;
        }
        else if (frameRms.size() > static_cast<size_t>(peakFrameIdx + 1))
        {
            double dt = frameTime.back() - peakTime;
            double endRms = std::max(1e-9, frameRms.back());
            double dropDb = 20.0 * std::log10(maxRms / endRms);
            desc.decayRate = (dt > 1e-4 && dropDb > 0.0) ? (dropDb / dt) : 0.0;
        }
        else
        {
            desc.decayRate = 0.0;
        }
    }

    // Zero-crossing rate: sign changes in audio channel 0 divided by duration in seconds
    {
        int zeroCrossings = 0;
        const float* ch0 = buffer.getReadPointer(0);
        for (int i = 1; i < numSamples; ++i)
        {
            if ((ch0[i - 1] >= 0.0f && ch0[i] < 0.0f) || (ch0[i - 1] < 0.0f && ch0[i] >= 0.0f))
            {
                ++zeroCrossings;
            }
        }
        desc.zeroCrossingRate = (totalDuration > 0.0) ? (static_cast<double>(zeroCrossings) / totalDuration) : 0.0;
    }

    // Sub-bands & Spectral Flux:
    // 5 bands: Sub <80Hz, Low 80-350Hz, Mid 350-2500Hz, High-Mid 2.5-7kHz, Air >7kHz
    {
        constexpr int fftOrder = 11;
        constexpr int fftSize = 1 << fftOrder; // 2048
        constexpr int numBins = fftSize / 2 + 1;
        const int fftHop = fftSize / 2; // 1024

        juce::dsp::FFT fft(fftOrder);
        juce::dsp::WindowingFunction<float> window(fftSize, juce::dsp::WindowingFunction<float>::hann, false);

        std::array<double, 5> bandEnergySum {};
        std::vector<float> prevNormalizedSpectrum;
        double totalSpectralFlux = 0.0;
        int fluxPairCount = 0;

        std::array<float, fftSize * 2> transform {};
        std::vector<float> frameSpectrum(numBins, 0.0f);

        const int bin80 = std::min(numBins, static_cast<int>(std::ceil(80.0 * static_cast<double>(fftSize) / sampleRate)));
        const int bin350 = std::min(numBins, static_cast<int>(std::ceil(350.0 * static_cast<double>(fftSize) / sampleRate)));
        const int bin2500 = std::min(numBins, static_cast<int>(std::ceil(2500.0 * static_cast<double>(fftSize) / sampleRate)));
        const int bin7000 = std::min(numBins, static_cast<int>(std::ceil(7000.0 * static_cast<double>(fftSize) / sampleRate)));

        int numFftFrames = (numSamples <= fftSize) ? 1 : ((numSamples - fftSize) / fftHop + 1);

        for (int f = 0; f < numFftFrames; ++f)
        {
            int offset = f * fftHop;
            int count = std::min(fftSize, numSamples - offset);
            if (count <= 0)
                break;

            std::fill(frameSpectrum.begin(), frameSpectrum.end(), 0.0f);

            // Accumulate power across channels to prevent out-of-phase cancellation
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float* chData = buffer.getReadPointer(ch) + offset;
                transform.fill(0.0f);
                std::copy(chData, chData + count, transform.begin());

                window.multiplyWithWindowingTable(transform.data(), fftSize);
                fft.performFrequencyOnlyForwardTransform(transform.data());

                for (int k = 0; k < numBins; ++k)
                {
                    float mag = transform[static_cast<size_t>(k)];
                    frameSpectrum[static_cast<size_t>(k)] += mag;

                    int band = (k < bin80) ? 0 : ((k < bin350) ? 1 : ((k < bin2500) ? 2 : ((k < bin7000) ? 3 : 4)));
                    bandEnergySum[static_cast<size_t>(band)] += static_cast<double>(mag) * static_cast<double>(mag);
                }
            }

            // Normalize frame spectrum for spectral flux
            double frameMagL2 = 0.0;
            for (int k = 0; k < numBins; ++k)
            {
                frameSpectrum[static_cast<size_t>(k)] *= invChannels;
                frameMagL2 += static_cast<double>(frameSpectrum[static_cast<size_t>(k)])
                              * static_cast<double>(frameSpectrum[static_cast<size_t>(k)]);
            }
            frameMagL2 = std::sqrt(frameMagL2);

            std::vector<float> normalizedSpectrum(numBins, 0.0f);
            if (frameMagL2 > 1e-9)
            {
                float invL2 = static_cast<float>(1.0 / frameMagL2);
                for (int k = 0; k < numBins; ++k)
                    normalizedSpectrum[static_cast<size_t>(k)] = frameSpectrum[static_cast<size_t>(k)] * invL2;
            }

            if (!prevNormalizedSpectrum.empty())
            {
                double diffL2 = 0.0;
                for (int k = 0; k < numBins; ++k)
                {
                    double diff = static_cast<double>(normalizedSpectrum[static_cast<size_t>(k)])
                                  - static_cast<double>(prevNormalizedSpectrum[static_cast<size_t>(k)]);
                    diffL2 += diff * diff;
                }
                totalSpectralFlux += std::sqrt(diffL2);
                fluxPairCount++;
            }

            prevNormalizedSpectrum = std::move(normalizedSpectrum);
        }

        double totalBandEnergy = 0.0;
        for (double e : bandEnergySum)
            totalBandEnergy += e;

        if (totalBandEnergy > 1e-12)
        {
            for (int b = 0; b < 5; ++b)
                desc.subBandEnergy[static_cast<size_t>(b)] = bandEnergySum[static_cast<size_t>(b)] / totalBandEnergy;
        }
        else
        {
            desc.subBandEnergy.fill(0.0);
        }

        desc.spectralFlux = (fluxPairCount > 0) ? (totalSpectralFlux / fluxPairCount) : 0.0;
    }

    // Pitch clarity: YIN normalized difference function on representative window
    {
        if (numSamples < 64)
        {
            desc.pitchClarity = 0.0;
        }
        else
        {
            int tauMax = std::max(10, static_cast<int>(sampleRate / 30.0));
            int tauMin = std::max(2, static_cast<int>(sampleRate / 2500.0));

            if (tauMax > numSamples / 2)
                tauMax = numSamples / 2;

            if (tauMax <= tauMin)
                tauMin = std::max(1, tauMax / 2);

            if (tauMax > tauMin)
            {
                constexpr int targetW = 512;
                int W = std::min(targetW, numSamples - tauMax);

                int peakSample = 0;
                float peakMonoVal = 0.0f;
                const float* monoData = mono.data();
                for (int i = 0; i < numSamples; ++i)
                {
                    float absVal = std::abs(monoData[i]);
                    if (absVal > peakMonoVal)
                    {
                        peakMonoVal = absVal;
                        peakSample = i;
                    }
                }

                int start = peakSample;
                if (start + W + tauMax > numSamples)
                    start = std::max(0, numSamples - W - tauMax);

                const float* pA = monoData + start;
                std::vector<double> d(static_cast<size_t>(tauMax + 1), 0.0);
                for (int tau = 1; tau <= tauMax; ++tau)
                {
                    const float* pB = pA + tau;
                    float sumSq = 0.0f;
                    for (int j = 0; j < W; ++j)
                    {
                        float diff = pA[j] - pB[j];
                        sumSq += diff * diff;
                    }
                    d[static_cast<size_t>(tau)] = static_cast<double>(sumSq);
                }

                double runningSum = 0.0;
                double minNormDiff = 1.0;
                for (int tau = 1; tau <= tauMax; ++tau)
                {
                    runningSum += d[static_cast<size_t>(tau)];
                    double normDiff = (runningSum > 1e-9) ? (d[static_cast<size_t>(tau)] * tau / runningSum) : 1.0;
                    if (tau >= tauMin)
                    {
                        if (normDiff < minNormDiff)
                            minNormDiff = normDiff;
                    }
                }

                desc.pitchClarity = juce::jlimit(0.0, 1.0, 1.0 - minNormDiff);
            }
            else
            {
                desc.pitchClarity = 0.0;
            }
        }
    }

    return desc;
}

} // namespace sonicator
