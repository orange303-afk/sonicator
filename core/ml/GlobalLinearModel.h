#pragma once
#include <juce_core/juce_core.h>
#include <utility>
#include <vector>

namespace sonicator {

// The schema-2 softmax layer of the shipped model (model.v1, kind M2). It standardises the 84-value
// vector with the model's own mu/sd, multiplies by the label matrix, applies the temperature and maps
// the probability through the similarity table, so its score means the same thing as a prototype
// cosine for the app's acceptance thresholds.
class GlobalLinearModel
{
public:
    struct Prediction
    {
        juce::String label;
        float score = 0.0f;
        int index = -1;
    };

    // Parses and validates a model document. On failure the previous model is kept and `error`
    // explains why. Rejects a foreign schema, a foreign feature schema, empty labels, ragged
    // matrices, non-finite numbers, a bad temperature and a non-monotone similarity table.
    bool load(const juce::String& json, juce::String& error);
    void clear();

    bool loaded() const { return loaded_; }
    int labelCount() const { return static_cast<int>(labels_.size()); }
    int featureCount() const { return static_cast<int>(indices_.size()); }
    juce::String generatedAt() const { return generatedAt_; }
    juce::StringArray labels() const { return labels_; }

    // Scores in model label order. `scores` is resized only when its size differs, so a caller can
    // reuse the vector in the hot path.
    void predict(const std::vector<float>& features, std::vector<float>& scores) const;

    std::vector<Prediction> rank(const std::vector<float>& features, int topK = 3) const;

private:
    bool loaded_ = false;
    juce::String generatedAt_;
    juce::StringArray labels_;
    std::vector<int> indices_;
    std::vector<float> mu_;
    std::vector<float> sd_;
    std::vector<float> weights_; // labels x dims, row-major
    std::vector<float> bias_;
    float temperature_ = 1.0f;
    std::vector<std::pair<float, float>> similarity_;
};

} // namespace sonicator
