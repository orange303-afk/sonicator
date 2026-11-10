#include <catch2/catch_test_macros.hpp>
#include "core/database/SampleRepository.h"
#include "core/smart/SmartResolver.h"
#include "TestHelpers.h"
#include <sqlite3.h>
using namespace sonicator;
namespace {
void rawExec(const juce::File& file, const char* sql)
{
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(file.getFullPathName().toRawUTF8(), &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
}
int rawScalar(const juce::File& file, const char* sql)
{
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(file.getFullPathName().toRawUTF8(), &db) == SQLITE_OK);
    sqlite3_stmt* s = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, sql, -1, &s, nullptr) == SQLITE_OK);
    const int value = sqlite3_step(s) == SQLITE_ROW ? sqlite3_column_int(s, 0) : -1;
    sqlite3_finalize(s);
    sqlite3_close(db);
    return value;
}
SmartCandidate candidate(SmartField field, juce::String source, juce::String text, double number)
{
    SmartCandidate c; c.field = field; c.source = std::move(source); c.textValue = std::move(text);
    c.numericValue = number;
    return c;
}
}
TEST_CASE("Schema 5 catalogs upgrade to current schema with a backup and keep identity", "[smart][database]")
{
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("sonicator-smart-upgrade", ".sqlite");
    int64_t id = 0;
    {
        Database db(file); SampleRepository repo(db); repo.createTables();
        SampleRecord r; r.filePath = "/lib/kick.wav"; r.fileName = "kick.wav";
        r.fileHash = "h"; r.version = basicFeaturesVersion;
        id = repo.upsert(r);
        repo.setStarred({ id }, true);
        const auto tag = repo.createTag("Phase2", 0xff00ff00);
        repo.setSampleTag(id, tag, true);
    }
    rawExec(file, "PRAGMA user_version=5;");
    {
        Database db(file); SampleRepository repo(db); repo.createTables();
        REQUIRE(rawScalar(file, "SELECT user_version FROM pragma_user_version") == 12);
        auto r = repo.findByPath("/lib/kick.wav");
        REQUIRE(r.has_value());
        REQUIRE(r->id == id);
        REQUIRE(r->starred);
        REQUIRE(repo.tagsForSample(id).size() == 1);
        bool backup = false;
        for (const auto& entry : file.getParentDirectory().findChildFiles(juce::File::findFiles, false,
                 file.getFileName() + ".backup-*"))
            backup = entry.getFileName().startsWith(file.getFileName() + ".backup-");
        REQUIRE(backup);
    }
    file.deleteFile();
}
TEST_CASE("Smart candidates are replaced atomically and results survive reopen", "[smart][database]")
{
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("sonicator-smart-store", ".sqlite");
    int64_t id = 0;
    {
        Database db(file); SampleRepository repo(db); repo.createTables();
        SampleRecord r; r.filePath = "/lib/loop.wav"; r.fileName = "loop.wav"; r.fileHash = "h";
        id = repo.upsert(r);
        repo.replaceSmartCandidates(id, { candidate(SmartField::Bpm, "filename", {}, 128),
                                          candidate(SmartField::Key, "filename", "C minor", 0) });
        REQUIRE(repo.smartCandidates(id).size() == 2);
        repo.replaceSmartCandidates(id, { candidate(SmartField::Bpm, "wav_acid", {}, 120) });
        REQUIRE(repo.smartCandidates(id).size() == 1);
        REQUIRE(repo.smartCandidates(id).front().source == "wav_acid");
        SmartResult result;
        result.bpm = 120; result.hasBpm = true; result.bpmState = SmartState::Resolved;
        result.keyTonic = 0; result.keyMode = "minor"; result.keyState = SmartState::Conflicting;
        result.explanation = "bpm: wav_acid=120";
        result.pipelineState = SmartPipelineState::Complete;
        result.revision = 1;
        result.extractorVersion = smartExtractorsVersion;
        result.resolverVersion = smartResolverVersion;
        result.dspVersion = smartDspVersion;
        repo.setSmartResult(id, result);
        REQUIRE(repo.samplesNeedingSmartAnalysis(0, 10).empty());
    }
    {
        Database db(file); SampleRepository repo(db); repo.createTables();
        auto result = repo.smartResult(id);
        REQUIRE(result.has_value());
        REQUIRE(result->bpm == 120);
        REQUIRE(result->hasBpm);
        REQUIRE(result->keyTonic == 0);
        REQUIRE(result->keyMode == "minor");
        REQUIRE(result->keyState == SmartState::Conflicting);
        REQUIRE(result->explanation == "bpm: wav_acid=120");
    }
    file.deleteFile();
}
TEST_CASE("Overrides survive candidate replacement and can be cleared", "[smart][database]")
{
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("sonicator-smart-override", ".sqlite");
    {
        Database db(file); SampleRepository repo(db); repo.createTables();
        SampleRecord r; r.filePath = "/lib/snare.wav"; r.fileName = "snare.wav"; r.fileHash = "h";
        const auto id = repo.upsert(r);
        SmartOverride overrideValue; overrideValue.textValue = "D minor";
        repo.setSmartOverride(id, SmartField::Key, overrideValue);
        repo.replaceSmartCandidates(id, { candidate(SmartField::Key, "filename", "C major", 0) });
        auto overrides = repo.smartOverrides(id);
        REQUIRE(overrides.count(SmartField::Key) == 1);
        REQUIRE(overrides[SmartField::Key].textValue == "D minor");
        auto result = resolveSmart(repo.smartCandidates(id), overrides);
        REQUIRE(result.keyTonic == 2);
        REQUIRE(result.keyState == SmartState::Resolved);
        REQUIRE(result.keyOverride == OverrideState::Value);
        repo.clearSmartOverride(id, SmartField::Key);
        REQUIRE(repo.smartOverrides(id).empty());
    }
    file.deleteFile();
}
TEST_CASE("Deleting a sample removes its smart rows", "[smart][database]")
{
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("sonicator-smart-cascade", ".sqlite");
    Database db(file); SampleRepository repo(db); repo.createTables();
    SampleRecord r; r.filePath = "/lib/x.wav"; r.fileName = "x.wav"; r.fileHash = "h";
    const auto id = repo.upsert(r);
    repo.replaceSmartCandidates(id, { candidate(SmartField::Bpm, "filename", {}, 100) });
    SmartResult result; result.bpm = 100; result.hasBpm = true; repo.setSmartResult(id, result);
    rawExec(file, ("PRAGMA foreign_keys=ON; DELETE FROM samples WHERE id=" + juce::String(id) + ";").toRawUTF8());
    REQUIRE(repo.smartCandidates(id).empty());
    REQUIRE_FALSE(repo.smartResult(id).has_value());
    file.deleteFile();
}

TEST_CASE("SampleRepository custom types and ml feedback storage", "[smart][database]")
{
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("sonicator-custom-types-test", ".sqlite");
    {
        Database db(file);
        SampleRepository repo(db);
        repo.createTables();

        CHECK(repo.customTypes().empty());
        CHECK(repo.addCustomType("Sub-Drop"));
        CHECK(repo.addCustomType("Glitch"));
        CHECK_FALSE(repo.addCustomType("sub-drop")); // duplicate case-insensitive
        auto types = repo.customTypes();
        CHECK(types.size() == 2);

        std::vector<float> features(14, 0.5f);
        repo.recordFeedback(101, "Sub-Drop", {"bass", "heavy"}, features);
        auto all = repo.allFeedback();
        REQUIRE(all.size() == 1);
        CHECK(all[0].sampleId == 101);
        CHECK(all[0].targetType == "Sub-Drop");
        CHECK(all[0].tags.size() == 2);
        CHECK(all[0].features.size() == 14);

        repo.removeFeedback(101);
        CHECK(repo.allFeedback().empty());
    }
    file.deleteFile();
}

