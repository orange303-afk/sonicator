#include "core/database/SampleRepository.h"
#include "core/ml/FeatureV2Backfill.h"
#include "TestHelpers.h"
#include <catch2/catch_test_macros.hpp>

using namespace sonicator;

namespace {

int64_t addSample(SampleRepository& repository, const juce::File& file)
{
    SampleRecord record;
    record.filePath = file.getFullPathName();
    record.fileName = file.getFileName();
    record.fileHash = "hash-" + file.getFileName();
    return repository.upsert(record);
}

void confirm(SampleRepository& repository, int64_t id)
{
    repository.recordFeedback(id, "Snare", { "Snare" }, std::vector<float>(14, 0.4f),
                              SampleRepository::FeedbackOrigin::user);
}

} // namespace

TEST_CASE("the v2 backfill fills schema-1 rows and counts the rest", "[ml][features]")
{
    TestDirectory directory;
    const auto good = directory.wave("good.wav", 220.0);
    const auto missing = directory.dir.getChildFile("gone.wav");
    const auto broken = directory.dir.getChildFile("broken.wav");
    broken.replaceWithText("not audio");
    Database db(directory.dir.getChildFile("catalog.sqlite"));
    SampleRepository repository(db);
    repository.createTables();
    const auto goodId = addSample(repository, good);
    const auto missingId = addSample(repository, missing);
    const auto brokenId = addSample(repository, broken);
    confirm(repository, goodId);
    confirm(repository, missingId);
    confirm(repository, brokenId);

    const auto result = backfillFeatureV2(repository);
    REQUIRE(result.scanned == 3);
    REQUIRE(result.updated == 1);
    REQUIRE(result.missing == 1);
    REQUIRE(result.failed == 1);

    int schemaGood = 0, schemaMissing = 0, schemaBroken = 0;
    std::size_t v2Size = 0;
    for (const auto& row : repository.allFeedback())
    {
        if (row.sampleId == goodId) { schemaGood = row.featureSchema; v2Size = row.featuresV2.size(); }
        if (row.sampleId == missingId) schemaMissing = row.featureSchema;
        if (row.sampleId == brokenId) schemaBroken = row.featureSchema;
    }
    REQUIRE(schemaGood == 2);
    REQUIRE(v2Size == FeatureExtractorV2::dimensions());
    REQUIRE(schemaMissing == 1);
    REQUIRE(schemaBroken == 1);
}

TEST_CASE("the v2 backfill is idempotent and obeys cancellation", "[ml][features]")
{
    TestDirectory directory;
    const auto wave = directory.wave("one.wav", 440.0);
    Database db(directory.dir.getChildFile("catalog.sqlite"));
    SampleRepository repository(db);
    repository.createTables();
    const auto id = addSample(repository, wave);
    confirm(repository, id);

    REQUIRE(backfillFeatureV2(repository).updated == 1);
    REQUIRE(backfillFeatureV2(repository).updated == 0);

    const auto cancelled = backfillFeatureV2(repository, [] { return true; });
    REQUIRE(cancelled.scanned == 0);
}
