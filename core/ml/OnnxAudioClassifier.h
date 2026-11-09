#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <vector>
#include <memory>

namespace sonicator {

struct MlPrediction {
    juce::String className;
    float probability = 0.0f;
};

class OnnxAudioClassifier {
public:
    OnnxAudioClassifier();
    ~OnnxAudioClassifier();

    bool loadModel(const juce::File& modelFile);
    bool loadModelFromMemory(const void* data, size_t size);
    bool isLoaded() const;

    // Returns topK predictions sorted by probability descending
    std::vector<MlPrediction> classify(const juce::AudioBuffer<float>& buffer, double sampleRate, int topK = 3);

    static juce::StringArray supportedClasses();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace sonicator
