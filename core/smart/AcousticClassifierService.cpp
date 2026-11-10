#include "AcousticClassifierService.h"
#include "GenreContextExtractor.h"
#include "core/dsp/AcousticFeatures.h"
#include "core/dsp/FastAcousticRules.h"
#include "core/dsp/AnalysisAudio.h"

namespace sonicator {

namespace {

std::optional<SmartCandidate> extractGenreCandidate(
    const juce::File& file,
    const std::vector<SmartCandidate>& existingCandidates)
{
    const auto genreResult = GenreContextExtractor::extract(file, existingCandidates);
    if (genreResult.confidence >= 0.80 && (genreResult.subgenre.isNotEmpty() || genreResult.genre.isNotEmpty()))
    {
        SmartCandidate c;
        c.field = SmartField::Type;
        c.source = genreResult.source;
        c.textValue = genreResult.subgenre.isNotEmpty() ? genreResult.subgenre : genreResult.genre;
        c.detail = genreResult.genre;
        c.numericValue = genreResult.confidence;
        c.status = CandidateStatus::Valid;
        c.extractorId = genreResult.source == "path" ? "name" : (genreResult.source == "metadata" ? "container" : genreResult.source);
        c.extractorVersion = smartExtractorsVersion;
        c.rawValue = c.textValue;
        c.limitation = "genre context inferred from path or metadata";
        return c;
    }
    return std::nullopt;
}

} // namespace

OnnxAudioClassifier& AcousticClassifierService::getSharedClassifier()
{
    static OnnxAudioClassifier classifier;
    return classifier;
}

AdaptiveClassifier& AcousticClassifierService::getSharedAdaptiveClassifier()
{
    static AdaptiveClassifier classifier;
    return classifier;
}

std::vector<SmartCandidate> AcousticClassifierService::analyzeBuffer(
    const juce::AudioBuffer<float>& buffer,
    double sampleRate,
    const juce::File& file,
    const std::vector<SmartCandidate>& existingCandidates)
{
    std::vector<SmartCandidate> candidates;

    if (buffer.getNumSamples() > 0 && sampleRate > 0.0)
    {
        auto& adaptive = getSharedAdaptiveClassifier();
        if (adaptive.hasPrototypes())
        {
            const auto features = AdaptiveClassifier::extractFeatures(buffer, sampleRate);
            const auto preds = adaptive.predictType(features, 1);
            if (! preds.empty() && preds.front().score >= 0.80f)
            {
                SmartCandidate c;
                c.field = SmartField::Type;
                c.source = "user_adaptive";
                c.textValue = preds.front().label;
                c.detail = "adaptive_sim=" + juce::String(preds.front().score, 2);
                c.numericValue = preds.front().score;
                c.status = CandidateStatus::Valid;
                c.extractorId = "dsp";
                c.extractorVersion = smartDspVersion;
                c.rawValue = preds.front().label;
                c.limitation = "type learned from user feedback prototype";
                candidates.push_back(std::move(c));
            }
        }

        const double duration = static_cast<double>(buffer.getNumSamples()) / sampleRate;
        const auto desc = AcousticFeatures::extract(buffer, sampleRate);
        const auto fastDecision = FastAcousticRules::evaluate(desc, duration);

        if (fastDecision.determined && fastDecision.confidence >= 0.85)
        {
            SmartCandidate c;
            c.field = SmartField::Type;
            c.source = "dsp_acoustic";
            c.textValue = fastDecision.canonicalType;
            c.detail = fastDecision.ruleReason;
            c.numericValue = fastDecision.confidence;
            c.status = CandidateStatus::Valid;
            c.extractorId = "dsp";
            c.extractorVersion = smartDspVersion;
            c.rawValue = fastDecision.canonicalType;
            c.limitation = "type derived from acoustic features";
            candidates.push_back(std::move(c));
        }
        else
        {
            auto& classifier = getSharedClassifier();
            if (classifier.isLoaded())
            {
                auto preds = classifier.classify(buffer, sampleRate, 3);
                if (! preds.empty() && preds.front().probability >= 0.50f)
                {
                    const auto& topPred = preds.front();
                    SmartCandidate c;
                    c.field = SmartField::Type;
                    c.source = "ml_classifier";
                    c.textValue = topPred.className;
                    c.detail = "ml_prob=" + juce::String(topPred.probability, 2);
                    c.numericValue = topPred.probability;
                    c.status = CandidateStatus::Valid;
                    c.extractorId = "dsp";
                    c.extractorVersion = smartDspVersion;
                    c.rawValue = topPred.className;
                    c.limitation = "type predicted by machine learning classifier";
                    candidates.push_back(std::move(c));
                }
            }
        }
    }

    bool hasGenre = false;
    for (const auto& c : existingCandidates)
    {
        if (c.field == SmartField::Type && (c.source == "path" || c.source == "id3" || c.source == "metadata"))
        {
            hasGenre = true;
            break;
        }
    }

    if (! hasGenre)
    {
        if (auto genreCand = extractGenreCandidate(file, existingCandidates))
        {
            candidates.push_back(std::move(*genreCand));
        }
    }

    return candidates;
}

std::vector<SmartCandidate> AcousticClassifierService::analyzeAudioAndContext(
    const juce::File& file,
    const SampleRecord& record,
    CancelCheck cancel)
{
    return analyzeAudioAndContext(file, record, {}, std::move(cancel));
}

std::vector<SmartCandidate> AcousticClassifierService::analyzeAudioAndContext(
    const juce::File& file,
    const SampleRecord& record,
    const std::vector<SmartCandidate>& existingCandidates,
    CancelCheck cancel)
{
    const juce::File targetFile = file.existsAsFile() ? file : juce::File(record.filePath);

    if (cancel && cancel())
    {
        std::vector<SmartCandidate> candidates;
        if (auto genreCand = extractGenreCandidate(targetFile, existingCandidates))
            candidates.push_back(std::move(*genreCand));
        return candidates;
    }

    AnalysisAudioOptions options;
    options.maxSeconds = 10.0;
    const auto audio = readAnalysisAudio(targetFile, options, cancel);

    if (! audio.ok() || (cancel && cancel()))
    {
        std::vector<SmartCandidate> candidates;
        if (auto genreCand = extractGenreCandidate(targetFile, existingCandidates))
            candidates.push_back(std::move(*genreCand));
        return candidates;
    }

    return analyzeBuffer(audio.buffer, audio.sampleRate, targetFile, existingCandidates);
}

} // namespace sonicator
