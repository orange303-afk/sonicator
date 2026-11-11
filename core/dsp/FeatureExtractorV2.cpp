#include "FeatureExtractorV2.h"
#include "AcousticFeatures.h"
#include "core/ml/AdaptiveClassifier.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace sonicator {
namespace {

constexpr int fftOrder = 11;
constexpr int fftSize = 1 << fftOrder;
constexpr int fftHop = 512;
constexpr double analysisLowHz = 40.0;
constexpr double analysisHighHz = 8000.0;
constexpr int numBins = fftSize / 2 + 1;
constexpr int melFilters = 40;
constexpr int mfccCount = 13;
constexpr int contrastBands = 6;
constexpr int envelopeBands = 4;
constexpr int shapeStats = 9;

constexpr double contrastCentres[contrastBands] = {100.0, 300.0, 800.0, 2000.0, 4000.0, 7000.0};
constexpr double envelopeEdges[envelopeBands + 1] = {0.0, 300.0, 1500.0, 4000.0, 8000.0};
constexpr double earlyShareSeconds = 0.05;

float clamp01(double value)
{
    if (! std::isfinite(value))
        return 0.0f;
    return static_cast<float>(std::clamp(value, 0.0, 1.0));
}

double binToHz(int bin, double sampleRate) { return static_cast<double>(bin) * sampleRate / fftSize; }
int hzToBin(double hz, double sampleRate) { return static_cast<int>(std::lround(hz * fftSize / sampleRate)); }
double melOf(double hz) { return 2595.0 * std::log10(1.0 + hz / 700.0); }
double hzOfMel(double mel) { return 700.0 * (std::pow(10.0, mel / 2595.0) - 1.0); }

struct MelFilter
{
    int low = 0;
    int centre = 0;
    int high = 0;
};

std::vector<MelFilter> makeMelFilters(double sampleRate)
{
    std::vector<MelFilter> filters;
    const double melLow = melOf(analysisLowHz);
    const double melHigh = melOf(std::min(analysisHighHz, sampleRate * 0.5));
    std::vector<int> edges(static_cast<size_t>(melFilters) + 2);
    for (int i = 0; i < melFilters + 2; ++i)
    {
        const double mel = melLow + (melHigh - melLow) * static_cast<double>(i) / (melFilters + 1);
        edges[static_cast<size_t>(i)] = std::clamp(hzToBin(hzOfMel(mel), sampleRate), 0, numBins - 1);
    }
    for (int i = 0; i < melFilters; ++i)
    {
        MelFilter filter { edges[static_cast<size_t>(i)], edges[static_cast<size_t>(i) + 1],
                           edges[static_cast<size_t>(i) + 2] };
        filter.centre = std::clamp(filter.centre, filter.low, filter.high);
        filters.push_back(filter);
    }
    return filters;
}

double mean(const std::vector<double>& values)
{
    if (values.empty()) return 0.0;
    double sum = 0.0;
    for (double value : values) sum += value;
    return sum / static_cast<double>(values.size());
}

double standardDeviation(const std::vector<double>& values)
{
    if (values.size() < 2) return 0.0;
    const double average = mean(values);
    double sum = 0.0;
    for (double value : values) sum += (value - average) * (value - average);
    return std::sqrt(sum / static_cast<double>(values.size()));
}

double pearson(const std::vector<double>& a, const std::vector<double>& b)
{
    const size_t size = std::min(a.size(), b.size());
    if (size < 2) return 0.0;
    double meanA = 0.0, meanB = 0.0;
    for (size_t i = 0; i < size; ++i) { meanA += a[i]; meanB += b[i]; }
    meanA /= static_cast<double>(size);
    meanB /= static_cast<double>(size);
    double covariance = 0.0, varA = 0.0, varB = 0.0;
    for (size_t i = 0; i < size; ++i)
    {
        const double da = a[i] - meanA;
        const double db = b[i] - meanB;
        covariance += da * db;
        varA += da * da;
        varB += db * db;
    }
    if (varA <= 1e-12 || varB <= 1e-12) return 0.0;
    return std::clamp(covariance / std::sqrt(varA * varB), -1.0, 1.0);
}

// Peak of the normalised autocorrelation of a mean-removed envelope, over a lag window in frames.
double periodicity(const std::vector<double>& envelope, double secondsPerFrame, double minLagSeconds,
                   double maxLagSeconds)
{
    if (envelope.size() < 4) return 0.0;
    const double average = mean(envelope);
    std::vector<double> centred(envelope.size());
    for (size_t i = 0; i < envelope.size(); ++i) centred[i] = envelope[i] - average;

    const size_t minLag = std::max<size_t>(1, static_cast<size_t>(minLagSeconds / secondsPerFrame));
    const size_t maxLag = std::min<size_t>(centred.size() / 2, static_cast<size_t>(maxLagSeconds / secondsPerFrame));
    if (maxLag <= minLag) return 0.0;

    double best = 0.0;
    for (size_t lag = minLag; lag <= maxLag; ++lag)
    {
        double covariance = 0.0, energyA = 0.0, energyB = 0.0;
        for (size_t i = 0; i + lag < centred.size(); ++i)
        {
            covariance += centred[i] * centred[i + lag];
            energyA += centred[i] * centred[i];
            energyB += centred[i + lag] * centred[i + lag];
        }
        if (energyA <= 1e-12 || energyB <= 1e-12) continue;
        best = std::max(best, covariance / std::sqrt(energyA * energyB));
    }
    return std::clamp(best, 0.0, 1.0);
}

} // namespace

FeatureVectorV2 FeatureExtractorV2::extract(const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    FeatureVectorV2 out;
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    if (sampleRate <= 0.0 || numChannels <= 0 || numSamples <= 0)
    {
        out.values.assign(static_cast<size_t>(dimensions()), 0.0f);
        return out;
    }

    double peak = 0.0;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* data = buffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
        {
            if (! std::isfinite(data[i]))
            {
                out.error = "non-finite sample";
                return out;
            }
            peak = std::max(peak, static_cast<double>(std::abs(data[i])));
        }
    }

    out.values.assign(static_cast<size_t>(dimensions()), 0.0f);
    if (peak < 1e-5)
        return out; // silence: a zero vector, not an error

    // Analysis runs on a peak-normalised copy, so gain cannot change the vector. The band is limited
    // to 8 kHz by the layout below, which is what makes the definition agree across source rates; the
    // schema-1 RMS value still reports the original level, because it is a level by definition.
    const float normalise = static_cast<float>(1.0 / peak);
    const double analysisRate = sampleRate;
    juce::AudioBuffer<float> analysis(numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::copyWithMultiply(analysis.getWritePointer(ch), buffer.getReadPointer(ch),
                                                      normalise, numSamples);

    const int analysisSamples = analysis.getNumSamples();
    const auto legacy = AdaptiveClassifier::extractFeatures(analysis, analysisRate);
    for (int i = 0; i < 13 && i < static_cast<int>(legacy.size()); ++i)
        out.values[static_cast<size_t>(i)] = legacy[static_cast<size_t>(i)];
    double rms = 0.0;
    for (int ch = 0; ch < numChannels; ++ch)
        rms = std::max(rms, static_cast<double>(buffer.getRMSLevel(ch, 0, numSamples)));
    out.values[13] = clamp01(rms * 2.0);

    juce::dsp::FFT fft(fftOrder);
    juce::dsp::WindowingFunction<float> window(fftSize, juce::dsp::WindowingFunction<float>::hann, false);
    const int binLow = std::max(1, hzToBin(analysisLowHz, analysisRate));
    const int binHigh = std::min(numBins - 1, hzToBin(std::min(analysisHighHz, analysisRate * 0.5), analysisRate));
    if (binHigh - binLow < 8)
    {
        out.error = "analysis band too narrow";
        out.values.assign(static_cast<size_t>(dimensions()), 0.0f);
        return out;
    }

    const int bandBins = binHigh - binLow + 1;
    std::vector<float> spectrum(static_cast<size_t>(numBins), 0.0f);
    std::vector<float> scratch(static_cast<size_t>(fftSize) * 2, 0.0f);
    std::vector<std::vector<float>> frames;
    std::vector<std::array<double, 5>> bandPowers;
    std::vector<std::array<double, envelopeBands>> bandEnergies;
    std::vector<double> frameEnergy;

    const float invChannels = 1.0f / static_cast<float>(numChannels);
    for (int offset = 0; offset < analysisSamples; offset += fftHop)
    {
        std::fill(spectrum.begin(), spectrum.end(), 0.0f);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            std::fill(scratch.begin(), scratch.end(), 0.0f);
            const int count = std::min(fftSize, analysisSamples - offset);
            std::copy_n(analysis.getReadPointer(ch) + offset, count, scratch.begin());
            window.multiplyWithWindowingTable(scratch.data(), fftSize);
            fft.performFrequencyOnlyForwardTransform(scratch.data());
            for (int k = 0; k < numBins; ++k) spectrum[static_cast<size_t>(k)] += scratch[static_cast<size_t>(k)];
        }

        std::vector<float> band(static_cast<size_t>(bandBins), 0.0f);
        std::array<double, 5> powers {};
        std::array<double, envelopeBands> energies {};
        for (int k = binLow; k <= binHigh; ++k)
        {
            const float magnitude = spectrum[static_cast<size_t>(k)] * invChannels;
            band[static_cast<size_t>(k - binLow)] = magnitude;
            const double power = static_cast<double>(magnitude) * magnitude;
            const double hz = binToHz(k, analysisRate);
            const int sub = hz < 80.0 ? 0 : hz < 350.0 ? 1 : hz < 2500.0 ? 2 : hz < 7000.0 ? 3 : 4;
            powers[static_cast<size_t>(sub)] += power;
            for (int b = 0; b < envelopeBands; ++b)
                if (hz >= envelopeEdges[b] && hz < envelopeEdges[b + 1])
                {
                    energies[static_cast<size_t>(b)] += power;
                    break;
                }
        }
        double total = 0.0;
        for (double value : energies) total += value;
        frames.push_back(std::move(band));
        bandPowers.push_back(powers);
        bandEnergies.push_back(energies);
        frameEnergy.push_back(std::sqrt(total));
    }

    const int frameCount = static_cast<int>(frames.size());
    if (frameCount == 0)
    {
        out.error = "no frames";
        return out;
    }

    // 4..8: five band shares, the top one ending at 8 kHz so every source agrees on its meaning.
    {
        std::array<double, 5> totals {};
        for (const auto& powers : bandPowers)
            for (int b = 0; b < 5; ++b) totals[static_cast<size_t>(b)] += powers[static_cast<size_t>(b)];
        double all = 0.0;
        for (double value : totals) all += value;
        if (all > 1e-12)
            for (int b = 0; b < 5; ++b)
                out.values[static_cast<size_t>(4 + b)] = clamp01(totals[static_cast<size_t>(b)] / all);
    }

    const double secondsPerFrame = static_cast<double>(fftHop) / analysisRate;

    // 14..31: spectral shape, mean and standard deviation over frames.
    {
        std::vector<std::array<double, shapeStats>> perFrame;
        perFrame.reserve(static_cast<size_t>(frameCount));
        const double bandHigh = std::min(analysisHighHz, analysisRate * 0.5);
        for (const auto& band : frames)
        {
            double sum = 0.0, sumFrequency = 0.0;
            for (int i = 0; i < bandBins; ++i)
            {
                const double magnitude = band[static_cast<size_t>(i)];
                sum += magnitude;
                sumFrequency += magnitude * binToHz(binLow + i, analysisRate);
            }
            std::array<double, shapeStats> stats {};
            if (sum <= 1e-12)
            {
                perFrame.push_back(stats);
                continue;
            }
            const double centroid = sumFrequency / sum;
            double second = 0.0, third = 0.0, fourth = 0.0;
            for (int i = 0; i < bandBins; ++i)
            {
                const double magnitude = band[static_cast<size_t>(i)];
                const double delta = binToHz(binLow + i, analysisRate) - centroid;
                second += magnitude * delta * delta;
                third += magnitude * delta * delta * delta;
                fourth += magnitude * delta * delta * delta * delta;
            }
            const double spread = std::sqrt(second / sum);
            const double skew = spread > 1e-9 ? (third / sum) / (spread * spread * spread) : 0.0;
            const double kurtosis = spread > 1e-9 ? (fourth / sum) / (spread * spread * spread * spread) : 0.0;

            double cumulative = 0.0;
            double rolloff85 = bandHigh, rolloff95 = bandHigh;
            bool found85 = false, found95 = false;
            double squareSum = 0.0, logSum = 0.0, tiltLow = 0.0, tiltHigh = 0.0;
            int tiltLowBins = 0, tiltHighBins = 0;
            for (int i = 0; i < bandBins; ++i)
            {
                const double magnitude = band[static_cast<size_t>(i)];
                cumulative += magnitude;
                if (! found85 && cumulative >= 0.85 * sum) { rolloff85 = binToHz(binLow + i, analysisRate); found85 = true; }
                if (! found95 && cumulative >= 0.95 * sum) { rolloff95 = binToHz(binLow + i, analysisRate); found95 = true; }
                squareSum += magnitude * magnitude;
                logSum += std::log(magnitude + 1e-9);
                const double hz = binToHz(binLow + i, analysisRate);
                if (hz >= 500.0 && hz < 1500.0) { tiltLow += magnitude; ++tiltLowBins; }
                else if (hz >= 3000.0 && hz <= 7000.0) { tiltHigh += magnitude; ++tiltHighBins; }
            }
            const double arithmetic = sum / bandBins;
            const double geometric = std::exp(logSum / bandBins);
            const double flatness = arithmetic > 1e-12 ? geometric / arithmetic : 0.0;

            double entropy = 0.0;
            if (squareSum > 1e-12)
                for (int i = 0; i < bandBins; ++i)
                {
                    const double p = static_cast<double>(band[static_cast<size_t>(i)])
                                     * band[static_cast<size_t>(i)] / squareSum;
                    if (p > 1e-12) entropy -= p * std::log(p);
                }
            // Tilt instead of a regression slope: the dB difference between two bands of the same
            // frame is stable across sample rates, while a fitted slope follows the window leakage.
            const double tiltLowDb = 20.0 * std::log10(tiltLow / std::max(1, tiltLowBins) + 1e-9);
            const double tiltHighDb = 20.0 * std::log10(tiltHigh / std::max(1, tiltHighBins) + 1e-9);

            stats[0] = centroid / bandHigh;
            stats[1] = spread / bandHigh;
            // Soft maps, not clipping: a tonal spectrum has a skewness and a kurtosis far outside
            // any fixed range, and clamping them would make two different sounds look identical.
            stats[2] = 0.5 + 0.5 * skew / (std::abs(skew) + 3.0);
            stats[3] = kurtosis / (kurtosis + 8.0);
            stats[4] = rolloff85 / bandHigh;
            stats[5] = rolloff95 / bandHigh;
            stats[6] = flatness;
            stats[7] = entropy / std::log(static_cast<double>(bandBins));
            stats[8] = 0.5 + (tiltLowDb - tiltHighDb) / 80.0;
            perFrame.push_back(stats);
        }
        for (int s = 0; s < shapeStats; ++s)
        {
            std::vector<double> values;
            values.reserve(perFrame.size());
            for (const auto& stats : perFrame) values.push_back(stats[static_cast<size_t>(s)]);
            out.values[static_cast<size_t>(14 + s)] = clamp01(mean(values));
            out.values[static_cast<size_t>(14 + shapeStats + s)] = clamp01(standardDeviation(values));
        }
    }

    // 32..37: band contrast, peak minus valley in dB over an octave band, averaged over frames.
    {
        std::vector<double> average(static_cast<size_t>(bandBins), 0.0);
        for (const auto& band : frames)
            for (int i = 0; i < bandBins; ++i) average[static_cast<size_t>(i)] += band[static_cast<size_t>(i)] / frameCount;
        for (int b = 0; b < contrastBands; ++b)
        {
            const double centre = contrastCentres[b];
            const double low = centre / std::sqrt(2.0);
            const double high = centre * std::sqrt(2.0);
            double lowest = 1e9, highest = -1e9;
            for (int i = 0; i < bandBins; ++i)
            {
                const double hz = binToHz(binLow + i, analysisRate);
                if (hz < low || hz > high) continue;
                const double db = 20.0 * std::log10(average[static_cast<size_t>(i)] + 1e-9);
                lowest = std::min(lowest, db);
                highest = std::max(highest, db);
            }
            out.values[static_cast<size_t>(32 + b)] = highest > lowest ? clamp01((highest - lowest) / 60.0) : 0.0f;
        }
    }

    // 38..63: MFCC 1..13 from the average power spectrum, mean and standard deviation over frames.
    {
        const auto filters = makeMelFilters(analysisRate);
        std::vector<double> averagePower(static_cast<size_t>(bandBins), 0.0);
        for (const auto& band : frames)
            for (int i = 0; i < bandBins; ++i)
            {
                const double magnitude = band[static_cast<size_t>(i)];
                averagePower[static_cast<size_t>(i)] += magnitude * magnitude / frameCount;
            }

        std::vector<double> logEnergies(static_cast<size_t>(melFilters), 0.0);
        for (int f = 0; f < melFilters; ++f)
        {
            const auto& filter = filters[static_cast<size_t>(f)];
            double energy = 1e-12;
            for (int k = std::max(filter.low, binLow); k <= std::min(filter.high, binHigh); ++k)
            {
                const double position = k <= filter.centre
                                            ? static_cast<double>(k - filter.low) / std::max(1, filter.centre - filter.low)
                                            : static_cast<double>(filter.high - k) / std::max(1, filter.high - filter.centre);
                energy += averagePower[static_cast<size_t>(k - binLow)] * std::max(0.0, position);
            }
            logEnergies[static_cast<size_t>(f)] = std::log(energy);
        }

        for (int c = 1; c <= mfccCount; ++c)
        {
            double coefficient = 0.0;
            for (int f = 0; f < melFilters; ++f)
                coefficient += logEnergies[static_cast<size_t>(f)]
                               * std::cos(juce::MathConstants<double>::pi * c * (f + 0.5) / melFilters);
            coefficient *= std::sqrt(2.0 / melFilters);
            out.values[static_cast<size_t>(37 + c)] = clamp01(0.5 + coefficient / 100.0);
        }
        // Per-frame variation of the same coefficients: frames are compared through the mean
        // spectrum, so the standard deviation uses each frame's own log-mel energies.
        std::vector<std::array<double, mfccCount>> perFrame;
        for (const auto& band : frames)
        {
            std::vector<double> logFrame(static_cast<size_t>(melFilters), 0.0);
            for (int f = 0; f < melFilters; ++f)
            {
                const auto& filter = filters[static_cast<size_t>(f)];
                double energy = 1e-12;
                for (int k = std::max(filter.low, binLow); k <= std::min(filter.high, binHigh); ++k)
                {
                    const double position = k <= filter.centre
                                                ? static_cast<double>(k - filter.low) / std::max(1, filter.centre - filter.low)
                                                : static_cast<double>(filter.high - k) / std::max(1, filter.high - filter.centre);
                    const double magnitude = band[static_cast<size_t>(k - binLow)];
                    energy += magnitude * magnitude * std::max(0.0, position);
                }
                logFrame[static_cast<size_t>(f)] = std::log(energy);
            }
            std::array<double, mfccCount> coefficients {};
            for (int c = 1; c <= mfccCount; ++c)
            {
                double value = 0.0;
                for (int f = 0; f < melFilters; ++f)
                    value += logFrame[static_cast<size_t>(f)]
                             * std::cos(juce::MathConstants<double>::pi * c * (f + 0.5) / melFilters);
                coefficients[static_cast<size_t>(c - 1)] = value * std::sqrt(2.0 / melFilters);
            }
            perFrame.push_back(coefficients);
        }
        for (int c = 0; c < mfccCount; ++c)
        {
            std::vector<double> values;
            values.reserve(perFrame.size());
            for (const auto& coefficients : perFrame) values.push_back(coefficients[static_cast<size_t>(c)]);
            out.values[static_cast<size_t>(38 + mfccCount + c)] = clamp01(standardDeviation(values) / 50.0);
        }
    }

    // 64..75: attack, decay and early share for four bands.
    {
        const double earlyFrames = earlyShareSeconds / secondsPerFrame;
        for (int b = 0; b < envelopeBands; ++b)
        {
            std::vector<double> envelope;
            envelope.reserve(static_cast<size_t>(frameCount));
            for (const auto& energies : bandEnergies)
                envelope.push_back(std::sqrt(energies[static_cast<size_t>(b)]));

            double peakValue = 0.0;
            int peakIndex = 0;
            for (int f = 0; f < frameCount; ++f)
                if (envelope[static_cast<size_t>(f)] > peakValue)
                {
                    peakValue = envelope[static_cast<size_t>(f)];
                    peakIndex = f;
                }

            double attack = 0.0;
            if (peakValue > 1e-9)
            {
                int first = 0, last = peakIndex;
                for (int f = 0; f <= peakIndex; ++f)
                    if (envelope[static_cast<size_t>(f)] >= 0.1 * peakValue) { first = f; break; }
                for (int f = first; f <= peakIndex; ++f)
                    if (envelope[static_cast<size_t>(f)] >= 0.9 * peakValue) { last = f; break; }
                attack = static_cast<double>(std::max(0, last - first)) * secondsPerFrame;
            }

            double decay = 0.0;
            if (peakValue > 1e-9 && peakIndex < frameCount - 1)
            {
                const double endValue = std::max(1e-9, envelope.back());
                const double dropDb = 20.0 * std::log10(peakValue / endValue);
                const double seconds = static_cast<double>(frameCount - 1 - peakIndex) * secondsPerFrame;
                if (seconds > 1e-4 && dropDb > 0.0) decay = dropDb / seconds;
            }

            double total = 0.0, early = 0.0;
            for (int f = 0; f < frameCount; ++f)
            {
                const double value = bandEnergies[static_cast<size_t>(f)][static_cast<size_t>(b)];
                total += value;
                if (static_cast<double>(f) <= earlyFrames) early += value;
            }

            out.values[static_cast<size_t>(64 + b * 3)] = clamp01((std::log10(std::max(1e-4, attack)) + 4.0) / 4.0);
            out.values[static_cast<size_t>(64 + b * 3 + 1)] = clamp01(decay / 500.0);
            out.values[static_cast<size_t>(64 + b * 3 + 2)] = total > 1e-12 ? clamp01(early / total) : 0.0f;
        }
    }

    // 76: share of energy in spectral peaks (a tone concentrates it, noise spreads it out).
    // 77: mean positive frame-to-frame change of the normalised spectrum.
    {
        double peakShare = 0.0;
        double flux = 0.0;
        std::vector<float> previous;
        for (const auto& band : frames)
        {
            double sum = 0.0, peaks = 0.0;
            for (int i = 0; i < bandBins; ++i) sum += band[static_cast<size_t>(i)];
            const double threshold = sum / bandBins * 2.0;
            for (int i = 1; i + 1 < bandBins; ++i)
            {
                const double magnitude = band[static_cast<size_t>(i)];
                if (magnitude > threshold && magnitude >= band[static_cast<size_t>(i - 1)]
                    && magnitude >= band[static_cast<size_t>(i + 1)])
                    peaks += magnitude;
            }
            peakShare += sum > 1e-12 ? peaks / sum : 0.0;

            if (! previous.empty())
            {
                double difference = 0.0, normPrevious = 0.0;
                for (int i = 0; i < bandBins; ++i)
                {
                    difference += std::abs(static_cast<double>(band[static_cast<size_t>(i)])
                                            - previous[static_cast<size_t>(i)]);
                    normPrevious += previous[static_cast<size_t>(i)];
                }
                if (normPrevious > 1e-12) flux += std::min(2.0, difference / normPrevious);
            }
            previous = band;
        }
        out.values[76] = clamp01(peakShare / frameCount);
        out.values[77] = clamp01(frameCount > 1 ? 2.0 * flux / (frameCount - 1) : 0.0);
    }

    // 78..81: onset density, envelope periodicity, mean onset strength, low-band periodicity.
    {
        std::vector<double> logEnergy(static_cast<size_t>(frameCount), 0.0);
        for (int f = 0; f < frameCount; ++f) logEnergy[static_cast<size_t>(f)] = std::log(frameEnergy[static_cast<size_t>(f)] + 1e-9);
        std::vector<double> delta(static_cast<size_t>(frameCount), 0.0);
        for (int f = 1; f < frameCount; ++f)
            delta[static_cast<size_t>(f)] = std::max(0.0, logEnergy[static_cast<size_t>(f)] - logEnergy[static_cast<size_t>(f - 1)]);

        std::vector<double> interior(delta.begin() + 1, delta.end());
        const double average = mean(interior);
        const double deviation = standardDeviation(interior);
        const double threshold = std::max(0.2, average + 1.5 * deviation);
        int onsets = 0;
        double strength = 0.0;
        for (int f = 1; f < frameCount; ++f)
        {
            const double value = delta[static_cast<size_t>(f)];
            strength += value;
            const double next = f + 1 < frameCount ? delta[static_cast<size_t>(f + 1)] : 0.0;
            const double previous = delta[static_cast<size_t>(f - 1)];
            if (value >= threshold && value >= previous && value >= next) ++onsets;
        }

        const double duration = static_cast<double>(analysisSamples) / analysisRate;
        out.values[78] = clamp01(duration > 0.0 ? onsets / duration / 10.0 : 0.0);
        out.values[79] = clamp01(periodicity(logEnergy, secondsPerFrame, 0.08, 1.0));
        out.values[80] = clamp01(frameCount > 1 ? strength / (frameCount - 1) / 3.0 : 0.0);

        std::vector<double> lowBand;
        lowBand.reserve(static_cast<size_t>(frameCount));
        for (const auto& energies : bandEnergies) lowBand.push_back(std::log(std::sqrt(energies[0]) + 1e-9));
        out.values[81] = clamp01(periodicity(lowBand, secondsPerFrame, 0.08, 1.0));
    }

    // 82..83: stereo width and channel correlation; mono sources get 0 and 1.
    {
        if (numChannels >= 2)
        {
            std::vector<double> left(static_cast<size_t>(numSamples));
            std::vector<double> right(static_cast<size_t>(numSamples));
            double side = 0.0, middle = 0.0;
            for (int i = 0; i < numSamples; ++i)
            {
                const double l = buffer.getSample(0, i);
                const double r = buffer.getSample(1, i);
                left[static_cast<size_t>(i)] = l;
                right[static_cast<size_t>(i)] = r;
                side += (l - r) * (l - r);
                middle += (l + r) * (l + r);
            }
            out.values[82] = middle > 1e-12 ? clamp01(std::sqrt(side / middle)) : 0.0f;
            out.values[83] = clamp01((pearson(left, right) + 1.0) / 2.0);
        }
        else
        {
            out.values[82] = 0.0f;
            out.values[83] = 1.0f;
        }
    }

    return out;
}

juce::StringArray FeatureExtractorV2::dimensionNames()
{
    juce::StringArray names {
        "attack", "decay", "crest", "temporal_centroid", "band_sub", "band_low", "band_mid",
        "band_high_mid", "band_high", "zcr", "flux", "pitch_clarity", "duration", "rms"
    };
    const char* shape[] = { "centroid", "spread", "skewness", "kurtosis", "rolloff85", "rolloff95",
                            "flatness", "entropy", "slope" };
    for (const char* name : shape) names.add(juce::String("spectral_") + name + "_mean");
    for (const char* name : shape) names.add(juce::String("spectral_") + name + "_std");
    for (double centre : contrastCentres)
        names.add("contrast_" + juce::String(static_cast<int>(centre)));
    for (int c = 1; c <= mfccCount; ++c) names.add("mfcc_" + juce::String(c) + "_mean");
    for (int c = 1; c <= mfccCount; ++c) names.add("mfcc_" + juce::String(c) + "_std");
    for (const char* band : { "low", "mid", "high", "air" })
    {
        names.add(juce::String("envelope_") + band + "_attack");
        names.add(juce::String("envelope_") + band + "_decay");
        names.add(juce::String("envelope_") + band + "_early");
    }
    names.add("harmonic_ratio");
    names.add("percussive_ratio");
    names.add("onset_density");
    names.add("envelope_periodicity");
    names.add("onset_strength");
    names.add("low_band_periodicity");
    names.add("stereo_width");
    names.add("stereo_correlation");
    return names;
}

} // namespace sonicator
