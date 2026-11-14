#include <catch2/catch_test_macros.hpp>
#include "core/database/SampleRepository.h"
#include "core/smart/GenreContextExtractor.h"
#include "ui/MainComponent.h"
#include "ui/library/FolderTree.h"
#include "ui/library/FileListPanel.h"
#include "TestHelpers.h"
#include <sqlite3.h>

using namespace sonicator;

namespace {
void genreSql(const juce::File& file, const juce::String& sql)
{
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(file.getFullPathName().toRawUTF8(), &db) == SQLITE_OK);
    const int rc = sqlite3_exec(db, sql.toRawUTF8(), nullptr, nullptr, nullptr);
    sqlite3_close(db);
    REQUIRE(rc == SQLITE_OK);
}
struct CustomGenreFixture
{
    TestDirectory t;
    juce::File catalog = t.dir.getChildFile("genres.sqlite");
    Database db { catalog };
    SampleRepository repo { db };
    juce::String folder = t.dir.getChildFile("pack").getFullPathName();
    CustomGenreFixture() { repo.createTables(); }
    int64_t add(const juce::String& name = "a.wav")
    {
        SampleRecord r;
        r.filePath = folder + "/" + name; r.fileName = name; r.fileHash = name;
        return repo.upsert(r);
    }
    void assign(int64_t id, const juce::String& name)
    {
        SmartOverride value; value.textValue = name;
        repo.setSmartOverride(id, SmartField::Genre, value);
    }
};
}

TEST_CASE("Custom genre vocabulary survives restart and removal of its last assignment", "[custom-genres][database]")
{
    CustomGenreFixture f;
    const auto id = f.add();
    f.assign(id, "  Minimal House  ");
    REQUIRE(f.repo.genreNames().contains("Minimal House"));
    REQUIRE(f.repo.smartOverrides(id).at(SmartField::Genre).textValue == "Minimal House");
    f.repo.clearSmartOverride(id, SmartField::Genre);
    f.repo.removeByPaths({ f.folder + "/a.wav" });
    Database reopenedDb(f.catalog);
    SampleRepository reopened(reopenedDb);
    reopened.createTables();
    REQUIRE(reopened.genreNames().contains("Minimal House"));
    Database independentDb(f.t.dir.getChildFile("other.sqlite"));
    SampleRepository independent(independentDb);
    independent.createTables();
    REQUIRE_FALSE(independent.genreNames().contains("Minimal House"));
}

TEST_CASE("Genre menu vocabulary deduplicates builtins and custom names by folded case", "[custom-genres][database]")
{
    CustomGenreFixture f;
    const auto id = f.add();
    f.assign(id, "house");
    f.assign(id, "  Minimal House  ");
    f.assign(id, "minimal house");
    f.assign(id, juce::String::fromUTF8("Мой жанр"));
    f.assign(id, juce::String::fromUTF8("мой жанр"));
    const auto names = f.repo.genreNames();
    REQUIRE(names.size() == GenreContextExtractor::canonicalGenres().size() + 2);
    REQUIRE(names[1] == "House");
    REQUIRE(names.contains("Minimal House"));
    REQUIRE(names.contains(juce::String::fromUTF8("Мой жанр")));
    REQUIRE_FALSE(names.contains("minimal house"));
}

TEST_CASE("Genre vocabulary ignores special states and automatic guesses", "[custom-genres][database]")
{
    CustomGenreFixture f;
    const auto id = f.add();
    SmartOverride special; special.textValue = "Not a genre";
    special.cleared = true;
    f.repo.setSmartOverride(id, SmartField::Genre, special);
    special.cleared = false; special.notApplicable = true;
    f.repo.setSmartOverride(id, SmartField::Genre, special);
    special.notApplicable = false; special.textValue = " \t ";
    f.repo.setSmartOverride(id, SmartField::Genre, special);
    SmartResult automatic; automatic.genre = "Automatic Only";
    automatic.genreSource = "info"; automatic.genreState = SmartState::Resolved;
    f.repo.setSmartResult(id, automatic);
    REQUIRE(f.repo.genreNames().size() == GenreContextExtractor::canonicalGenres().size());
}

TEST_CASE("Folder assignment registers only successfully applied custom genres", "[custom-genres][database]")
{
    CustomGenreFixture f;
    const auto id = f.add();
    f.assign(id, "Jazz");
    REQUIRE(f.repo.setGenreInFolder(f.folder, "Blocked Genre").assigned == 0);
    REQUIRE_FALSE(f.repo.genreNames().contains("Blocked Genre"));
    REQUIRE(f.repo.setGenreInFolder(f.folder + "/empty", "Empty Genre").assigned == 0);
    REQUIRE_FALSE(f.repo.genreNames().contains("Empty Genre"));
    REQUIRE(f.repo.setGenreInFolder(f.folder, "Folder Genre", true).assigned == 1);
    REQUIRE(f.repo.genreNames().contains("Folder Genre"));
    genreSql(f.catalog, "CREATE TRIGGER fail_custom_genre BEFORE INSERT ON smart_overrides "
        "WHEN NEW.field='genre' BEGIN SELECT RAISE(ABORT,'write failure'); END;");
    REQUIRE_THROWS(f.repo.setGenreInFolder(f.folder, "Failed Genre", true));
    REQUIRE_FALSE(f.repo.genreNames().contains("Failed Genre"));
    SmartOverride failed; failed.textValue = "Failed Single Genre";
    REQUIRE_THROWS(f.repo.setSmartOverride(id, SmartField::Genre, failed));
    REQUIRE_FALSE(f.repo.genreNames().contains("Failed Single Genre"));
}

TEST_CASE("Schema 18 migration backfills existing manual genres without automatic or special values", "[custom-genres][schema]")
{
    CustomGenreFixture f;
    const auto id = f.add();
    f.assign(id, "Legacy Genre");
    const auto unknownId = f.add("unknown.wav");
    SmartOverride unknown; unknown.cleared = true; unknown.textValue = "Unknown Placeholder";
    f.repo.setSmartOverride(unknownId, SmartField::Genre, unknown);
    const auto autoId = f.add("auto.wav");
    SmartResult automatic; automatic.genre = "Auto Placeholder"; automatic.genreState = SmartState::Resolved;
    f.repo.setSmartResult(autoId, automatic);
    genreSql(f.catalog, "DROP TABLE custom_genres; PRAGMA user_version=18;");
    Database upgradedDb(f.catalog);
    SampleRepository upgraded(upgradedDb);
    upgraded.createTables();
    {
        sqlite3* raw = nullptr;
        REQUIRE(sqlite3_open(f.catalog.getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
        sqlite3_stmt* statement = nullptr;
        REQUIRE(sqlite3_prepare_v2(raw, "PRAGMA user_version", -1, &statement, nullptr) == SQLITE_OK);
        REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
        const int version = sqlite3_column_int(statement, 0);
        sqlite3_finalize(statement);
        sqlite3_close(raw);
        REQUIRE(version == 19);
    }
    REQUIRE(upgraded.genreNames().contains("Legacy Genre"));
    REQUIRE_FALSE(upgraded.genreNames().contains("Unknown Placeholder"));
    REQUIRE_FALSE(upgraded.genreNames().contains("Auto Placeholder"));
    upgraded.clearSmartOverride(id, SmartField::Genre);
    upgraded.createTables();
    REQUIRE(upgraded.genreNames().contains("Legacy Genre"));
}

namespace {
void openGenreAssignmentMenu(MainComponent& app, CustomGenreFixture& f, int64_t target, bool folder)
{
    if (folder)
    {
        auto* tree = dynamic_cast<FolderTree*>(app.findChildWithID("folderTree"));
        REQUIRE(tree != nullptr);
        tree->onContextMenu(f.folder + "/target", false, { 300, 300 });
    }
    else
    {
        auto* list = dynamic_cast<FileListPanel*>(findDescendantWithID(app, "library"));
        REQUIRE(list != nullptr);
        const auto record = f.repo.findById(target);
        REQUIRE(record.has_value());
        list->onContextMenu({ *record }, *record, { 300, 300 });
    }
    REQUIRE(juce::Component::getCurrentlyModalComponent() != nullptr);
}
}

TEST_CASE("Both assignment menus reuse custom genres beyond the old fixed ID limits", "[custom-genres][ui]")
{
    CustomGenreFixture f;
    const auto seed = f.add("seed.wav");
    const auto target = f.add("target/a.wav");
    // More than the old folder (40) and row (90) ranges can represent, including service IDs.
    for (int i = 0; i < 105; ++i)
        f.assign(seed, "User Genre " + juce::String(i).paddedLeft('0', 3));
    const auto names = f.repo.genreNames();
    const int index = names.indexOf("User Genre 104");
    REQUIRE(index > 90);
    bool folder = false;
    SECTION("row menu") {}
    SECTION("folder menu") { folder = true; }
    MainComponent app(f.catalog, false, {}, f.t.dir.getChildFile("settings.json"));
    openGenreAssignmentMenu(app, f, target, folder);
    juce::Component::getCurrentlyModalComponent()->exitModalState(1000000 + index);
    REQUIRE(pumpUntil([&] { return f.repo.smartOverrides(target).count(SmartField::Genre) != 0; }));
    REQUIRE(f.repo.smartDisplays({ target }).at(target).genre == "User Genre 104");
    REQUIRE(f.repo.smartOverrides(target).count(SmartField::Type) == 0);
}

TEST_CASE("Genre menus retain the name mapping displayed when opened", "[custom-genres][ui]")
{
    CustomGenreFixture f;
    const auto seed = f.add("seed.wav");
    const auto target = f.add("target/a.wav");
    f.assign(seed, "Z Last");
    const int index = f.repo.genreNames().indexOf("Z Last");
    REQUIRE(index >= 0);
    bool folder = false;
    SECTION("row menu") {}
    SECTION("folder menu") { folder = true; }
    MainComponent app(f.catalog, false, {}, f.t.dir.getChildFile("settings.json"));
    openGenreAssignmentMenu(app, f, target, folder);
    f.assign(seed, "A First"); // another owner inserts a name that would shift the sorted list
    REQUIRE(f.repo.genreNames().indexOf("Z Last") != index);
    juce::Component::getCurrentlyModalComponent()->exitModalState(1000000 + index);
    REQUIRE(pumpUntil([&] { return f.repo.smartOverrides(target).count(SmartField::Genre) != 0; }));
    REQUIRE(f.repo.smartDisplays({ target }).at(target).genre == "Z Last");
}

TEST_CASE("A row genre menu cannot apply its saved choice to another catalog", "[custom-genres][ui]")
{
    CustomGenreFixture f;
    const auto seed = f.add("seed.wav");
    const auto target = f.add("target/a.wav");
    f.assign(seed, "Saved Genre");
    const auto otherPath = f.t.dir.getChildFile("other.sqlite");
    Database otherDb(otherPath);
    SampleRepository other(otherDb);
    other.createTables();
    auto record = *f.repo.findById(target);
    record.id = 0;
    const auto otherId = other.upsert(record);
    MainComponent app(f.catalog, false, {}, f.t.dir.getChildFile("settings.json"));
    openGenreAssignmentMenu(app, f, target, false);
    juce::Component::SafePointer<juce::Component> menu(juce::Component::getCurrentlyModalComponent());
    juce::String error;
    REQUIRE(app.switchCatalog(otherPath, false, error));
    if (menu != nullptr) menu->exitModalState(1000000 + f.repo.genreNames().indexOf("Saved Genre"));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    REQUIRE(other.smartOverrides(otherId).empty());
    REQUIRE(f.repo.smartOverrides(target).empty());
    REQUIRE_FALSE(other.genreNames().contains("Saved Genre"));
}
