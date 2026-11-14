#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/database/SampleRepository.h"
#include "core/smart/SmartTypes.h"
#include "TestHelpers.h"
#include <sqlite3.h>
#include <string>

using namespace sonicator;

namespace {
void execRaw(sqlite3* db, const char* sql)
{
    REQUIRE(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK);
}

std::string columnText(sqlite3_stmt* s, int column)
{
    auto* text = sqlite3_column_text(s, column);
    return text != nullptr ? std::string(reinterpret_cast<const char*>(text)) : std::string();
}

struct EffectiveValues
{
    std::string type, bpm, keyTonic, keyMode, genre;
};

// Uses SQLite's quote() so an unknown (NULL) value is distinguishable from an empty string.
EffectiveValues effectiveValues(const juce::File& file, int64_t id)
{
    sqlite3* raw = nullptr;
    REQUIRE(sqlite3_open(file.getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
    sqlite3_stmt* s = nullptr;
    REQUIRE(sqlite3_prepare_v2(raw,
        "SELECT quote(eff_type), quote(eff_bpm), quote(eff_key_tonic), quote(eff_key_mode),"
        " quote(eff_genre) FROM samples WHERE id=?", -1, &s, nullptr) == SQLITE_OK);
    sqlite3_bind_int64(s, 1, id);
    REQUIRE(sqlite3_step(s) == SQLITE_ROW);
    EffectiveValues values;
    values.type = columnText(s, 0);
    values.bpm = columnText(s, 1);
    values.keyTonic = columnText(s, 2);
    values.keyMode = columnText(s, 3);
    values.genre = columnText(s, 4);
    sqlite3_finalize(s); sqlite3_close(raw);
    return values;
}

bool hasColumn(const juce::File& file, const char* table, const char* column)
{
    sqlite3* raw = nullptr;
    REQUIRE(sqlite3_open(file.getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
    auto sql = std::string("PRAGMA table_info(") + table + ")";
    sqlite3_stmt* s = nullptr;
    REQUIRE(sqlite3_prepare_v2(raw, sql.c_str(), -1, &s, nullptr) == SQLITE_OK);
    bool found = false;
    while (sqlite3_step(s) == SQLITE_ROW)
        if (columnText(s, 1) == column) found = true;
    sqlite3_finalize(s); sqlite3_close(raw);
    return found;
}

int rawScalar(const juce::File& file, const char* sql)
{
    sqlite3* raw = nullptr;
    REQUIRE(sqlite3_open(file.getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
    sqlite3_stmt* s = nullptr;
    REQUIRE(sqlite3_prepare_v2(raw, sql, -1, &s, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(s) == SQLITE_ROW);
    const int value = sqlite3_column_int(s, 0);
    sqlite3_finalize(s); sqlite3_close(raw);
    return value;
}

int64_t addSample(SampleRepository& repo, const juce::String& name)
{
    SampleRecord r;
    r.filePath = "/lib/" + name;
    r.fileName = name;
    r.fileHash = "h-" + name;
    return repo.upsert(r);
}

SmartCandidate candidate(SmartField field, const juce::String& source, const juce::String& text,
                         double number = 0)
{
    SmartCandidate c;
    c.field = field;
    c.source = source;
    c.textValue = text;
    c.numericValue = number;
    c.status = CandidateStatus::Valid;
    return c;
}

void publish(SampleRepository& repo, int64_t id, const juce::String& type, double bpm,
             const juce::String& key, const juce::String& genre)
{
    SmartPublication publication;
    publication.sampleId = id;
    publication.expectedRevision = repo.smartRevision(id);
    publication.candidates = { candidate(SmartField::Type, "filename", type),
                               candidate(SmartField::Bpm, "filename", {}, bpm),
                               candidate(SmartField::Key, "filename", key),
                               candidate(SmartField::Genre, "folder", genre) };
    publication.catalogPath = repo.catalogPath();
    REQUIRE(repo.publishSmartAnalysis(publication).published);
}
}

TEST_CASE("Effective sort values follow publish, override and clear", "[sort][schema][database]")
{
    TestDirectory t;
    const auto file = t.dir.getChildFile("sort.sqlite");
    Database db(file); SampleRepository repo(db); repo.createTables();
    const auto id = addSample(repo, "a.wav");

    const auto unknown = effectiveValues(file, id);
    REQUIRE(unknown.type == "NULL");
    REQUIRE(unknown.bpm == "NULL");
    REQUIRE(unknown.keyTonic == "NULL");
    REQUIRE(unknown.keyMode == "NULL");
    REQUIRE(unknown.genre == "NULL");

    publish(repo, id, "kick", 128, "C minor", "Deep House");
    const auto published = effectiveValues(file, id);
    REQUIRE(published.type == "'kick'");
    REQUIRE(published.bpm == "128.0");
    REQUIRE(published.keyTonic == "0");
    REQUIRE(published.keyMode == "'minor'");
    REQUIRE(published.genre == "'Deep House'");

    SmartOverride type; type.textValue = "bass";
    SmartOverride bpm; bpm.numericValue = 120;
    SmartOverride key; key.textValue = "D minor";
    SmartOverride genre; genre.textValue = "Jazz";
    repo.setSmartOverride(id, SmartField::Type, type);
    repo.setSmartOverride(id, SmartField::Bpm, bpm);
    repo.setSmartOverride(id, SmartField::Key, key);
    repo.setSmartOverride(id, SmartField::Genre, genre);
    const auto overridden = effectiveValues(file, id);
    REQUIRE(overridden.type == "'bass'");
    REQUIRE(overridden.bpm == "120.0");
    REQUIRE(overridden.keyTonic == "2");
    REQUIRE(overridden.keyMode == "'minor'");
    REQUIRE(overridden.genre == "'Jazz'");

    repo.clearSmartOverride(id, SmartField::Type);
    repo.clearSmartOverride(id, SmartField::Bpm);
    repo.clearSmartOverride(id, SmartField::Key);
    repo.clearSmartOverride(id, SmartField::Genre);
    const auto restored = effectiveValues(file, id);
    REQUIRE(restored.type == "'kick'");
    REQUIRE(restored.bpm == "128.0");
    REQUIRE(restored.keyTonic == "0");
    REQUIRE(restored.genre == "'Deep House'");

    // An explicit unknown override is NULL, not the automatic value.
    SmartOverride unknownGenre; unknownGenre.cleared = true;
    repo.setSmartOverride(id, SmartField::Genre, unknownGenre);
    REQUIRE(effectiveValues(file, id).genre == "NULL");
}

TEST_CASE("Sorting uses the effective-value indexes", "[sort][schema][database]")
{
    TestDirectory t;
    const auto file = t.dir.getChildFile("sort-plan.sqlite");
    Database db(file); SampleRepository repo(db); repo.createTables();
    publish(repo, addSample(repo, "a.wav"), "kick", 128, "C minor", "House");
    publish(repo, addSample(repo, "b.wav"), "pad", 90, "A minor", "Trap");

    for (const auto* index : { "samples_eff_type", "samples_eff_bpm", "samples_eff_key",
                               "samples_eff_genre", "samples_format", "samples_duration",
                               "samples_rate", "samples_channels", "samples_status" })
    {
        INFO(index);
        REQUIRE(rawScalar(file,
            ("SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name='" + std::string(index) + "'").c_str()) == 1);
    }

    sqlite3* raw = nullptr;
    REQUIRE(sqlite3_open(file.getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
    auto planFor = [&raw](const char* orderBy)
    {
        auto sql = std::string("EXPLAIN QUERY PLAN SELECT id FROM samples ORDER BY ") + orderBy;
        sqlite3_stmt* s = nullptr;
        REQUIRE(sqlite3_prepare_v2(raw, sql.c_str(), -1, &s, nullptr) == SQLITE_OK);
        std::string plan;
        while (sqlite3_step(s) == SQLITE_ROW) { plan += columnText(s, 3); plan += "\n"; }
        sqlite3_finalize(s);
        return plan;
    };
    REQUIRE(planFor("(eff_type IS NULL) ASC, eff_type COLLATE NOCASE ASC, id ASC").find("samples_eff_type") != std::string::npos);
    REQUIRE(planFor("(eff_bpm IS NULL) ASC, eff_bpm ASC, id ASC").find("samples_eff_bpm") != std::string::npos);
    REQUIRE(planFor("(eff_key_tonic IS NULL) ASC, eff_key_tonic ASC, eff_key_mode COLLATE NOCASE ASC, id ASC").find("samples_eff_key") != std::string::npos);
    REQUIRE(planFor("(eff_genre IS NULL) ASC, eff_genre COLLATE NOCASE ASC, id ASC").find("samples_eff_genre") != std::string::npos);
    sqlite3_close(raw);
}

TEST_CASE("Schema 17 upgrades in place and backfills the effective values", "[sort][schema][migration]")
{
    TestDirectory t;
    const auto file = t.dir.getChildFile("sort-upgrade.sqlite");
    int64_t id = 0;
    {
        Database db(file); SampleRepository repo(db); repo.createTables();
        id = addSample(repo, "upgrade.wav");
        publish(repo, id, "kick", 128, "C minor", "House");
        SmartOverride type; type.textValue = "bass";
        repo.setSmartOverride(id, SmartField::Type, type);
    }

    // Rewind the catalog to what a schema-17 build left behind: the columns and their indexes
    // do not exist yet, and nothing knows the effective values.
    {
        sqlite3* raw = nullptr;
        REQUIRE(sqlite3_open(file.getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
        for (const auto* index : { "samples_eff_type", "samples_eff_bpm", "samples_eff_key",
                                   "samples_eff_genre", "samples_format", "samples_duration",
                                   "samples_rate", "samples_channels", "samples_status" })
            execRaw(raw, (std::string("DROP INDEX IF EXISTS ") + index + ";").c_str());
        for (const auto* column : { "eff_type", "eff_bpm", "eff_key_tonic", "eff_key_mode", "eff_genre" })
            execRaw(raw, (std::string("ALTER TABLE samples DROP COLUMN ") + column + ";").c_str());
        execRaw(raw, "PRAGMA user_version=17;");
        sqlite3_close(raw);
    }
    REQUIRE_FALSE(hasColumn(file, "samples", "eff_type"));

    Database db(file); SampleRepository repo(db); repo.createTables();
    REQUIRE(hasColumn(file, "samples", "eff_type"));
    REQUIRE(hasColumn(file, "samples", "eff_bpm"));
    REQUIRE(hasColumn(file, "samples", "eff_key_tonic"));
    REQUIRE(hasColumn(file, "samples", "eff_key_mode"));
    REQUIRE(hasColumn(file, "samples", "eff_genre"));
    REQUIRE(rawScalar(file, "PRAGMA user_version") == 18);
    const auto values = effectiveValues(file, id);
    REQUIRE(values.type == "'bass'");       // the manual override is the effective value
    REQUIRE(values.bpm == "128.0");
    REQUIRE(values.keyTonic == "0");
    REQUIRE(values.keyMode == "'minor'");
    REQUIRE(values.genre == "'House'");
    // The other catalog data is untouched by the upgrade.
    REQUIRE(repo.findById(id)->fileHash == "h-upgrade.wav");
}
