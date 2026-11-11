#include "AdaptiveClassifier.h"
#include "core/contribute/ContributionPayload.h"
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

bool isPlausibleAdaptiveTag(const juce::String& tag, const std::vector<float>& features)
{
    if (features.size() < 13) return true;
    const float normAttack = features[0];
    const float normDuration = features[12]; // duration / 30.0
    const auto lower = tag.toLowerCase();

    // A Pad must have sustained duration (> 0.75s) and slow attack
    if (lower == "pad" || lower == "pads" || lower == "atmos" || lower == "atmosphere" || lower == "drone" || lower == "ambient")
    {
        if (normDuration < 0.025f) return false;
        if (normAttack < 0.05f && normDuration < 0.05f) return false;
    }
    // A Loop must have sustained duration (> 1.2s)
    if (lower == "loop" || lower == "loops")
    {
        if (normDuration < 0.04f) return false;
    }
    return true;
}

// Global evidence is worth slightly less than a person's own confirmation.
constexpr float globalPenalty = 0.95f;
// A local label wins when the global best is not clearly better than it.
constexpr float localPriorityMargin = 0.02f;

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
    std::vector<Prototype> globalPrototypes;
    juce::String globalGeneratedAt;
    int globalLabelCount = 0;
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

bool AdaptiveClassifier::loadGlobalPrototypes(const juce::String& json)
{
    const auto parsed = juce::JSON::parse(json);
    auto* document = parsed.getDynamicObject();
    if (document == nullptr) return false;
    if (document->getProperty("schema").toString() != "prototypes.v1") return false;
    if ((int) document->getProperty("featureSchema") != contribute::featureSchemaVersion) return false;

    std::vector<Prototype> loaded;
    int64_t syntheticId = -1;
    int labelCount = 0;
    if (auto* labels = document->getProperty("labels").getArray())
    {
        for (const auto& labelValue : *labels)
        {
            auto* label = labelValue.getDynamicObject();
            if (label == nullptr) continue;
            ++labelCount;
            // Tags are kept in the file for a later stage, but never loaded here: AutoTag must
            // not start tagging from other people's libraries in this build.
            if (label->getProperty("kind").toString() != "type") continue;
            const auto name = label->getProperty("label").toString().trim();
            if (name.isEmpty()) return false;
            auto* vectors = label->getProperty("prototypes").getArray();
            if (vectors == nullptr) return false;
            for (const auto& vectorValue : *vectors)
            {
                auto* values = vectorValue.getArray();
                if (values == nullptr || values->size() != contribute::featureCount) return false;
                Prototype prototype;
                prototype.sampleId = syntheticId--;
                prototype.targetType = name;
                prototype.features.reserve(contribute::featureCount);
                for (const auto& value : *values)
                {
                    if (! value.isInt() && ! value.isDouble()) return false;
                    const auto number = (float) (double) value;
                    if (! std::isfinite(number) || number < 0.0f || number > 1.0f) return false;
                    prototype.features.push_back(number);
                }
                loaded.push_back(std::move(prototype));
            }
        }
    }

    std::lock_guard lock(impl->mutex);
    impl->globalPrototypes = std::move(loaded);
    impl->globalGeneratedAt = document->getProperty("generatedAt").toString();
    impl->globalLabelCount = labelCount;
    return true;
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
    impl->globalPrototypes.clear();
    impl->globalGeneratedAt.clear();
    impl->globalLabelCount = 0;
}

bool AdaptiveClassifier::hasPrototypes() const
{
    std::lock_guard lock(impl->mutex);
    return ! impl->prototypes.empty();
}

bool AdaptiveClassifier::hasTypePrototypes() const
{
    std::lock_guard lock(impl->mutex);
    const auto hasType = [](const std::vector<Prototype>& list)
    {
        return std::any_of(list.begin(), list.end(),
                           [](const Prototype& p) { return p.targetType.isNotEmpty(); });
    };
    return hasType(impl->prototypes) || hasType(impl->globalPrototypes);
}

bool AdaptiveClassifier::hasGlobalPrototypes() const
{
    std::lock_guard lock(impl->mutex);
    return ! impl->globalPrototypes.empty();
}

int AdaptiveClassifier::globalLabelCount() const
{
    std::lock_guard lock(impl->mutex);
    return impl->globalLabelCount;
}

juce::String AdaptiveClassifier::globalGeneratedAt() const
{
    std::lock_guard lock(impl->mutex);
    return impl->globalGeneratedAt;
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
    if (impl->prototypes.empty() && impl->globalPrototypes.empty()) return {};

    struct LabelAgg {
        juce::String display;
        float localScore = 0.0f;
        float globalScore = 0.0f;
        float score() const { return std::max(localScore, globalScore); }
        juce::String source() const { return localScore >= globalScore ? "user_adaptive" : "global_adaptive"; }
    };
    std::map<juce::String, LabelAgg> typeBestScore;

    const auto accumulate = [&](const std::vector<Prototype>& list, bool global)
    {
        for (const auto& p : list)
        {
            if (p.targetType.isEmpty() || p.features.empty()) continue;
            if (! isPlausibleAdaptiveTag(p.targetType, features)) continue;
            const float sim = computeCosineSimilarity(features, p.features);
            if (sim <= 0.0f) continue;
            const auto key = p.targetType.toLowerCase();
            auto& agg = typeBestScore[key];
            if (agg.display.isEmpty()) agg.display = p.targetType;
            if (global) agg.globalScore = std::max(agg.globalScore, sim * globalPenalty);
            else agg.localScore = std::max(agg.localScore, sim);
        }
    };
    accumulate(impl->prototypes, false);
    accumulate(impl->globalPrototypes, true);

    struct Candidate {
        juce::String display;
        float score = 0.0f;
        float localScore = 0.0f;
        juce::String source;
    };
    std::vector<Candidate> results;
    results.reserve(typeBestScore.size());
    for (const auto& [key, agg] : typeBestScore)
    {
        juce::ignoreUnused(key);
        results.push_back({ agg.display, agg.score(), agg.localScore, agg.source() });
    }

    std::sort(results.begin(), results.end(), [](const Candidate& a, const Candidate& b) {
        if (std::abs(a.score - b.score) > 1e-6f) return a.score > b.score;
        return a.display.compareIgnoreCase(b.display) < 0;
    });

    // A person's own confirmation wins over a global prototype that is not clearly better.
    if (! results.empty() && results.front().source == "global_adaptive")
    {
        size_t bestLocal = results.size();
        for (size_t i = 0; i < results.size(); ++i)
            if (results[i].localScore > 0.0f
                && (bestLocal == results.size() || results[i].localScore > results[bestLocal].localScore))
                bestLocal = i;
        if (bestLocal < results.size()
            && results.front().score - results[bestLocal].localScore <= localPriorityMargin + 1e-6f)
        {
            auto local = results[bestLocal];
            local.score = local.localScore;
            local.source = "user_adaptive";
            results.erase(results.begin() + (long) bestLocal);
            results.insert(results.begin(), std::move(local));
        }
    }

    std::vector<Prediction> out;
    out.reserve(results.size());
    for (const auto& candidate : results)
        out.push_back({ candidate.display, candidate.score, candidate.source });
    if (static_cast<size_t>(topK) < out.size())
        out.resize(static_cast<size_t>(topK));
    return out;
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

    // Local prototypes only: global tag prototypes ship in the file for a later stage but are
    // deliberately not loaded, so AutoTag cannot tag a library from other people's guesses.
    for (const auto& p : impl->prototypes)
    {
        if (p.tags.isEmpty() || p.features.empty()) continue;
        const float sim = computeCosineSimilarity(features, p.features);
        if (sim <= 0.0f) continue;
        for (const auto& tag : p.tags)
        {
            const auto trimmed = tag.trim();
            if (trimmed.isEmpty()) continue;
            if (! isPlausibleAdaptiveTag(trimmed, features)) continue;
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
