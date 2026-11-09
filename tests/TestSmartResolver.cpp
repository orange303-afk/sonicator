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
    SmartOverride bpm; bpm.numericValue = 90.0;
    overrides[SmartField::Bpm] = bpm;
    SmartOverride key; key.textValue = "A minor";
    overrides[SmartField::Key] = key;
    auto overridden = resolveSmart({ candidate(SmartField::Bpm, "filename", {}, 128.0),
                                     candidate(SmartField::Key, "filename", "C major") }, overrides);
    REQUIRE(overridden.bpm == Catch::Approx(90.0));
    REQUIRE(overridden.bpmOverride == OverrideState::Value);
    // The automated analysis keeps its own state; the override is a separate attribute.
    REQUIRE(overridden.bpmState == SmartState::Resolved);
    REQUIRE(overridden.keyTonic == 9);
    REQUIRE(overridden.keyOverride == OverrideState::Value);
    REQUIRE(overridden.keyState == SmartState::Resolved);
    SmartOverrideMap cleared;
    SmartOverride unknownBpm; unknownBpm.cleared = true;
    cleared[SmartField::Bpm] = unknownBpm;
    auto unknown = resolveSmart({ candidate(SmartField::Bpm, "filename", {}, 128.0) }, cleared);
    REQUIRE_FALSE(unknown.hasBpm);
    REQUIRE(unknown.bpmOverride == OverrideState::Unknown);
    REQUIRE(unknown.bpmState == SmartState::Resolved);
    REQUIRE(unknown.explanation.contains("override"));
    SmartOverrideMap notApplicable;
    SmartOverride na; na.cleared = true; na.notApplicable = true;
    notApplicable[SmartField::Bpm] = na;
    auto naResult = resolveSmart({ candidate(SmartField::Bpm, "filename", {}, 128.0) }, notApplicable);
    REQUIRE(naResult.bpmOverride == OverrideState::NotApplicable);
    REQUIRE_FALSE(naResult.hasBpm);
}
TEST_CASE("Type ambiguity is preserved", "[smart][resolver]")
{
    auto result = resolveSmart({ candidate(SmartField::Type, "filename", "kick"),
                                 candidate(SmartField::Type, "folder", "percussion") });
    REQUIRE(result.typeState == SmartState::Ambiguous);
    REQUIRE(result.type == "kick");
    REQUIRE(result.explanation.contains("differ"));
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
TEST_CASE("Half and double BPM are an ambiguity, not a conflict", "[smart][resolver]")
{
    auto result = resolveSmart({ candidate(SmartField::Bpm, "wav_acid", {}, 60.0),
                                 candidate(SmartField::Bpm, "filename", {}, 120.0) });
    REQUIRE(result.bpmState == SmartState::Ambiguous);
    REQUIRE(result.bpm == Catch::Approx(60.0));
    REQUIRE(result.explanation.contains("half/double"));
}
TEST_CASE("Variable tempo is not reduced to one number", "[smart][resolver]")
{
    auto variable = candidate(SmartField::Bpm, "asd_warp", {}, 120.0);
    variable.variableTempo = true;
    auto result = resolveSmart({ variable });
    REQUIRE_FALSE(result.hasBpm);
    REQUIRE(result.bpmState == SmartState::Ambiguous);
    REQUIRE(result.explanation.contains("variable"));
}
TEST_CASE("Enharmonic keys agree and relative keys stay ambiguous", "[smart][resolver]")
{
    auto enharmonic = resolveSmart({ candidate(SmartField::Key, "filename", "C# minor"),
                                     candidate(SmartField::Key, "id3", "Db minor") });
    REQUIRE(enharmonic.keyState == SmartState::Resolved);
    REQUIRE(enharmonic.keyTonic == 1);
    auto relative = resolveSmart({ candidate(SmartField::Key, "filename", "C major"),
                                   candidate(SmartField::Key, "id3", "A minor") });
    REQUIRE(relative.keyState == SmartState::Ambiguous);
    REQUIRE(relative.explanation.contains("relative"));
}
TEST_CASE("Pitch is stored apart from musical key", "[smart][resolver]")
{
    auto result = resolveSmart({ candidate(SmartField::Pitch, "dsp", "A", 69.0) });
    REQUIRE(result.hasPitch);
    REQUIRE(result.pitchMidi == 69);
    REQUIRE(result.pitchState == SmartState::Resolved);
    REQUIRE(result.keyTonic == -1);
    REQUIRE(result.keyState == SmartState::Unknown);
    REQUIRE(result.rootNote == -1);
}
TEST_CASE("A tonic without a mode never gains an invented mode", "[smart][resolver]")
{
    auto result = resolveSmart({ candidate(SmartField::Key, "filename", "A") });
    REQUIRE(result.keyTonic == 9);
    REQUIRE(result.keyMode.isEmpty());
}

TEST_CASE("An ambiguous DSP tempo stays ambiguous instead of becoming a number", "[smart][resolver][dsp]")
{
    SmartCandidate dsp;
    dsp.field = SmartField::Bpm; dsp.source = "dsp";
    dsp.numericValue = 128.0; dsp.auxValue = 0.4; dsp.status = CandidateStatus::Invalid;
    dsp.limitation = "tempo estimated from audio; alternatives: 64.0, 256.0";
    auto result = resolveSmart({ dsp });
    REQUIRE_FALSE(result.hasBpm);
    REQUIRE(result.bpmState == SmartState::Ambiguous);
    REQUIRE(result.explanation.contains("ambiguous onset grid"));
}
TEST_CASE("A DSP tempo carries its heuristic confidence", "[smart][resolver][dsp]")
{
    auto dsp = candidate(SmartField::Bpm, "dsp", {}, 128.0);
    dsp.auxValue = 0.72;
    auto result = resolveSmart({ dsp });
    REQUIRE(result.hasBpm);
    REQUIRE(result.bpm == Catch::Approx(128.0));
    REQUIRE(result.bpmState == SmartState::Resolved);
    REQUIRE(result.explanation.contains("confidence 0.72"));
}
TEST_CASE("DSP structure competes with name structure honestly", "[smart][resolver][dsp]")
{
    auto result = resolveSmart({ candidate(SmartField::Structure, "dsp", "loop"),
                                 candidate(SmartField::Structure, "filename", "one-shot") });
    REQUIRE(result.structure == "one-shot"); // the name has the stronger source priority
    REQUIRE(result.structureState == SmartState::Ambiguous);
}
TEST_CASE("An ambiguous DSP tempo yields to a valid metadata tempo", "[smart][resolver][dsp]")
{
    SmartCandidate dsp;
    dsp.field = SmartField::Bpm; dsp.source = "dsp";
    dsp.numericValue = 100.0; dsp.status = CandidateStatus::Invalid;
    auto result = resolveSmart({ candidate(SmartField::Bpm, "wav_acid", {}, 128.0), dsp });
    REQUIRE(result.hasBpm);
    REQUIRE(result.bpm == Catch::Approx(128.0));
    REQUIRE(result.bpmState == SmartState::Resolved);
}
