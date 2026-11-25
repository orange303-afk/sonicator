#include <catch2/catch_test_macros.hpp>
#include "core/cloud/CloudSource.h"
#include "TestHelpers.h"
using namespace sonicator;

namespace
{
struct CloudFixture
{
    TestDirectory t;
    std::unique_ptr<Database> db;
    std::unique_ptr<SampleRepository> repo;
    int64_t kickId = 0, drumsId = 0, metaId = 0;
    int64_t kickTag = 0, drumsTag = 0;
    int64_t dropId = 0; // no descriptors and no tags: must not appear

    CloudFixture()
    {
        db = std::make_unique<Database>(t.dir.getChildFile("cloud.sqlite"));
        repo = std::make_unique<SampleRepository>(*db);
        repo->createTables();
        kickTag = repo->createTag("Kick", 0xffe05a5a);
        drumsTag = repo->createTag("Drums", 0xff8fd06a);
        auto add = [this](const char* name, int version, double duration, double rms)
        {
            SampleRecord r;
            r.filePath = t.dir.getChildFile(name).getFullPathName();
            r.fileName = name;
            r.fileHash = juce::String("hash-") + name;
            r.format = "WAV";
            r.version = version;
            r.duration = duration;
            r.sampleRate = 44100;
            r.numChannels = 2;
            r.rmsLevel = rms;
            r.peakLevel = rms * 2;
            r.spectralCentroid = 1200;
            r.spectralRolloff = 4000;
            r.spectralFlatness = 0.2;
            r.dateAnalyzed = 1000 + version;
            return repo->upsert(r);
        };
        kickId = add("kick.wav", 1, 0.4, 0.3);
        drumsId = add("drums.wav", 1, 0.2, 0.1);
        metaId = add("pad.rx2", -1, 0, 0);
        dropId = add("untagged.wav", 1, 1.0, 0.2);
        repo->setSampleTag(kickId, kickTag, true);
        repo->setSampleTag(drumsId, kickTag, true);
        repo->setSampleTag(drumsId, drumsTag, true);
        repo->setSampleTag(metaId, drumsTag, true);

        repo->recordFeedback(kickId, "Kick", { "Kick" }, std::vector<float>(14, 0.5f),
                             SampleRepository::FeedbackOrigin::user);
        repo->setFeedbackV2(kickId, std::vector<float>(84, 0.25f));
    }
};
} // namespace

TEST_CASE("Cloud input batches tagged files, descriptors and vectors", "[cloud]")
{
    CloudFixture f;
    const auto input = cloud::loadCloudInput(*f.repo);

    REQUIRE(input.nodes.size() == 3); // the untagged file is not on the map
    REQUIRE(input.nodes[0].sampleId == f.kickId);
    REQUIRE(input.nodes[1].sampleId == f.drumsId);
    REQUIRE(input.nodes[2].sampleId == f.metaId);

    REQUIRE(input.nodes[0].hasBasic);
    REQUIRE(input.nodes[0].hasV2);
    REQUIRE(input.nodes[1].hasBasic);
    REQUIRE_FALSE(input.nodes[1].hasV2);
    REQUIRE_FALSE(input.nodes[2].hasBasic);
    REQUIRE_FALSE(input.nodes[2].hasV2);

    REQUIRE(input.nodes[1].tagIds == std::vector<int64_t> { f.kickTag, f.drumsTag });
    REQUIRE(input.v2.size() == input.nodes.size());
    REQUIRE(input.v2[0].size() == 84);
    REQUIRE(input.v2[1].empty());

    REQUIRE(input.tags.size() == 2);
    REQUIRE(input.tags[0].id < input.tags[1].id);
    REQUIRE(input.cooccurrence.size() == 1);
    REQUIRE(input.cooccurrence[0].tagA == f.kickTag);
    REQUIRE(input.cooccurrence[0].tagB == f.drumsTag);
    REQUIRE(input.cooccurrence[0].count == 1);
    REQUIRE(input.fingerprint != 0);
}

TEST_CASE("Cloud input gives the worker back as soon as the build is cancelled", "[cloud]")
{
    CloudFixture f;
    // The first check runs before the batch query, so a superseded generation never reads a 50k-file
    // catalog to the end just to throw the result away.
    const auto cancelled = cloud::loadCloudInput(*f.repo, [] { return true; });
    REQUIRE(cancelled.nodes.empty());
    REQUIRE(cancelled.tags.empty());
    REQUIRE(cancelled.fingerprint == 0);
}

TEST_CASE("Cloud fingerprint follows tags and features, not stars or filters", "[cloud]")
{
    CloudFixture f;
    const auto before = cloud::loadCloudInput(*f.repo);

    f.repo->setStarred({ f.kickId }, true);
    REQUIRE(cloud::loadCloudInput(*f.repo).fingerprint == before.fingerprint);

    f.repo->setSampleTag(f.drumsId, f.drumsTag, false);
    const auto retagged = cloud::loadCloudInput(*f.repo);
    REQUIRE(retagged.fingerprint != before.fingerprint);
    REQUIRE(retagged.nodes[1].tagIds == std::vector<int64_t> { f.kickTag });

    const auto again = cloud::loadCloudInput(*f.repo);
    REQUIRE(again.fingerprint == retagged.fingerprint); // stable across reads
}

TEST_CASE("Cloud fingerprint ignores the analysis time and follows the descriptors", "[cloud]")
{
    CloudFixture f;
    const auto before = cloud::loadCloudInput(*f.repo);

    // A background reanalysis that lands on the same numbers must not rebuild an identical map.
    auto record = *f.repo->findById(f.kickId);
    record.dateAnalyzed += 5000;
    f.repo->upsert(record);
    REQUIRE(cloud::loadCloudInput(*f.repo).fingerprint == before.fingerprint);

    // A real change of a descriptor that moves the point must rebuild it.
    record.rmsLevel *= 0.5;
    f.repo->upsert(record);
    REQUIRE(cloud::loadCloudInput(*f.repo).fingerprint != before.fingerprint);
}

TEST_CASE("Cloud colour blends the tag colours per file", "[cloud]")
{
    CloudFixture f;
    const auto input = cloud::loadCloudInput(*f.repo);
    const auto& kick = input.nodes[0];
    REQUIRE(kick.colour == 0xffe05a5a);
    const auto& drums = input.nodes[1]; // Kick + Drums: channel average, rounded
    auto avg = [](int a, int b) { return (int) std::lround((a + b) / 2.0); };
    REQUIRE(((drums.colour >> 16) & 0xff) == (unsigned) avg(0xe0, 0x8f));
    REQUIRE(((drums.colour >> 8) & 0xff) == (unsigned) avg(0x5a, 0xd0));
    REQUIRE((drums.colour & 0xff) == (unsigned) avg(0x5a, 0x6a));
    REQUIRE(((drums.colour >> 24) & 0xff) == 0xff);
}

TEST_CASE("Cloud basic features stay finite for silent and zero-duration rows", "[cloud]")
{
    SampleRecord silent;
    silent.version = 1;
    silent.duration = 0.5;
    silent.sampleRate = 44100;
    silent.numChannels = 1;
    silent.rmsLevel = 0;
    silent.peakLevel = 0;
    const auto values = cloud::basicFeaturesOf(silent);
    for (auto v : values) REQUIRE(std::isfinite(v));
    REQUIRE(values[1] < -100.0f);
}
