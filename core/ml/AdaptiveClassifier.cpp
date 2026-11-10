#include "AdaptiveClassifier.h"
#include "core/dsp/AcousticFeatures.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>

namespace sonicator {

namespace {

float computeCosineSimilarity(const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.empty() || b.empty()) return 0.0f;
    const size_t n = std::min(a.size(), b.size());
    float dot = 0.0f, normA = 0.0f, normB = 0.0f;
    for (size_t i = 0; i < n; ++i)
    {
        dot += a[i] * b[i];
        normA += a[i] * a[i];
        normB += b[i] * b[i];
    }
    if (normA <= 0.0f || normB <= 0.0f) return 0.0f;
    const float sim = dot / (std::sqrt(normA) * std::sqrt(normB));
    return std::max(0.0f, std::min(1.0f, sim));
}

} // namespace

struct Prototype {
    int64_t sampleId = 0;
    juce::String targetType;
    juce::StringArray tags;
    std::vector<float> features;
};

struct AdaptiveClassifier::Impl {
    mutable std::mutex mutex;
    std::vector<Prototype> prototypes;
};

AdaptiveClassifier::AdaptiveClassifier()
    : impl(std::make_unique<Impl>())
{
}

AdaptiveClassifier::~AdaptiveClassifier() = default;

void AdaptiveClassifier::loadFromRepository(const SampleRepository& repo)
{
    auto all = repo.allFeedback();
    std::lock_guard lock(impl->mutex);
    impl->prototypes.clear();
    impl->prototypes.reserve(all.size());
    for (auto& r : all)
    {
        Prototype p;
        p.sampleId = r.sampleId;
        p.targetType = r.targetType;
        p.tags = r.tags;
        p.features = std::move(r.features);
        impl->prototypes.push_back(std::move(p));
    }
}

void AdaptiveClassifier::updatePrototype(int64_t sampleId, const juce::String& targetType,
                                         const juce::StringArray& tags, const std::vector<float>& features)
{
    std::lock_guard lock(impl->mutex);
    for (auto& p : impl->prototypes)
    {
        if (p.sampleId == sampleId)
        {
            p.targetType = targetType.trim();
            p.tags = tags;
            p.features = features;
            return;
        }
    }
    Prototype p;
    p.sampleId = sampleId;
    p.targetType = targetType.trim();
    p.tags = tags;
    p.features = features;
    impl->prototypes.push_back(std::move(p));
}

void AdaptiveClassifier::removePrototype(int64_t sampleId)
{
    std::lock_guard lock(impl->mutex);
    impl->prototypes.erase(
        std::remove_if(impl->prototypes.begin(), impl->prototypes.end(),
                       [sampleId](const Prototype& p) { return p.sampleId == sampleId; }),
        impl->prototypes.end());
}

void AdaptiveClassifier::clear()
{
    std::lock_guard lock(impl->mutex);
    impl->prototypes.clear();
}

bool AdaptiveClassifier::hasPrototypes() const
{
    std::lock_guard lock(impl->mutex);
    return ! impl->prototypes.empty();
}

size_t AdaptiveClassifier::prototypeCount() const
{
    std::lock_guard lock(impl->mutex);
    return impl->prototypes.size();
}

std::vector<AdaptiveClassifier::Prediction> AdaptiveClassifier::predictType(
    const std::vector<float>& features, int topK) const
{
    if (features.empty() || topK <= 0) return {};
    std::lock_guard lock(impl->mutex);
    if (impl->prototypes.empty()) return {};

    struct LabelAgg {
        juce::String display;
        float score = 0.0f;
    };
    std::map<juce::String, LabelAgg> typeBestScore;

    for (const auto& p : impl->prototypes)
    {
        if (p.targetType.isEmpty() || p.features.empty()) continue;
        const float sim = computeCosineSimilarity(features, p.features);
        if (sim <= 0.0f) continue;
        const auto key = p.targetType.toLowerCase();
        auto& agg = typeBestScore[key];
        if (agg.display.isEmpty()) agg.display = p.targetType;
        if (sim > agg.score) agg.score = sim;
    }

    std::vector<Prediction> results;
    results.reserve(typeBestScore.size());
    for (const auto& pair : typeBestScore)
    {
        results.push_back({ pair.second.display, pair.second.score });
    }

    std::sort(results.begin(), results.end(), [](const Prediction& a, const Prediction& b) {
        return a.score > b.score;
    });

    if (static_cast<size_t>(topK) < results.size())
        results.resize(static_cast<size_t>(topK));

    return results;
}

std::vector<AdaptiveClassifier::Prediction> AdaptiveClassifier::predictTags(
    const std::vector<float>& features, int topK) const
{
    if (features.empty() || topK <= 0) return {};
    std::lock_guard lock(impl->mutex);
    if (impl->prototypes.empty()) return {};

    struct LabelAgg {
        juce::String display;
        float score = 0.0f;
    };
    std::map<juce::String, LabelAgg> tagBestScore;

    for (const auto& p : impl->prototypes)
    {
        if (p.tags.isEmpty() || p.features.empty()) continue;
        const float sim = computeCosineSimilarity(features, p.features);
        if (sim <= 0.0f) continue;
        for (const auto& tag : p.tags)
        {
            const auto trimmed = tag.trim();
            if (trimmed.isEmpty()) continue;
            const auto key = trimmed.toLowerCase();
            auto& agg = tagBestScore[key];
            if (agg.display.isEmpty()) agg.display = trimmed;
            if (sim > agg.score) agg.score = sim;
        }
    }

    std::vector<Prediction> results;
    results.reserve(tagBestScore.size());
    for (const auto& pair : tagBestScore)
    {
        results.push_back({ pair.second.display, pair.second.score });
    }

    std::sort(results.begin(), results.end(), [](const Prediction& a, const Prediction& b) {
        return a.score > b.score;
    });

    if (static_cast<size_t>(topK) < results.size())
        results.resize(static_cast<size_t>(topK));

    return results;
}

std::vector<float> AdaptiveClassifier::extractFeatures(const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    if (buffer.getNumSamples() == 0 || sampleRate <= 0.0) return {};

    float rms = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        rms = std::max(rms, buffer.getRMSLevel(ch, 0, buffer.getNumSamples()));

    if (rms < 1e-4f) return {};

    const auto desc = AcousticFeatures::extract(buffer, sampleRate);
    const double duration = static_cast<double>(buffer.getNumSamples()) / sampleRate;

    // Normalize feature dimensions into comparable ranges around [0..1]
    const float normAttack = static_cast<float>(std::clamp((desc.logAttackTime + 4.0) / 4.0, 0.0, 1.0));
    const float normDecay = static_cast<float>(std::clamp(desc.decayRate / 500.0, 0.0, 1.0));
    const float normCrest = static_cast<float>(std::clamp(std::log1p(desc.crestFactor) / 4.0, 0.0, 1.0));
    const float normTemporal = static_cast<float>(std::clamp(desc.temporalCentroid, 0.0, 1.0));
    const float normZcr = static_cast<float>(std::clamp(desc.zeroCrossingRate / 10000.0, 0.0, 1.0));
    const float normFlux = static_cast<float>(std::clamp(desc.spectralFlux / 100.0, 0.0, 1.0));
    const float normClarity = static_cast<float>(std::clamp(desc.pitchClarity, 0.0, 1.0));
    const float normDuration = static_cast<float>(std::clamp(duration / 30.0, 0.0, 1.0));

    std::vector<float> vec;
    vec.reserve(14);
    vec.push_back(normAttack);
    vec.push_back(normDecay);
    vec.push_back(normCrest);
    vec.push_back(normTemporal);
    vec.push_back(static_cast<float>(desc.subBandEnergy[0]));
    vec.push_back(static_cast<float>(desc.subBandEnergy[1]));
    vec.push_back(static_cast<float>(desc.subBandEnergy[2]));
    vec.push_back(static_cast<float>(desc.subBandEnergy[3]));
    vec.push_back(static_cast<float>(desc.subBandEnergy[4]));
    vec.push_back(normZcr);
    vec.push_back(normFlux);
    vec.push_back(normClarity);
    vec.push_back(normDuration);
    vec.push_back(std::min(1.0f, rms * 2.0f));

    return vec;
}

} // namespace sonicator
