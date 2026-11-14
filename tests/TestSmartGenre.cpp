#include <catch2/catch_test_macros.hpp>
#include "core/database/Database.h"
#include "core/database/SampleRepository.h"
#include "core/smart/SmartTypes.h"
#include "core/smart/SmartResolver.h"
#include "core/smart/GenreContextExtractor.h"
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

TEST_CASE("A library or drive name never assigns a genre", "[genre]")
{
    // Two nearest folders only: "samples" and "_Kurzweil" carry no genre, and "1TBSSD" is a volume.
    const juce::File deep("/Volumes/1TBSSD/samples/_Kurzweil/Programs/strings.wav");
    REQUIRE(GenreContextExtractor::candidates(deep).empty());
    const juce::File driveRoot("/Volumes/1TBSSD/Techno/kick.wav");
    // "Techno" is a real folder one level above the file, so it does match; a volume is never
    // consulted, but a genuine genre folder next to the file is.
    REQUIRE(GenreContextExtractor::candidates(driveRoot).size() == 1);
}

TEST_CASE("Whole tokens only: housekeeper is not house", "[genre]")
{
    const juce::File file("/Samples/Various/housekeeper_01.wav");
    REQUIRE(GenreContextExtractor::candidates(file).empty());
    const juce::File real("/Samples/Various/house_loop.wav");
    const auto candidates = GenreContextExtractor::candidates(real);
    REQUIRE(candidates.size() == 1);
    REQUIRE(candidates[0].textValue == "House");
    REQUIRE(candidates[0].source == "filename");
}

TEST_CASE("Metadata outranks the folder and the file name", "[genre]")
{
    SmartCandidate metadata;
    metadata.field = SmartField::Genre;
    metadata.source = "id3";
    metadata.textValue = "Ambient";
    SmartCandidate folder;
    folder.field = SmartField::Genre;
    folder.source = "folder";
    folder.textValue = "Rock";
    const auto result = resolveSmart({ metadata, folder });
    REQUIRE(result.genre == "Ambient");
    REQUIRE(result.genreSource == "id3");
    REQUIRE(result.genreState == SmartState::Resolved);
}

TEST_CASE("Two disagreeing sources leave the genre ambiguous, not guessed", "[genre]")
{
    SmartCandidate folder;
    folder.field = SmartField::Genre;
    folder.source = "folder";
    folder.textValue = "House";
    SmartCandidate name;
    name.field = SmartField::Genre;
    name.source = "filename";
    name.textValue = "Trap";
    const auto result = resolveSmart({ folder, name });
    REQUIRE(result.genreState == SmartState::Ambiguous);
    REQUIRE(result.genre.isEmpty());
    REQUIRE(result.explanation.contains("sources disagree"));
}

TEST_CASE("A manual genre beats a confident candidate and unknown blocks guessing", "[genre]")
{
    SmartCandidate folder;
    folder.field = SmartField::Genre;
    folder.source = "folder";
    folder.textValue = "House";
    SmartOverride manual;
    manual.textValue = "Jazz";
    SmartOverrideMap overrides;
    overrides[SmartField::Genre] = manual;
    const auto overridden = resolveSmart({ folder }, overrides);
    REQUIRE(overridden.genre == "Jazz");
    REQUIRE(overridden.genreSource == "override");
    REQUIRE(overridden.genreOverride == OverrideState::Value);

    SmartOverride unknown;
    unknown.cleared = true;
    overrides[SmartField::Genre] = unknown;
    const auto cleared = resolveSmart({ folder }, overrides);
    REQUIRE(cleared.genre.isEmpty());
    REQUIRE(cleared.genreOverride == OverrideState::Unknown);
}

TEST_CASE("The same source contradicting itself is noise, not a conflict", "[genre]")
{
    SmartCandidate first;
    first.field = SmartField::Genre;
    first.source = "filename";
    first.textValue = "House";
    SmartCandidate second = first;
    second.textValue = "Techno";
    const auto result = resolveSmart({ first, second });
    REQUIRE(result.genreState == SmartState::Resolved);
    REQUIRE(result.genre.isNotEmpty());
}
