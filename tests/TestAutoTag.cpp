#include <catch2/catch_test_macros.hpp>
#include "core/tagging/AutoTagRules.h"
using namespace sonicator;
namespace {
AutoTagOptions options()
{
    AutoTagOptions o;
    const char* names[] = { "Kick", "Snare", "Clap", "Hat", "Percussion", "Bass", "Sub",
                            "Lead", "Pad", "Keys", "Guitar", "Vocal", "FX" };
    int64_t id = 1;
    for (auto name : names) o.tagIds[name] = id++;
    return o;
}
AutoTagInput input(const juce::String& path)
{
    AutoTagInput i;
    i.sampleId = 42;
    i.filePath = path;
    return i;
}
juce::String proposalType(const AutoTagInput& in)
{
    const auto decision = AutoTagRules::evaluate(in, options());
    return decision.proposal ? decision.proposal->canonicalType : juce::String();
}
juce::String skipCode(const AutoTagInput& in)
{
    const auto decision = AutoTagRules::evaluate(in, options());
    return decision.skip ? decision.skip->code : juce::String();
}
SmartCandidate typeCandidate(const juce::String& source, const juce::String& type,
                             int version = smartExtractorsVersion)
{
    SmartCandidate c; c.field = SmartField::Type; c.source = source; c.textValue = type;
    c.status = CandidateStatus::Valid; c.extractorVersion = version;
    return c;
}
SmartResult resolved(const juce::String& type, const juce::String& source = "wav_smpl")
{
    SmartResult r; r.type = type; r.typeState = SmartState::Resolved;
    r.typeSource = source; r.pipelineState = SmartPipelineState::Complete;
    r.extractorVersion = smartExtractorsVersion; r.resolverVersion = smartResolverVersion;
    return r;
}
}
TEST_CASE("AutoTag maps instrument tokens and abbreviations", "[autotag][rules]")
{
    REQUIRE(proposalType(input("/lib/kick 120.wav")) == "Kick");
    REQUIRE(proposalType(input("/lib/BD.wav")) == "Kick");
    REQUIRE(proposalType(input("/lib/snare_01.wav")) == "Snare");
    REQUIRE(proposalType(input("/lib/SD.wav")) == "Snare");
    REQUIRE(proposalType(input("/lib/hihat.wav")) == "Hat");
    REQUIRE(proposalType(input("/lib/tambourine.wav")) == "Percussion");
    REQUIRE(proposalType(input("/lib/bassline.wav")) == "Bass");
    REQUIRE(proposalType(input("/lib/pads.wav")) == "Pad");
    REQUIRE(proposalType(input("/lib/rhodes.wav")) == "Keys");
    REQUIRE(proposalType(input("/lib/Snares.wav")) == "Snare");
    REQUIRE(proposalType(input("/lib/fx_riser.wav")) == "FX");
    REQUIRE(proposalType(input("/lib/vox_take.wav")) == "Vocal");
}
TEST_CASE("AutoTag compound rules take precedence over single tokens", "[autotag][rules]")
{
    REQUIRE(proposalType(input("/lib/bass drum.wav")) == "Kick");
    REQUIRE(proposalType(input("/lib/bassdrum.wav")) == "Kick");
    REQUIRE(proposalType(input("/lib/sub bass.wav")) == "Sub");
    REQUIRE(proposalType(input("/lib/808 bass.wav")) == "Bass");
    REQUIRE(proposalType(input("/lib/808 kick.wav")) == "Kick");
    const auto drum = AutoTagRules::evaluate(input("/lib/808.wav"), options());
    REQUIRE_FALSE(drum.proposal.has_value());
    REQUIRE(drum.skip.has_value());
    REQUIRE(drum.skip->code == "ambiguous-types");
}
TEST_CASE("AutoTag matches whole tokens, not substrings", "[autotag][rules]")
{
    REQUIRE(proposalType(input("/lib/abdx.wav")).isEmpty());
    REQUIRE(proposalType(input("/lib/kickstand.wav")).isEmpty());
    REQUIRE(skipCode(input("/lib/kick_snare.wav")) == "ambiguous-types");
    REQUIRE(skipCode(input("/lib/kick snare.wav")) == "ambiguous-types");
}
TEST_CASE("AutoTag is case-insensitive and accepts separators", "[autotag][rules]")
{
    REQUIRE(proposalType(input("/lib/KICK-01.wav")) == "Kick");
    REQUIRE(proposalType(input("/lib/SnAre.02.wav")) == "Snare");
    REQUIRE(proposalType(input("/lib/hi-hat.wav")) == "Hat");
}
TEST_CASE("AutoTag never proves a type from the folder or pack name alone", "[autotag][rules]")
{
    REQUIRE(skipCode(input("/Packs/Kicks/sample.wav")) == "folder-only");
    REQUIRE(proposalType(input("/Packs/Kicks/snare.wav")) == "Snare");
    REQUIRE(proposalType(input("/Packs/Bass/kick_01.wav")) == "Kick");
}
TEST_CASE("AutoTag accepts current metadata and resolved smart type evidence", "[autotag][rules]")
{
    auto metadata = input("/lib/sample.wav");
    metadata.candidates.push_back(typeCandidate("wav_smpl", "kick"));
    const auto decision = AutoTagRules::evaluate(metadata, options());
    REQUIRE(decision.proposal.has_value());
    REQUIRE(decision.proposal->canonicalType == "Kick");
    REQUIRE(decision.proposal->rule == AutoTagRule::MetadataType);

    auto smart = input("/lib/sample.wav");
    smart.result = resolved("pad", "wav_smpl");
    REQUIRE(proposalType(smart) == "Pad");

    auto folderDerived = input("/lib/sample.wav");
    folderDerived.result = resolved("kick", "folder");
    REQUIRE(skipCode(folderDerived) == "folder-only");
}
TEST_CASE("AutoTag rejects stale or invalid evidence", "[autotag][rules]")
{
    auto staleCandidate = input("/lib/sample.wav");
    staleCandidate.candidates.push_back(typeCandidate("wav_smpl", "kick", smartExtractorsVersion - 1));
    REQUIRE(skipCode(staleCandidate) == "stale-evidence");

    auto staleResult = input("/lib/sample.wav");
    staleResult.result = resolved("pad");
    staleResult.result->pipelineState = SmartPipelineState::Stale;
    staleResult.candidates.push_back(typeCandidate("wav_smpl", "kick"));
    REQUIRE(skipCode(staleResult) == "stale-evidence");

    auto invalid = input("/lib/sample.wav");
    auto candidate = typeCandidate("wav_smpl", "kick");
    candidate.status = CandidateStatus::Invalid;
    invalid.candidates.push_back(candidate);
    REQUIRE(skipCode(invalid) == "no-evidence");

    auto changed = input("/lib/kick.wav");
    changed.inputCurrent = false;
    REQUIRE(skipCode(changed) == "changed-file");
}
TEST_CASE("AutoTag excludes proposals that contradict a manual Type override", "[autotag][rules]")
{
    auto conflict = input("/lib/kick.wav");
    SmartOverride overrideValue; overrideValue.textValue = "Snare";
    conflict.typeOverride = overrideValue;
    const auto decision = AutoTagRules::evaluate(conflict, options());
    REQUIRE_FALSE(decision.proposal.has_value());
    REQUIRE(decision.skip->code == "override-conflict");

    auto agrees = input("/lib/kick.wav");
    SmartOverride matching; matching.textValue = "Kick";
    agrees.typeOverride = matching;
    REQUIRE(proposalType(agrees) == "Kick");

    auto compoundAgrees = input("/lib/kick.wav");
    SmartOverride compound; compound.textValue = "Kick drum";
    compoundAgrees.typeOverride = compound;
    REQUIRE(proposalType(compoundAgrees) == "Kick");

    auto unknown = input("/lib/kick.wav");
    SmartOverride cleared; cleared.cleared = true;
    unknown.typeOverride = cleared;
    REQUIRE(skipCode(unknown) == "override-conflict");
}
TEST_CASE("AutoTag skips unsupported instrument classes and missing tags", "[autotag][rules]")
{
    REQUIRE(skipCode(input("/lib/synth_01.wav")) == "unsupported-type");
    REQUIRE(skipCode(input("/lib/trumpet.wav")) == "unsupported-type");
    REQUIRE(skipCode(input("/lib/violin.wav")) == "unsupported-type");
    auto missing = options();
    missing.tagIds.erase("Pad");
    auto pad = input("/lib/pad.wav");
    const auto decision = AutoTagRules::evaluate(pad, missing);
    REQUIRE_FALSE(decision.proposal.has_value());
    REQUIRE(decision.skip->code == "tag-missing");
}
TEST_CASE("AutoTag evaluation is deterministic and explains its evidence", "[autotag][rules]")
{
    auto in = input("/lib/bass drum 120.wav");
    const auto first = AutoTagRules::evaluate(in, options());
    const auto second = AutoTagRules::evaluate(in, options());
    REQUIRE(first.proposal.has_value());
    REQUIRE(second.proposal.has_value());
    REQUIRE(first.proposal->canonicalType == second.proposal->canonicalType);
    REQUIRE(first.proposal->reason == second.proposal->reason);
    REQUIRE(first.proposal->reason.containsIgnoreCase("bass drum"));
    REQUIRE(first.proposal->source == "filename");
}
