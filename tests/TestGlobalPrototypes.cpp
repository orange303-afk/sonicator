#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/ml/AdaptiveClassifier.h"
#include "core/smart/AcousticClassifierService.h"
#include "core/smart/SmartResolver.h"
#include "BinaryData.h"
#include <cmath>
#include <vector>

using namespace sonicator;

namespace {
juce::String prototypeDocument(const juce::String& label, const std::vector<float>& features,
                               const juce::String& schema = "prototypes.v1", int featureSchema = 1)
{
    juce::Array<juce::var> vector;
    for (const auto value : features) vector.add(value);
    juce::Array<juce::var> vectors;
    vectors.add(juce::var(vector));

    auto* entry = new juce::DynamicObject();
    entry->setProperty("label", label);
    entry->setProperty("kind", "type");
    entry->setProperty("support", 10);
    entry->setProperty("installSupport", 3);
    entry->setProperty("prototypes", vectors);

    juce::Array<juce::var> labels;
    labels.add(juce::var(entry));

    auto* document = new juce::DynamicObject();
    document->setProperty("schema", schema);
    document->setProperty("featureSchema", featureSchema);
    document->setProperty("generatedAt", "2026-11-11T00:00:00Z");
    document->setProperty("records", 10);
    document->setProperty("installs", 3);
    document->setProperty("labels", labels);
    return juce::JSON::toString(juce::var(document), false);
}

// A unit vector whose cosine with the axis (1,0,0,...) is exactly `cosine`.
std::vector<float> ray(double cosine)
{
    std::vector<float> out(14, 0.0f);
    out[0] = (float) cosine;
    out[1] = (float) std::sqrt(1.0 - cosine * cosine);
    return out;
}
std::vector<float> axis()
{
    std::vector<float> out(14, 0.0f);
    out[0] = 1.0f;
    return out;
}
} // namespace

TEST_CASE("The build carries a readable global prototype resource", "[contribute][training]")
{
    AdaptiveClassifier classifier;
    const auto json = juce::String::fromUTF8(BinaryData::global_prototypes_json,
                                             BinaryData::global_prototypes_jsonSize);
    CHECK(classifier.loadGlobalPrototypes(json));
    CHECK(classifier.globalGeneratedAt().isNotEmpty());
}

TEST_CASE("Global prototypes are loaded, but do not enable the AutoTag path", "[contribute][training]")
{
    AdaptiveClassifier classifier;
    REQUIRE(classifier.loadGlobalPrototypes(prototypeDocument("Snare", ray(0.99))));
    CHECK(classifier.hasGlobalPrototypes());
    CHECK(classifier.hasTypePrototypes());
    // hasPrototypes() is what AutoTag checks; a shipped file must not start background tagging.
    CHECK_FALSE(classifier.hasPrototypes());
    CHECK(classifier.prototypeCount() == 0);

    const auto predictions = classifier.predictType(axis(), 1);
    REQUIRE(predictions.size() == 1);
    CHECK(predictions[0].label == "Snare");
    CHECK(predictions[0].source == "global_adaptive");
    CHECK(predictions[0].score == Catch::Approx(0.99f * 0.95f).epsilon(0.001));
}

TEST_CASE("A broken or foreign prototype file is refused as a whole", "[contribute][training]")
{
    AdaptiveClassifier classifier;
    REQUIRE(classifier.loadGlobalPrototypes(prototypeDocument("Snare", ray(0.99))));

    CHECK_FALSE(classifier.loadGlobalPrototypes(prototypeDocument("Snare", ray(0.99), "other.v1")));
    CHECK_FALSE(classifier.loadGlobalPrototypes(prototypeDocument("Snare", ray(0.99), "prototypes.v1", 2)));
    CHECK_FALSE(classifier.loadGlobalPrototypes("not json"));
    CHECK_FALSE(classifier.loadGlobalPrototypes(prototypeDocument("Snare", std::vector<float>(13, 0.5f))));

    // The previous set survives every failed load.
    CHECK(classifier.hasGlobalPrototypes());
    REQUIRE_FALSE(classifier.predictType(axis(), 1).empty());
}

TEST_CASE("A local confirmation outranks a near-equal global prototype", "[contribute][training]")
{
    AdaptiveClassifier classifier;
    REQUIRE(classifier.loadGlobalPrototypes(prototypeDocument("Snare", ray(0.95))));
    classifier.updatePrototype(1, "Kick", {}, ray(0.90));

    const auto predictions = classifier.predictType(axis(), 1);
    REQUIRE(predictions.size() == 1);
    CHECK(predictions[0].label == "Kick");
    CHECK(predictions[0].source == "user_adaptive");
    CHECK(predictions[0].score == Catch::Approx(0.90f).epsilon(0.001));
}

TEST_CASE("A clearly better global prototype still wins", "[contribute][training]")
{
    AdaptiveClassifier classifier;
    REQUIRE(classifier.loadGlobalPrototypes(prototypeDocument("Snare", ray(0.99))));
    classifier.updatePrototype(1, "Kick", {}, ray(0.90));

    const auto predictions = classifier.predictType(axis(), 1);
    REQUIRE(predictions.size() == 1);
    CHECK(predictions[0].label == "Snare");
    CHECK(predictions[0].source == "global_adaptive");
}

TEST_CASE("Smart analysis reports where an adaptive type came from", "[contribute][training]")
{
    juce::AudioBuffer<float> buffer(1, 44100);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample(0, i, 0.5f * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * i / 44100.0));

    const auto features = AdaptiveClassifier::extractFeatures(buffer, 44100.0);
    REQUIRE(features.size() == 14);

    auto& shared = AcousticClassifierService::getSharedAdaptiveClassifier();
    shared.clear();
    shared.updatePrototype(1, "Zap", {}, features);

    const auto candidates = AcousticClassifierService::analyzeBuffer(buffer, 44100.0, {}, {});
    bool found = false;
    for (const auto& candidate : candidates)
    {
        if (candidate.field != SmartField::Type) continue;
        if (candidate.source == "user_adaptive")
        {
            found = true;
            CHECK(candidate.textValue == "Zap");
            CHECK(candidate.limitation.contains("user feedback"));
        }
    }
    CHECK(found);
    shared.clear();
}

TEST_CASE("A global type does not compete with an existing metadata type", "[contribute][training]")
{
    juce::AudioBuffer<float> buffer(1, 44100);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample(0, i, 0.5f * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * i / 44100.0));
    const auto features = AdaptiveClassifier::extractFeatures(buffer, 44100.0);
    REQUIRE(features.size() == 14);

    SmartCandidate filename;
    filename.field = SmartField::Type;
    filename.source = "filename";
    filename.textValue = "Kick";
    filename.status = CandidateStatus::Valid;

    auto& shared = AcousticClassifierService::getSharedAdaptiveClassifier();
    shared.clear();
    REQUIRE(shared.loadGlobalPrototypes(prototypeDocument("Zap", features)));
    for (const auto& candidate : AcousticClassifierService::analyzeBuffer(buffer, 44100.0, {}, { filename }))
        CHECK(candidate.source != "global_adaptive");

    // The person's own prototype is not a fallback, so it still competes with the name.
    shared.updatePrototype(1, "Zap", {}, features);
    bool found = false;
    for (const auto& candidate : AcousticClassifierService::analyzeBuffer(buffer, 44100.0, {}, { filename }))
        if (candidate.source == "user_adaptive") found = true;
    CHECK(found);
    shared.clear();
}

TEST_CASE("global_adaptive sits right below the user's own confirmation", "[contribute][training]")
{
    CHECK(smartSourcePriority("global_adaptive") == smartSourcePriority("user_adaptive") + 1);
    CHECK(smartSourcePriority("global_adaptive") < smartSourcePriority("dsp_acoustic"));
    CHECK(smartSourceGroup("global_adaptive") == "adaptive");
}
