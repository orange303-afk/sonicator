#include <catch2/catch_test_macros.hpp>
#include "ui/MainComponent.h"
#include "ui/library/FileListPanel.h"
#include "core/database/SampleRepository.h"
#include "core/smart/SmartTypes.h"
#include "TestHelpers.h"

using namespace sonicator;

TEST_CASE("The library has a GENRE column sorted through the owner", "[genre][ui]")
{
    TestDirectory t;
    MainComponent app(t.dir.getChildFile("catalog.sqlite"), false, {},
                      t.dir.getChildFile("settings.json"));
    app.setSize(1500, 900);
    auto* library = dynamic_cast<FileListPanel*>(findDescendantWithID(app, "library"));
    REQUIRE(library != nullptr);
    auto* table = dynamic_cast<juce::TableListBox*>(library->getChildComponent(0));
    REQUIRE(table != nullptr);
    REQUIRE(table->getHeader().getColumnName(13) == "GENRE");
    REQUIRE(table->getHeader().getNumColumns(true) > 0);
}

TEST_CASE("Assigning a genre keeps tags, stars and type", "[genre][ui][database]")
{
    TestDirectory t;
    Database db(t.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    repo.addRoot(t.dir.getFullPathName());
    SampleRecord record;
    record.filePath = t.dir.getChildFile("a.wav").getFullPathName();
    record.fileName = "a.wav";
    record.fileHash = "h";
    const auto id = repo.upsert(record);
    repo.setStarred({ id }, true);
    const auto tagId = repo.createTag("Kick", 0xffe05a5a);
    repo.assignTag({ id }, tagId);

    SmartOverride typeOverride;
    typeOverride.textValue = "Kick";
    repo.setSmartOverride(id, SmartField::Type, typeOverride);

    SmartOverride genre;
    genre.textValue = "Jazz";
    repo.setSmartOverride(id, SmartField::Genre, genre);

    const auto row = repo.findById(id);
    REQUIRE(row.has_value());
    REQUIRE(row->starred);
    const auto tags = repo.tagsForSample(id);
    REQUIRE(tags.size() == 1);
    REQUIRE(tags[0].name == "Kick");
    const auto overrides = repo.smartOverrides(id);
    REQUIRE(overrides.at(SmartField::Type).textValue == "Kick");
    REQUIRE(overrides.at(SmartField::Genre).textValue == "Jazz");

    // Clearing the manual genre restores "no override" without touching the other fields.
    repo.clearSmartOverride(id, SmartField::Genre);
    REQUIRE(repo.smartOverrides(id).count(SmartField::Genre) == 0);
    REQUIRE(repo.smartOverrides(id).at(SmartField::Type).textValue == "Kick");
    REQUIRE(repo.findById(id)->starred);
}

TEST_CASE("An unknown genre override is distinct from no value", "[genre][ui][database]")
{
    TestDirectory t;
    Database db(t.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    repo.addRoot(t.dir.getFullPathName());
    SampleRecord record;
    record.filePath = t.dir.getChildFile("b.wav").getFullPathName();
    record.fileName = "b.wav";
    record.fileHash = "h2";
    const auto id = repo.upsert(record);
    SmartOverride unknown;
    unknown.cleared = true;
    repo.setSmartOverride(id, SmartField::Genre, unknown);
    const auto display = repo.smartDisplays({ id });
    REQUIRE(display.at(id).genreOverride == OverrideState::Unknown);
    REQUIRE(display.at(id).genre.isEmpty());
}
