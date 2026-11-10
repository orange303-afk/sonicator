#pragma once
#include "core/database/SampleRepository.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <vector>
#include <memory>

namespace sonicator {

class AdaptiveClassifier {
public:
    struct Prediction {
        juce::String label;
        float score = 0.0f;
    };

    AdaptiveClassifier();
    ~AdaptiveClassifier();

    void loadFromRepository(const SampleRepository& repo);
    void updatePrototype(int64_t sampleId, const juce::String& targetType,
                         const juce::StringArray& tags, const std::vector<float>& features);
    void removePrototype(int64_t sampleId);
    void clear();

    bool hasPrototypes() const;
    size_t prototypeCount() const;

    std::vector<Prediction> predictType(const std::vector<float>& features, int topK = 3) const;
    std::vector<Prediction> predictTags(const std::vector<float>& features, int topK = 5) const;

    static std::vector<float> extractFeatures(const juce::AudioBuffer<float>& buffer, double sampleRate);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace sonicator
