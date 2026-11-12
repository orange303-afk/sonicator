#include "core/database/SampleRepository.h"
#include "TestHelpers.h"
#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>

using namespace sonicator;

namespace {

int64_t addSample(SampleRepository& repository, const juce::String& path)
{
    SampleRecord record;
    record.filePath = path;
    record.fileName = juce::File(path).getFileName();
    record.fileHash = "hash-" + path;
    return repository.upsert(record);
}

void execRaw(const juce::File& file, const char* sql)
{
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(file.getFullPathName().toRawUTF8(), &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
}

int userVersion(const juce::File& file)
{
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(file.getFullPathName().toRawUTF8(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);
    sqlite3_stmt* statement = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &statement, nullptr) == SQLITE_OK);
    int version = 0;
    if (sqlite3_step(statement) == SQLITE_ROW) version = sqlite3_column_int(statement, 0);
    sqlite3_finalize(statement);
    sqlite3_close(db);
    return version;
}

} // namespace

TEST_CASE("a confirmed row keeps its v1 vector and gains a v2 slot", "[database][feedback]")
{
    TestDirectory directory;
    Database db(directory.dir.getChildFile("catalog.sqlite"));
    SampleRepository repository(db);
    repository.createTables();
    const auto id = addSample(repository, "/lib/kick.wav");
    repository.recordFeedback(id, "Kick", { "Kick" }, std::vector<float>(14, 0.5f),
                              SampleRepository::FeedbackOrigin::user);

    auto rows = repository.allFeedback();
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].featureSchema == 1);
    REQUIRE(rows[0].featuresV2.empty());
    REQUIRE(rows[0].features.size() == 14);

    repository.setFeedbackV2(id, std::vector<float>(84, 0.25f));
    rows = repository.allFeedback();
    REQUIRE(rows[0].featureSchema == 2);
    REQUIRE(rows[0].featuresV2.size() == 84);
    REQUIRE(rows[0].features.size() == 14);
}

TEST_CASE("a schema-15 catalog gains the v2 columns without losing rows", "[database][feedback]")
{
    TestDirectory directory;
    const auto file = directory.dir.getChildFile("catalog.sqlite");
    int64_t id = 0;
    {
        Database db(file);
        SampleRepository repository(db);
        repository.createTables();
        id = addSample(repository, "/lib/snare.wav");
        repository.recordFeedback(id, "Snare", { "Snare" }, std::vector<float>(14, 0.4f),
                                  SampleRepository::FeedbackOrigin::user);
    }
    execRaw(file, "ALTER TABLE ml_feedback_samples DROP COLUMN features_v2;"
                  "ALTER TABLE ml_feedback_samples DROP COLUMN feature_schema;"
                  "PRAGMA user_version=15;");
    REQUIRE(userVersion(file) == 15);

    {
        Database db(file);
        SampleRepository repository(db);
        repository.createTables();
        const auto rows = repository.allFeedback();
        REQUIRE(rows.size() == 1);
        REQUIRE(rows[0].targetType == "Snare");
        REQUIRE(rows[0].featureSchema == 1);
        REQUIRE(rows[0].featuresV2.empty());
    }
    REQUIRE(userVersion(file) == 16);
}

TEST_CASE("the contribution record carries both feature schemas", "[database][feedback]")
{
    TestDirectory directory;
    Database db(directory.dir.getChildFile("catalog.sqlite"));
    SampleRepository repository(db);
    repository.createTables();
    const auto id = addSample(repository, "/lib/hat.wav");
    repository.recordFeedback(id, "Hat", { "Hat" }, std::vector<float>(14, 0.6f),
                              SampleRepository::FeedbackOrigin::user);
    repository.setFeedbackV2(id, std::vector<float>(84, 0.7f));

    const auto records = repository.contributionRecords(0, 10);
    REQUIRE(records.size() == 1);
    REQUIRE(records[0].featureSchema == 2);
    REQUIRE(records[0].featuresV2.size() == 84);
    REQUIRE(records[0].features.size() == 14);
}
