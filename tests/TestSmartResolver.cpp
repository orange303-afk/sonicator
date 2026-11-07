#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/smart/SmartResolver.h"
#include "core/smart/KeyNames.h"
using namespace sonicator;
namespace {
SmartCandidate candidate(SmartField field, juce::String source, juce::String text,
                         double number = 0, CandidateStatus status = CandidateStatus::Valid)
{
    SmartCandidate c;
    c.field = field; c.source = std::move(source); c.textValue = std::move(text);
    c.numericValue = number; c.status = status;
    return c;
}
}
TEST_CASE("A single BPM candidate resolves", "[smart][resolver]")
{
    auto result = resolveSmart({ candidate(SmartField::Bpm, "filename", {}, 128.0) });
    REQUIRE(result.hasBpm);
    REQUIRE(result.bpm == Catch::Approx(128.0));
    REQUIRE(result.bpmState == SmartState::Resolved);
    REQUIRE(result.bpmSource == "filename");
}
TEST_CASE("BPM sources within one BPM agree, wider disagreement is a conflict", "[smart][resolver]")
{
    auto agree = resolveSmart({ candidate(SmartField::Bpm, "wav_acid", {}, 128.0),
                                candidate(SmartField::Bpm, "filename", {}, 128.4) });
    REQUIRE(agree.bpmState == SmartState::Resolved);
    REQUIRE(agree.bpm == Catch::Approx(128.0));
    REQUIRE(agree.explanation.contains("wav_acid=128"));
    auto conflict = resolveSmart({ candidate(SmartField::Bpm, "wav_acid", {}, 120.0),
                                   candidate(SmartField::Bpm, "filename", {}, 128.0) });
    REQUIRE(conflict.bpmState == SmartState::Conflicting);
    REQUIRE(conflict.bpm == Catch::Approx(120.0)); // higher-priority source is shown
    REQUIRE(conflict.explanation.contains("conflict"));
}
TEST_CASE("Out-of-range BPM is never used", "[smart][resolver]")
{
    auto result = resolveSmart({ candidate(SmartField::Bpm, "filename", {}, 900.0),
                                 candidate(SmartField::Bpm, "wav_acid", {}, 0.0) });
    REQUIRE_FALSE(result.hasBpm);
    REQUIRE(result.bpmState == SmartState::Unknown);
}
TEST_CASE("A sampler root note never becomes a musical key", "[smart][resolver]")
{
    auto result = resolveSmart({ candidate(SmartField::RootNote, "wav_smpl", {}, 60.0) });
    REQUIRE(result.rootNote == 60);
    REQUIRE(result.keyTonic == -1);
    REQUIRE(result.keyState == SmartState::Unknown);
}
TEST_CASE("A default sampler root is marked default, not resolved", "[smart][resolver]")
{
    auto result = resolveSmart({ candidate(SmartField::RootNote, "wav_smpl", {}, 60.0,
                                           CandidateStatus::DefaultValue) });
    REQUIRE(result.rootNote == 60);
    REQUIRE(result.rootState == SmartState::Default);
}
TEST_CASE("Keys resolve and conflicting keys stay visible", "[smart][resolver]")
{
    auto one = resolveSmart({ candidate(SmartField::Key, "filename", "C# minor") });
    REQUIRE(one.keyTonic == 1);
    REQUIRE(one.keyMode == "minor");
    REQUIRE(one.keyState == SmartState::Resolved);
    auto conflict = resolveSmart({ candidate(SmartField::Key, "filename", "C major"),
                                   candidate(SmartField::Key, "id3", "D minor") });
    REQUIRE(conflict.keyState == SmartState::Conflicting);
    REQUIRE(conflict.keyTonic == 2); // id3 has the stronger priority
    REQUIRE(conflict.explanation.contains("conflict"));
}
TEST_CASE("Overrides win and can express explicit unknown", "[smart][resolver]")
{
    SmartOverrideMap overrides;
    overrides[SmartField::Bpm] = { false, {}, 90.0 };
    overrides[SmartField::Key] = { false, "A minor", 0.0 };
    auto overridden = resolveSmart({ candidate(SmartField::Bpm, "filename", {}, 128.0),
                                     candidate(SmartField::Key, "filename", "C major") }, overrides);
    REQUIRE(overridden.bpm == Catch::Approx(90.0));
    REQUIRE(overridden.bpmState == SmartState::Overridden);
    REQUIRE(overridden.keyTonic == 9);
    REQUIRE(overridden.keyState == SmartState::Overridden);
    SmartOverrideMap cleared;
    cleared[SmartField::Bpm] = { true, {}, 0.0 };
    auto unknown = resolveSmart({ candidate(SmartField::Bpm, "filename", {}, 128.0) }, cleared);
    REQUIRE_FALSE(unknown.hasBpm);
    REQUIRE(unknown.bpmState == SmartState::Unknown);
    REQUIRE(unknown.explanation.contains("override"));
}
TEST_CASE("Type ambiguity is preserved", "[smart][resolver]")
{
    auto result = resolveSmart({ candidate(SmartField::Type, "filename", "kick"),
                                 candidate(SmartField::Type, "folder", "percussion") });
    REQUIRE(result.typeState == SmartState::Ambiguous);
    REQUIRE(result.type == "kick");
    REQUIRE(result.explanation.contains("ambiguous"));
}
TEST_CASE("Key tokens parse conservatively", "[smart][keynames]")
{
    REQUIRE(parseKeyToken("C#min")->canonical == "C# minor");
    REQUIRE(parseKeyToken("BbMaj")->canonical == "Bb major");
    REQUIRE(parseKeyToken("key_Emin")->tonic == 4);
    REQUIRE(parseKeyToken("F#m")->mode == "minor");
    REQUIRE_FALSE(parseKeyToken("Cataract").has_value());
    REQUIRE_FALSE(parseKeyToken("Amen").has_value());
    REQUIRE_FALSE(parseKeyToken("Windows_95").has_value());
}
