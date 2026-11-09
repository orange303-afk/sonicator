#include "TonalAnalyzer.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <vector>
namespace sonicator {
namespace {
constexpr int pitchClassCount = 12;
constexpr double minAnalysisSeconds = 0.20;
constexpr double silenceRms = 1.0e-5;
constexpr double chromaKernelSigma = 0.5;
constexpr int tuningHistogramBins = 120;
constexpr int medianRadius = 4; // ~0.2 s at 44.1 kHz with a 1024 hop

// Krumhansl–Kessler (1982) and Temperley (2001) key profiles, C at index 0.
constexpr double krumhanslMajor[pitchClassCount] =
    { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
constexpr double krumhanslMinor[pitchClassCount] =
    { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
constexpr double temperleyMajor[pitchClassCount] =
    { 5.0, 2.0, 3.5, 2.0, 4.5, 4.0, 2.0, 4.5, 2.0, 3.5, 1.5, 4.0 };
constexpr double temperleyMinor[pitchClassCount] =
    { 5.0, 2.0, 3.5, 4.5, 2.0, 4.0, 2.0, 4.5, 3.5, 2.0, 1.5, 4.0 };

using Chroma = std::array<double, pitchClassCount>;

int nextPowerOfTwo(int value)
{
    int power = 1;
    while (power < value) power <<= 1;
    return power;
}

double clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

double pearson(const Chroma& a, const Chroma& b)
{
    const double meanA = std::accumulate(a.begin(), a.end(), 0.0) / (double) a.size();
    const double meanB = std::accumulate(b.begin(), b.end(), 0.0) / (double) b.size();
    double covariance = 0.0, varianceA = 0.0, varianceB = 0.0;
    for (int i = 0; i < pitchClassCount; ++i)
    {
        covariance += (a[(size_t) i] - meanA) * (b[(size_t) i] - meanB);
        varianceA += (a[(size_t) i] - meanA) * (a[(size_t) i] - meanA);
        varianceB += (b[(size_t) i] - meanB) * (b[(size_t) i] - meanB);
    }
    if (varianceA <= 1.0e-15 || varianceB <= 1.0e-15) return 0.0;
    return covariance / std::sqrt(varianceA * varianceB);
}

struct Peak
{
    double frequency = 0.0;
    double magnitude = 0.0;
};

struct Frame
{
    std::vector<Peak> peaks;
    double rms = 0.0;
    double flatness = 1.0;
    double flux = 0.0;
};

struct ChromaFrame
{
    Chroma chroma {};
    double weight = 0.0;
};

struct KeyScore
{
    double score = 0.0;
    double krumhansl = 0.0;
    double temperley = 0.0;
    int tonic = -1;
    juce::String mode;
};

std::vector<Frame> collectFrames(const juce::AudioBuffer<float>& audio, double sampleRate,
                                 const TonalOptions& options, const CancelCheck& cancel,
                                 int& fftSize, int& hop, juce::String& error)
{
    std::vector<Frame> frames;
    const int channels = audio.getNumChannels();
    const int samples = audio.getNumSamples();
    if (sampleRate <= 0.0 || channels <= 0 || samples <= 0)
    {
        error = "invalid audio";
        return frames;
    }
    const double duration = (double) samples / sampleRate;
    if (duration < minAnalysisSeconds) return frames;

    fftSize = nextPowerOfTwo(std::min(samples, options.keyFftSize));
    fftSize = std::clamp(fftSize, 1024, 16384);
    hop = std::clamp(options.keyHop, 64, fftSize / 2);
    const int frameCount = std::min((int) std::ceil(options.maxKeySeconds * sampleRate / hop),
                                    samples >= fftSize ? 1 + (samples - fftSize) / hop : 1);
    if (frameCount <= 0) return frames;

    int order = 0;
    while ((1 << order) < fftSize) ++order;
    juce::dsp::FFT fft(order);
    juce::dsp::WindowingFunction<float> window((size_t) fftSize,
                                               juce::dsp::WindowingFunction<float>::hann, false);
    const int bins = fftSize / 2 + 1;
    const int minBin = std::max(1, (int) std::ceil(options.minKeyFrequency * fftSize / sampleRate));
    const int maxBin = std::min(bins - 1,
                                (int) std::floor(options.maxKeyFrequency * fftSize / sampleRate));
    if (maxBin <= minBin) return frames;

    std::vector<float> frameBuffer((size_t) fftSize * 2, 0.0f);
    std::vector<double> power((size_t) bins, 0.0), previous((size_t) bins, 0.0);
    frames.reserve((size_t) frameCount);
    for (int index = 0; index < frameCount; ++index)
    {
        if (cancel && cancel())
        {
            error = "Cancelled";
            frames.clear();
            return frames;
        }
        const int offset = index * hop;
        const int count = std::min(fftSize, samples - offset);
        std::fill(power.begin(), power.end(), 0.0);
        double squares = 0.0;
        for (int channel = 0; channel < channels; ++channel)
        {
            std::fill(frameBuffer.begin(), frameBuffer.end(), 0.0f);
            const auto* data = audio.getReadPointer(channel);
            for (int i = 0; i < count; ++i)
            {
                frameBuffer[(size_t) i] = data[offset + i];
                squares += (double) data[offset + i] * data[offset + i];
            }
            window.multiplyWithWindowingTable(frameBuffer.data(), (size_t) fftSize);
            fft.performFrequencyOnlyForwardTransform(frameBuffer.data());
            for (int k = 0; k < bins; ++k)
            {
                const double magnitude = frameBuffer[(size_t) k];
                power[(size_t) k] += magnitude * magnitude;
            }
        }
        Frame frame;
        frame.rms = std::sqrt(squares / ((double) count * channels));
        const int binCount = maxBin - minBin + 1;
        double sum = 0.0, logSum = 0.0;
        for (int k = minBin; k <= maxBin; ++k)
        {
            const double value = power[(size_t) k];
            sum += value;
            logSum += std::log(std::max(value, 1.0e-30));
        }
        frame.flatness = sum > 0.0 ? std::exp(logSum / binCount) / (sum / binCount) : 1.0;
        double flux = 0.0;
        for (int k = minBin; k <= maxBin; ++k)
        {
            const double magnitude = std::sqrt(power[(size_t) k]) * 2.0 / (double) fftSize;
            const double compressed = std::log1p(magnitude * 8.0);
            flux += std::max(0.0, compressed - previous[(size_t) k]);
            previous[(size_t) k] = compressed;
        }
        frame.flux = flux / (double) binCount;

        double frameMaximum = 0.0;
        for (int k = minBin; k <= maxBin; ++k)
            frameMaximum = std::max(frameMaximum, power[(size_t) k]);
        if (frameMaximum > 1.0e-20)
        {
            for (int k = minBin; k < maxBin; ++k)
            {
                if (power[(size_t) k] < power[(size_t) (k - 1)]
                    || power[(size_t) k] <= power[(size_t) (k + 1)]
                    || power[(size_t) k] <= frameMaximum * 0.01)
                    continue;
                const double left = std::log(std::max(power[(size_t) (k - 1)], 1.0e-30));
                const double centre = std::log(std::max(power[(size_t) k], 1.0e-30));
                const double right = std::log(std::max(power[(size_t) (k + 1)], 1.0e-30));
                const double denominator = left - 2.0 * centre + right;
                const double delta = std::abs(denominator) > 1.0e-12
                                         ? 0.5 * (left - right) / denominator : 0.0;
                const double bin = (double) k + std::clamp(delta, -0.5, 0.5);
                frame.peaks.push_back({ bin * sampleRate / (double) fftSize,
                                        std::sqrt(power[(size_t) k]) * 2.0 / (double) fftSize });
            }
        }
        frames.push_back(std::move(frame));
    }
    return frames;
}

double estimateTuningCents(const std::vector<Frame>& frames, const TonalOptions& options)
{
    std::array<double, tuningHistogramBins> histogram {};
    double total = 0.0;
    for (const auto& frame : frames)
    {
        if (frame.rms < silenceRms || frame.flatness >= options.percussiveFlatness) continue;
        for (const auto& peak : frame.peaks)
        {
            if (peak.frequency < options.tuningMinFrequency
                || peak.frequency > options.tuningMaxFrequency)
                continue;
            const double midi = 69.0 + 12.0 * std::log2(peak.frequency / 440.0);
            const double deviation = midi - std::round(midi);
            int bin = (int) std::floor((deviation + 0.5) * tuningHistogramBins);
            bin = std::clamp(bin, 0, tuningHistogramBins - 1);
            histogram[(size_t) bin] += std::sqrt(peak.magnitude);
            total += std::sqrt(peak.magnitude);
        }
    }
    if (total <= 1.0e-20) return 0.0;
    std::array<double, tuningHistogramBins> smoothed {};
    for (int i = 0; i < tuningHistogramBins; ++i)
        for (int d = -2; d <= 2; ++d)
            smoothed[(size_t) i] += histogram[(size_t) ((i + d + tuningHistogramBins)
                                                        % tuningHistogramBins)];
    int best = 0;
    for (int i = 1; i < tuningHistogramBins; ++i)
        if (smoothed[(size_t) i] > smoothed[(size_t) best]) best = i;
    const double left = smoothed[(size_t) ((best - 1 + tuningHistogramBins) % tuningHistogramBins)];
    const double centre = smoothed[(size_t) best];
    const double right = smoothed[(size_t) ((best + 1) % tuningHistogramBins)];
    const double denominator = left - 2.0 * centre + right;
    const double delta = std::abs(denominator) > 1.0e-12
                             ? 0.5 * (left - right) / denominator : 0.0;
    const double deviation = ((double) best + 0.5 + std::clamp(delta, -0.5, 0.5))
                                 / tuningHistogramBins - 0.5;
    return deviation * 100.0;
}

std::vector<ChromaFrame> buildChroma(const std::vector<Frame>& frames, double tuningCents,
                                     const TonalOptions& options)
{
    std::vector<ChromaFrame> out;
    out.reserve(frames.size());
    for (const auto& frame : frames)
    {
        ChromaFrame chromaFrame;
        if (frame.rms >= silenceRms)
        {
            const double tonalWeight = clamp01(1.0 - frame.flatness / options.percussiveFlatness);
            const double transientWeight = 1.0 / (1.0 + 4.0 * frame.flux);
            chromaFrame.weight = tonalWeight * transientWeight;
            if (chromaFrame.weight > 0.0)
            {
                for (const auto& peak : frame.peaks)
                {
                    const double midi = 69.0 + 12.0 * std::log2(peak.frequency / 440.0)
                                        - tuningCents / 100.0;
                    const double nearest = std::round(midi);
                    const double deviation = midi - nearest;
                    const double kernel = std::exp(-0.5 * (deviation / chromaKernelSigma)
                                                   * (deviation / chromaKernelSigma));
                    int pitchClass = ((int) nearest % pitchClassCount + pitchClassCount)
                                     % pitchClassCount;
                    chromaFrame.chroma[(size_t) pitchClass] += peak.magnitude * peak.magnitude
                                                               * kernel * chromaFrame.weight;
                }
            }
        }
        out.push_back(chromaFrame);
    }
    return out;
}

std::vector<KeyScore> scoreKeys(const Chroma& chroma)
{
    std::vector<KeyScore> scores;
    scores.reserve(24);
    for (int tonic = 0; tonic < pitchClassCount; ++tonic)
    {
        for (int modeIndex = 0; modeIndex < 2; ++modeIndex)
        {
            const bool major = modeIndex == 0;
            Chroma krumhansl {}, temperley {};
            for (int i = 0; i < pitchClassCount; ++i)
            {
                const int source = (i - tonic + pitchClassCount) % pitchClassCount;
                krumhansl[(size_t) i] = major ? krumhanslMajor[source] : krumhanslMinor[source];
                temperley[(size_t) i] = major ? temperleyMajor[source] : temperleyMinor[source];
            }
            KeyScore score;
            score.krumhansl = pearson(chroma, krumhansl);
            score.temperley = pearson(chroma, temperley);
            score.score = 0.5 * (score.krumhansl + score.temperley);
            score.tonic = tonic;
            score.mode = major ? "major" : "minor";
            scores.push_back(score);
        }
    }
    return scores;
}
} // namespace

KeyAnalysis TonalAnalyzer::analyzeKey(const juce::AudioBuffer<float>& audio, double sampleRate,
                                      const TonalOptions& options, const CancelCheck& cancel)
{
    KeyAnalysis result;
    int fftSize = 0, hop = 0;
    const auto frames = collectFrames(audio, sampleRate, options, cancel, fftSize, hop,
                                      result.error);
    if (result.error.isNotEmpty()) return result;
    if (frames.empty())
    {
        result.explanation = "key: not enough audio for a tonal analysis";
        return result;
    }
    if (hop <= 0 || fftSize <= 0)
    {
        result.explanation = "key: invalid analysis geometry";
        return result;
    }

    result.tuningCents = estimateTuningCents(frames, options);
    const auto chromaFrames = buildChroma(frames, result.tuningCents, options);
    if (chromaFrames.empty()) return result;

    const int frameCount = (int) chromaFrames.size();
    Chroma summed {};
    double rawEnergy = 0.0, medianEnergy = 0.0, weightSum = 0.0;
    int audibleFrames = 0;
    for (const auto& frame : chromaFrames)
    {
        if (frame.weight <= 0.0) continue;
        ++audibleFrames;
        weightSum += frame.weight;
        for (double value : frame.chroma) rawEnergy += value;
    }
    std::vector<double> window;
    for (int pitchClass = 0; pitchClass < pitchClassCount; ++pitchClass)
    {
        for (int index = 0; index < frameCount; ++index)
        {
            window.clear();
            const int low = std::max(0, index - medianRadius);
            const int high = std::min(frameCount - 1, index + medianRadius);
            for (int j = low; j <= high; ++j)
                window.push_back(chromaFrames[(size_t) j].chroma[(size_t) pitchClass]);
            const auto middle = window.begin() + (std::ptrdiff_t) (window.size() / 2);
            std::nth_element(window.begin(), middle, window.end());
            const double value = *middle;
            summed[(size_t) pitchClass] += value;
            medianEnergy += value;
        }
    }
    const double weightAverage = audibleFrames > 0 ? weightSum / (double) audibleFrames : 0.0;
    const double retained = rawEnergy > 1.0e-20 ? medianEnergy / rawEnergy : 0.0;
    result.tonality = std::min(weightAverage, retained);
    result.percussive = result.tonality < options.minTonality;
    double norm = 0.0;
    for (double value : summed) norm += value * value;
    norm = std::sqrt(norm);
    if (norm <= 1.0e-12)
    {
        result.explanation = "key: no stable tonal content";
        return result;
    }
    for (double& value : summed) value /= norm;

    const auto scores = scoreKeys(summed);
    const auto best = *std::max_element(scores.begin(), scores.end(),
        [](const KeyScore& a, const KeyScore& b) { return a.score < b.score; });
    const KeyScore* runnerUp = nullptr;
    const KeyScore* sameTonicOtherMode = nullptr;
    for (const auto& score : scores)
    {
        if (score.tonic == best.tonic)
        {
            if (score.mode != best.mode && sameTonicOtherMode == nullptr)
                sameTonicOtherMode = &score;
            continue;
        }
        if (runnerUp == nullptr || score.score > runnerUp->score) runnerUp = &score;
    }
    result.bestCorrelation = best.score;
    result.runnerUpCorrelation = runnerUp != nullptr ? runnerUp->score : 0.0;
    result.runnerUpTonic = runnerUp != nullptr ? runnerUp->tonic : -1;
    result.runnerUpMode = runnerUp != nullptr ? runnerUp->mode : juce::String();

    juce::String explanation = "key: dsp chroma, Krumhansl-Kessler + Temperley; best "
        + juce::String(best.score, 3) + " for tonic " + juce::String(best.tonic)
        + " " + best.mode;
    if (runnerUp != nullptr)
        explanation += ", runner-up " + juce::String(runnerUp->score, 3)
            + " for tonic " + juce::String(runnerUp->tonic) + " " + runnerUp->mode;
    explanation += ", tuning " + juce::String(result.tuningCents, 1) + " cents"
        + ", tonality " + juce::String(result.tonality, 3);

    if (result.tonality < options.minTonality)
    {
        result.explanation = explanation + " (percussive or noise-dominated)";
        return result;
    }
    if (best.score < options.minKeyCorrelation)
    {
        result.explanation = explanation + " (below the correlation floor)";
        return result;
    }
    const double tonicMargin = runnerUp != nullptr ? best.score - runnerUp->score : best.score;
    if (tonicMargin < options.minTonicMargin)
    {
        result.explanation = explanation + " (tonic dispute)";
        return result;
    }
    const double modeMargin = sameTonicOtherMode != nullptr
                                  ? best.score - sameTonicOtherMode->score : best.score;
    result.tonic = best.tonic;
    result.mode = modeMargin >= options.modeMargin ? best.mode : juce::String();
    result.found = true;
    const double correlationConfidence = clamp01((best.score - 0.25) / 0.55);
    const double marginConfidence = clamp01(tonicMargin / 0.15);
    result.confidence = clamp01(correlationConfidence * marginConfidence);
    result.explanation = explanation
        + ", tonic margin " + juce::String(tonicMargin, 3)
        + ", mode margin " + juce::String(modeMargin, 3)
        + (result.mode.isEmpty() ? " (confident tonic without a mode)" : "")
        + ", confidence " + juce::String(result.confidence, 2);
    return result;
}
} // namespace sonicator
