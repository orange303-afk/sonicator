#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/match/MatchPolicy.h"
#include "core/match/SourceMetadata.h"
#include "core/smart/SmartTypes.h"

using namespace sonicator;
using sonicator::plugin::HostTempoValue;

namespace
{
MatchSource loop (double bpm = 120.0, double frames = 352800.0, double rate = 44100.0)
{
    MatchSource source;
    source.identity = "catalog:1:/Music/bass.wav";
    source.sourceFrames = (int64_t) frames;
    source.sampleRate = rate;
    source.channels = 2;
    source.structure = MatchStructure::loop;
    source.bpm = bpm;
    source.bpmState = MetadataState::known;
    return source;
}

HostTempoValue host (double bpm = 128.0)
{
    return { true, bpm, 1 };
}

PluginMatchPreferences bpmOnly()
{
    PluginMatchPreferences prefs;
    prefs.matchBpm = true;
    return prefs;
}

PluginMatchPreferences scaleOnly (int tonic = 0, TargetMode mode = TargetMode::minor)
{
    PluginMatchPreferences prefs;
    prefs.matchScale = true;
    prefs.targetTonic = tonic;
    prefs.targetMode = mode;
    return prefs;
}

void setKey (MatchSource& source, int tonic, const juce::String& mode)
{
    source.keyTonic = tonic;
    source.keyMode = mode;
    source.keyState = MetadataState::known;
}
} // namespace

TEST_CASE ("Match BPM shortens an eight second loop from 120 to 128", "[plugin-match][policy]")
{
    auto source = loop();
    const auto plan = resolveMatch (source, bpmOnly(), host (128.0));
    REQUIRE (plan.action == MatchAction::transform);
    CHECK (plan.tempoApplied);
    CHECK_FALSE (plan.pitchApplied);
    CHECK (plan.timeRatio == Catch::Approx (120.0 / 128.0));
    CHECK (plan.targetFrames == 330750);
    CHECK (plan.semitones == 0.0);
}

TEST_CASE ("Both off is a bypass even with unknown source metadata", "[plugin-match][policy]")
{
    MatchSource source;
    source.structure = MatchStructure::unsupported;
    const auto plan = resolveMatch (source, {}, HostTempoValue{});
    CHECK (plan.action == MatchAction::bypass);
    CHECK_FALSE (plan.tempoApplied);
    CHECK_FALSE (plan.pitchApplied);
}

TEST_CASE ("Match Scale transposes by the shortest interval and keeps the length",
           "[plugin-match][policy]")
{
    auto source = loop();
    setKey (source, 9, "minor"); // A minor
    auto plan = resolveMatch (source, scaleOnly (0), host());
    REQUIRE (plan.action == MatchAction::transform);
    CHECK_FALSE (plan.tempoApplied);
    CHECK (plan.pitchApplied);
    CHECK (plan.semitones == Catch::Approx (3.0));
    CHECK (plan.targetFrames == source.sourceFrames);

    setKey (source, 0, "minor"); // C minor -> A minor
    plan = resolveMatch (source, scaleOnly (9), host());
    CHECK (plan.semitones == Catch::Approx (-3.0));

    setKey (source, 0, "minor"); // C -> F#: tritone is deterministically +6
    plan = resolveMatch (source, scaleOnly (6), host());
    CHECK (plan.semitones == Catch::Approx (6.0));
}

TEST_CASE ("Match Scale refuses to convert a mode", "[plugin-match][policy]")
{
    auto source = loop();
    setKey (source, 9, "minor");
    const auto plan = resolveMatch (source, scaleOnly (0, TargetMode::major), host());
    CHECK (plan.action == MatchAction::blocked);
    CHECK (plan.reason == MatchBlockReason::modeMismatch);
}

TEST_CASE ("Match Scale without a usable key or root is blocked", "[plugin-match][policy]")
{
    auto source = loop();
    const auto plan = resolveMatch (source, scaleOnly (0), host());
    CHECK (plan.action == MatchAction::blocked);
    CHECK (plan.reason == MatchBlockReason::sourcePitchUnknown);

    source.pitchState = MetadataState::stale;
    source.hasPitch = true;
    source.pitchMidi = 69;
    CHECK (resolveMatch (source, scaleOnly (0), host()).action == MatchAction::blocked);
}

TEST_CASE ("A tonal one-shot can be tuned to the target tonic", "[plugin-match][policy]")
{
    MatchSource source;
    source.identity = "catalog:2:/Music/pad.wav";
    source.sourceFrames = 44100;
    source.sampleRate = 44100;
    source.channels = 1;
    source.structure = MatchStructure::oneShot;
    source.pitchMidi = 69; // A
    source.pitchState = MetadataState::known;
    source.rootNote = 69;
    source.rootState = MetadataState::known;

    const auto plan = resolveMatch (source, scaleOnly (0), host());
    REQUIRE (plan.action == MatchAction::transform);
    CHECK (plan.pitchApplied);
    CHECK (plan.semitones == Catch::Approx (3.0));
}

TEST_CASE ("Confirmed one-shot never stretches and never needs the host tempo",
           "[plugin-match][policy]")
{
    MatchSource source;
    source.identity = "catalog:3:/Music/hit.wav";
    source.sourceFrames = 22050;
    source.sampleRate = 44100;
    source.channels = 2;
    source.structure = MatchStructure::oneShot;

    auto plan = resolveMatch (source, bpmOnly(), HostTempoValue{});
    CHECK (plan.action == MatchAction::bypass);
    CHECK (plan.reason == MatchBlockReason::bpmNotApplicable);
    CHECK_FALSE (plan.tempoApplied);

    // BPM is not applicable, but a valid pitch still gives a pitch-only result without a tempo.
    source.pitchMidi = 69;
    source.hasPitch = true;
    source.pitchState = MetadataState::known;
    plan = resolveMatch (source, [&]
    {
        auto prefs = scaleOnly (0);
        prefs.matchBpm = true;
        return prefs;
    }(), HostTempoValue{});
    REQUIRE (plan.action == MatchAction::transform);
    CHECK (plan.pitchApplied);
    CHECK_FALSE (plan.tempoApplied);
    CHECK (plan.targetFrames == source.sourceFrames);
}

TEST_CASE ("Unknown structure is not guessed into a loop", "[plugin-match][policy]")
{
    auto source = loop();
    source.structure = MatchStructure::unknown;
    auto plan = resolveMatch (source, bpmOnly(), host());
    CHECK (plan.action == MatchAction::bypass);
    CHECK (plan.reason == MatchBlockReason::bpmStructureUnknown);

    auto both = bpmOnly();
    both.matchScale = true;
    both.targetTonic = 0;
    source.keyTonic = 9;
    source.keyMode = "minor";
    source.keyState = MetadataState::known;
    plan = resolveMatch (source, both, host());
    REQUIRE (plan.action == MatchAction::transform);
    CHECK (plan.pitchApplied);
    CHECK_FALSE (plan.tempoApplied);
}

TEST_CASE ("A loop with Match BPM needs a valid host tempo", "[plugin-match][policy]")
{
    auto source = loop();
    auto plan = resolveMatch (source, bpmOnly(), HostTempoValue{});
    CHECK (plan.action == MatchAction::blocked);
    CHECK (plan.reason == MatchBlockReason::hostTempoUnavailable);

    plan = resolveMatch (source, bpmOnly(), HostTempoValue{ true, 0.0, 1 });
    CHECK (plan.action == MatchAction::blocked);
    plan = resolveMatch (source, bpmOnly(), HostTempoValue{ true, std::nan (""), 1 });
    CHECK (plan.action == MatchAction::blocked);
}

TEST_CASE ("Source BPM must be known and trustworthy", "[plugin-match][policy]")
{
    auto source = loop();
    source.bpmState = MetadataState::unknown;
    CHECK (resolveMatch (source, bpmOnly(), host()).reason == MatchBlockReason::sourceBpmUnknown);
    source.bpmState = MetadataState::stale;
    CHECK (resolveMatch (source, bpmOnly(), host()).reason == MatchBlockReason::sourceBpmUnknown);
    source.bpmState = MetadataState::conflicting;
    CHECK (resolveMatch (source, bpmOnly(), host()).reason == MatchBlockReason::sourceBpmUnknown);
    source.bpmState = MetadataState::known;
    source.bpm = std::nan ("");
    CHECK (resolveMatch (source, bpmOnly(), host()).reason == MatchBlockReason::sourceBpmUnknown);
}

TEST_CASE ("The time ratio is bounded instead of silently clamped", "[plugin-match][policy]")
{
    auto source = loop (60.0);
    auto plan = resolveMatch (source, bpmOnly(), host (120.0));
    REQUIRE (plan.action == MatchAction::transform);
    CHECK (plan.timeRatio == Catch::Approx (0.5));

    source.bpm = 240.0;
    plan = resolveMatch (source, bpmOnly(), host (120.0));
    REQUIRE (plan.action == MatchAction::transform);
    CHECK (plan.timeRatio == Catch::Approx (2.0));

    source.bpm = 300.0;
    CHECK (resolveMatch (source, bpmOnly(), host (120.0)).reason == MatchBlockReason::ratioOutOfRange);
    source.bpm = 30.0;
    CHECK (resolveMatch (source, bpmOnly(), host (120.0)).reason == MatchBlockReason::ratioOutOfRange);
}

TEST_CASE ("Invalid source length and overflow are blocked", "[plugin-match][policy]")
{
    auto source = loop();
    source.sourceFrames = 0;
    CHECK (resolveMatch (source, bpmOnly(), host()).reason == MatchBlockReason::invalidLength);
    source.sourceFrames = std::numeric_limits<int64_t>::max();
    CHECK (resolveMatch (source, bpmOnly(), host (60.0)).reason == MatchBlockReason::invalidLength);
}

TEST_CASE ("Both matches compute the output length once", "[plugin-match][policy]")
{
    auto source = loop (120.0, 44100.0 * 8.0);
    setKey (source, 9, "minor");
    auto prefs = bpmOnly();
    prefs.matchScale = true;
    prefs.targetTonic = 0;
    const auto plan = resolveMatch (source, prefs, host (128.0));
    REQUIRE (plan.action == MatchAction::transform);
    CHECK (plan.tempoApplied);
    CHECK (plan.pitchApplied);
    CHECK (plan.timeRatio == Catch::Approx (120.0 / 128.0));
    CHECK (plan.semitones == Catch::Approx (3.0));
    CHECK (plan.targetFrames == 330750);
}

TEST_CASE ("Unsupported sources stay unsupported", "[plugin-match][policy]")
{
    auto source = loop();
    source.structure = MatchStructure::unsupported;
    const auto plan = resolveMatch (source, bpmOnly(), host());
    CHECK (plan.action == MatchAction::blocked);
    CHECK (plan.reason == MatchBlockReason::unsupportedSource);
}

TEST_CASE ("Smart metadata maps into a match source without parsing captions",
           "[plugin-match][policy]")
{
    SmartResult result;
    result.bpm = 120.0;
    result.hasBpm = true;
    result.bpmState = SmartState::Resolved;
    result.keyTonic = 9;
    result.keyMode = "minor";
    result.keyState = SmartState::Resolved;
    result.structure = "loop";
    result.structureState = SmartState::Resolved;
    result.hasPitch = true;
    result.pitchMidi = 69;
    result.pitchState = SmartState::Resolved;
    result.revision = 7;

    auto source = sourceFromSmartResult (result, "catalog:9:/Music/a.wav", 0, 352800, 44100.0, 2);
    CHECK (source.identity == "catalog:9:/Music/a.wav");
    CHECK (source.structure == MatchStructure::loop);
    CHECK (source.bpm == Catch::Approx (120.0));
    CHECK (source.bpmState == MetadataState::known);
    CHECK (source.keyTonic == 9);
    CHECK (source.keyMode == "minor");
    CHECK (source.pitchMidi == 69);
    CHECK (source.metadataRevision == 7);

    result.structure = "one-shot";
    CHECK (sourceFromSmartResult (result, "x", 0, 1, 44100.0, 1).structure
           == MatchStructure::oneShot);
    result.structure = "phrase";
    CHECK (sourceFromSmartResult (result, "x", 0, 1, 44100.0, 1).structure
           == MatchStructure::unknown);
    result.bpmState = SmartState::Stale;
    CHECK (sourceFromSmartResult (result, "x", 0, 1, 44100.0, 1).bpmState
           == MetadataState::stale);
}
