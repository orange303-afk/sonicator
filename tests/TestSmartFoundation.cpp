#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/database/SampleRepository.h"
#include "core/smart/SmartAnalysisService.h"
#include "core/smart/SmartResolver.h"
#include "core/smart/RhythmCandidates.h"
#include "core/smart/KeyNames.h"
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
SmartCandidate candidate(SmartField field, juce::String source, juce::String text, double number,
                         CandidateStatus status = CandidateStatus::Valid)
{
    SmartCandidate c;
    c.field = field; c.source = std::move(source); c.textValue = std::move(text);
    c.numericValue = number; c.status = status;
    c.extractorId = source; c.extractorVersion = smartExtractorsVersion;
    return c;
}
SampleRecord recordFor(const juce::File& file)
{
    SampleRecord r;
    r.filePath = file.getFullPathName(); r.fileName = file.getFileName();
    r.fileSize = file.getSize(); r.dateModified = file.getLastModificationTime().toMilliseconds();
    r.fileHash = "h"; r.version = basicFeaturesVersion;
    return r;
}
struct Bytes
{
    std::vector<juce::uint8> v;
    void ascii(const juce::String& s) { v.insert(v.end(), s.toRawUTF8(), s.toRawUTF8() + s.getNumBytesAsUTF8()); }
    void u16le(juce::uint16 x) { v.push_back(x & 0xff); v.push_back(x >> 8); }
    void u32le(juce::uint32 x) { for (int i = 0; i < 4; ++i) v.push_back((x >> (8 * i)) & 0xff); }
    void f32le(float x) { juce::uint32 b; std::memcpy(&b, &x, 4); u32le(b); }
    void f64(double x) { juce::uint64 b; std::memcpy(&b, &x, 8); for (int i = 0; i < 8; ++i) v.push_back((b >> (8 * i)) & 0xff); }
};
void chunk(Bytes& out, const char* id, Bytes payload)
{
    out.ascii(id); out.u32le((juce::uint32) payload.v.size());
    out.v.insert(out.v.end(), payload.v.begin(), payload.v.end());
    if (payload.v.size() & 1) out.v.push_back(0);
}
juce::File writeAcidWav(TestDirectory& t, const juce::String& name, float tempo)
{
    Bytes fmt; fmt.u16le(1); fmt.u16le(1); fmt.u32le(44100); fmt.u32le(88200); fmt.u16le(2); fmt.u16le(16);
    Bytes data; data.v.resize(4000, 0);
    Bytes acid; acid.u32le(0); acid.u16le(60); acid.u16le(0); acid.u32le(0);
    acid.u32le(8); acid.u16le(4); acid.u16le(4); acid.f32le(tempo);
    Bytes body; chunk(body, "fmt ", fmt); chunk(body, "data", data); chunk(body, "acid", acid);
    Bytes file; file.ascii("RIFF"); file.u32le((juce::uint32) body.v.size() + 4); file.ascii("WAVE");
    file.v.insert(file.v.end(), body.v.begin(), body.v.end());
    auto path = t.dir.getChildFile(name);
    path.replaceWithData(file.v.data(), file.v.size());
    return path;
}
juce::File writeAsd(TestDirectory& t, const juce::String& name, double bpm)
{
    Bytes b;
    auto marker = [&](int index, double seconds, double beats)
    {
        b.ascii("WarpMarker"); b.u32le((juce::uint32) index); b.f64(seconds); b.f64(beats);
    };
    marker(0, 0.0, 0.0);
    marker(1, 2.0, 2.0 * bpm / 60.0);
    Bytes file;
    file.v.push_back(6); file.v.push_back('I'); file.u16le(3); file.u32le(0); file.u32le(1); file.u16le(0);
    file.u32le(12345); file.ascii("SampleData");
    file.v.insert(file.v.end(), b.v.begin(), b.v.end());
    auto path = t.dir.getChildFile(name);
    path.replaceWithData(file.v.data(), file.v.size());
    return path;
}
}
TEST_CASE("Schema 6 upgrades to 7 keeping smart results, overrides, tags and stars", "[smart][foundation]")
{
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("sonicator-foundation-upgrade", ".sqlite");
    int64_t id = 0;
    {
        Database db(file); SampleRepository repo(db); repo.createTables();
        SampleRecord r; r.filePath = "/lib/loop.wav"; r.fileName = "loop.wav";
        r.fileHash = "hash"; r.version = basicFeaturesVersion; r.duration = 2.5;
        id = repo.upsert(r);
        repo.setStarred({ id }, true);
        const auto tag = repo.createTag("Keep", 0xff00ff00);
        repo.setSampleTag(id, tag, true);
        repo.replaceSmartCandidates(id, { candidate(SmartField::Bpm, "filename", {}, 128) });
        repo.markSmartStale(id); // a result row exists before the first publication
        SmartOverride overrideValue; overrideValue.numericValue = 126;
        repo.setSmartOverride(id, SmartField::Bpm, overrideValue);
    }
    rawExec(file, "PRAGMA user_version=6;");
    {
        Database db(file); SampleRepository repo(db); repo.createTables();
        REQUIRE(rawScalar(file, "SELECT user_version FROM pragma_user_version") == 12);
        auto result = repo.smartResult(id);
        REQUIRE(result.has_value());
        REQUIRE(result->hasBpm);
        REQUIRE(result->bpm == Catch::Approx(126.0)); // the override is the effective value
        REQUIRE(result->bpmOverride == OverrideState::Value);
        REQUIRE(repo.smartOverrides(id).count(SmartField::Bpm) == 1);
        REQUIRE(repo.findByPath("/lib/loop.wav")->starred);
        REQUIRE(repo.tagsForSample(id).size() == 1);
        REQUIRE(repo.findByPath("/lib/loop.wav")->fileHash == "hash");
        REQUIRE(repo.findByPath("/lib/loop.wav")->duration == Catch::Approx(2.5));
    }
    file.deleteFile();
}
TEST_CASE("Publication is atomic, revision-guarded and catalog-guarded", "[smart][foundation]")
{
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("sonicator-foundation-publish", ".sqlite");
    Database db(file); SampleRepository repo(db); repo.createTables();
    SampleRecord r; r.filePath = "/lib/a.wav"; r.fileName = "a.wav"; r.fileHash = "h";
    const auto id = repo.upsert(r);
    SmartPublication publication;
    publication.sampleId = id;
    publication.candidates = { candidate(SmartField::Bpm, "wav_acid", {}, 120),
                               candidate(SmartField::Bpm, "filename", {}, 128) };
    publication.inputs = { { "audio", r.filePath, 10, 20 } };
    publication.extractions = { { "wav", smartExtractorsVersion, "ok", {}, 1 } };
    publication.catalogPath = repo.catalogPath();
    auto first = repo.publishSmartAnalysis(publication);
    REQUIRE(first.published);
    REQUIRE(first.revision == 1);
    REQUIRE(first.result.bpmState == SmartState::Conflicting);
    REQUIRE(first.result.extractorVersion == smartExtractorsVersion);
    REQUIRE(repo.smartCandidates(id).size() == 2);
    REQUIRE(repo.smartInputs(id).size() == 1);
    REQUIRE(repo.smartExtractions(id).size() == 1);
    REQUIRE(first.result.generation > 0);
    // A job that read the previous revision must not overwrite the newer result.
    publication.expectedRevision = 0;
    publication.candidates = { candidate(SmartField::Bpm, "filename", {}, 90) };
    auto stale = repo.publishSmartAnalysis(publication);
    REQUIRE_FALSE(stale.published);
    REQUIRE(stale.reason == "revision-changed");
    REQUIRE(repo.smartCandidates(id).size() == 2);
    REQUIRE(repo.smartResult(id)->bpm == Catch::Approx(120.0));
    // A job from another catalog is refused.
    publication.expectedRevision = 1;
    publication.catalogPath = "/tmp/another-catalog.sqlite";
    auto wrongCatalog = repo.publishSmartAnalysis(publication);
    REQUIRE_FALSE(wrongCatalog.published);
    REQUIRE(wrongCatalog.reason == "catalog-changed");
    // The correct revision publishes atomically.
    publication.catalogPath = repo.catalogPath();
    auto second = repo.publishSmartAnalysis(publication);
    REQUIRE(second.published);
    REQUIRE(second.revision == 2);
    REQUIRE(repo.smartCandidates(id).size() == 1);
    file.deleteFile();
}
TEST_CASE("An override edited during a job wins the race and survives", "[smart][foundation]")
{
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("sonicator-foundation-race", ".sqlite");
    Database db(file); SampleRepository repo(db); repo.createTables();
    SampleRecord r; r.filePath = "/lib/b.wav"; r.fileName = "b.wav"; r.fileHash = "h";
    const auto id = repo.upsert(r);
    SmartPublication publication;
    publication.sampleId = id;
    publication.candidates = { candidate(SmartField::Bpm, "filename", {}, 128) };
    publication.catalogPath = repo.catalogPath();
    REQUIRE(repo.publishSmartAnalysis(publication).published);
    const auto revisionReadByJob = repo.smartRevision(id);
    SmartOverride manual; manual.numericValue = 90;
    repo.setSmartOverride(id, SmartField::Bpm, manual); // bumps the revision
    publication.expectedRevision = revisionReadByJob;
    auto outcome = repo.publishSmartAnalysis(publication);
    REQUIRE_FALSE(outcome.published);
    REQUIRE(outcome.reason == "revision-changed");
    auto result = repo.smartResult(id);
    REQUIRE(result->bpm == Catch::Approx(90.0));
    REQUIRE(result->bpmOverride == OverrideState::Value);
    REQUIRE(repo.smartOverrides(id).count(SmartField::Bpm) == 1);
    file.deleteFile();
}
TEST_CASE("A failed analysis keeps the last completed values and records a structured error",
          "[smart][foundation]")
{
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("sonicator-foundation-error", ".sqlite");
    Database db(file); SampleRepository repo(db); repo.createTables();
    SampleRecord r; r.filePath = "/lib/c.wav"; r.fileName = "c.wav"; r.fileHash = "h";
    const auto id = repo.upsert(r);
    SmartPublication publication;
    publication.sampleId = id;
    publication.candidates = { candidate(SmartField::Bpm, "filename", {}, 100) };
    publication.catalogPath = repo.catalogPath();
    REQUIRE(repo.publishSmartAnalysis(publication).published);
    repo.markSmartError(id, "extract-error", "truncated chunk");
    auto errored = repo.smartResult(id);
    REQUIRE(errored.has_value());
    REQUIRE(errored->pipelineState == SmartPipelineState::Error);
    REQUIRE(errored->bpm == Catch::Approx(100.0)); // previous generation is retained
    REQUIRE(errored->errorCode == "extract-error");
    auto failure = repo.smartFailure(id);
    REQUIRE(failure.has_value());
    REQUIRE(failure->attempts == 1);
    REQUIRE(failure->message == "truncated chunk");
    // Retry includes the failed sample, and success clears the failure.
    REQUIRE_FALSE(repo.samplesNeedingSmartAnalysis(0, 10).empty());
    publication.expectedRevision = repo.smartRevision(id);
    publication.candidates = { candidate(SmartField::Bpm, "filename", {}, 110) };
    REQUIRE(repo.publishSmartAnalysis(publication).published);
    REQUIRE_FALSE(repo.smartFailure(id).has_value());
    REQUIRE(repo.samplesNeedingSmartAnalysis(0, 10).empty());
    file.deleteFile();
}
TEST_CASE("A changed sidecar is reprocessed in one incremental pass and a deleted one disappears",
          "[smart][foundation]")
{
    TestDirectory t;
    auto wav = writeAcidWav(t, "loop_128bpm.wav", 120.0f);
    auto asd = writeAsd(t, "loop_128bpm.wav.asd", 100.0);
    Database db(t.dir.getChildFile("smart-sidecar.sqlite"));
    SampleRepository repo(db); repo.createTables();
    const auto id = repo.upsert(recordFor(wav));
    SmartAnalysisService service(repo);
    REQUIRE(service.start());
    REQUIRE(service.waitUntilFinished(10000));
    REQUIRE(repo.smartInputs(id).size() == 2);
    auto first = repo.smartResult(id);
    REQUIRE(first.has_value());
    REQUIRE(first->pipelineState == SmartPipelineState::Complete);
    writeAsd(t, "loop_128bpm.wav.asd", 90.0);
    // Make the change unambiguous even on a coarse file system timestamp.
    asd.setLastModificationTime(juce::Time(juce::Time::currentTimeMillis() + 5000));
    REQUIRE(service.start());
    REQUIRE(service.waitUntilFinished(10000));
    auto changed = repo.smartCandidates(id);
    bool sawNewWarp = false;
    for (const auto& c : changed)
        if (c.source == "asd_warp" && std::abs(c.numericValue - 90.0) < 0.5) sawNewWarp = true;
    REQUIRE(sawNewWarp);
    REQUIRE(repo.smartResult(id)->pipelineState == SmartPipelineState::Complete);
    asd.deleteFile();
    REQUIRE(service.start());
    REQUIRE(service.waitUntilFinished(10000));
    REQUIRE(repo.smartInputs(id).size() == 1);
    for (const auto& c : repo.smartCandidates(id)) REQUIRE_FALSE(c.source.startsWith("asd_"));
}
TEST_CASE("A queued sample is analyzed without blocking the caller", "[smart][foundation]")
{
    TestDirectory t;
    auto wav = writeAcidWav(t, "queued.wav", 110.0f);
    Database db(t.dir.getChildFile("smart-queue.sqlite"));
    SampleRepository repo(db); repo.createTables();
    const auto id = repo.upsert(recordFor(wav));
    SmartAnalysisService service(repo);
    REQUIRE(service.requestSample(id));
    REQUIRE(service.waitUntilFinished(10000));
    auto result = repo.smartResult(id);
    REQUIRE(result.has_value());
    REQUIRE(result->pipelineState == SmartPipelineState::Complete);
    REQUIRE(result->bpm == Catch::Approx(110.0));
}
TEST_CASE("A real schema 6 database gains current schema columns in place", "[smart][foundation]")
{
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("sonicator-foundation-v6", ".sqlite");
    rawExec(file,
        "CREATE TABLE samples (id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE,"
        " name TEXT NOT NULL, hash TEXT NOT NULL, format TEXT NOT NULL, error TEXT NOT NULL,"
        " size INTEGER, modified INTEGER, analyzed INTEGER, duration REAL, rate REAL, bits INTEGER,"
        " channels INTEGER, version INTEGER, peak REAL, rms REAL, centroid REAL, rolloff REAL,"
        " flatness REAL, missing INTEGER, search_key TEXT NOT NULL DEFAULT '',"
        " starred INTEGER NOT NULL DEFAULT 0);"
        "CREATE TABLE smart_candidates (sample_id INTEGER NOT NULL, field TEXT NOT NULL,"
        " source TEXT NOT NULL, status TEXT NOT NULL, detail TEXT NOT NULL DEFAULT '',"
        " text_value TEXT NOT NULL DEFAULT '', numeric_value REAL NOT NULL DEFAULT 0,"
        " PRIMARY KEY(sample_id, field, source, detail)) WITHOUT ROWID;"
        "CREATE TABLE smart_results (sample_id INTEGER PRIMARY KEY, type TEXT NOT NULL DEFAULT '',"
        " structure TEXT NOT NULL DEFAULT '', bpm REAL NOT NULL DEFAULT 0,"
        " has_bpm INTEGER NOT NULL DEFAULT 0, key_tonic INTEGER NOT NULL DEFAULT -1,"
        " key_mode TEXT NOT NULL DEFAULT '', root_note INTEGER NOT NULL DEFAULT -1,"
        " type_state TEXT NOT NULL DEFAULT 'unknown', structure_state TEXT NOT NULL DEFAULT 'unknown',"
        " bpm_state TEXT NOT NULL DEFAULT 'unknown', key_state TEXT NOT NULL DEFAULT 'unknown',"
        " root_state TEXT NOT NULL DEFAULT 'unknown', explanation TEXT NOT NULL DEFAULT '',"
        " resolver_version INTEGER NOT NULL DEFAULT 1, updated_at INTEGER NOT NULL DEFAULT 0) WITHOUT ROWID;"
        "CREATE TABLE smart_overrides (sample_id INTEGER NOT NULL, field TEXT NOT NULL,"
        " cleared INTEGER NOT NULL DEFAULT 0, text_value TEXT NOT NULL DEFAULT '',"
        " numeric_value REAL NOT NULL DEFAULT 0, updated_at INTEGER NOT NULL DEFAULT 0,"
        " PRIMARY KEY(sample_id, field)) WITHOUT ROWID;"
        "CREATE TABLE smart_inputs (sample_id INTEGER NOT NULL, kind TEXT NOT NULL, path TEXT NOT NULL,"
        " size INTEGER NOT NULL DEFAULT 0, modified INTEGER NOT NULL DEFAULT 0,"
        " PRIMARY KEY(sample_id, kind, path)) WITHOUT ROWID;"
        "INSERT INTO samples(id,path,name,hash,format,error,version,starred)"
        " VALUES(7,'/lib/old.wav','old.wav','h','WAV','',1,1);"
        "INSERT INTO smart_candidates(sample_id,field,source,status,detail,text_value,numeric_value)"
        " VALUES(7,'bpm','filename','valid','128bpm','',128);"
        "INSERT INTO smart_candidates(sample_id,field,source,status,detail,text_value,numeric_value)"
        " VALUES(7,'key','filename','valid','Cmin','C minor',0);"
        "INSERT INTO smart_results(sample_id,bpm,has_bpm,bpm_state,key_tonic,key_mode,key_state)"
        " VALUES(7,128,1,'resolved',0,'minor','resolved');"
        "INSERT INTO smart_overrides(sample_id,field,cleared,text_value,numeric_value)"
        " VALUES(7,'bpm',0,'',126);"
        "PRAGMA user_version=6;");
    {
        Database db(file); SampleRepository repo(db); repo.createTables();
        REQUIRE(rawScalar(file, "SELECT user_version FROM pragma_user_version") == 12);
        auto result = repo.smartResult(7);
        REQUIRE(result.has_value());
        REQUIRE(result->bpm == Catch::Approx(128.0));
        REQUIRE(result->keyMode == "minor");
        // The old row has no recorded versions, so the next pass must analyze it.
        REQUIRE_FALSE(repo.samplesNeedingSmartAnalysis(0, 10).empty());
        // The override is reapplied from the stored candidates and stays effective.
        SmartOverride overrideValue; overrideValue.numericValue = 126;
        repo.setSmartOverride(7, SmartField::Bpm, overrideValue);
        REQUIRE(repo.smartResult(7)->bpm == Catch::Approx(126.0));
        REQUIRE(repo.smartCandidates(7).front().extractorVersion == 0);
        REQUIRE(repo.findByPath("/lib/old.wav")->starred);
        // Pre-v7 values stay visible as stale, never as "never analyzed".
        auto legacy = repo.smartDisplays({ 7 })[7];
        REQUIRE(legacy.pipelineState == SmartPipelineState::Stale);
        REQUIRE(legacy.hasBpm);
        REQUIRE(legacy.bpm == Catch::Approx(126.0)); // the stored override is effective
        REQUIRE(legacy.key == "C minor");
    }
    file.deleteFile();
}
TEST_CASE("Clearing an override restores the automatic value", "[smart][foundation]")
{
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("sonicator-foundation-reset", ".sqlite");
    Database db(file); SampleRepository repo(db); repo.createTables();
    SampleRecord r; r.filePath = "/lib/reset.wav"; r.fileName = "reset.wav"; r.fileHash = "h";
    const auto id = repo.upsert(r);
    SmartPublication publication;
    publication.sampleId = id;
    publication.candidates = { candidate(SmartField::Bpm, "filename", {}, 128) };
    publication.catalogPath = repo.catalogPath();
    REQUIRE(repo.publishSmartAnalysis(publication).published);
    SmartOverride manual; manual.numericValue = 90;
    repo.setSmartOverride(id, SmartField::Bpm, manual);
    REQUIRE(repo.smartResult(id)->bpm == Catch::Approx(90.0));
    REQUIRE(repo.smartResult(id)->bpmOverride == OverrideState::Value);
    repo.clearSmartOverride(id, SmartField::Bpm);
    auto restored = repo.smartResult(id);
    REQUIRE(restored->bpm == Catch::Approx(128.0));
    REQUIRE(restored->bpmOverride == OverrideState::None);
    REQUIRE(restored->pipelineState == SmartPipelineState::Complete);
    file.deleteFile();
}
TEST_CASE("A resolver version bump makes completed results candidates again", "[smart][foundation]")
{
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("sonicator-foundation-version", ".sqlite");
    Database db(file); SampleRepository repo(db); repo.createTables();
    SampleRecord r; r.filePath = "/lib/v.wav"; r.fileName = "v.wav"; r.fileHash = "h";
    const auto id = repo.upsert(r);
    SmartPublication publication;
    publication.sampleId = id;
    publication.candidates = { candidate(SmartField::Bpm, "filename", {}, 128) };
    publication.catalogPath = repo.catalogPath();
    REQUIRE(repo.publishSmartAnalysis(publication).published);
    REQUIRE(repo.samplesNeedingSmartAnalysis(0, 10).empty());
    auto old = *repo.smartResult(id);
    old.resolverVersion = smartResolverVersion - 1;
    repo.setSmartResult(id, old);
    REQUIRE(repo.samplesNeedingSmartAnalysis(0, 10).size() == 1);
    file.deleteFile();
}
namespace {
std::vector<int64_t> idsOf(const LibraryPage& page)
{
    std::vector<int64_t> ids;
    for (const auto& sample : page.samples) ids.push_back(sample.id);
    return ids;
}
int64_t publishSmart(SampleRepository& repo, int64_t id, const juce::String& type,
                     double bpm, const juce::String& key)
{
    SmartPublication publication;
    publication.sampleId = id;
    publication.candidates = { candidate(SmartField::Type, "filename", type, 0),
                               candidate(SmartField::Bpm, "filename", {}, bpm),
                               candidate(SmartField::Key, "filename", key, 0) };
    publication.catalogPath = repo.catalogPath();
    REQUIRE(repo.publishSmartAnalysis(publication).published);
    return id;
}
}
TEST_CASE("Effective displays apply overrides and keep the automatic state", "[smart][columns]")
{
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("sonicator-smart-display", ".sqlite");
    Database db(file); SampleRepository repo(db); repo.createTables();
    SampleRecord r; r.filePath = "/lib/display.wav"; r.fileName = "display.wav"; r.fileHash = "h";
    const auto id = repo.upsert(r);
    auto fresh = repo.smartDisplays({ id })[id];
    REQUIRE(fresh.pipelineState == SmartPipelineState::NotAnalyzed);
    REQUIRE(fresh.type.isEmpty());
    publishSmart(repo, id, "pad", 128, "C minor");
    SmartOverride type; type.textValue = "bass";
    repo.setSmartOverride(id, SmartField::Type, type);
    SmartOverride bpm; bpm.numericValue = 120;
    repo.setSmartOverride(id, SmartField::Bpm, bpm);
    SmartOverride key; key.textValue = "D minor";
    repo.setSmartOverride(id, SmartField::Key, key);
    auto display = repo.smartDisplays({ id })[id];
    REQUIRE(display.type == "bass");
    REQUIRE(display.typeOverride == OverrideState::Value);
    REQUIRE(display.typeState == SmartState::Resolved); // automated state is untouched
    REQUIRE(display.hasBpm);
    REQUIRE(display.bpm == Catch::Approx(120.0));
    REQUIRE(display.bpmOverride == OverrideState::Value);
    REQUIRE(display.key == "D minor");
    REQUIRE(display.keyOverride == OverrideState::Value);
    REQUIRE(display.pipelineState == SmartPipelineState::Complete);
    repo.clearSmartOverride(id, SmartField::Type);
    REQUIRE(repo.smartDisplays({ id })[id].type == "pad");
    file.deleteFile();
}
TEST_CASE("Whole-catalog sort uses effective Type, BPM and Key with unknown last", "[smart][columns]")
{
    TestDirectory t; Database db(t.dir.getChildFile("smart-sort.sqlite"));
    SampleRepository repo(db); repo.createTables();
    auto add = [&repo](const juce::String& name)
    {
        SampleRecord r; r.filePath = "/lib/" + name; r.fileName = name; r.fileHash = "h";
        return repo.upsert(r);
    };
    const auto a = add("a.wav"); // kick, 128, C minor
    const auto b = add("b.wav"); // pad, 90, A minor
    const auto c = add("c.wav"); // no smart result
    const auto d = add("d.wav"); // effective bass, 120, D minor through overrides
    publishSmart(repo, a, "kick", 128, "C minor");
    publishSmart(repo, b, "pad", 90, "A minor");
    publishSmart(repo, d, "pad", 100, "A minor");
    SmartOverride type; type.textValue = "bass";
    repo.setSmartOverride(d, SmartField::Type, type);
    SmartOverride bpm; bpm.numericValue = 120;
    repo.setSmartOverride(d, SmartField::Bpm, bpm);
    SmartOverride key; key.textValue = "D minor";
    repo.setSmartOverride(d, SmartField::Key, key);
    LibraryFilter filter;
    filter.sort = LibrarySort::Type; filter.ascending = true;
    REQUIRE(idsOf(repo.query(filter)) == std::vector<int64_t>{ d, a, b, c });
    filter.sort = LibrarySort::Bpm; filter.ascending = true;
    REQUIRE(idsOf(repo.query(filter)) == std::vector<int64_t>{ b, d, a, c });
    filter.sort = LibrarySort::Bpm; filter.ascending = false;
    REQUIRE(idsOf(repo.query(filter)) == std::vector<int64_t>{ a, d, b, c });
    filter.sort = LibrarySort::Key; filter.ascending = true;
    REQUIRE(idsOf(repo.query(filter)) == std::vector<int64_t>{ a, d, b, c });
    filter.sort = LibrarySort::Key; filter.ascending = false;
    REQUIRE(idsOf(repo.query(filter)) == std::vector<int64_t>{ b, d, a, c });
}
TEST_CASE("A key display keeps a tonic without an invented mode", "[smart][columns]")
{
    REQUIRE(keyDisplayName(9, "minor") == "A minor");
    REQUIRE(keyDisplayName(9, "") == "A");
    REQUIRE(keyDisplayName(9, "unknown") == "A");
}

TEST_CASE("MIDI note names and cents format for the selection bar", "[smart][display]")
{
    REQUIRE(midiNoteName(69) == "A4");
    REQUIRE(midiNoteName(61) == "C#4");
    REQUIRE(midiNoteName(60) == "C4");
    REQUIRE(midiNoteName(0) == "C-1");
    REQUIRE(midiNoteName(-1).isEmpty());
    REQUIRE(midiNoteName(128).isEmpty());
    REQUIRE(formatCents(12.0) == "+12c");
    REQUIRE(formatCents(-3.0) == "-3c");
    REQUIRE(formatCents(0.0) == "0c");
}

TEST_CASE("Smart displays expose resolved pitch and its override", "[smart][display]")
{
    TestDirectory t; Database db(t.dir.getChildFile("smart-pitch.sqlite"));
    SampleRepository repo(db); repo.createTables();
    SampleRecord r; r.filePath = "/lib/pitch.wav"; r.fileName = "pitch.wav"; r.fileHash = "h";
    const auto id = repo.upsert(r);
    SmartResult result;
    result.pipelineState = SmartPipelineState::Complete;
    result.keyTonic = 0; result.keyMode = "major"; result.keyState = SmartState::Resolved;
    result.pitchMidi = 69; result.pitchCents = 12.0; result.pitchState = SmartState::Resolved;
    result.pitchSource = "dsp";
    repo.setSmartResult(id, result);
    auto display = repo.smartDisplays({ id })[id];
    REQUIRE(display.hasPitch);
    REQUIRE(display.pitchMidi == 69);
    REQUIRE(display.pitchCents == Catch::Approx(12.0));
    REQUIRE(display.pitchState == SmartState::Resolved);

    SmartOverride pitch;
    pitch.numericValue = 62;
    pitch.auxValue = 5.0;
    repo.setSmartOverride(id, SmartField::Pitch, pitch);
    display = repo.smartDisplays({ id })[id];
    REQUIRE(display.pitchOverride == OverrideState::Value);
    REQUIRE(display.pitchMidi == 62);
    REQUIRE(display.pitchCents == Catch::Approx(5.0));
}

TEST_CASE("Pitch display text carries the state markers", "[smart][display]")
{
    SmartDisplay display;
    display.pipelineState = SmartPipelineState::Complete;
    display.pitchState = SmartState::Resolved;
    display.hasPitch = true;
    display.pitchMidi = 69;
    display.pitchCents = 12.0;
    REQUIRE(pitchDisplayText(display) == "A4 +12c");
    display.pitchCents = 0.0;
    REQUIRE(pitchDisplayText(display) == "A4");
    display.pitchState = SmartState::Conflicting;
    REQUIRE(pitchDisplayText(display) == "A4 (conflict)");
    display.pitchState = SmartState::Unknown;
    display.hasPitch = false;
    REQUIRE(pitchDisplayText(display) == "?");
    display.pipelineState = SmartPipelineState::NotAnalyzed;
    REQUIRE(pitchDisplayText(display) == "--");
    display.pipelineState = SmartPipelineState::Complete;
    display.pitchOverride = OverrideState::Unknown;
    REQUIRE(pitchDisplayText(display) == "manual: unknown");
    display.pitchOverride = OverrideState::NotApplicable;
    REQUIRE(pitchDisplayText(display) == "n/a");
}


namespace {
const SmartExtraction* findExtraction(const SmartCollection& collection, const juce::String& source)
{
    for (const auto& extraction : collection.extractions)
        if (extraction.source == source) return &extraction;
    return nullptr;
}
const SmartCandidate* findCandidate(const SmartCollection& collection, SmartField field,
                                    const juce::String& source)
{
    for (const auto& candidate : collection.candidates)
        if (candidate.field == field && candidate.source == source) return &candidate;
    return nullptr;
}
}

TEST_CASE("The smart pass adds a DSP key when metadata has none", "[smart][dsp]")
{
    TestDirectory t;
    const auto file = t.wave("loop.wav", 440.0, 1, 88200);
    const auto collection = SmartAnalysisService::collect(recordFor(file));
    const auto* key = findCandidate(collection, SmartField::Key, "dsp");
    INFO("dsp error: " << (findExtraction(collection, "dsp") != nullptr
                               ? findExtraction(collection, "dsp")->error : juce::String()));
    REQUIRE(key != nullptr);
    REQUIRE(key->textValue == "A");
    REQUIRE(key->status == CandidateStatus::Valid);
}

TEST_CASE("A filename key suppresses the DSP key", "[smart][dsp]")
{
    TestDirectory t;
    const auto file = t.wave("loop_Cm.wav", 440.0, 1, 88200);
    const auto collection = SmartAnalysisService::collect(recordFor(file));
    REQUIRE(findCandidate(collection, SmartField::Key, "dsp") == nullptr);
}

TEST_CASE("A one-shot without pitch metadata gets a DSP pitch", "[smart][dsp]")
{
    TestDirectory t;
    const auto file = t.wave("bass_oneshot.wav", 440.0, 1, 44100);
    const auto collection = SmartAnalysisService::collect(recordFor(file));
    const auto* pitch = findCandidate(collection, SmartField::Pitch, "dsp");
    INFO("dsp error: " << (findExtraction(collection, "dsp") != nullptr
                               ? findExtraction(collection, "dsp")->error : juce::String()));
    REQUIRE(pitch != nullptr);
    REQUIRE(pitch->numericValue == Catch::Approx(69.0).margin(0.05));
    REQUIRE(findCandidate(collection, SmartField::Key, "dsp") == nullptr);
}

TEST_CASE("A loop never gets a DSP pitch", "[smart][dsp]")
{
    TestDirectory t;
    const auto file = t.wave("bass_loop.wav", 440.0, 1, 88200);
    const auto collection = SmartAnalysisService::collect(recordFor(file));
    REQUIRE(findCandidate(collection, SmartField::Pitch, "dsp") == nullptr);
}

TEST_CASE("A truncated rhythm pass keeps its limitation note", "[smart][dsp]")
{
    RhythmAnalysis analysis;
    analysis.kind = RhythmKind::RhythmicLoop;
    analysis.tempo = RhythmTempo::Determined;
    analysis.bpm = 120.0;
    analysis.explanation = "loop";
    const auto extraction = rhythmCandidatesFrom(analysis, true, 60.0);
    REQUIRE(extraction.candidates.size() == 2);
    REQUIRE(extraction.candidates[0].limitation.contains("first 60 s"));
    REQUIRE(extraction.candidates[0].detail.contains("first 60 s"));
    REQUIRE(extraction.candidates[1].limitation.contains("first 60 s"));
}

TEST_CASE("A drum path never gets a DSP key", "[smart][dsp]")
{
    TestDirectory t;
    const auto file = t.wave("drum_loop.wav", 440.0, 1, 88200);
    const auto collection = SmartAnalysisService::collect(recordFor(file));
    REQUIRE(findCandidate(collection, SmartField::Key, "dsp") == nullptr);
}

TEST_CASE("The DSP extraction is skipped when metadata answers", "[smart][dsp]")
{
    TestDirectory t;
    const auto file = t.wave("bass_loop_128bpm_Cm.wav", 440.0, 1, 88200);
    const auto collection = SmartAnalysisService::collect(recordFor(file));
    const auto* extraction = findExtraction(collection, "dsp");
    REQUIRE(extraction != nullptr);
    REQUIRE(extraction->status == "skipped");
    REQUIRE(findCandidate(collection, SmartField::Key, "dsp") == nullptr);
}

TEST_CASE("The DSP version is two after the tonal pass", "[smart][dsp]")
{
    REQUIRE(smartDspVersion == 2);
}

TEST_CASE("A corrupt audio file records an unsupported DSP pass", "[smart][dsp]")
{
    TestDirectory t;
    const auto file = t.dir.getChildFile("broken.wav");
    file.replaceWithText("this is not audio");
    const auto collection = SmartAnalysisService::collect(recordFor(file));
    const auto* extraction = findExtraction(collection, "dsp");
    REQUIRE(extraction != nullptr);
    REQUIRE(extraction->status == "unsupported");
}

