#include <catch2/catch_test_macros.hpp>
#include "core/database/Database.h"
#include "core/database/SampleRepository.h"
#include "core/smart/SmartTypes.h"
#include "core/smart/SmartResolver.h"
#include "TestHelpers.h"
#include <sqlite3.h>

using namespace sonicator;

namespace
{
void execRaw(const juce::File& file, const char* sql)
{
    sqlite3* handle = nullptr;
    REQUIRE(sqlite3_open(file.getFullPathName().toRawUTF8(), &handle) == SQLITE_OK);
    char* error = nullptr;
    const int result = sqlite3_exec(handle, sql, nullptr, nullptr, &error);
    if (error != nullptr) sqlite3_free(error);
    sqlite3_close(handle);
    REQUIRE(result == SQLITE_OK);
}

bool hasColumn(const juce::File& file, const char* table, const char* column)
{
    sqlite3* handle = nullptr;
    REQUIRE(sqlite3_open(file.getFullPathName().toRawUTF8(), &handle) == SQLITE_OK);
    sqlite3_stmt* statement = nullptr;
    bool found = false;
    const auto query = juce::String("PRAGMA table_info(") + table + ")";
    if (sqlite3_prepare_v2(handle, query.toRawUTF8(), -1, &statement, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(statement) == SQLITE_ROW)
            if (juce::String::fromUTF8((const char*) sqlite3_column_text(statement, 1)) == column)
                found = true;
        sqlite3_finalize(statement);
    }
    sqlite3_close(handle);
    return found;
}
}

TEST_CASE("Genre is the seventh smart field", "[genre]")
{
    REQUIRE(smartFieldCount == 7);
    REQUIRE(smartFieldKey(SmartField::Genre) == "genre");
    REQUIRE(smartFieldFromKey("genre") == SmartField::Genre);
    for (const auto field : { SmartField::Type, SmartField::Structure, SmartField::Bpm,
                              SmartField::Key, SmartField::Pitch, SmartField::RootNote,
                              SmartField::Genre })
        REQUIRE(smartFieldFromKey(smartFieldKey(field)) == field);
}

TEST_CASE("A catalog from schema 16 gains genre columns without losing data", "[genre][schema]")
{
    TestDirectory t;
    const auto file = t.dir.getChildFile("catalog.sqlite");
    int64_t sampleId = 0;
    {
        Database db(file);
        SampleRepository repo(db);
        repo.createTables();
        repo.addRoot(t.dir.getFullPathName());
        SampleRecord record;
        record.filePath = t.dir.getChildFile("kick.wav").getFullPathName();
        record.fileName = "kick.wav";
        record.format = "WAV";
        record.duration = 0.5;
        record.sampleRate = 44100;
        record.numChannels = 1;
        record.bitsPerSample = 16;
        sampleId = repo.upsert(record);
        REQUIRE(sampleId > 0);
        repo.setStarred({ sampleId }, true);
        SmartOverride overrideValue;
        overrideValue.textValue = "Kick";
        repo.setSmartOverride(sampleId, SmartField::Type, overrideValue);
    }
    // Rewind the file to what a schema-16 build left behind: the genre columns do not exist yet.
    execRaw(file, "ALTER TABLE smart_results DROP COLUMN genre;"
                  "ALTER TABLE smart_results DROP COLUMN genre_state;"
                  "ALTER TABLE smart_results DROP COLUMN genre_source;"
                  "ALTER TABLE smart_results DROP COLUMN genre_override;"
                  "PRAGMA user_version=16;");
    REQUIRE_FALSE(hasColumn(file, "smart_results", "genre"));

    Database db(file);
    SampleRepository repo(db);
    repo.createTables();
    REQUIRE(hasColumn(file, "smart_results", "genre"));
    REQUIRE(hasColumn(file, "smart_results", "genre_state"));
    REQUIRE(hasColumn(file, "smart_results", "genre_source"));
    REQUIRE(hasColumn(file, "smart_results", "genre_override"));
    const auto row = repo.findById(sampleId);
    REQUIRE(row.has_value());
    REQUIRE(row->starred);
    const auto overrides = repo.smartOverrides(sampleId);
    REQUIRE(overrides.count(SmartField::Type) == 1);
    REQUIRE(overrides.at(SmartField::Type).textValue == "Kick");
}
