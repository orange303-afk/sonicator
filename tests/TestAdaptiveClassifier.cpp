#include "core/ml/AdaptiveClassifier.h"
#include <catch2/catch_test_macros.hpp>

using namespace sonicator;

TEST_CASE("AdaptiveClassifier prototype learning and dual-prediction", "[ml][adaptive]")
{
    AdaptiveClassifier classifier;
    std::vector<float> kickFeatures = {100.0f, 0.2f, 0.1f, 0.8f, 0.1f, 0.05f, 0.02f, 0.08f, 0.5f, 0.1f, 0.0f, 0.0f, 0.0f, 0.25f};
    std::vector<float> hatFeatures  = {8000.0f, 0.9f, 0.8f, 0.3f, 0.01f, 0.4f, 0.01f, 0.02f, 0.1f, 0.8f, 0.0f, 0.0f, 0.0f, 0.05f};

    classifier.updatePrototype(1, "Kick", {"drum", "low"}, kickFeatures);
    classifier.updatePrototype(2, "Hat", {"cymbal", "high"}, hatFeatures);

    auto typePreds = classifier.predictType(kickFeatures, 1);
    REQUIRE_FALSE(typePreds.empty());
    CHECK(typePreds[0].label == "Kick");
    CHECK(typePreds[0].score > 0.9f);

    auto tagPreds = classifier.predictTags(hatFeatures, 2);
    REQUIRE_FALSE(tagPreds.empty());
    CHECK(tagPreds[0].label == "cymbal");
}

TEST_CASE("AdaptiveClassifier silence extraction produces empty features", "[ml][adaptive]")
{
    juce::AudioBuffer<float> silent(2, 44100);
    silent.clear();
    auto features = AdaptiveClassifier::extractFeatures(silent, 44100.0);
    CHECK(features.empty());
}

TEST_CASE("Accepted AutoTag tags refresh tags without replacing the type", "[ml][adaptive]")
{
    AdaptiveClassifier classifier;
    const std::vector<float> features = { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f };
    classifier.updatePrototype(1, "Kick", { "old" }, features);
    classifier.updatePrototypeTags(1, { "Punchy", "Kick" });

    const auto types = classifier.predictType(features, 1);
    REQUIRE(types.size() == 1);
    CHECK(types[0].label == "Kick");

    const auto tags = classifier.predictTags(features, 3);
    REQUIRE_FALSE(tags.empty());
    CHECK(tags[0].label.equalsIgnoreCase("Kick"));
    bool punchy = false;
    for (const auto& tag : tags) if (tag.label.equalsIgnoreCase("Punchy")) punchy = true;
    CHECK(punchy);
}

TEST_CASE("AdaptiveClassifier case-insensitive label aggregation", "[ml][adaptive]")
{
    AdaptiveClassifier classifier;
    std::vector<float> f1 = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f};
    std::vector<float> f2 = {0.11f, 0.21f, 0.31f, 0.41f, 0.51f, 0.61f, 0.71f, 0.81f, 0.91f, 0.11f, 0.21f, 0.31f, 0.41f, 0.51f};

    classifier.updatePrototype(1, "Synth", {"analog"}, f1);
    classifier.updatePrototype(2, "synth", {"Analog"}, f2);

    auto typePreds = classifier.predictType(f1, 5);
    // Should aggregate into a single canonical class rather than returning two duplicates
    CHECK(typePreds.size() == 1);
    CHECK(typePreds[0].label.equalsIgnoreCase("Synth"));

    auto tagPreds = classifier.predictTags(f1, 5);
    CHECK(tagPreds.size() == 1);
    CHECK(tagPreds[0].label.equalsIgnoreCase("analog"));
}


TEST_CASE("schema-2 prototypes answer only schema-2 queries", "[ml][adaptive]")
{
    AdaptiveClassifier classifier;
    std::vector<float> v1(14, 0.5f);
    std::vector<float> v2(84, 0.5f);
    v2[0] = 0.9f;
    classifier.updatePrototype(7, "Kick", { "Kick" }, v1, v2);
    REQUIRE(classifier.hasV2TypePrototypes());

    const auto v2Preds = classifier.predictTypeV2(v2, 1);
    REQUIRE_FALSE(v2Preds.empty());
    CHECK(v2Preds[0].label == "Kick");
    CHECK(v2Preds[0].source == "user_adaptive");
    CHECK(classifier.predictType(v1, 1).empty());
}

TEST_CASE("the global model serves schema-2 queries as global_model", "[ml][adaptive]")
{
    AdaptiveClassifier classifier;
    const juce::String model = R"({
      "schema": "model.v1", "featureSchema": 2, "generatedAt": "2026-11-12T00:00:00.000Z",
      "featureIndices": [0, 1], "mu": [0.0, 0.0], "sd": [1.0, 1.0],
      "model": { "kind": "M2", "labels": ["kick", "hat"], "W": [[1.0, 0.0], [0.0, 1.0]],
                 "b": [0.0, 0.0], "temperature": 1.0 },
      "similarityMap": [[0.0, 0.0], [1.0, 1.0]],
      "groups": { "kick": "percussion", "hat": "percussion" },
      "datasets": []
    })";
    REQUIRE(classifier.loadGlobalModel(model));
    REQUIRE(classifier.hasGlobalModel());
    REQUIRE(classifier.globalLabelCount() == 2);

    std::vector<float> v2(84, 0.0f);
    v2[0] = 2.0f;
    const auto preds = classifier.predictTypeV2(v2, 2);
    REQUIRE_FALSE(preds.empty());
    CHECK(preds[0].label == "kick");
    CHECK(preds[0].source == "global_model");

    std::vector<float> v1(14, 0.0f);
    CHECK(classifier.predictType(v1, 1).empty());
}
