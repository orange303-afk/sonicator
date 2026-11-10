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
