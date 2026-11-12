#include "core/database/SampleRepository.h"
#include "TestHelpers.h"
#include "tools/sonicator-features/CatalogReader.h"
#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>

using namespace sonicator;

namespace {

void execRaw(sqlite3* db, const char* sql)
{
    REQUIRE(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK);
}

void addFeedback(sqlite3* db, int64_t sampleId, const char* hash, const char* type, const char* tags,
                 const char* origin)
{
    sqlite3_stmt* statement = nullptr;
    REQUIRE(sqlite3_prepare_v2(db,
        "INSERT INTO ml_feedback_samples (sample_id, file_hash, target_type, tags, features, updated_at, origin)"
        " VALUES (?, ?, ?, ?, ?, ?, ?)", -1, &statement, nullptr) == SQLITE_OK);
    const float features[14] {};
    sqlite3_bind_int64(statement, 1, sampleId);
    sqlite3_bind_text(statement, 2, hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, type, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, tags, -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(statement, 5, features, static_cast<int>(sizeof(features)), SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 6, 1);
    sqlite3_bind_text(statement, 7, origin, -1, SQLITE_TRANSIENT);
    REQUIRE(sqlite3_step(statement) == SQLITE_DONE);
    sqlite3_finalize(statement);
}

int readUserVersion(const juce::File& file)
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

TEST_CASE("catalog gold keeps confirmed rows only and never writes a path", "[features][catalog]")
{
    TestDirectory directory;
    const auto first = directory.wave("first.wav", 440.0);
    const auto second = directory.wave("second.wav", 880.0);
    const auto gone = directory.dir.getChildFile("gone.wav");
    const auto databaseFile = directory.dir.getChildFile("catalog.sqlite");

    int64_t firstId = 0, secondId = 0, goneId = 0;
    {
        Database db(databaseFile);
        SampleRepository repository(db);
        repository.createTables();
        REQUIRE(repository.addCustomType("Rimshot"));
        const auto add = [&](const juce::File& file, const juce::String& hash) {
            SampleRecord record;
            record.filePath = file.getFullPathName();
            record.fileName = file.getFileName();
            record.fileHash = hash;
            return repository.upsert(record);
        };
        firstId = add(first, "hash-first");
        secondId = add(second, "hash-second");
        goneId = add(gone, "hash-gone");
        const auto legacyId = add(directory.dir.getChildFile("legacy.wav"), "hash-legacy");

        sqlite3* raw = nullptr;
        REQUIRE(sqlite3_open(databaseFile.getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
        addFeedback(raw, firstId, "hash-first", "Snare", "Hat,Snap", "user");
        addFeedback(raw, secondId, "hash-second", "Rimshot", "", "approved");
        addFeedback(raw, legacyId, "hash-legacy", "Kick", "", "legacy");
        addFeedback(raw, goneId, "hash-gone", "Kick", "", "user");
        sqlite3_close(raw);
    }

    const auto versionBefore = readUserVersion(databaseFile);
    const auto sizeBefore = databaseFile.getSize();

    const auto out = directory.dir.getChildFile("gold-v2.jsonl");
    const auto paths = directory.dir.getChildFile("gold-v2.paths.jsonl");
    const auto result = writeCatalogGold(databaseFile, out, paths);

    REQUIRE(result.error.isEmpty());
    REQUIRE(result.written == 2);
    REQUIRE(result.missingFile == 1);
    REQUIRE(result.unreadable == 0);
    REQUIRE(readUserVersion(databaseFile) == versionBefore);
    REQUIRE(databaseFile.getSize() == sizeBefore);

    auto lines = juce::StringArray::fromLines(out.loadFileAsString());
    lines.removeEmptyStrings();
    REQUIRE(lines.size() == 2);
    bool sawSnare = false, sawRimshot = false;
    for (const auto& line : lines)
    {
        REQUIRE_FALSE(line.contains("\"path\""));
        const auto parsed = juce::JSON::parse(line);
        REQUIRE(parsed.isObject());
        REQUIRE(static_cast<int>(parsed.getProperty("featureSchema", 0)) == 2);
        REQUIRE(static_cast<int>(parsed.getProperty("dimensions", 0)) == 84);
        REQUIRE(parsed.getProperty("features", juce::var()).size() == 84);
        REQUIRE(parsed.getProperty("recordId", juce::var()).toString().length() == 16);
        const auto type = parsed.getProperty("type", juce::var()).toString();
        if (type == "snare")
        {
            sawSnare = true;
            REQUIRE_FALSE(static_cast<bool>(parsed.getProperty("typeIsCustom", true)));
            REQUIRE(parsed.getProperty("origin", juce::var()).toString() == "user");
            REQUIRE(parsed.getProperty("tags", juce::var()).size() == 2);
        }
        if (type == "rimshot")
        {
            sawRimshot = true;
            REQUIRE(static_cast<bool>(parsed.getProperty("typeIsCustom", false)));
            REQUIRE(parsed.getProperty("origin", juce::var()).toString() == "approved");
        }
        REQUIRE(type != "kick");
    }
    REQUIRE(sawSnare);
    REQUIRE(sawRimshot);

    auto pathLines = juce::StringArray::fromLines(paths.loadFileAsString());
    pathLines.removeEmptyStrings();
    REQUIRE(pathLines.size() == 2);
    REQUIRE(paths.loadFileAsString().contains(first.getFullPathName()));
}
