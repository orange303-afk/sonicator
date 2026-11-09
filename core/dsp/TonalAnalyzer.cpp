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
                    || power[(size_t) k] <= frameMaximum * options.peakThreshold)
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
    // Circular mean of the strongest low/mid peak per tonal frame. Averaging in the phase domain
    // rejects drums and noise, whose deviations are spread across the circle, instead of letting
    // them pull a histogram to an arbitrary offset.
    double sumSin = 0.0, sumCos = 0.0, weightSum = 0.0;
    for (const auto& frame : frames)
    {
        if (frame.rms < silenceRms || frame.flatness >= options.percussiveFlatness) continue;
        const Peak* strongest = nullptr;
        for (const auto& peak : frame.peaks)
        {
            if (peak.frequency < options.tuningMinFrequency
                || peak.frequency > options.tuningMaxFrequency)
                continue;
            if (strongest == nullptr || peak.magnitude > strongest->magnitude)
                strongest = &peak;
        }
        if (strongest == nullptr) continue;
        const double midi = 69.0 + 12.0 * std::log2(strongest->frequency / 440.0);
        const double deviation = midi - std::round(midi);
        const double weight = std::sqrt(strongest->magnitude);
        sumSin += weight * std::sin(2.0 * juce::MathConstants<double>::pi * deviation);
        sumCos += weight * std::cos(2.0 * juce::MathConstants<double>::pi * deviation);
        weightSum += weight;
    }
    if (weightSum <= 1.0e-20) return 0.0;
    const double strength = std::sqrt(sumSin * sumSin + sumCos * sumCos) / weightSum;
    if (strength < 0.05) return 0.0;
    const double deviation = std::atan2(sumSin, sumCos)
                             / (2.0 * juce::MathConstants<double>::pi);
    return deviation * 100.0;
}

std::vector<ChromaFrame> buildChroma(const std::vector<Frame>& frames, double tuningCents,
                                     const TonalOptions& options, Chroma& bassChroma)
{
    std::vector<ChromaFrame> out;
    out.reserve(frames.size());
    for (const auto& frame : frames)
    {
        ChromaFrame chromaFrame;
        if (frame.rms >= silenceRms)
        {
            const double tonalWeight = clamp01(1.0 - frame.flatness / options.percussiveFlatness);
            const double transientWeight = 1.0 / (1.0 + 8.0 * frame.flux);
            chromaFrame.weight = tonalWeight * transientWeight;
            if (chromaFrame.weight > 0.0)
            {
                double frameMaximum = 0.0;
                for (const auto& peak : frame.peaks)
                    frameMaximum = std::max(frameMaximum, peak.magnitude);
                const Peak* bassPeak = nullptr;
                for (const auto& peak : frame.peaks)
                {
                    if (peak.frequency < 40.0 || peak.frequency > 300.0) continue;
                    if (peak.magnitude < frameMaximum * 0.01) continue;
                    if (bassPeak == nullptr || peak.magnitude > bassPeak->magnitude)
                        bassPeak = &peak;
                }
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
                    // Whitening against the frame maximum keeps the bass and the strongest
                    // partial from drowning the rest of the chord.
                    const double relative = frameMaximum > 0.0
                                                ? peak.magnitude / frameMaximum : 0.0;
                    chromaFrame.chroma[(size_t) pitchClass] += std::pow(relative,
                                                                        options.magnitudePower)
                                                               * kernel;
                }
                if (options.normalizeFrames)
                {
                    double norm = 0.0;
                    for (double value : chromaFrame.chroma) norm += value * value;
                    norm = std::sqrt(norm);
                    if (norm > 1.0e-12)
                        for (double& value : chromaFrame.chroma) value /= norm;
                }
                for (double& value : chromaFrame.chroma) value *= chromaFrame.weight;
                if (bassPeak != nullptr)
                {
                    const double midi = 69.0 + 12.0 * std::log2(bassPeak->frequency / 440.0)
                                        - tuningCents / 100.0;
                    const double nearest = std::round(midi);
                    const double deviation = midi - nearest;
                    const double kernel = std::exp(-0.5 * (deviation / chromaKernelSigma)
                                                   * (deviation / chromaKernelSigma));
                    int pitchClass = ((int) nearest % pitchClassCount + pitchClassCount)
                                     % pitchClassCount;
                    const double relative = frameMaximum > 0.0
                                                ? bassPeak->magnitude / frameMaximum : 0.0;
                    bassChroma[(size_t) pitchClass] += std::pow(relative,
                                                                options.magnitudePower)
                                                       * kernel * chromaFrame.weight;
                }
            }
        }
        out.push_back(chromaFrame);
    }
    return out;
}

std::vector<KeyScore> scoreKeys(const Chroma& chroma, const Chroma& bassChroma, double bassWeight)
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
            score.score = 0.5 * (score.krumhansl + score.temperley)
                          + bassWeight * bassChroma[(size_t) tonic];
            score.tonic = tonic;
            score.mode = major ? "major" : "minor";
            scores.push_back(score);
        }
    }
    return scores;
}

struct PitchFrame
{
    int index = 0;
    double midi = 0.0;
    double clarity = 0.0;
    double rms = 0.0;
};

struct PitchRun
{
    int begin = 0, end = 0;
    double midi = 0.0;
    double clarity = 0.0;
    double deviationCents = 0.0;
    double score = 0.0;
};

double midiOfFrequency(double frequency)
{
    return 69.0 + 12.0 * std::log2(frequency / 440.0);
}

struct Harmonicity
{
    double ratio = 0.0;
    double fundamentalShare = 0.0;
};

Harmonicity harmonicityOf(const float* data, int length, double sampleRate, double frequency,
                          const TonalOptions& options)
{
    Harmonicity result;
    const int fftSize = nextPowerOfTwo(std::max(1024, length));
    int order = 0;
    while ((1 << order) < fftSize) ++order;
    juce::dsp::FFT fft(order);
    juce::dsp::WindowingFunction<float> window((size_t) fftSize,
                                               juce::dsp::WindowingFunction<float>::hann, false);
    std::vector<float> buffer((size_t) fftSize * 2, 0.0f);
    for (int i = 0; i < length && i < fftSize; ++i) buffer[(size_t) i] = data[i];
    window.multiplyWithWindowingTable(buffer.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform(buffer.data());
    const int bins = fftSize / 2 + 1;
    const int lowBin = std::max(1, (int) std::floor(options.minPitchFrequency * fftSize / sampleRate));
    const int highBin = std::min(bins - 1, (int) std::ceil(5000.0 * fftSize / sampleRate));
    double total = 0.0;
    for (int bin = lowBin; bin <= highBin; ++bin) total += buffer[(size_t) bin];
    if (total <= 1.0e-20) return result;
    auto sumAround = [&](double centre)
    {
        const double bin = centre * fftSize / sampleRate;
        const int first = std::clamp((int) std::floor(bin) - 1, lowBin, highBin);
        const int last = std::clamp((int) std::ceil(bin) + 1, lowBin, highBin);
        double sum = 0.0;
        for (int index = first; index <= last; ++index) sum += buffer[(size_t) index];
        return sum;
    };
    double partials = 0.0;
    for (int harmonic = 1; harmonic <= 8; ++harmonic)
    {
        const double partial = frequency * (double) harmonic;
        if (partial > 5000.0 || partial >= sampleRate * 0.5) break;
        partials += sumAround(partial);
    }
    result.ratio = std::clamp(partials / total, 0.0, 1.0);
    result.fundamentalShare = std::clamp(sumAround(frequency) / total, 0.0, 1.0);
    return result;
}

PitchAnalysis analyzeChannelPitch(const float* data, int length, double sampleRate,
                                  const TonalOptions& options, const CancelCheck& cancel)
{
    PitchAnalysis result;
    if (data == nullptr || length <= 0 || sampleRate <= 0.0)
    {
        result.error = "invalid audio";
        return result;
    }
    const int activeWindow = std::min(1024, length);
    const int activeHop = std::max(64, options.pitchHop);
    if (activeWindow < 64) { result.explanation = "pitch: too short"; return result; }
    double peak = 0.0;
    for (int start = 0; start + activeWindow <= length; start += activeHop)
    {
        double squares = 0.0;
        for (int i = 0; i < activeWindow; ++i) squares += (double) data[start + i] * data[start + i];
        peak = std::max(peak, std::sqrt(squares / activeWindow));
    }
    if (peak < silenceRms)
    {
        result.explanation = "pitch: silence";
        return result;
    }
    const double activeThreshold = peak * std::pow(10.0, options.minActiveDbfs / 20.0);
    int firstActive = -1, lastActive = -1;
    for (int start = 0; start + activeWindow <= length; start += activeHop)
    {
        double squares = 0.0;
        for (int i = 0; i < activeWindow; ++i) squares += (double) data[start + i] * data[start + i];
        if (std::sqrt(squares / activeWindow) >= activeThreshold)
        {
            if (firstActive < 0) firstActive = start / activeHop;
            lastActive = start;
        }
    }
    if (firstActive < 0 || lastActive < 0)
    {
        result.explanation = "pitch: no active region";
        return result;
    }
    const int activeStart = firstActive * activeHop;
    const int activeEnd = std::min(length, lastActive + activeWindow);
    const int startSample = activeStart + (int) std::llround(options.attackSkipSeconds * sampleRate);
    if (startSample >= activeEnd - activeWindow / 4)
    {
        result.explanation = "pitch: no stable region after the attack";
        return result;
    }
    const int endSample = std::min(activeEnd,
                                   startSample + (int) std::llround(options.maxPitchSeconds * sampleRate));

    int window = nextPowerOfTwo((int) std::ceil(2.2 * sampleRate / options.minPitchFrequency));
    window = std::clamp(window, 1024, 16384);
    window = std::min(window, nextPowerOfTwo(std::max(1024, length)));
    const int hop = std::max(options.pitchHop, window / 8);
    const int fftSize = nextPowerOfTwo(2 * window);
    int order = 0;
    while ((1 << order) < fftSize) ++order;
    juce::dsp::FFT fft(order);
    const int maxLag = std::min(window - 1, (int) std::floor(sampleRate / options.minPitchFrequency));
    const int minLag = std::max(2, (int) std::floor(sampleRate / options.maxPitchFrequency));
    if (maxLag <= minLag || maxLag <= 2)
    {
        result.explanation = "pitch: invalid lag range";
        return result;
    }
    std::vector<juce::dsp::Complex<float>> input((size_t) fftSize), output((size_t) fftSize);
    std::vector<float> frame((size_t) window, 0.0f);
    std::vector<double> prefix((size_t) window + 1, 0.0);
    std::vector<double> difference((size_t) maxLag + 1, 1.0);
    std::vector<PitchFrame> frames;
    int frameIndex = 0;
    for (int start = startSample; start < endSample; start += hop, ++frameIndex)
    {
        if (cancel && cancel())
        {
            result.error = "Cancelled";
            return result;
        }
        const int count = std::min(window, length - start);
        if (count <= window / 4) break;
        std::fill(frame.begin(), frame.end(), 0.0f);
        double squares = 0.0;
        for (int i = 0; i < count; ++i)
        {
            frame[(size_t) i] = data[start + i];
            squares += (double) data[start + i] * data[start + i];
        }
        const double rms = std::sqrt(squares / count);
        if (rms < activeThreshold) continue;
        prefix[0] = 0.0;
        for (int i = 0; i < window; ++i) prefix[(size_t) i + 1] = prefix[(size_t) i]
            + (double) frame[(size_t) i] * frame[(size_t) i];
        for (int i = 0; i < fftSize; ++i)
            input[(size_t) i] = { i < window ? frame[(size_t) i] : 0.0f, 0.0f };
        fft.perform(input.data(), output.data(), false);
        for (int i = 0; i < fftSize; ++i)
        {
            const float real = output[(size_t) i].real();
            const float imaginary = output[(size_t) i].imag();
            output[(size_t) i] = { real * real + imaginary * imaginary, 0.0f };
        }
        fft.perform(output.data(), input.data(), true);
        const double acf0 = input[0].real();
        const double energy = prefix[(size_t) window];
        if (acf0 <= 1.0e-20 || energy <= 1.0e-20) continue;
        const double scale = acf0 / energy;
        difference[0] = 1.0;
        double running = 0.0;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            const double acf = input[(size_t) tau].real() / scale;
            double value = prefix[(size_t) (window - tau)] + (energy - prefix[(size_t) tau])
                           - 2.0 * acf;
            value = std::max(value, 0.0);
            running += value;
            difference[(size_t) tau] = running > 1.0e-20
                                           ? value * (double) tau / running : 1.0;
        }
        int bestTau = -1;
        for (int tau = minLag; tau <= maxLag; ++tau)
            if (difference[(size_t) tau] < options.yinThreshold) { bestTau = tau; break; }
        if (bestTau < 0)
        {
            bestTau = minLag;
            for (int tau = minLag + 1; tau <= maxLag; ++tau)
                if (difference[(size_t) tau] < difference[(size_t) bestTau]) bestTau = tau;
        }
        else
        {
            while (bestTau + 1 <= maxLag
                   && difference[(size_t) (bestTau + 1)] < difference[(size_t) bestTau])
                ++bestTau;
        }
        if (bestTau <= 1 || bestTau >= maxLag) continue;
        const double left = difference[(size_t) (bestTau - 1)];
        const double centre = difference[(size_t) bestTau];
        const double right = difference[(size_t) (bestTau + 1)];
        const double denominator = left - 2.0 * centre + right;
        const double delta = std::abs(denominator) > 1.0e-12
                                 ? 0.5 * (left - right) / denominator : 0.0;
        const double tau = (double) bestTau + std::clamp(delta, -0.5, 0.5);
        if (tau <= 0.0) continue;
        const double frequency = sampleRate / tau;
        const double clarity = std::clamp(1.0 - centre, 0.0, 1.0);
        if (frequency < options.minPitchFrequency || frequency > options.maxPitchFrequency
            || clarity < options.minPitchClarity)
            continue;
        frames.push_back({ frameIndex, midiOfFrequency(frequency), clarity, rms });
    }
    if (frames.empty())
    {
        result.explanation = "pitch: no periodic frames";
        return result;
    }

    std::vector<PitchRun> runs;
    for (size_t i = 0; i < frames.size();)
    {
        size_t j = i + 1;
        while (j < frames.size()
               && frames[j].index - frames[j - 1].index <= 2
               && std::abs(frames[j].midi - frames[j - 1].midi)
                      <= options.maxRunCents / 100.0)
            ++j;
        if ((int) (j - i) >= options.minStableFrames)
        {
            std::vector<double> values;
            double clarity = 0.0;
            for (size_t k = i; k < j; ++k)
            {
                values.push_back(frames[k].midi);
                clarity += frames[k].clarity;
            }
            std::sort(values.begin(), values.end());
            const double median = values[values.size() / 2];
            double squares = 0.0;
            for (double value : values) squares += (value - median) * (value - median);
            const double deviation = std::sqrt(squares / (double) values.size()) * 100.0;
            if (deviation <= options.maxStableDeviationCents)
            {
                PitchRun run;
                run.begin = (int) i;
                run.end = (int) j;
                run.midi = median;
                run.clarity = clarity / (double) (j - i);
                run.deviationCents = deviation;
                run.score = (double) (j - i) * run.clarity;
                runs.push_back(run);
            }
        }
        i = j;
    }
    if (runs.empty())
    {
        result.explanation = "pitch: no stable segment";
        return result;
    }
    const auto run = *std::max_element(runs.begin(), runs.end(),
        [](const PitchRun& a, const PitchRun& b) { return a.score < b.score; });
    const int middle = frames[(size_t) (run.begin + (run.end - run.begin) / 2)].index;
    const int segmentStart = startSample + middle * hop;
    const int segmentLength = std::min(window, std::max(0, length - segmentStart));
    if (segmentLength <= 0)
    {
        result.explanation = "pitch: stable segment outside the buffer";
        return result;
    }
    const double frequency = 440.0 * std::pow(2.0, (run.midi - 69.0) / 12.0);
    const auto harmonicity = harmonicityOf(data + segmentStart, segmentLength, sampleRate,
                                           frequency, options);
    if (harmonicity.ratio < options.minHarmonicity
        || harmonicity.fundamentalShare < options.minFundamentalShare)
    {
        result.explanation = "pitch: stable but inharmonic (harmonicity "
            + juce::String(harmonicity.ratio, 2) + ", fundamental "
            + juce::String(harmonicity.fundamentalShare, 2) + ")";
        return result;
    }
    result.found = true;
    result.midi = run.midi;
    result.cents = (run.midi - std::round(run.midi)) * 100.0;
    result.clarity = run.clarity;
    result.stabilityCents = run.deviationCents;
    result.harmonicity = harmonicity.ratio;
    result.stableFrames = run.end - run.begin;
    result.confidence = std::clamp(run.clarity
        * std::clamp(1.0 - run.deviationCents / options.maxStableDeviationCents, 0.0, 1.0)
        * harmonicity.ratio, 0.0, 1.0);
    result.explanation = "pitch: YIN, " + juce::String(run.end - run.begin)
        + " stable frames, clarity " + juce::String(run.clarity, 2)
        + ", deviation " + juce::String(run.deviationCents, 1) + " cents"
        + ", harmonicity " + juce::String(harmonicity.ratio, 2)
        + ", fundamental " + juce::String(harmonicity.fundamentalShare, 2)
        + ", MIDI " + juce::String(run.midi, 2)
        + (std::abs(result.cents) >= 0.5 ? " (" + juce::String(result.cents, 1) + " cents)" : "");
    return result;
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
    Chroma bassChroma {};
    const auto chromaFrames = buildChroma(frames, result.tuningCents, options, bassChroma);
    if (chromaFrames.empty()) return result;

    const int frameCount = (int) chromaFrames.size();
    Chroma meanChroma {};
    int harmonicFrames = 0;
    for (const auto& frame : chromaFrames)
    {
        if (frame.weight <= 0.0) continue;
        double norm = 0.0;
        for (double value : frame.chroma) norm += value * value;
        norm = std::sqrt(norm);
        if (norm <= 1.0e-12) continue;
        for (int i = 0; i < pitchClassCount; ++i)
            meanChroma[(size_t) i] += frame.chroma[(size_t) i] / norm;
        ++harmonicFrames;
    }
    double consistency = 0.0;
    if (harmonicFrames > 0)
    {
        double meanNorm = 0.0;
        for (double value : meanChroma) meanNorm += value * value;
        meanNorm = std::sqrt(meanNorm);
        if (meanNorm > 1.0e-12)
        {
            for (double& value : meanChroma) value /= meanNorm;
            double similarity = 0.0;
            int compared = 0;
            for (const auto& frame : chromaFrames)
            {
                if (frame.weight <= 0.0) continue;
                double norm = 0.0;
                for (double value : frame.chroma) norm += value * value;
                norm = std::sqrt(norm);
                if (norm <= 1.0e-12) continue;
                double dot = 0.0;
                for (int i = 0; i < pitchClassCount; ++i)
                    dot += (frame.chroma[(size_t) i] / norm) * meanChroma[(size_t) i];
                similarity += std::clamp(dot, 0.0, 1.0);
                ++compared;
            }
            if (compared > 0) consistency = similarity / (double) compared;
        }
    }
    result.chromaConsistency = consistency;
    double rmsVariation = 0.0, onsetDensity = 0.0;
    {
        std::vector<double> rmsValues, fluxValues;
        for (const auto& frame : frames)
        {
            if (frame.rms < silenceRms) continue;
            rmsValues.push_back(frame.rms);
            fluxValues.push_back(frame.flux);
        }
        if (! rmsValues.empty())
        {
            const double mean = std::accumulate(rmsValues.begin(), rmsValues.end(), 0.0)
                                / (double) rmsValues.size();
            double variance = 0.0;
            for (double value : rmsValues) variance += (value - mean) * (value - mean);
            variance /= (double) rmsValues.size();
            rmsVariation = mean > 1.0e-20 ? std::sqrt(variance) / mean : 0.0;
        }
        if (! fluxValues.empty() && hop > 0)
        {
            std::vector<double> sorted = fluxValues;
            const auto middle = sorted.begin() + (std::ptrdiff_t) (sorted.size() / 2);
            std::nth_element(sorted.begin(), middle, sorted.end());
            const double threshold = std::max(*middle * 2.5, 0.01);
            int onsets = 0;
            for (size_t i = 1; i + 1 < fluxValues.size(); ++i)
                if (fluxValues[i] > threshold && fluxValues[i] >= fluxValues[i - 1]
                    && fluxValues[i] > fluxValues[i + 1])
                    ++onsets;
            const double duration = (double) rmsValues.size() * (double) hop / sampleRate;
            onsetDensity = duration > 0.0 ? (double) onsets / duration : 0.0;
        }
    }
    result.rmsVariation = rmsVariation;
    result.onsetDensity = onsetDensity;
    const int radius = std::max(0, options.medianFrames);
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
            const int low = std::max(0, index - radius);
            const int high = std::min(frameCount - 1, index + radius);
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
    result.percussive = result.tonality < options.minTonality
                        || (consistency < options.minChromaConsistency
                            && (rmsVariation > 0.8 || onsetDensity > 8.0));
    double norm = 0.0;
    for (double value : summed) norm += value * value;
    norm = std::sqrt(norm);
    if (norm <= 1.0e-12)
    {
        result.explanation = "key: no stable tonal content";
        return result;
    }
    for (double& value : summed) value /= norm;

    double bassNorm = 0.0;
    for (double value : bassChroma) bassNorm += value * value;
    bassNorm = std::sqrt(bassNorm);
    if (bassNorm > 1.0e-12)
        for (double& value : bassChroma) value /= bassNorm;

    result.chroma = summed;
    result.bassChroma = bassChroma;
    const auto scores = scoreKeys(summed, bassChroma, options.bassWeight);
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
        + ", tonality " + juce::String(result.tonality, 3)
        + ", consistency " + juce::String(consistency, 3)
        + ", rms variation " + juce::String(rmsVariation, 2)
        + ", onsets/s " + juce::String(onsetDensity, 1);

    if (result.percussive)
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
    // A relative major/minor pair shares every scale degree; the profiles cannot settle the tonic
    // without a clear margin. Below it the honest answer is "no key", not an invented tonic.
    const KeyScore* relativeRunner = nullptr;
    for (const auto& score : scores)
    {
        if (score.tonic == best.tonic) continue;
        const bool relative = (best.mode == "major" && score.mode == "minor"
                               && (best.tonic + 9) % 12 == score.tonic)
                           || (best.mode == "minor" && score.mode == "major"
                               && (score.tonic + 9) % 12 == best.tonic);
        if (relative && (relativeRunner == nullptr || score.score > relativeRunner->score))
            relativeRunner = &score;
    }
    if (relativeRunner != nullptr
        && best.score - relativeRunner->score < options.relativeMargin)
    {
        result.explanation = explanation + " (relative major/minor tonic dispute)";
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

PitchAnalysis TonalAnalyzer::analyzePitch(const juce::AudioBuffer<float>& audio, double sampleRate,
                                          const TonalOptions& options, const CancelCheck& cancel)
{
    PitchAnalysis best;
    std::vector<PitchAnalysis> found;
    juce::String firstExplanation;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        auto result = analyzeChannelPitch(audio.getReadPointer(channel), audio.getNumSamples(),
                                          sampleRate, options, cancel);
        if (result.error == "Cancelled") return result;
        if (firstExplanation.isEmpty()) firstExplanation = result.explanation;
        if (result.found) found.push_back(std::move(result));
    }
    if (found.empty())
    {
        best.explanation = firstExplanation.isNotEmpty() ? firstExplanation
                                                         : "pitch: no periodic frames";
        return best;
    }
    best = found.front();
    for (const auto& result : found)
        if (result.confidence > best.confidence) best = result;
    if (found.size() >= 2)
    {
        const double difference = std::abs(found[0].midi - found[1].midi);
        const double high = std::max(found[0].confidence, found[1].confidence);
        const double low = std::min(found[0].confidence, found[1].confidence);
        if (difference > 0.5 && low >= 0.7 * high)
        {
            PitchAnalysis ambiguous;
            ambiguous.explanation = "pitch: channels disagree ("
                + juce::String(found[0].midi, 2) + " vs " + juce::String(found[1].midi, 2) + ")";
            return ambiguous;
        }
    }
    return best;
}
} // namespace sonicator
