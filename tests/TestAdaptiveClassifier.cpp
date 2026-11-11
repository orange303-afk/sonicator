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

