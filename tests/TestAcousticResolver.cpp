#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/smart/SmartResolver.h"
#include "core/tagging/AutoTagRules.h"

using namespace sonicator;

namespace {
SmartCandidate makeCandidate(SmartField field,
                            const juce::String& source,
                            const juce::String& text,
                            int version = smartDspVersion)
{
    SmartCandidate c;
    c.field = field;
    c.source = source;
    c.textValue = text;
    c.status = CandidateStatus::Valid;
    c.extractorVersion = version;
    return c;
}
} // namespace

TEST_CASE("Case 1: Unnamed file with dsp_acoustic candidate resolves type", "[smart][resolver][acoustic]")
{
    // Case 1: Unnamed file (Audio_01.wav) with SmartCandidate(Type, "dsp_acoustic", "Kick"):
    // resolved to type == "Kick", typeState == SmartState::Resolved.
    auto c = makeCandidate(SmartField::Type, "dsp_acoustic", "Kick");
    auto res = resolveSmart({ c });
    REQUIRE(res.type == "Kick");
    REQUIRE(res.typeState == SmartState::Resolved);
    REQUIRE(res.typeSource == "dsp_acoustic");
}

TEST_CASE("Case 2: Unnamed file with ml_classifier candidate resolves type", "[smart][resolver][acoustic]")
{
    // Case 2: Unnamed file (sample.wav) with SmartCandidate(Type, "ml_classifier", "Guitar"):
    // resolved to type == "Guitar", typeState == SmartState::Resolved.
    auto c = makeCandidate(SmartField::Type, "ml_classifier", "Guitar");
    auto res = resolveSmart({ c });
    REQUIRE(res.type == "Guitar");
    REQUIRE(res.typeState == SmartState::Resolved);
    REQUIRE(res.typeSource == "ml_classifier");
}

TEST_CASE("Case 3: Agreement between filename and acoustic candidate", "[smart][resolver][acoustic]")
{
    // Case 3: Agreement: filename candidate "kick" and acoustic candidate "Kick":
    // resolved to Kick, state Resolved, agreement note includes both sources.
    auto fn = makeCandidate(SmartField::Type, "filename", "kick", smartExtractorsVersion);
    auto ac = makeCandidate(SmartField::Type, "dsp_acoustic", "Kick", smartDspVersion);
    auto res = resolveSmart({ fn, ac });
    REQUIRE(res.type == "Kick");
    REQUIRE(res.typeState == SmartState::Resolved);
    REQUIRE(res.explanation.contains("dsp_acoustic"));
    REQUIRE(res.explanation.contains("filename"));
    REQUIRE(res.explanation.contains("independent sources agree"));
}

TEST_CASE("Case 4: Disagreement where high-confidence acoustic overrides filename", "[smart][resolver][acoustic]")
{
    // Case 4: Disagreement where high-confidence acoustic overrides filename:
    // filename has "snare", dsp_acoustic has "Pad": resolves to "Pad" with source "dsp_acoustic".
    auto fn = makeCandidate(SmartField::Type, "filename", "snare", smartExtractorsVersion);
    auto ac = makeCandidate(SmartField::Type, "dsp_acoustic", "Pad", smartDspVersion);
    auto res = resolveSmart({ fn, ac });
    REQUIRE(res.type == "Pad");
    REQUIRE(res.typeSource == "dsp_acoustic");
    REQUIRE(res.typeState == SmartState::Ambiguous);
    REQUIRE(res.explanation.contains("interpretations differ"));
}

TEST_CASE("Case 5: AutoTagRules evaluates resolved smart type Kick from dsp_acoustic", "[autotag][rules][acoustic]")
{
    // Case 5: AutoTagRules::evaluate on input with no filename tokens and resolved smart type Kick from dsp_acoustic:
    // produces proposal with canonicalType == "Kick", rule == AutoTagRule::SmartType, source == "dsp_acoustic".
    AutoTagInput in;
    in.sampleId = 42;
    in.filePath = "/samples/Audio_01.wav";

    SmartResult r;
    r.type = "Kick";
    r.typeState = SmartState::Resolved;
    r.typeSource = "dsp_acoustic";
    r.pipelineState = SmartPipelineState::Complete;
    r.extractorVersion = smartExtractorsVersion;
    r.resolverVersion = smartResolverVersion;
    in.result = r;

    AutoTagOptions options;
    options.tagIds["Kick"] = 100;
    options.tagIds["Snare"] = 101;
    options.tagIds["Pad"] = 102;

    const auto decision = AutoTagRules::evaluate(in, options);
    REQUIRE(decision.proposal.has_value());
    REQUIRE(decision.proposal->canonicalType == "Kick");
    REQUIRE(decision.proposal->rule == AutoTagRule::SmartType);
    REQUIRE(decision.proposal->source == "dsp_acoustic");
    REQUIRE(decision.proposal->tagId == 100);
}

TEST_CASE("AutoTagRules evaluates acoustic candidate directly when unnamed and no smart result", "[autotag][rules][acoustic]")
{
    AutoTagInput in;
    in.sampleId = 43;
    in.filePath = "/samples/sample.wav";
    auto ac = makeCandidate(SmartField::Type, "dsp_acoustic", "Kick", smartDspVersion);
    in.candidates.push_back(ac);

    AutoTagOptions options;
    options.tagIds["Kick"] = 100;

    const auto decision = AutoTagRules::evaluate(in, options);
    REQUIRE(decision.proposal.has_value());
    REQUIRE(decision.proposal->canonicalType == "Kick");
    REQUIRE(decision.proposal->rule == AutoTagRule::SmartType);
    REQUIRE(decision.proposal->source == "dsp_acoustic");
}
