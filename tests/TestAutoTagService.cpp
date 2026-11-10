#include <catch2/catch_test_macros.hpp>
#include "core/tagging/AutoTagService.h"
#include "core/smart/AcousticClassifierService.h"
#include "TestHelpers.h"
using namespace sonicator;
namespace {
struct AutoFixture
{
    TestDirectory t;
    std::unique_ptr<Database> db;
    std::unique_ptr<SampleRepository> repo;
    AutoFixture()
    {
        db = std::make_unique<Database>(t.dir.getChildFile("autotag.sqlite"));
        repo = std::make_unique<SampleRepository>(*db);
        repo->createTables();
    }
    int64_t add(const juce::String& relative, int samples = 4410)
    {
        const auto file = t.wave("lib/" + relative, 440.0, 1, samples);
        SampleRecord r;
        r.filePath = file.getFullPathName(); r.fileName = file.getFileName();
        r.fileHash = "hash-" + relative; r.fileSize = file.getSize();
        r.dateModified = file.getLastModificationTime().toMilliseconds();
        return repo->upsert(r);
    }
    int64_t tag(const juce::String& name)
    {
        for (const auto& t : repo->tags()) if (t.name.equalsIgnoreCase(name)) return t.id;
        return 0;
    }
    int tagged(int64_t sampleId, int64_t tagId)
    {
        for (const auto& t : repo->tagsForSample(sampleId)) if (t.id == tagId) return 1;
        return 0;
    }
};
}
TEST_CASE("AutoTag preparation proposes explained instruments and writes nothing", "[autotag][service]")
{
    AutoFixture f;
    const auto kick = f.add("Drums/kick 120.wav");
    const auto snare = f.add("Drums/snare.wav");
    const auto ambiguous = f.add("Drums/kick snare.wav");
    const auto folderOnly = f.add("Kicks/sample.wav");
    const auto bassDrum = f.add("Drums/bass drum.wav");
    const auto fx = f.add("Drums/noise.wav");
    AutoTagService service(*f.repo);
    REQUIRE(service.startSelected({ kick, snare, ambiguous, folderOnly, bassDrum, fx }));
    REQUIRE(service.waitUntilFinished(20000));
    AutoTagResult result;
    REQUIRE(service.takeResult(result));
    REQUIRE(result.checked == 6);
    REQUIRE(result.proposals.size() == 4);
    REQUIRE(result.errors == 0);
    REQUIRE(result.noEvidence == 0);
    REQUIRE_FALSE(service.progress().cancelled);
    int kickProposals = 0, snareProposals = 0, fxProposals = 0, ambiguousSkipped = 0, folderSkipped = 0;
    for (const auto& proposal : result.proposals)
    {
        if (proposal.canonicalType == "Kick") ++kickProposals;
        if (proposal.canonicalType == "Snare") ++snareProposals;
        if (proposal.canonicalType == "FX") ++fxProposals;
        REQUIRE_FALSE(proposal.reason.isEmpty());
        REQUIRE((proposal.sampleId == kick || proposal.sampleId == snare
                 || proposal.sampleId == bassDrum || proposal.sampleId == fx));
    }
    for (const auto& skipped : result.skipped)
    {
        if (skipped.code == "ambiguous-types") ++ambiguousSkipped;
        if (skipped.code == "folder-only") ++folderSkipped;
    }
    REQUIRE(kickProposals == 2); // kick and bass drum both become Kick
    REQUIRE(snareProposals == 1);
    REQUIRE(fxProposals == 1);
    REQUIRE(ambiguousSkipped == 1);
    REQUIRE(folderSkipped == 1);
    for (auto id : { kick, snare, ambiguous, folderOnly, bassDrum, fx })
        REQUIRE(f.repo->tagsForSample(id).empty());
}
TEST_CASE("AutoTag folder preparation is recursive and excludes siblings", "[autotag][service][folders]")
{
    AutoFixture f;
    f.add("Drums/kick.wav");
    f.add("Drums/deep/snare.wav");
    f.add("DrumsExtra/kick.wav");
    AutoTagService service(*f.repo);
    REQUIRE(service.startFolder(f.t.dir.getChildFile("lib/Drums").getFullPathName()));
    REQUIRE(service.waitUntilFinished(20000));
    AutoTagResult result;
    REQUIRE(service.takeResult(result));
    REQUIRE(result.checked == 2);
    REQUIRE(result.proposals.size() == 2);
    for (const auto& proposal : result.proposals)
        REQUIRE(proposal.filePath.contains("/Drums/"));
}
TEST_CASE("AutoTag marks already assigned tags without counting them as new", "[autotag][service]")
{
    AutoFixture f;
    const auto kick = f.add("Drums/kick.wav");
    const auto snare = f.add("Drums/snare.wav");
    const auto kickTag = f.tag("Kick");
    REQUIRE(kickTag > 0);
    REQUIRE(f.repo->assignTag({ kick }, kickTag) == 1);
    AutoTagService service(*f.repo);
    REQUIRE(service.startSelected({ kick, snare }));
    REQUIRE(service.waitUntilFinished(20000));
    AutoTagResult result;
    REQUIRE(service.takeResult(result));
    REQUIRE(result.proposals.size() == 2);
    REQUIRE(result.alreadyAssigned == 1);
    REQUIRE(result.newAssignments == 1);
}
TEST_CASE("AutoTag apply is idempotent and can be cancelled between batches", "[autotag][apply]")
{
    AutoFixture f;
    for (int i = 0; i < 6; ++i) f.add("Drums/kick " + juce::String(i) + ".wav");
    AutoTagService prepareService(*f.repo);
    REQUIRE(prepareService.startFolder(f.t.dir.getChildFile("lib/Drums").getFullPathName()));
    REQUIRE(prepareService.waitUntilFinished(20000));
    AutoTagResult prepared;
    REQUIRE(prepareService.takeResult(prepared));
    REQUIRE(prepared.proposals.size() == 6);
    const auto proposals = prepared.proposals;
    AutoTagService applyService(*f.repo);
    REQUIRE(applyService.startApply(proposals));
    REQUIRE(applyService.waitUntilFinished(20000));
    const auto first = applyService.applyResult();
    REQUIRE(first.applied == 6);
    REQUIRE(first.rejected == 0);
    for (const auto& proposal : proposals)
        REQUIRE(f.tagged(proposal.sampleId, proposal.tagId) == 1);
    REQUIRE(applyService.startApply(proposals));
    REQUIRE(applyService.waitUntilFinished(20000));
    REQUIRE(applyService.applyResult().applied == 0);

    // A fresh scope cancelled after two single-item batches leaves exactly two assignments.
    AutoFixture g;
    for (int i = 0; i < 6; ++i) g.add("Drums/snare " + juce::String(i) + ".wav");
    AutoTagService prep(*g.repo);
    REQUIRE(prep.startFolder(g.t.dir.getChildFile("lib/Drums").getFullPathName()));
    REQUIRE(prep.waitUntilFinished(20000));
    AutoTagResult result;
    REQUIRE(prep.takeResult(result));
    REQUIRE(result.proposals.size() == 6);
    AutoTagService apply(*g.repo, 1);
    apply.onApplyBatch = [&apply](const AutoTagProgress& p) { if (p.applied >= 2) apply.cancel(); };
    REQUIRE(apply.startApply(result.proposals));
    REQUIRE(apply.waitUntilFinished(20000));
    const auto cancelled = apply.applyResult();
    REQUIRE(cancelled.cancelled);
    REQUIRE(cancelled.applied >= 2);
    REQUIRE(cancelled.applied < 6);
    int tagged = 0;
    for (const auto& proposal : result.proposals) tagged += g.tagged(proposal.sampleId, proposal.tagId);
    REQUIRE(tagged == cancelled.applied);
}
TEST_CASE("AutoTag revalidates changed files and deleted tags before applying", "[autotag][apply]")
{
    {
        AutoFixture f;
        const auto kick = f.add("Drums/kick.wav");
        AutoTagService service(*f.repo);
        REQUIRE(service.startSelected({ kick }));
        REQUIRE(service.waitUntilFinished(20000));
        AutoTagResult result;
        REQUIRE(service.takeResult(result));
        REQUIRE(result.proposals.size() == 1);
        // Rewrite with a different size after preparation.
        f.t.wave("lib/Drums/kick.wav", 220.0, 1, 22050);
        REQUIRE(service.startApply(result.proposals));
        REQUIRE(service.waitUntilFinished(20000));
        REQUIRE(service.applyResult().rejected == 1);
        REQUIRE(service.applyResult().applied == 0);
        REQUIRE(f.repo->tagsForSample(kick).empty());
    }
    {
        AutoFixture f;
        const auto kick = f.add("Drums/kick.wav");
        AutoTagService service(*f.repo);
        REQUIRE(service.startSelected({ kick }));
        REQUIRE(service.waitUntilFinished(20000));
        AutoTagResult result;
        REQUIRE(service.takeResult(result));
        REQUIRE(result.proposals.size() == 1);
        f.repo->deleteTag(result.proposals.front().tagId);
        REQUIRE(service.startApply(result.proposals));
        REQUIRE(service.waitUntilFinished(20000));
        REQUIRE(service.applyResult().rejected == 1);
        REQUIRE(service.applyResult().applied == 0);
    }
}
TEST_CASE("Cancelling AutoTag preparation writes no tags", "[autotag][cancel]")
{
    AutoFixture f;
    std::vector<int64_t> ids;
    for (int i = 0; i < 6; ++i) ids.push_back(f.add("Drums/kick " + juce::String(i) + ".wav"));
    AutoTagService service(*f.repo);
    service.onSample = [&service](const AutoTagProgress& p) { if (p.processed >= 2) service.cancel(); };
    REQUIRE(service.startSelected(ids));
    REQUIRE(service.waitUntilFinished(20000));
    REQUIRE(service.progress().cancelled);
    AutoTagResult result;
    REQUIRE(service.takeResult(result));
    REQUIRE(result.proposals.size() < ids.size());
    for (auto id : ids) REQUIRE(f.repo->tagsForSample(id).empty());
}
TEST_CASE("Empty folder AutoTag preparation produces no proposals and writes nothing", "[autotag][empty]")
{
    AutoFixture f;
    AutoTagService service(*f.repo);
    REQUIRE(service.startFolder(f.t.dir.getChildFile("empty").getFullPathName()));
    REQUIRE(service.waitUntilFinished(15000));
    AutoTagResult result;
    REQUIRE(service.takeResult(result));
    REQUIRE(result.checked == 0);
    REQUIRE(result.proposals.empty());
    REQUIRE(f.repo->tags().size() == 26);
}
TEST_CASE("AutoTag writes Crash rather than Percussion for a crash file", "[autotag][service][instruments]")
{
    AutoFixture f;
    const auto crash = f.add("Drums/crash_01.wav");
    const auto crashTag = f.tag("Crash");
    const auto percussionTag = f.tag("Percussion");
    REQUIRE(crashTag > 0);
    REQUIRE(percussionTag > 0);
    AutoTagService service(*f.repo);
    REQUIRE(service.startSelected({ crash }));
    REQUIRE(service.waitUntilFinished(20000));
    AutoTagResult result;
    REQUIRE(service.takeResult(result));
    REQUIRE(result.proposals.size() == 1);
    REQUIRE(result.proposals.front().canonicalType == "Crash");
    REQUIRE(result.proposals.front().tagId == crashTag);
    REQUIRE(service.startApply(result.proposals));
    REQUIRE(service.waitUntilFinished(20000));
    REQUIRE(service.applyResult().applied == 1);
    REQUIRE(f.tagged(crash, crashTag) == 1);
    REQUIRE(f.tagged(crash, percussionTag) == 0);
}

TEST_CASE("AutoTag proposes learned prototype tag when adaptive classifier matches", "[autotag][service][adaptive]")
{
    AutoFixture f;
    const auto unnamed = f.add("Samples/synth_sound.wav");
    const auto padTag = f.tag("Pad");
    REQUIRE(padTag > 0);

    // Register prototype with "Pad" tag
    std::vector<float> features(14, 0.5f);
    AcousticClassifierService::getSharedAdaptiveClassifier().clear();
    AcousticClassifierService::getSharedAdaptiveClassifier().updatePrototype(999, "Pad", { "Pad" }, features);

    // Give unnamed sample similar prototype features via feedback recording
    f.repo->recordFeedback(unnamed, "Pad", { "Pad" }, features);

    AutoTagService service(*f.repo);
    REQUIRE(service.startSelected({ unnamed }));
    REQUIRE(service.waitUntilFinished(20000));
    AutoTagResult result;
    REQUIRE(service.takeResult(result));
    REQUIRE(result.proposals.size() == 1);
    CHECK(result.proposals.front().canonicalType == "Pad");
    CHECK(result.proposals.front().tagId == padTag);

    AcousticClassifierService::getSharedAdaptiveClassifier().clear();
}

