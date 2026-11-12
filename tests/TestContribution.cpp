#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/contribute/ContributionUploader.h"
#include "core/database/SampleRepository.h"
#include "core/settings/AppSettings.h"
#include "core/smart/SmartResolver.h"
#include "core/smart/NameHeuristics.h"
#include "TestHelpers.h"
#include <sqlite3.h>
using namespace sonicator;
using namespace sonicator::contribute;

namespace {
using FeedbackOrigin = SampleRepository::FeedbackOrigin;
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

std::vector<Record> oneRecord()
{
    Record record;
    record.recordId = "1c4e7a90-5d2b-4f68-b3a1-8e0f2d7c9b45";
    record.type = "kick";
    record.tags.addArray({ "acoustic", "mytag" });
    record.features.assign(14, 0.5f);
    record.features[0] = 0.12345f;
    record.features[1] = 2.0f;      // clamped
    record.key = "A minor";
    record.bpm = 128.04;
    record.hasBpm = true;
    record.updatedAt = 1700000000000LL;
    return { record };
}

Vocabulary vocabulary()
{
    Vocabulary out;
    out.types.addArray({ "kick", "snare" });
    out.tags.addArray({ "acoustic" });
    return out;
}
class FakeTransport final : public Transport
{
public:
    Response postResult { 204, {} }, removeResult { 204, {} };
    int posts = 0, removes = 0;
    juce::String lastUrl, lastBody;
    Response post(const juce::String& url, const juce::String& body) override
    {
        ++posts;
        lastUrl = url;
        lastBody = body;
        return postResult;
    }
    Response remove(const juce::String& url) override
    {
        ++removes;
        lastUrl = url;
        return removeResult;
    }
};
struct Harness
{
    FakeTransport* transport = nullptr;
    bool allowed = true;
    juce::String installId = "3f2a8c1e-7b4d-4a9e-9c2f-1d6b8a0e5f31";
    juce::String stored;
    int64_t watermark = 0;
    int64_t committed = -1;
    std::vector<Record> records = oneRecord();

    Uploader make()
    {
        auto fake = std::make_unique<FakeTransport>();
        transport = fake.get();
        Uploader::Source source;
        source.allowed = [this] { return allowed; };
        source.installId = [this] { return installId; };
        source.storeInstallId = [this](const juce::String& id) { installId = id; stored = id; };
        source.endpoint = [] { return juce::String("https://intake.test"); };
        source.watermark = [this] { return watermark; };
        source.commitWatermark = [this](int64_t value) { watermark = value; committed = value; };
        source.fetch = [this](int64_t after, int limit)
        {
            std::vector<Record> out;
            for (const auto& record : records)
                if (record.updatedAt > after && (int) out.size() < limit) out.push_back(record);
            return out;
        };
        source.vocabulary = [] { return vocabulary(); };
        return Uploader(std::move(fake), std::move(source), "0.4.2", "mac");
    }
};
} // namespace

TEST_CASE("Contribution batch carries numbers and labels only", "[contribute]")
{
    const auto batch = buildBatch(oneRecord(), vocabulary(),
        "3f2a8c1e-7b4d-4a9e-9c2f-1d6b8a0e5f31", "9b1d6c33-2e58-4f07-8a41-0c7d5e9b2a64",
        "0.4.2", "mac", juce::Time(2026, 11, 11, 10, 0, 0));
    REQUIRE(batch.recordCount == 1);
    CHECK(batch.watermark == 1700000000000LL);

    auto parsed = juce::JSON::parse(batch.json);
    auto* object = parsed.getDynamicObject();
    REQUIRE(object != nullptr);
    CHECK(object->getProperty("schema").toString() == "contribute.v1");
    CHECK(object->getProperty("os").toString() == "mac");

    auto* records = object->getProperty("records").getArray();
    REQUIRE(records != nullptr);
    REQUIRE(records->size() == 1);
    auto* record = (*records)[0].getDynamicObject();
    REQUIRE(record != nullptr);
    CHECK(record->getProperty("type").toString() == "kick");
    CHECK_FALSE((bool) record->getProperty("typeIsCustom"));
    CHECK(record->getProperty("key").toString() == "A minor");
    CHECK((double) record->getProperty("bpm") == Catch::Approx(128.0));

    auto* features = record->getProperty("features").getArray();
    REQUIRE(features != nullptr);
    CHECK(features->size() == 14);
    CHECK((double) (*features)[0] == Catch::Approx(0.123));
    CHECK((double) (*features)[1] == Catch::Approx(1.0));

    auto* tags = record->getProperty("tags").getArray();
    REQUIRE(tags != nullptr);
    REQUIRE(tags->size() == 2);
    CHECK_FALSE((bool) (*tags)[0].getDynamicObject()->getProperty("custom"));
    CHECK((bool) (*tags)[1].getDynamicObject()->getProperty("custom"));

    // Nothing that could identify a person or a file is in the batch.
    for (const auto* forbidden : { "/Users/", "fileHash", "sampleId", "path", "\\", "@" })
        CHECK(batch.json.contains(forbidden) == false);
}

TEST_CASE("Contribution batch drops unsafe labels and malformed records", "[contribute]")
{
    auto records = oneRecord();
    records[0].tags.addArray({ "/Users/me/pack", "me@example.com", juce::String::repeatedString("x", 40) });
    Record broken;
    broken.recordId = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
    broken.type = "snare";
    broken.features.assign(3, 0.5f);     // wrong length
    records.push_back(broken);

    const auto batch = buildBatch(records, vocabulary(),
        "3f2a8c1e-7b4d-4a9e-9c2f-1d6b8a0e5f31", "9b1d6c33-2e58-4f07-8a41-0c7d5e9b2a64",
        "0.4.2", "mac", juce::Time::getCurrentTime());
    CHECK(batch.recordCount == 1);
    CHECK(batch.json.contains("me@example.com") == false);
    CHECK(batch.json.contains("/Users/me/pack") == false);
    CHECK(batch.json.contains(juce::String::repeatedString("x", 40)) == false);
}

TEST_CASE("Contribution uploader obeys consent and moves the watermark only on success", "[contribute]")
{
    Harness harness;
    auto uploader = harness.make();

    harness.allowed = false;
    auto blocked = uploader.uploadOnce();
    CHECK_FALSE(blocked.sent);
    CHECK(harness.transport->posts == 0);

    harness.allowed = true;
    harness.transport->postResult = { 500, "boom" };
    auto failed = uploader.uploadOnce();
    CHECK_FALSE(failed.sent);
    CHECK(harness.committed == -1);

    harness.transport->postResult = { 204, {} };
    auto sent = uploader.uploadOnce();
    CHECK(sent.sent);
    CHECK(sent.recordCount == 1);
    CHECK(harness.committed == 1700000000000LL);
    CHECK(harness.transport->lastUrl == "https://intake.test/v1/contribute");
    CHECK(harness.transport->lastBody.contains("contribute.v1"));
}

TEST_CASE("Contribution uploader replaces a deleted install id", "[contribute]")
{
    Harness harness;
    auto uploader = harness.make();
    const auto before = harness.installId;
    harness.transport->postResult = { 410, "install id was deleted" };

    const auto outcome = uploader.uploadOnce();
    CHECK_FALSE(outcome.sent);
    CHECK(outcome.replacedId);
    CHECK(harness.stored != before);
    CHECK(harness.stored.length() == 36);
    CHECK(harness.committed == 0);
}

TEST_CASE("Contribution deletion removes the prefix and issues a new id", "[contribute]")
{
    Harness harness;
    auto uploader = harness.make();
    const auto before = harness.installId;

    const auto outcome = uploader.deleteMine();
    CHECK(outcome.sent);
    CHECK(harness.transport->removes == 1);
    CHECK(harness.transport->lastUrl == "https://intake.test/v1/contributions/" + before);
    CHECK(harness.installId != before);
    CHECK(harness.committed == 0);

    harness.transport->removeResult = { 500, "no" };
    CHECK_FALSE(uploader.deleteMine().sent);
}

TEST_CASE("Contribution preview builds a batch and sends nothing", "[contribute]")
{
    Harness harness;
    auto uploader = harness.make();
    const auto json = uploader.previewJson();
    CHECK(json.contains("contribute.v1"));
    CHECK(harness.transport->posts == 0);
    CHECK(harness.committed == -1);
}

TEST_CASE("Repository stores a stable contribution id and a per-catalog watermark", "[contribute][database]")
{
    TestDirectory t;
    Database db(t.dir.getChildFile("contribute.sqlite"));
    SampleRepository repo(db);
    repo.createTables();

    SampleRecord sample;
    sample.filePath = "/library/kick.wav";
    sample.fileName = "kick.wav";
    sample.fileHash = "hash-of-the-file";
    const auto id = repo.upsert(sample);

    const std::vector<float> features { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f };
    const auto acoustic = repo.createTag("acoustic", 0xff00ff00);
    repo.setSampleTag(id, acoustic, true);
    repo.recordFeedback(id, "kick", { "acoustic" }, features, SampleRepository::FeedbackOrigin::user);

    auto rows = repo.contributionRecords(0, 10);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].recordId.length() == 36);
    CHECK(rows[0].targetType == "kick");
    CHECK(rows[0].tags.contains("acoustic"));
    CHECK(rows[0].features.size() == 14);
    CHECK(rows[0].updatedAt > 0);

    // A correction keeps the same record id, so the export can replace the earlier label.
    const auto firstId = rows[0].recordId;
    repo.recordFeedback(id, "snare", { "acoustic" }, features, SampleRepository::FeedbackOrigin::user);
    rows = repo.contributionRecords(0, 10);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].recordId == firstId);
    CHECK(rows[0].targetType == "snare");

    CHECK(repo.contributionWatermark() == 0);
    repo.setContributionWatermark(rows[0].updatedAt);
    CHECK(repo.contributionWatermark() == rows[0].updatedAt);
    CHECK(repo.contributionRecords(rows[0].updatedAt, 10).empty());
}

TEST_CASE("Only user-origin feedback is eligible for contribution", "[contribute][database]")
{
    TestDirectory t;
    Database db(t.dir.getChildFile("origins.sqlite"));
    SampleRepository repo(db);
    repo.createTables();

    SampleRecord first;
    first.filePath = "/library/kick.wav";
    first.fileName = "kick.wav";
    first.fileHash = "hash-kick";
    const auto userSample = repo.upsert(first);

    SampleRecord second;
    second.filePath = "/library/snare.wav";
    second.fileName = "snare.wav";
    second.fileHash = "hash-snare";
    const auto machineSample = repo.upsert(second);

    const std::vector<float> features(14, 0.25f);
    repo.recordFeedback(userSample, "kick", { "acoustic" }, features, FeedbackOrigin::user);
    repo.recordFeedback(machineSample, "snare", { "acoustic" }, features, FeedbackOrigin::autoTag);

    // The local prototype store still sees both rows: the app keeps learning from its own tags.
    CHECK(repo.allFeedback().size() == 2);

    // Only a person's confirmation may leave the machine.
    const auto rows = repo.contributionRecords(0, 10);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].targetType == "kick");
}

TEST_CASE("Rows written before schema 14 count as legacy and are not contributed", "[contribute][database]")
{
    TestDirectory t;
    const auto file = t.dir.getChildFile("legacy.sqlite");
    {
        Database db(file);
        SampleRepository repo(db);
        repo.createTables();
        SampleRecord sample;
        sample.filePath = "/library/pad.wav";
        sample.fileName = "pad.wav";
        sample.fileHash = "hash-pad";
        const auto id = repo.upsert(sample);
        repo.recordFeedback(id, "Pad", { "Pad" }, std::vector<float>(14, 0.4f),
                            FeedbackOrigin::user);
    }
    // Shape of a catalog written by the build before the origin column existed.
    rawExec(file,
            "ALTER TABLE ml_feedback_samples DROP COLUMN origin; PRAGMA user_version=13;");

    Database db(file);
    SampleRepository repo(db);
    repo.createTables();
    CHECK(rawScalar(file, "SELECT user_version FROM pragma_user_version") == 16);
    CHECK(repo.allFeedback().size() == 1);            // legacy rows stay on disk
    CHECK(repo.contributionRecords(0, 10).empty());   // and out of the dataset
}

TEST_CASE("A machine write never replaces a person's confirmation", "[contribute][database]")
{
    TestDirectory t;
    Database db(t.dir.getChildFile("no-downgrade.sqlite"));
    SampleRepository repo(db);
    repo.createTables();

    SampleRecord sample;
    sample.filePath = "/library/kick.wav";
    sample.fileName = "kick.wav";
    sample.fileHash = "hash-kick";
    const auto id = repo.upsert(sample);

    const std::vector<float> features(14, 0.3f);
    repo.recordFeedback(id, "kick", { "acoustic" }, features, FeedbackOrigin::user);
    CHECK(repo.hasUserFeedback(id));

    // AutoTag later applies a tag and would have overwritten the row with its own guess.
    repo.recordFeedback(id, "snare", { "acoustic", "punchy" }, features, FeedbackOrigin::autoTag);

    const auto rows = repo.contributionRecords(0, 10);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].targetType == "kick");
    CHECK_FALSE(rows[0].tags.contains("punchy"));
    REQUIRE(repo.allFeedback().size() == 1);
    CHECK(repo.allFeedback()[0].targetType == "kick");
}

TEST_CASE("An accepted AutoTag proposal counts as a confirmation", "[contribute][database]")
{
    TestDirectory t;
    Database db(t.dir.getChildFile("approved.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    const std::vector<float> features(14, 0.3f);

    SampleRecord manual;
    manual.filePath = "/library/manual.wav";
    manual.fileName = "manual.wav";
    manual.fileHash = "hash-manual";
    const auto manualId = repo.upsert(manual);
    repo.recordFeedback(manualId, "Kick", {}, features, FeedbackOrigin::user);

    SampleRecord machine;
    machine.filePath = "/library/machine.wav";
    machine.fileName = "machine.wav";
    machine.fileHash = "hash-machine";
    const auto machineId = repo.upsert(machine);

    const auto punchy = repo.createTag("Punchy", 0xff00ff00);
    repo.setSampleTag(manualId, punchy, true);
    repo.setSampleTag(machineId, punchy, true);

    // The review dialog applies a proposal for both samples. The manual row keeps its type.
    repo.recordFeedback(manualId, "Snare", { "Punchy" }, features, FeedbackOrigin::approved);
    repo.recordFeedback(machineId, "Snare", { "Punchy" }, features, FeedbackOrigin::approved);

    const auto rows = repo.contributionRecords(0, 10);
    REQUIRE(rows.size() == 2);
    for (const auto& row : rows)
    {
        CHECK(row.tags.contains("Punchy"));
        CHECK_FALSE(row.typeConfirmed); // no type override on either sample
    }
    const auto manualRow = std::find_if(rows.begin(), rows.end(),
        [&](const SampleRepository::ContributionRecord& row) { return row.targetType == "Kick"; });
    REQUIRE(manualRow != rows.end());
    CHECK(repo.hasUserFeedback(manualId));
    CHECK_FALSE(repo.hasUserFeedback(machineId));
}

TEST_CASE("Tag history starts at schema 15 but local prototypes keep old tags", "[contribute][database]")
{
    TestDirectory t;
    const auto file = t.dir.getChildFile("tag-history.sqlite");
    int64_t id = 0;
    {
        Database db(file);
        SampleRepository repo(db);
        repo.createTables();
        SampleRecord sample;
        sample.filePath = "/library/old.wav";
        sample.fileName = "old.wav";
        sample.fileHash = "hash-old";
        id = repo.upsert(sample);
        const auto tag = repo.createTag("OldTag", 0xff00ff00);
        repo.setSampleTag(id, tag, true);
        repo.recordFeedback(id, "Pad", { "OldTag" }, std::vector<float>(14, 0.4f),
                            FeedbackOrigin::user);
        SmartOverride overrideType;
        overrideType.textValue = "Pad";
        repo.setSmartOverride(id, SmartField::Type, overrideType);
    }
    rawExec(file, "ALTER TABLE ml_feedback_samples DROP COLUMN tag_history_schema; PRAGMA user_version=14;");
    {
        Database db(file);
        SampleRepository repo(db);
        repo.createTables();
        CHECK(rawScalar(file, "SELECT user_version FROM pragma_user_version") == 16);
        REQUIRE(repo.allFeedback().size() == 1);
        CHECK(repo.allFeedback()[0].tags.contains("OldTag")); // local prototype still learns it
        const auto rows = repo.contributionRecords(0, 10);
        REQUIRE(rows.size() == 1);
        CHECK(rows[0].tags.isEmpty());                        // but it is not uploaded
        CHECK(rows[0].typeConfirmed);
        CHECK(rows[0].targetType == "Pad");
    }
}

TEST_CASE("Exported tags follow the current catalog", "[contribute][database]")
{
    TestDirectory t;
    Database db(t.dir.getChildFile("tag-current.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    SampleRecord sample;
    sample.filePath = "/library/tagged.wav";
    sample.fileName = "tagged.wav";
    sample.fileHash = "hash-tagged";
    const auto id = repo.upsert(sample);
    const auto tag = repo.createTag("OldName", 0xff00ff00);
    repo.setSampleTag(id, tag, true);
    repo.recordFeedback(id, "Kick", { "OldName" }, std::vector<float>(14, 0.2f),
                        FeedbackOrigin::user);
    REQUIRE(repo.contributionRecords(0, 10)[0].tags.contains("OldName"));

    REQUIRE(repo.renameTag(tag, "NewName"));
    auto rows = repo.contributionRecords(0, 10);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].tags.contains("NewName"));
    CHECK_FALSE(rows[0].tags.contains("OldName"));

    const auto beforeDelete = rows[0].updatedAt;
    juce::Thread::sleep(2);
    repo.deleteTag(tag);
    rows = repo.contributionRecords(0, 10);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].tags.isEmpty());
    CHECK(rows[0].updatedAt > beforeDelete);
}

TEST_CASE("A tag action exports tags without claiming the resolved type", "[contribute][database]")
{
    TestDirectory t;
    Database db(t.dir.getChildFile("type-origin.sqlite"));
    SampleRepository repo(db);
    repo.createTables();

    SampleRecord sample;
    sample.filePath = "/library/pad.wav";
    sample.fileName = "pad.wav";
    sample.fileHash = "hash-pad";
    const auto id = repo.upsert(sample);

    const std::vector<float> features(14, 0.4f);
    repo.recordFeedback(id, "Pad", { "warm" }, features, FeedbackOrigin::user);
    auto rows = repo.contributionRecords(0, 10);
    REQUIRE(rows.size() == 1);
    CHECK_FALSE(rows[0].typeConfirmed);

    SmartOverride overrideType;
    overrideType.textValue = "Pad";
    repo.setSmartOverride(id, SmartField::Type, overrideType);
    rows = repo.contributionRecords(0, 10);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].typeConfirmed);
}

TEST_CASE("A batch may carry tags without a type", "[contribute]")
{
    auto records = oneRecord();
    records[0].typeConfirmed = false;
    records[0].type = "Pad"; // what the app resolved, not what the person confirmed
    const auto batch = buildBatch(records, vocabulary(),
        "3f2a8c1e-7b4d-4a9e-9c2f-1d6b8a0e5f31", "9b1d6c33-2e58-4f07-8a41-0c7d5e9b2a64",
        "0.4.2", "mac", juce::Time::getCurrentTime());
    REQUIRE(batch.recordCount == 1);
    auto parsed = juce::JSON::parse(batch.json);
    const auto recordsValue = parsed.getDynamicObject()->getProperty("records");
    auto* recordsArray = recordsValue.getArray();
    REQUIRE(recordsArray != nullptr);
    auto* record = (*recordsArray)[0].getDynamicObject();
    REQUIRE(record != nullptr);
    CHECK_FALSE(record->hasProperty("type"));
    CHECK_FALSE(record->hasProperty("typeIsCustom"));
    const auto tagsValue = record->getProperty("tags");
    REQUIRE(tagsValue.getArray() != nullptr);
    CHECK(tagsValue.getArray()->size() == 2);

    // Without a type and without tags there is nothing to learn from.
    auto empty = oneRecord();
    empty[0].typeConfirmed = false;
    empty[0].tags.clear();
    const auto emptyBatch = buildBatch(empty, vocabulary(),
        "3f2a8c1e-7b4d-4a9e-9c2f-1d6b8a0e5f31", "9b1d6c33-2e58-4f07-8a41-0c7d5e9b2a64",
        "0.4.2", "mac", juce::Time::getCurrentTime());
    CHECK(emptyBatch.recordCount == 0);
}

TEST_CASE("A type offered by the Type menu counts as standard", "[contribute]")
{
    // Pluck is offered by the Type menu but is not inferred from a filename, so it exercises
    // the menu-only path; Piano is now both a menu entry and a filename token.
    CHECK(standardSampleTypeNames().contains("Pluck", true));
    CHECK_FALSE(canonicalSmartTypeNames().contains("Pluck", true));
    CHECK(standardSampleTypeNames().contains("Piano", true));
    CHECK(canonicalSmartTypeNames().contains("Piano", true));

    auto records = oneRecord();
    records[0].type = "Pluck";
    Vocabulary shipped;
    shipped.types = standardSampleTypeNames();
    shipped.tags = vocabulary().tags;
    const auto batch = buildBatch(records, shipped,
        "3f2a8c1e-7b4d-4a9e-9c2f-1d6b8a0e5f31", "9b1d6c33-2e58-4f07-8a41-0c7d5e9b2a64",
        "0.4.2", "mac", juce::Time::getCurrentTime());
    auto parsed = juce::JSON::parse(batch.json);
    const auto recordsValue = parsed.getDynamicObject()->getProperty("records");
    auto* parsedRecords = recordsValue.getArray();
    REQUIRE(parsedRecords != nullptr);
    auto* record = (*parsedRecords)[0].getDynamicObject();
    REQUIRE(record != nullptr);
    CHECK_FALSE((bool) record->getProperty("typeIsCustom"));
}

TEST_CASE("A standard tag stays standard when the catalog created it as custom", "[contribute][database]")
{
    TestDirectory t;
    const auto file = t.dir.getChildFile("builtin-tags.sqlite");
    Database db(file);
    SampleRepository repo(db);
    repo.createTables();
    rawExec(file, "UPDATE tags SET builtin=0 WHERE name='Crash'");
    CHECK(repo.builtinTagNames().contains("Crash", true));
    CHECK(repo.builtinTagNames().contains("Kick", true));
}

TEST_CASE("A contribution batch stays under the intake body limit", "[contribute]")
{
    std::vector<Record> records;
    for (int i = 0; i < 500; ++i)
    {
        auto record = oneRecord()[0];
        record.recordId = juce::String::formatted("%08d-0000-4000-8000-%012d", i, i);
        for (int tag = 0; tag < 12; ++tag)
            record.tags.add(juce::String::repeatedString("x", 28) + juce::String(i * 100 + tag));
        records.push_back(std::move(record));
    }
    const auto batch = buildBatch(records, vocabulary(),
        "3f2a8c1e-7b4d-4a9e-9c2f-1d6b8a0e5f31", "9b1d6c33-2e58-4f07-8a41-0c7d5e9b2a64",
        "0.4.2", "mac", juce::Time::getCurrentTime());
    CHECK(batch.recordCount > 0);
    CHECK(batch.recordCount < 500);
    CHECK(batch.json.length() < 256 * 1024);
    CHECK(batch.json.contains("contribute.v1"));
}

TEST_CASE("Contribution settings persist and default to off", "[contribute][settings]")
{
    // Version 3 changed what is eligible: accepted AutoTag proposals count, older tag history does not.
    CHECK(AppSettings::currentContributionConsentVersion >= 3);
    AppSettings s;
    CHECK_FALSE(s.contributeTrainingData);
    CHECK(s.contributionConsentVersion == 0);
    CHECK(s.contributionInstallId.isEmpty());
    CHECK(s.contributionEndpoint.isEmpty()); // the default address is applied on load

    s.contributeTrainingData = true;
    s.contributionConsentVersion = AppSettings::currentContributionConsentVersion;
    s.contributionInstallId = "3f2a8c1e-7b4d-4a9e-9c2f-1d6b8a0e5f31";

    auto file = juce::File::createTempFile("sonicator_contribution_settings");
    SettingsStore store(file);
    store.save(s);

    const auto loaded = store.load();
    CHECK(loaded.contributeTrainingData);
    CHECK(loaded.contributionConsentVersion == AppSettings::currentContributionConsentVersion);
    CHECK(loaded.contributionInstallId == s.contributionInstallId);
    CHECK(loaded.contributionEndpoint == AppSettings::defaultContributionEndpoint());
    file.deleteFile();
}

TEST_CASE("Live intake accepts a batch and deletes it", "[contribute][integration]")
{
    // Opt-in so the ordinary suite and CI never touch the network.
    if (juce::SystemStats::getEnvironmentVariable("SONICATOR_CONTRIBUTE_LIVE", {}) != "1")
    {
        SUCCEED("live intake test skipped");
        return;
    }
    WebTransport transport;
    const auto installId = Uploader::makeUuid();
    const auto batch = buildBatch(oneRecord(), vocabulary(), installId, Uploader::makeUuid(),
                                  "0.4.2", "mac", juce::Time::getCurrentTime());
    const auto post = transport.post("https://contribute.sampledicer.pro/v1/contribute",
                                     batch.json);
    CHECK(post.status == 204);
    const auto remove = transport.remove(
        "https://contribute.sampledicer.pro/v1/contributions/" + installId);
    CHECK(remove.status == 204);
}
