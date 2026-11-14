#include <catch2/catch_test_macros.hpp>
#include "core/database/SampleRepository.h"
#include "ui/MainComponent.h"
#include "ui/library/FolderTree.h"
#include "TestHelpers.h"
#include <sqlite3.h>

using namespace sonicator;

namespace {
struct FolderGenreFixture
{
    TestDirectory t;
    juce::File catalog = t.dir.getChildFile("catalog.sqlite");
    Database db { catalog };
    SampleRepository repo { db };
    juce::String folder = t.dir.getChildFile("CM119 Minimal House Samples").getFullPathName();
    FolderGenreFixture() { repo.createTables(); }
    int64_t add(const juce::String& path)
    {
        SampleRecord r;
        r.filePath = path;
        r.fileName = juce::File(path).getFileName();
        r.fileHash = path;
        r.format = "WAV";
        r.numChannels = 1;
        r.sampleRate = 44100;
        r.duration = 1.0;
        return repo.upsert(r);
    }
};
}

TEST_CASE("Folder genre reaches descendants without affecting prefix siblings or future files", "[folder-genre][database]")
{
    FolderGenreFixture f;
    const auto direct = f.add(f.folder + "/a.wav");
    const auto deep = f.add(f.folder + "/Groove Criminals/Beats/02a(120BPM).wav");
    const auto sibling = f.add(f.folder + " Extra/b.wav");
    const auto result = f.repo.setGenreInFolder(f.folder, "House");
    REQUIRE(result.total == 2);
    REQUIRE(result.assigned == 2);
    REQUIRE(result.conflicts == 0);
    for (const auto id : { direct, deep })
    {
        REQUIRE(f.repo.smartOverrides(id).at(SmartField::Genre).textValue == "House");
        REQUIRE(f.repo.smartDisplays({ id }).at(id).genre == "House");
    }
    REQUIRE(f.repo.smartOverrides(sibling).empty());
    const auto later = f.add(f.folder + "/new.wav");
    REQUIRE(f.repo.smartOverrides(later).empty());
}

TEST_CASE("Folder genre refuses the whole assignment until manual conflicts are confirmed", "[folder-genre][database]")
{
    FolderGenreFixture f;
    const auto blank = f.add(f.folder + "/a.wav");
    const auto edited = f.add(f.folder + "/b.wav");
    SmartOverride previous;
    SECTION("a different manual genre") { previous.textValue = "Jazz"; }
    SECTION("deliberately Unknown") { previous.cleared = true; }
    SECTION("not applicable") { previous.notApplicable = true; }
    f.repo.setSmartOverride(edited, SmartField::Genre, previous);
    const auto revision = f.repo.smartRevision(edited);
    const auto blocked = f.repo.setGenreInFolder(f.folder, "House");
    REQUIRE(blocked.total == 2);
    REQUIRE(blocked.conflicts == 1);
    REQUIRE(blocked.assigned == 0);
    REQUIRE(f.repo.smartOverrides(blank).empty());
    REQUIRE(f.repo.smartRevision(edited) == revision);
    const auto unchanged = f.repo.smartOverrides(edited).at(SmartField::Genre);
    REQUIRE(unchanged.textValue == previous.textValue);
    REQUIRE(unchanged.cleared == previous.cleared);
    REQUIRE(unchanged.notApplicable == previous.notApplicable);

    const auto confirmed = f.repo.setGenreInFolder(f.folder, "House", true);
    REQUIRE(confirmed.assigned == 2);
    REQUIRE(confirmed.conflicts == 1);
    REQUIRE(f.repo.smartRevision(edited) > revision);
    for (const auto id : { blank, edited })
        REQUIRE(f.repo.smartDisplays({ id }).at(id).genre == "House");
    const auto repeated = f.repo.setGenreInFolder(f.folder, "House");
    REQUIRE(repeated.conflicts == 0);
    REQUIRE(repeated.assigned == 0);
}

TEST_CASE("Folder genre preserves other metadata and survives a new smart publication", "[folder-genre][database]")
{
    FolderGenreFixture f;
    const auto file = f.t.wave("CM119 Minimal House Samples/source.wav", 440, 1, 1000);
    const auto bytes = file.loadFileAsString();
    const auto id = f.add(file.getFullPathName());
    f.repo.setStarred({ id }, true);
    const auto tag = f.repo.createTag("Keep me", 0xff123456);
    f.repo.assignTag({ id }, tag);
    SmartOverride type; type.textValue = "Kick";
    SmartOverride bpm; bpm.numericValue = 120;
    SmartOverride key; key.textValue = "C minor";
    f.repo.setSmartOverride(id, SmartField::Type, type);
    f.repo.setSmartOverride(id, SmartField::Bpm, bpm);
    f.repo.setSmartOverride(id, SmartField::Key, key);
    const auto before = f.repo.smartDisplays({ id }).at(id);
    const auto revision = f.repo.smartRevision(id);
    REQUIRE(f.repo.setGenreInFolder(f.folder, "House").assigned == 1);
    // A result already being computed before the manual action cannot overwrite it.
    SmartPublication stale;
    stale.sampleId = id;
    stale.expectedRevision = revision;
    REQUIRE_FALSE(f.repo.publishSmartAnalysis(stale).published);
    SmartPublication fresh;
    fresh.sampleId = id;
    fresh.expectedRevision = f.repo.smartRevision(id);
    SmartCandidate inferred;
    inferred.field = SmartField::Genre;
    inferred.source = "folder";
    inferred.textValue = "Techno";
    fresh.candidates = { inferred };
    REQUIRE(f.repo.publishSmartAnalysis(fresh).published);
    const auto after = f.repo.smartDisplays({ id }).at(id);
    REQUIRE(after.genre == "House");
    REQUIRE(after.genreOverride == OverrideState::Value);
    REQUIRE(after.type == before.type);
    REQUIRE(after.bpm == before.bpm);
    REQUIRE(after.key == before.key);
    REQUIRE(f.repo.findById(id)->starred);
    REQUIRE(f.repo.tagsForSample(id).size() == 1);
    REQUIRE(f.repo.tagsForSample(id)[0].id == tag);
    REQUIRE(file.loadFileAsString() == bytes);
}

TEST_CASE("Folder genre handles literal wildcard paths, ignored paths and hidden stereo partners", "[folder-genre][database]")
{
    FolderGenreFixture f;
    const auto folder = f.t.dir.getChildFile("100%_House").getFullPathName();
    const auto left = f.add(folder + "/a_L.wav");
    const auto right = f.add(folder + "/a_R.wav");
    const auto ignored = f.add(folder + "/skip/b.wav");
    const auto other = f.add(f.t.dir.getChildFile("100xxHouse/b.wav").getFullPathName());
    f.repo.ignorePath(folder + "/skip", true);
    REQUIRE(f.repo.refreshDuplicates());
    REQUIRE(f.repo.idsInFolder(folder).size() == 1); // the tree/table merges this mono pair
    const auto result = f.repo.setGenreInFolder(folder + "/", juce::String::fromUTF8("  Мой жанр  "));
    REQUIRE(result.total == 2);
    REQUIRE(result.assigned == 2);
    for (const auto id : { left, right })
        REQUIRE(f.repo.smartOverrides(id).at(SmartField::Genre).textValue == juce::String::fromUTF8("Мой жанр"));
    REQUIRE(f.repo.smartOverrides(ignored).empty());
    REQUIRE(f.repo.smartOverrides(other).empty());
}

TEST_CASE("Folder genre rejects empty input and allows catalog-only unavailable folders", "[folder-genre][database]")
{
    FolderGenreFixture f;
    const auto id = f.add(f.folder + "/offline.wav");
    REQUIRE_FALSE(juce::File(f.folder).exists());
    REQUIRE(f.repo.setGenreInFolder({}, "House").assigned == 0);
    REQUIRE(f.repo.setGenreInFolder(f.folder, " \t ").assigned == 0);
    REQUIRE(f.repo.setGenreInFolder(f.folder + " Empty", "House").total == 0);
    REQUIRE(f.repo.smartOverrides(id).empty());
    REQUIRE(f.repo.setGenreInFolder(f.folder, "House").assigned == 1);
}

TEST_CASE("A folder genre database error rolls back earlier rows", "[folder-genre][database]")
{
    FolderGenreFixture f;
    const auto first = f.add(f.folder + "/a.wav");
    const auto second = f.add(f.folder + "/b.wav");
    sqlite3* raw = nullptr;
    REQUIRE(sqlite3_open(f.catalog.getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
    const auto sql = "CREATE TRIGGER fail_folder_genre BEFORE INSERT ON smart_overrides "
        "WHEN NEW.field='genre' AND NEW.sample_id=" + juce::String(second)
        + " BEGIN SELECT RAISE(ABORT,'test write failure'); END;";
    const auto rc = sqlite3_exec(raw, sql.toRawUTF8(), nullptr, nullptr, nullptr);
    sqlite3_close(raw);
    REQUIRE(rc == SQLITE_OK);
    REQUIRE_THROWS(f.repo.setGenreInFolder(f.folder, "House"));
    REQUIRE(f.repo.smartOverrides(first).empty());
    REQUIRE(f.repo.smartOverrides(second).empty());
    REQUIRE(f.repo.smartRevision(first) == 0);
}

namespace {
void chooseFolderGenre(MainComponent& app, const juce::String& folder, int item = 7051)
{
    auto* tree = dynamic_cast<FolderTree*>(app.findChildWithID("folderTree"));
    REQUIRE(tree != nullptr);
    tree->onContextMenu(folder, false, { 300, 300 });
    auto* menu = juce::Component::getCurrentlyModalComponent();
    REQUIRE(menu != nullptr);
    menu->exitModalState(item); // choose House from Set genre (existing canonical list)
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
}
juce::AlertWindow* genreWarning()
{
    return dynamic_cast<juce::AlertWindow*>(juce::Component::getCurrentlyModalComponent());
}
}

TEST_CASE("Folder menu assigns genre recursively regardless of table filters", "[folder-genre][ui]")
{
    FolderGenreFixture f;
    const auto id = f.add(f.folder + "/Groove Criminals/Beats/02a(120BPM).wav");
    const auto sibling = f.add(f.folder + " Extra/b.wav");
    MainComponent app(f.catalog, false, {}, f.t.dir.getChildFile("settings.json"));
    app.searchLibrary("no files match this");
    chooseFolderGenre(app, f.folder);
    REQUIRE(pumpUntil([&] { return f.repo.smartOverrides(id).count(SmartField::Genre) != 0; }));
    REQUIRE(f.repo.smartDisplays({ id }).at(id).genre == "House");
    REQUIRE(f.repo.smartOverrides(sibling).empty());
}

TEST_CASE("Folder menu confirms replacement before changing any genre", "[folder-genre][ui]")
{
    FolderGenreFixture f;
    const auto blank = f.add(f.folder + "/a.wav");
    const auto edited = f.add(f.folder + "/b.wav");
    SmartOverride old; old.textValue = "Jazz";
    f.repo.setSmartOverride(edited, SmartField::Genre, old);
    MainComponent app(f.catalog, false, {}, f.t.dir.getChildFile("settings.json"));
    chooseFolderGenre(app, f.folder);
    REQUIRE(pumpUntil([] { return genreWarning() != nullptr; }));
    REQUIRE(f.repo.smartOverrides(blank).empty());
    REQUIRE(f.repo.smartDisplays({ edited }).at(edited).genre == "Jazz");
    SECTION("Cancel leaves all files untouched")
    {
        genreWarning()->exitModalState(0);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        REQUIRE(f.repo.smartOverrides(blank).empty());
        REQUIRE(f.repo.smartDisplays({ edited }).at(edited).genre == "Jazz");
    }
    SECTION("Replace updates all descendants")
    {
        genreWarning()->exitModalState(1);
        REQUIRE(pumpUntil([&] { return f.repo.smartDisplays({ edited }).at(edited).genre == "House"; }));
        REQUIRE(f.repo.smartDisplays({ blank }).at(blank).genre == "House");
    }
}

TEST_CASE("Folder menu custom genre accepts trimmed text and can be cancelled", "[folder-genre][ui]")
{
    FolderGenreFixture f;
    const auto id = f.add(f.folder + "/a.wav");
    MainComponent app(f.catalog, false, {}, f.t.dir.getChildFile("settings.json"));
    chooseFolderGenre(app, f.folder, 7090);
    REQUIRE(pumpUntil([] { return genreWarning() != nullptr; }));
    auto* editor = genreWarning()->getTextEditor("value");
    REQUIRE(editor != nullptr);
    editor->setText("  Minimal House  ");
    SECTION("Accept saves the custom genre")
    {
        genreWarning()->exitModalState(1);
        REQUIRE(pumpUntil([&] { return f.repo.smartOverrides(id).count(SmartField::Genre) != 0; }));
        REQUIRE(f.repo.smartDisplays({ id }).at(id).genre == "Minimal House");
    }
    SECTION("Cancel does not assign anything")
    {
        genreWarning()->exitModalState(0);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        REQUIRE(f.repo.smartOverrides(id).empty());
    }
}

TEST_CASE("A deferred folder genre confirmation cannot write into a switched catalog", "[folder-genre][ui]")
{
    FolderGenreFixture f;
    const auto id = f.add(f.folder + "/a.wav");
    SmartOverride old; old.textValue = "Jazz";
    f.repo.setSmartOverride(id, SmartField::Genre, old);
    const auto otherCatalog = f.t.dir.getChildFile("other.sqlite");
    Database otherDb(otherCatalog);
    SampleRepository other(otherDb);
    other.createTables();
    SampleRecord r; r.filePath = f.folder + "/a.wav"; r.fileName = "a.wav";
    const auto otherId = other.upsert(r);
    MainComponent app(f.catalog, false, {}, f.t.dir.getChildFile("settings.json"));
    chooseFolderGenre(app, f.folder);
    REQUIRE(pumpUntil([] { return genreWarning() != nullptr; }));
    juce::Component::SafePointer<juce::AlertWindow> warning(genreWarning());
    juce::String error;
    REQUIRE(app.switchCatalog(otherCatalog, false, error));
    if (warning != nullptr) warning->exitModalState(1);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    REQUIRE(other.smartOverrides(otherId).empty());
    REQUIRE(f.repo.smartDisplays({ id }).at(id).genre == "Jazz");
}

TEST_CASE("Closing the window waits for its outstanding folder genre write", "[folder-genre][ui]")
{
    FolderGenreFixture f;
    const auto id = f.add(f.folder + "/a.wav");
    auto app = std::make_unique<MainComponent>(f.catalog, false, AppSettings{}, f.t.dir.getChildFile("settings.json"));
    chooseFolderGenre(*app, f.folder);
    app.reset();
    REQUIRE(f.repo.smartDisplays({ id }).at(id).genre == "House");
}
