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

    struct ScoredType {
        juce::String type;
        float score = 0.0f;
    };
    std::map<juce::String, float> typeBestScore;

    for (const auto& p : impl->prototypes)
    {
        if (p.targetType.isEmpty() || p.features.empty()) continue;
        const float sim = computeCosineSimilarity(features, p.features);
        if (sim <= 0.0f) continue;
        auto& best = typeBestScore[p.targetType];
        if (sim > best) best = sim;
    }

    std::vector<Prediction> results;
    results.reserve(typeBestScore.size());
    for (const auto& pair : typeBestScore)
    {
        results.push_back({ pair.first, pair.second });
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

    std::map<juce::String, float> tagBestScore;

    for (const auto& p : impl->prototypes)
    {
        if (p.tags.isEmpty() || p.features.empty()) continue;
        const float sim = computeCosineSimilarity(features, p.features);
        if (sim <= 0.0f) continue;
        for (const auto& tag : p.tags)
        {
            if (tag.trim().isEmpty()) continue;
            auto& best = tagBestScore[tag];
            if (sim > best) best = sim;
        }
    }

    std::vector<Prediction> results;
    results.reserve(tagBestScore.size());
    for (const auto& pair : tagBestScore)
    {
        results.push_back({ pair.first, pair.second });
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

    const auto desc = AcousticFeatures::extract(buffer, sampleRate);
    const double duration = static_cast<double>(buffer.getNumSamples()) / sampleRate;

    float rms = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        rms = std::max(rms, buffer.getRMSLevel(ch, 0, buffer.getNumSamples()));

    std::vector<float> vec;
    vec.reserve(14);
    vec.push_back(static_cast<float>(desc.logAttackTime));
    vec.push_back(static_cast<float>(desc.decayRate));
    vec.push_back(static_cast<float>(desc.crestFactor));
    vec.push_back(static_cast<float>(desc.temporalCentroid));
    vec.push_back(static_cast<float>(desc.subBandEnergy[0]));
    vec.push_back(static_cast<float>(desc.subBandEnergy[1]));
    vec.push_back(static_cast<float>(desc.subBandEnergy[2]));
    vec.push_back(static_cast<float>(desc.subBandEnergy[3]));
    vec.push_back(static_cast<float>(desc.subBandEnergy[4]));
    vec.push_back(static_cast<float>(desc.zeroCrossingRate));
    vec.push_back(static_cast<float>(desc.spectralFlux));
    vec.push_back(static_cast<float>(desc.pitchClarity));
    vec.push_back(static_cast<float>(duration));
    vec.push_back(rms);

    return vec;
}

} // namespace sonicator
