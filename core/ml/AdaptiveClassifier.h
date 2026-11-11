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
        juce::String source; // user_adaptive or global_adaptive
    };

    AdaptiveClassifier();
    ~AdaptiveClassifier();

    void loadFromRepository(const SampleRepository& repo);
    // Loads the prototypes that ship with the build. Only type labels are used for now: feeding
    // global tags to AutoTag would tag other people's libraries from background guesses. Returns
    // false and keeps the previous global set when the document is not a prototypes.v1 file of
    // this feature schema or holds a malformed vector.
    bool loadGlobalPrototypes(const juce::String& json);
    void updatePrototype(int64_t sampleId, const juce::String& targetType,
                         const juce::StringArray& tags, const std::vector<float>& features);
    // Refreshes only the tag list of an existing prototype, so an accepted AutoTag tag is learned
    // locally without replacing the type the person set.
    void updatePrototypeTags(int64_t sampleId, const juce::StringArray& tags);
    void removePrototype(int64_t sampleId);
    void clear();

    // Local prototypes only: AutoTag keeps its current trigger.
    bool hasPrototypes() const;
    // Local or global type prototypes: smart analysis may use either.
    bool hasTypePrototypes() const;
    // Local or global tag prototypes: AutoTag may propose either.
    bool hasTagPrototypes() const;
    bool hasGlobalPrototypes() const;
    int globalLabelCount() const;
    juce::String globalGeneratedAt() const;
    size_t prototypeCount() const;

    std::vector<Prediction> predictType(const std::vector<float>& features, int topK = 3) const;
    std::vector<Prediction> predictTags(const std::vector<float>& features, int topK = 5) const;

    static std::vector<float> extractFeatures(const juce::AudioBuffer<float>& buffer, double sampleRate);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace sonicator
