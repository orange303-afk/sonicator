#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/database/Database.h"
#include "core/database/SampleRepository.h"
#include "core/smart/SmartTypes.h"
#include "TestHelpers.h"

using namespace sonicator;
using Catch::Approx;

namespace
{
int64_t addSample(SampleRepository& repo, const juce::File& file)
{
    SampleRecord record;
    record.filePath = file.getFullPathName();
    record.fileName = file.getFileName();
    record.format = "WAV";
    record.fileSize = file.getSize();
    return repo.upsert(record);
}

void labelSample(SampleRepository& repo, int64_t id)
{
    std::vector<float> features(14, 0.5f);
    repo.recordFeedback(id, "loop", { "Loop" }, features, SampleRepository::FeedbackOrigin::user);
}

void setManualBpm(SampleRepository& repo, int64_t id, double bpm)
{
    SmartOverride value;
    value.numericValue = bpm;
    repo.setSmartOverride(id, SmartField::Bpm, value);
}
} // namespace

TEST_CASE("A manual BPM overrides the analysed tempo in contribution", "[bpm][catalog]")
{
    TestDirectory t;
    Database db(t.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    const auto file = t.wave("loop.wav");
    const auto id = addSample(repo, file);
    labelSample(repo, id);
    setManualBpm(repo, id, 128.0);
    const auto records = repo.contributionRecords(0, 10);
    REQUIRE(records.size() == 1);
    REQUIRE(records.front().hasBpm);
    REQUIRE(records.front().bpm == Approx(128.0));
}

TEST_CASE("Clearing the manual BPM restores the machine tempo", "[bpm][catalog]")
{
    TestDirectory t;
    Database db(t.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    const auto file = t.wave("loop.wav");
    const auto id = addSample(repo, file);
    labelSample(repo, id);
    setManualBpm(repo, id, 128.0);
    repo.clearSmartOverride(id, SmartField::Bpm);
    repo.touchFeedback({ id });
    const auto records = repo.contributionRecords(0, 10);
    REQUIRE(records.size() == 1);
    REQUIRE_FALSE(records.front().hasBpm);
}

TEST_CASE("A manual BPM does not create feedback for an unlabelled sample", "[bpm][catalog]")
{
    TestDirectory t;
    Database db(t.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    const auto file = t.wave("loop.wav");
    const auto id = addSample(repo, file);
    setManualBpm(repo, id, 128.0);
    REQUIRE(repo.contributionRecords(0, 10).empty());
}

TEST_CASE("A manual BPM is visible as an override in the list display", "[bpm][catalog]")
{
    TestDirectory t;
    Database db(t.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    const auto file = t.wave("loop.wav");
    const auto id = addSample(repo, file);
    setManualBpm(repo, id, 128.0);
    const auto displays = repo.smartDisplays({ id });
    REQUIRE(displays.size() == 1);
    const auto& display = displays.at(id);
    REQUIRE(display.bpm == Approx(128.0));
    REQUIRE(display.bpmOverride == OverrideState::Value);
}
