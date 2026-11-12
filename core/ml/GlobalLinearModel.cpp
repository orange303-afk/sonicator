#include "GlobalLinearModel.h"
#include <cmath>
#include <mutex>

namespace sonicator {
namespace {

bool finiteNumber(const juce::var& value)
{
    if (! value.isDouble() && ! value.isInt() && ! value.isInt64()) return false;
    return std::isfinite(static_cast<double>(value));
}

bool readNumbers(const juce::var& value, std::vector<float>& out)
{
    auto* array = value.getArray();
    if (array == nullptr || array->isEmpty()) return false;
    out.clear();
    out.reserve(static_cast<size_t>(array->size()));
    for (const auto& entry : *array)
    {
        if (! finiteNumber(entry)) return false;
        out.push_back(static_cast<float>(static_cast<double>(entry)));
    }
    return true;
}

bool readSimilarity(const juce::var& value, std::vector<std::pair<float, float>>& out)
{
    auto* array = value.getArray();
    if (array == nullptr || array->size() < 2) return false;
    out.clear();
    float lastP = -1.0f, lastS = -1.0f;
    for (const auto& entry : *array)
    {
        auto* pair = entry.getArray();
        if (pair == nullptr || pair->size() != 2) return false;
        if (! finiteNumber((*pair)[0]) || ! finiteNumber((*pair)[1])) return false;
        const auto p = static_cast<float>(static_cast<double>((*pair)[0]));
        const auto s = static_cast<float>(static_cast<double>((*pair)[1]));
        if (p < 0.0f || p > 1.0f || s < 0.0f || s > 1.0f) return false;
        if (p < lastP - 1e-6f || s < lastS - 1e-6f) return false;
        lastP = p;
        lastS = s;
        out.emplace_back(p, s);
    }
    return true;
}

float mapSimilarity(const std::vector<std::pair<float, float>>& table, float probability)
{
    if (table.empty()) return probability;
    const auto p = std::min(1.0f, std::max(0.0f, probability));
    if (p <= table.front().first) return table.front().second;
    for (size_t i = 1; i < table.size(); ++i)
        if (p <= table[i].first)
        {
            const auto& left = table[i - 1];
            const auto& right = table[i];
            const auto span = right.first - left.first;
            if (span < 1e-9f) return right.second;
            const auto t = (p - left.first) / span;
            return left.second + (right.second - left.second) * t;
        }
    return table.back().second;
}

} // namespace

void GlobalLinearModel::clear()
{
    loaded_ = false;
    generatedAt_.clear();
    labels_.clear();
    indices_.clear();
    mu_.clear();
    sd_.clear();
    weights_.clear();
    bias_.clear();
    similarity_.clear();
    temperature_ = 1.0f;
}

bool GlobalLinearModel::load(const juce::String& json, juce::String& error)
{
    error.clear();
    const auto parsed = juce::JSON::parse(json);
    auto* document = parsed.getDynamicObject();
    if (document == nullptr) { error = "model is not a JSON object"; return false; }
    if (document->getProperty("schema").toString() != "model.v1") { error = "schema is not model.v1"; return false; }
    if (static_cast<int>(document->getProperty("featureSchema")) != 2)
    {
        error = "featureSchema is not 2";
        return false;
    }

    auto* model = document->getProperty("model").getDynamicObject();
    if (model == nullptr) { error = "model payload is missing"; return false; }
    if (model->getProperty("kind").toString() != "M2") { error = "model kind is not M2"; return false; }

    juce::StringArray labels;
    auto* labelArray = model->getProperty("labels").getArray();
    if (labelArray == nullptr || labelArray->isEmpty()) { error = "labels are missing"; return false; }
    for (const auto& entry : *labelArray)
    {
        const auto label = entry.toString().trim();
        if (label.isEmpty()) { error = "a label is empty"; return false; }
        labels.add(label.toLowerCase());
    }

    auto* indexArray = document->getProperty("featureIndices").getArray();
    if (indexArray == nullptr || indexArray->isEmpty()) { error = "featureIndices are missing"; return false; }
    std::vector<int> indices;
    for (const auto& entry : *indexArray)
    {
        if (! entry.isInt() && ! entry.isInt64()) { error = "featureIndices are not integers"; return false; }
        const auto index = static_cast<int>(entry);
        if (index < 0 || index >= 84) { error = "feature index is outside 0..83"; return false; }
        indices.push_back(index);
    }
    const auto dimensions = static_cast<int>(indices.size());

    std::vector<float> mu, sd;
    if (! readNumbers(document->getProperty("mu"), mu) || static_cast<int>(mu.size()) != dimensions)
    {
        error = "mu does not match featureIndices";
        return false;
    }
    if (! readNumbers(document->getProperty("sd"), sd) || static_cast<int>(sd.size()) != dimensions)
    {
        error = "sd does not match featureIndices";
        return false;
    }
    for (const auto value : sd)
        if (! (value >= 1e-6f)) { error = "sd is not positive"; return false; }

    auto* matrix = model->getProperty("W").getArray();
    if (matrix == nullptr || matrix->size() != labels.size()) { error = "W does not match labels"; return false; }
    std::vector<float> weights;
    weights.reserve(static_cast<size_t>(labels.size()) * static_cast<size_t>(dimensions));
    for (const auto& rowValue : *matrix)
    {
        std::vector<float> row;
        if (! readNumbers(rowValue, row) || static_cast<int>(row.size()) != dimensions)
        {
            error = "a W row does not match featureIndices";
            return false;
        }
        weights.insert(weights.end(), row.begin(), row.end());
    }

    std::vector<float> bias;
    if (! readNumbers(model->getProperty("b"), bias) || bias.size() != labels.size())
    {
        error = "b does not match labels";
        return false;
    }
    const auto temperature = static_cast<float>(static_cast<double>(model->getProperty("temperature")));
    if (! std::isfinite(temperature) || temperature <= 0.0f) { error = "temperature is not positive"; return false; }

    std::vector<std::pair<float, float>> similarity;
    if (! readSimilarity(document->getProperty("similarityMap"), similarity))
    {
        error = "similarityMap is missing or not monotone";
        return false;
    }

    labels_ = labels;
    indices_ = std::move(indices);
    mu_ = std::move(mu);
    sd_ = std::move(sd);
    weights_ = std::move(weights);
    bias_ = std::move(bias);
    temperature_ = temperature;
    similarity_ = std::move(similarity);
    generatedAt_ = document->getProperty("generatedAt").toString();
    loaded_ = true;
    return true;
}

void GlobalLinearModel::predict(const std::vector<float>& features, std::vector<float>& scores) const
{
    const auto labels = labels_.size();
    if (scores.size() != labels) scores.assign(labels, 0.0f);
    if (! loaded_ || labels == 0) return;

    thread_local std::vector<float> standardised;
    standardised.resize(indices_.size());
    for (size_t j = 0; j < indices_.size(); ++j)
    {
        const auto index = static_cast<size_t>(indices_[j]);
        const auto raw = index < features.size() ? features[index] : 0.0f;
        standardised[j] = (raw - mu_[j]) / sd_[j];
    }

    float maximum = -1.0e30f;
    for (size_t label = 0; label < labels; ++label)
    {
        float logit = bias_[label];
        const auto offset = label * indices_.size();
        for (size_t j = 0; j < indices_.size(); ++j) logit += weights_[offset + j] * standardised[j];
        logit /= temperature_;
        scores[label] = logit; // reuse as logits until softmax below
        maximum = std::max(maximum, logit);
    }
    float total = 0.0f;
    for (size_t label = 0; label < labels; ++label)
    {
        scores[label] = std::exp(scores[label] - maximum);
        total += scores[label];
    }
    if (! (total > 0.0f)) { scores.assign(labels, 0.0f); return; }
    for (size_t label = 0; label < labels; ++label)
        scores[label] = mapSimilarity(similarity_, scores[label] / total);
}

std::vector<GlobalLinearModel::Prediction> GlobalLinearModel::rank(const std::vector<float>& features,
                                                                   int topK) const
{
    std::vector<float> scores;
    predict(features, scores);
    std::vector<Prediction> ranked;
    ranked.reserve(labels_.size());
    for (size_t index = 0; index < labels_.size(); ++index)
        ranked.push_back({ labels_[static_cast<int>(index)], scores[index], static_cast<int>(index) });
    std::sort(ranked.begin(), ranked.end(), [](const Prediction& a, const Prediction& b)
    {
        if (a.score != b.score) return a.score > b.score;
        return a.label < b.label;
    });
    if (topK > 0 && static_cast<int>(ranked.size()) > topK) ranked.resize(static_cast<size_t>(topK));
    return ranked;
}

} // namespace sonicator
