#include "RhythmAnalyzer.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <numeric>
namespace sonicator {
namespace {
// 44100 / 512: one analysis frame per ~11.6 ms at any sample rate, so tempo resolution does not
// depend on the file's rate.
constexpr double framesPerSecondTarget = 86.1328125;
constexpr int maxFftSize = 8192;
constexpr int minFramesForTempo = 8;
constexpr double minAnalysableSeconds = 0.10;

struct GridFit
{
    bool valid = false;
    double bpm = 0;
    double score = 0;      // mean onset strength on the best grid
    double coverage = 0;   // share of onset strength explained by the grid
    double fill = 0;       // share of grid positions carrying an onset
    int count = 0;         // grid positions inside the active span (beats of context)
    // Ranking and ambiguity use score times coverage: a half-tempo grid that lands on every other
    // hit has the same mean strength but explains only half the material, so it is not a rival.
    double quality() const { return score * coverage; }
};

struct Envelope
{
    std::vector<double> odf;   // normalized onset strength per frame
    std::vector<double> rms;   // per-frame RMS
    std::vector<int> onsets;   // accepted peak frames
    std::vector<double> onsetStrength;
    int activeStart = 0, activeEnd = -1;
    double peak = 0, activeRms = 0;
    double maxOnset = 0;
};

double sampleAt(const std::vector<double>& values, double position)
{
    if (values.empty() || position < 0) return 0.0;
    const auto index = (size_t) position;
    if (index + 1 >= values.size()) return index < values.size() ? values[index] : 0.0;
    const double fraction = position - (double) index;
    return values[index] * (1.0 - fraction) + values[index + 1] * fraction;
}

double percentile95(std::vector<double> values)
{
    if (values.empty()) return 0.0;
    const auto index = (size_t) std::clamp<double>((double) (values.size() - 1) * 0.95,
                                                   0.0, (double) (values.size() - 1));
    std::nth_element(values.begin(), values.begin() + (std::ptrdiff_t) index, values.end());
    return values[index];
}

// Average value and spread of the onset signal, used for a threshold that follows the material
// instead of a fixed level.
void meanAndDeviation(const std::vector<double>& values, double& mean, double& deviation)
{
    mean = deviation = 0.0;
    if (values.empty()) return;
    mean = std::accumulate(values.begin(), values.end(), 0.0) / (double) values.size();
    double squares = 0;
    for (double value : values) squares += (value - mean) * (value - mean);
    deviation = std::sqrt(squares / (double) values.size());
}

double autocorrelation(const std::vector<double>& values, int lag)
{
    if (lag <= 0 || lag >= (int) values.size()) return 0.0;
    double dot = 0, left = 0, right = 0;
    for (size_t i = 0; i + (size_t) lag < values.size(); ++i)
    {
        dot += values[i] * values[i + (size_t) lag];
        left += values[i] * values[i];
        right += values[i + (size_t) lag] * values[i + (size_t) lag];
    }
    return left > 0 && right > 0 ? dot / std::sqrt(left * right) : 0.0;
}

GridFit fitGrid(const std::vector<double>& odf, double framesPerSecond, double bpm,
                int activeStart, int activeEnd, double minimumOnset = 0.20)
{
    GridFit fit;
    if (bpm <= 0 || activeEnd <= activeStart) return fit;
    const double period = 60.0 * framesPerSecond / bpm;
    if (period < 2.0) return fit;
    double bestScore = -1, bestPhase = 0;
    int bestCount = 0;
    // The phase is searched in half-frame steps; the period itself stays fractional, so a tempo
    // that does not fall on a whole frame is not snapped to the wrong grid.
    for (double phase = 0; phase < period; phase += 0.5)
    {
        double sum = 0;
        int count = 0;
        for (double position = (double) activeStart + phase;
             position <= (double) activeEnd; position += period)
        {
            sum += sampleAt(odf, position);
            ++count;
        }
        if (count < 2) continue;
        const double score = sum / (double) count;
        if (score > bestScore) { bestScore = score; bestPhase = phase; bestCount = count; }
    }
    if (bestScore < 0) return fit;
    fit.valid = true;
    fit.bpm = bpm;
    fit.score = bestScore;
    fit.count = bestCount;
    const double tolerance = std::max(2.0, 0.12 * period);
    // Weighted coverage: a ghost note off the grid counts for less than a beat that lands on it.
    double matched = 0, total = 0;
    for (size_t i = 0; i < odf.size(); ++i)
    {
        if ((int) i < activeStart || (int) i > activeEnd) continue;
        double relative = std::fmod((double) i - (double) activeStart - bestPhase, period);
        if (relative < 0) relative += period;
        const double distance = std::min(relative, period - relative);
        const double weight = odf[i];
        total += weight;
        if (distance <= tolerance) matched += weight;
    }
    fit.coverage = total > 0 ? matched / total : 0.0;
    int hits = 0;
    for (double position = (double) activeStart + bestPhase;
         position <= (double) activeEnd; position += period)
    {
        if (sampleAt(odf, position) > minimumOnset) ++hits;
    }
    fit.fill = bestCount > 0 ? (double) hits / (double) bestCount : 0.0;
    return fit;
}

GridFit refineBpm(const std::vector<double>& odf, double framesPerSecond, double bpm,
                  int activeStart, int activeEnd, double span)
{
    GridFit best;
    const double low = bpm * (1.0 - span), high = bpm * (1.0 + span);
    for (double candidate = low; candidate <= high; candidate += 0.05)
    {
        auto fit = fitGrid(odf, framesPerSecond, candidate, activeStart, activeEnd);
        if (fit.valid && fit.score > best.score) best = fit;
    }
    return best;
}

bool nearHalfOrDouble(double a, double b, double tolerance)
{
    if (a <= 0 || b <= 0) return false;
    const double ratio = a > b ? a / b : b / a;
    return std::abs(ratio - 2.0) <= 2.0 * tolerance;
}

Envelope buildEnvelope(const juce::AudioBuffer<float>& audio, double sampleRate,
                       const RhythmOptions& options, const CancelCheck& cancel)
{
    Envelope envelope;
    const int channels = audio.getNumChannels();
    const int64_t samples = audio.getNumSamples();
    const double seconds = (double) samples / sampleRate;
    int hop = (int) std::llround(sampleRate / framesPerSecondTarget);
    hop = std::clamp(hop, 64, 2048);
    int fftSize = 1024;
    while (fftSize < hop * 4) fftSize *= 2;
    fftSize = std::min(fftSize, maxFftSize);
    int order = 0;
    while ((1 << order) < fftSize) ++order;
    juce::dsp::FFT fft(order);
    juce::dsp::WindowingFunction<float> window((size_t) fftSize,
                                             juce::dsp::WindowingFunction<float>::hann, false);
    const int bins = fftSize / 2 + 1;
    const int frames = samples >= fftSize ? 1 + (int) ((samples - fftSize) / hop) : 1;
    const double framesPerSecond = sampleRate / (double) hop;
    std::vector<double> flux((size_t) frames, 0.0), rms((size_t) frames, 0.0);
    std::vector<double> previous((size_t) bins, 0.0), power((size_t) bins, 0.0);
    std::vector<float> frame((size_t) fftSize * 2, 0.0f);
    for (int index = 0; index < frames; ++index)
    {
        if (cancel && cancel()) return {};
        const auto offset = (int64_t) index * hop;
        const int count = (int) std::min<int64_t>(fftSize, samples - offset);
        std::fill(power.begin(), power.end(), 0.0);
        double squares = 0;
        for (int channel = 0; channel < channels; ++channel)
        {
            std::fill(frame.begin(), frame.end(), 0.0f);
            const auto* data = audio.getReadPointer(channel);
            for (int i = 0; i < count; ++i)
            {
                frame[(size_t) i] = data[offset + i];
                squares += (double) data[offset + i] * data[offset + i];
            }
            window.multiplyWithWindowingTable(frame.data(), (size_t) fftSize);
            fft.performFrequencyOnlyForwardTransform(frame.data());
            for (int k = 0; k < bins; ++k)
                power[(size_t) k] += (double) frame[(size_t) k] * frame[(size_t) k];
        }
        rms[(size_t) index] = std::sqrt(squares / ((double) count * channels));
        double difference = 0;
        for (int k = 0; k < bins; ++k)
        {
            const double magnitude = std::sqrt(power[(size_t) k] / channels)
                                     * 2.0 / (double) fftSize;
            const double compressed = std::log1p(magnitude * 8.0);
            difference += std::max(0.0, compressed - previous[(size_t) k]);
            previous[(size_t) k] = compressed;
        }
        flux[(size_t) index] = bins > 1 ? difference / (double) (bins - 1) : 0.0;
    }
    // Detrend the flux with a local mean so a steady timbre does not read as continuous onsets.
    const int radius = std::max(3, (int) std::llround(0.4 * framesPerSecond));
    std::vector<double> prefix((size_t) frames + 1, 0.0);
    for (int i = 0; i < frames; ++i) prefix[(size_t) i + 1] = prefix[(size_t) i] + flux[(size_t) i];
    std::vector<double> rectified((size_t) frames, 0.0);
    for (int i = 0; i < frames; ++i)
    {
        const int low = std::max(0, i - radius), high = std::min(frames, i + radius + 1);
        const double localMean = (prefix[(size_t) high] - prefix[(size_t) low]) / (double) (high - low);
        rectified[(size_t) i] = std::max(0.0, flux[(size_t) i] - localMean);
    }
    // A sparse loop has fewer than 5% onset frames, so the 95th percentile can be zero and would
    // silence the whole signal. The maximum keeps a few strong events visible; the percentile
    // still wins when one loud accent would otherwise flatten everything else.
    const double maximum = rectified.empty() ? 0.0
                                             : *std::max_element(rectified.begin(), rectified.end());
    const double scale = std::max(percentile95(rectified), 0.35 * maximum);
    envelope.odf.assign((size_t) frames, 0.0);
    if (scale > 1.0e-12)
        for (int i = 0; i < frames; ++i)
            envelope.odf[(size_t) i] = std::min(1.0, rectified[(size_t) i] / scale);
    envelope.rms = std::move(rms);
    // Active span: ignore leading and trailing near-silence, but never trim the audible body.
    double maxRms = 0;
    for (double value : envelope.rms) maxRms = std::max(maxRms, value);
    if (maxRms > 1.0e-9)
    {
        const double activeThreshold = std::max(0.02 * maxRms, options.silencePeak);
        for (int i = 0; i < frames; ++i)
        {
            if (envelope.rms[(size_t) i] < activeThreshold) continue;
            if (envelope.activeEnd < 0) envelope.activeStart = i;
            envelope.activeEnd = i;
        }
    }
    // Peak and RMS over the active span: crest separates a hit from a pad or a texture.
    double squares = 0, peak = 0;
    int counted = 0;
    const int64_t first = (int64_t) envelope.activeStart * hop;
    const int64_t last = std::min<int64_t>(samples, ((int64_t) envelope.activeEnd + 1) * hop);
    for (int channel = 0; channel < channels; ++channel)
    {
        const auto* data = audio.getReadPointer(channel);
        for (int64_t i = first; i < last; ++i)
        {
            const double value = (double) data[i];
            peak = std::max(peak, std::abs(value));
            squares += value * value;
            ++counted;
        }
    }
    envelope.peak = peak;
    envelope.activeRms = counted > 0 ? std::sqrt(squares / (double) counted) : 0.0;
    // Peak picking: local maxima above an adaptive threshold, greedily thinned by strength so a
    // single transient does not become several onsets.
    double mean = 0, deviation = 0;
    meanAndDeviation(envelope.odf, mean, deviation);
    const double threshold = std::max(0.08, mean + 0.5 * deviation);
    const int minimumSeparation = std::max(1, (int) std::llround(options.minOnsetSeparationSeconds
                                                               * framesPerSecond));
    std::vector<std::pair<double, int>> peaks;
    for (int i = 0; i < frames; ++i)
    {
        const double value = envelope.odf[(size_t) i];
        if (value < threshold) continue;
        const double left = i > 0 ? envelope.odf[(size_t) i - 1] : -1.0;
        const double right = i + 1 < frames ? envelope.odf[(size_t) i + 1] : -1.0;
        if (value < left || value < right) continue;
        if (value == left && i > 0) continue; // keep the first of a plateau
        peaks.emplace_back(value, i);
    }
    std::sort(peaks.begin(), peaks.end(), [](const auto& a, const auto& b)
    {
        return a.first > b.first;
    });
    std::vector<int> accepted;
    for (const auto& peak : peaks)
    {
        bool free = true;
        for (int existing : accepted)
            if (std::abs(existing - peak.second) < minimumSeparation) { free = false; break; }
        if (free) accepted.push_back(peak.second);
    }
    std::sort(accepted.begin(), accepted.end());
    envelope.onsets = std::move(accepted);
    for (int onset : envelope.onsets)
    {
        envelope.onsetStrength.push_back(envelope.odf[(size_t) onset]);
        envelope.maxOnset = std::max(envelope.maxOnset, envelope.odf[(size_t) onset]);
    }
    envelope.activeStart = std::max(0, envelope.activeStart);
    envelope.activeEnd = envelope.activeEnd < 0 ? frames - 1 : envelope.activeEnd;
    (void) seconds;
    return envelope;
}
} // namespace

RhythmAnalysis RhythmAnalyzer::analyze(const juce::AudioBuffer<float>& audio, double sampleRate,
                                       const RhythmOptions& options, const CancelCheck& cancel)
{
    RhythmAnalysis result;
    if (sampleRate <= 0 || audio.getNumChannels() <= 0 || audio.getNumSamples() <= 0)
    {
        result.error = "no audio to analyse";
        return result;
    }
    result.durationSeconds = (double) audio.getNumSamples() / sampleRate;
    auto envelope = buildEnvelope(audio, sampleRate, options, cancel);
    if (cancel && cancel()) { result.error = "Cancelled"; return result; }
    result.onsetCount = (int) envelope.onsets.size();
    if (envelope.peak <= options.silencePeak)
    {
        result.explanation = "silent or below -60 dBFS";
        return result;
    }
    const int64_t hop = std::clamp<int64_t>(
        (int64_t) std::llround(sampleRate / framesPerSecondTarget), 64, 2048);
    const double framesPerSecond = sampleRate / (double) hop;
    const int activeFrames = envelope.activeEnd - envelope.activeStart + 1;
    result.activeSeconds = activeFrames > 0 ? (double) activeFrames / framesPerSecond : 0.0;
    if (result.durationSeconds < minAnalysableSeconds
        || (int) envelope.odf.size() < minFramesForTempo)
    {
        result.explanation = "too short to analyse";
        return result;
    }
    // Candidate tempi come from autocorrelation peaks, then each is refined against a comb grid.
    const int frames = (int) envelope.odf.size();
    const int lagMin = std::max(2, (int) std::llround(60.0 * framesPerSecond / options.maxBpm));
    const int lagMax = std::min(frames - 2,
                                (int) std::llround(60.0 * framesPerSecond / options.minBpm));
    std::vector<std::pair<double, double>> initial; // bpm, acf
    if (lagMax > lagMin)
    {
        std::vector<double> acf((size_t) lagMax + 1, 0.0);
        for (int lag = lagMin; lag <= lagMax; ++lag)
            acf[(size_t) lag] = autocorrelation(envelope.odf, lag);
        std::vector<std::pair<double, double>> peaks;
        for (int lag = lagMin; lag <= lagMax; ++lag)
        {
            const double value = acf[(size_t) lag];
            const double left = lag > lagMin ? acf[(size_t) lag - 1] : -1.0;
            const double right = lag < lagMax ? acf[(size_t) lag + 1] : -1.0;
            if (value < 0.05 || value < left || value < right) continue;
            peaks.emplace_back(value, 60.0 * framesPerSecond / (double) lag);
        }
        std::sort(peaks.begin(), peaks.end(), [](const auto& a, const auto& b)
        {
            return a.first > b.first;
        });
        for (const auto& peak : peaks)
        {
            bool distinct = true;
            for (const auto& kept : initial)
            {
                if (std::abs(kept.first - peak.second) < std::max(1.0, 0.03 * peak.second))
                {
                    distinct = false;
                    break;
                }
            }
            if (distinct) initial.push_back({ peak.second, peak.first });
            if (initial.size() >= 8) break;
        }
    }
    std::vector<GridFit> fits;
    auto addFit = [&](const GridFit& fit)
    {
        if (! fit.valid || fit.bpm < options.minBpm || fit.bpm > options.maxBpm) return;
        for (auto& existing : fits)
            if (std::abs(existing.bpm - fit.bpm) < 1.0)
            {
                if (fit.quality() > existing.quality()) existing = fit;
                return;
            }
        fits.push_back(fit);
    };
    for (const auto& candidate : initial)
        addFit(refineBpm(envelope.odf, framesPerSecond, candidate.first,
                         envelope.activeStart, envelope.activeEnd, 0.025));
    // Half and double time are explicit candidates, not something to guess from the winner later.
    const size_t initialCount = fits.size();
    for (size_t i = 0; i < initialCount; ++i)
    {
        for (double factor : { 0.5, 2.0 })
        {
            const double bpm = fits[i].bpm * factor;
            if (bpm >= options.minBpm && bpm <= options.maxBpm)
                addFit(refineBpm(envelope.odf, framesPerSecond, bpm,
                                 envelope.activeStart, envelope.activeEnd, 0.015));
        }
    }
    std::sort(fits.begin(), fits.end(), [](const GridFit& a, const GridFit& b)
    {
        return a.quality() > b.quality();
    });
    bool hasGrid = ! fits.empty() && fits.front().quality() > 0;
    GridFit best = hasGrid ? fits.front() : GridFit {};
    bool ambiguous = false;
    if (hasGrid)
        for (size_t i = 1; i < fits.size(); ++i)
        {
            const double share = best.quality() > 0 ? fits[i].quality() / best.quality() : 0.0;
            if (share < options.ambiguityScoreShare) continue;
            if (nearHalfOrDouble(fits[i].bpm, best.bpm, options.halfDoubleRatioTolerance)
                || share >= options.unrelatedAmbiguityScoreShare)
                ambiguous = true;
        }
    const bool loopEvidence = hasGrid
        && result.onsetCount >= options.minRhythmicOnsets
        && best.count >= options.minBeatsForTempo
        && best.coverage >= options.minGridCoverage
        && best.fill >= options.minGridFill
        && best.score >= options.minGridScore;
    const double crest = envelope.activeRms > 1.0e-9 ? envelope.peak / envelope.activeRms : 0.0;
    if (loopEvidence)
    {
        result.kind = RhythmKind::RhythmicLoop;
        result.bpm = best.bpm;
        result.bpmConfidence = std::clamp(0.55 * best.score + 0.45 * best.coverage, 0.0, 1.0);
        if (ambiguous) result.bpmConfidence *= 0.7;
        result.tempo = best.score >= options.determinedGridScore && ! ambiguous
                           ? RhythmTempo::Determined
                           : RhythmTempo::Ambiguous;
        result.kindConfidence = std::clamp(0.5 + 0.5 * result.bpmConfidence, 0.0, 1.0);
        for (size_t i = 1; i < fits.size(); ++i)
        {
            if (fits[i].quality() < 0.6 * best.quality()) continue;
            if (std::abs(fits[i].bpm - best.bpm) < 1.0) continue;
            if (result.alternatives.size() < 4) result.alternatives.push_back(fits[i].bpm);
        }
        result.explanation = juce::String("audio onset grid: ")
            + juce::String(result.onsetCount) + " onsets, "
            + juce::String(best.count) + " beats, "
            + juce::String(best.coverage * 100.0, 0) + "% coverage"
            + (ambiguous ? ", competing tempi" : "");
        if (result.tempo == RhythmTempo::Ambiguous)
            result.explanation += "; BPM not published";
        return result;
    }
    // One clear attack (or a few transients on it) without a repeating grid is the one-shot case.
    // Evaluated before the short-fragment case: a decaying hit can leave a spurious second onset,
    // and that is still a hit, not a rhythm.
    const bool oneShot = result.onsetCount >= 1
        && result.onsetCount <= options.oneShotMaxOnsets
        && result.activeSeconds <= options.oneShotMaxActiveSeconds
        && crest >= options.oneShotMinCrest
        && envelope.maxOnset >= options.oneShotMinOnsetStrength;
    if (oneShot)
    {
        result.kind = RhythmKind::OneShot;
        result.kindConfidence = std::clamp(0.6 + 0.05 * (crest - options.oneShotMinCrest),
                                           0.0, 0.85);
        result.explanation = (result.onsetCount <= 1
                                  ? juce::String("single attack, crest ")
                                  : juce::String("few transients without a repeating grid, crest "))
            + juce::String(crest, 1);
        return result;
    }
    // A short but consistent fragment is not a loop: there is simply not enough context for a
    // tempo. Say so instead of guessing one.
    const bool shortRhythmicFragment = hasGrid && result.onsetCount >= 2
                                       && best.count >= 2 && best.score >= 0.45;
    if (shortRhythmicFragment)
    {
        result.kind = RhythmKind::Undetermined;
        result.explanation = "rhythmic fragment too short for a tempo ("
            + juce::String(result.onsetCount) + " onsets)";
        return result;
    }
    result.kind = RhythmKind::Undetermined;
    if (hasGrid)
        result.explanation = "no stable beat grid (coverage "
            + juce::String(best.coverage * 100.0, 0) + "%, "
            + juce::String(best.count) + " beats)";
    else
        result.explanation = "no periodic beat found ("
            + juce::String(result.onsetCount) + " onsets)";
    return result;
}

RhythmAnalysis RhythmAnalyzer::analyzeFile(const juce::File& file, const RhythmOptions& options,
                                           const CancelCheck& cancel)
{
    RhythmAnalysis result;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0 || reader->lengthInSamples <= 0
        || reader->numChannels == 0 || reader->numChannels > 64)
    {
        result.error = "unsupported, empty or corrupt audio file";
        return result;
    }
    const int64_t byTime = (int64_t) std::llround(options.maxAnalysisSeconds * reader->sampleRate);
    const int64_t byMemory = options.maxDecodedBytes / (4 * (int64_t) reader->numChannels);
    const int64_t length = std::min({ reader->lengthInSamples, byTime, byMemory });
    if (length <= 0) { result.error = "empty audio file"; return result; }
    juce::AudioBuffer<float> audio((int) reader->numChannels, (int) length);
    if (! reader->read(&audio, 0, (int) length, 0, true, true))
    {
        result.error = "audio decode failed";
        return result;
    }
    result = analyze(audio, reader->sampleRate, options, cancel);
    if (reader->lengthInSamples > length)
        result.explanation += " (first "
            + juce::String((double) length / reader->sampleRate, 0) + " s analysed)";
    return result;
}
} // namespace sonicator
