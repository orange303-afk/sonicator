#include "core/ml/GlobalLinearModel.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace sonicator;

namespace {

juce::String modelJson(const juce::String& overrides = {})
{
    juce::String json = R"({
  "schema": "model.v1",
  "featureSchema": 2,
  "generatedAt": "2026-11-12T00:00:00.000Z",
  "featureIndices": [0, 1],
  "mu": [0.0, 0.0],
  "sd": [1.0, 1.0],
  "model": {
    "kind": "M2",
    "labels": ["kick", "hat"],
    "W": [[1.0, 0.0], [0.0, 1.0]],
    "b": [0.0, 0.0],
    "temperature": 1.0
  },
  "similarityMap": [[0.0, 0.0], [1.0, 1.0]],
  "groups": {"kick": "percussion", "hat": "percussion"},
  "datasets": [{"id": "nsynth", "tier": "A", "clipsUsed": 5295}]
})";
    return overrides.isEmpty() ? json : overrides;
}

} // namespace

TEST_CASE("a linear model ranks labels and reports its metadata", "[ml][model]")
{
    GlobalLinearModel model;
    juce::String error;
    REQUIRE(model.load(modelJson(), error));
    REQUIRE(error.isEmpty());
    REQUIRE(model.loaded());
    REQUIRE(model.labelCount() == 2);
    REQUIRE(model.generatedAt() == "2026-11-12T00:00:00.000Z");

    std::vector<float> features(84, 0.0f);
    features[0] = 2.0f;
    const auto ranked = model.rank(features, 2);
    REQUIRE(ranked.size() == 2);
    REQUIRE(ranked[0].label == "kick");
    REQUIRE(ranked[0].score > ranked[1].score);
    REQUIRE(ranked[0].score == Catch::Approx(0.881).margin(0.002));

    std::vector<float> scores;
    model.predict(features, scores);
    REQUIRE(scores.size() == 2);
    REQUIRE(scores[0] > scores[1]);
}

TEST_CASE("a foreign schema or a malformed matrix is refused", "[ml][model]")
{
    GlobalLinearModel model;
    juce::String error;
    REQUIRE_FALSE(model.load(modelJson().replace("\"featureSchema\": 2", "\"featureSchema\": 1"), error));
    REQUIRE_FALSE(error.isEmpty());

    error.clear();
    REQUIRE_FALSE(model.load(modelJson().replace("\"W\": [[1.0, 0.0], [0.0, 1.0]]",
                                                 "\"W\": [[1.0], [0.0, 1.0]]"), error));
    REQUIRE_FALSE(error.isEmpty());

    error.clear();
    REQUIRE_FALSE(model.load("{ not json", error));
    REQUIRE_FALSE(model.loaded());
}

TEST_CASE("a zero vector and an unknown feature length stay finite", "[ml][model]")
{
    GlobalLinearModel model;
    juce::String error;
    REQUIRE(model.load(modelJson(), error));
    std::vector<float> zeros(84, 0.0f);
    const auto ranked = model.rank(zeros, 2);
    REQUIRE(ranked.size() == 2);
    for (const auto& entry : ranked) REQUIRE(std::isfinite(entry.score));

    std::vector<float> shortFeatures(2, 1.0f);
    REQUIRE_NOTHROW(model.rank(shortFeatures, 2));
}
