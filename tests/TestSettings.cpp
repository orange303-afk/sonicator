#include <catch2/catch_test_macros.hpp>
#include "core/settings/AppSettings.h"
#include "TestHelpers.h"
using namespace sonicator;
TEST_CASE("Hide empty defaults on and survives unrelated settings updates", "[settings][hide-empty]")
{
    TestDirectory t;
    const auto file = t.dir.getChildFile("preferences.json");
    SettingsStore store(file);
    REQUIRE(store.load().hideEmpty);
    REQUIRE(file.replaceWithText("{\"hideEmpty\":\"false\"}"));
    REQUIRE(store.load().hideEmpty); // malformed strings keep the enabled default
    store.update([](AppSettings& saved) { saved.hideEmpty = false; });
    store.update([](AppSettings& saved) { saved.windowWidth = 1200; saved.theme = ThemeMode::Light; });
    REQUIRE_FALSE(store.load().hideEmpty);
    store.update([](AppSettings& saved) { saved.hideEmpty = true; });
    REQUIRE(store.load().hideEmpty);
    REQUIRE(store.load().windowWidth == 1200);
    REQUIRE(store.load().theme == ThemeMode::Light);
}
TEST_CASE("Last folder and selected file round-trip through settings", "[settings][session]")
{
    TestDirectory t;
    SettingsStore store(t.dir.getChildFile("session.json"));
    REQUIRE(store.load().lastFolder.isEmpty());
    REQUIRE(store.load().lastSamplePath.isEmpty());
    store.update([](AppSettings& saved)
    {
        saved.lastFolder = "/lib/Drums";
        saved.lastSamplePath = "/lib/Drums/kick.wav";
    });
    const auto loaded = store.load();
    REQUIRE(loaded.lastFolder == "/lib/Drums");
    REQUIRE(loaded.lastSamplePath == "/lib/Drums/kick.wav");
    store.update([](AppSettings& saved) { saved.lastFolder.clear(); saved.lastSamplePath.clear(); });
    REQUIRE(store.load().lastFolder.isEmpty());
    REQUIRE(store.load().lastSamplePath.isEmpty());
}
TEST_CASE("Settings persist theme, scale, language and catalog path", "[settings]")
{
    TestDirectory t; auto file = t.dir.getChildFile("settings.json");
    SettingsStore store(file);
    const auto defaults = store.load();
    REQUIRE(defaults.theme == ThemeMode::System);
    REQUIRE(defaults.scalePercent == 100);
    REQUIRE(defaults.language == "English");
    REQUIRE(defaults.databasePath.isEmpty());

    AppSettings settings;
    settings.theme = ThemeMode::Light; settings.scalePercent = 125;
    settings.language = "English"; settings.databasePath = "/tmp/catalog.sqlite";
    store.save(settings);
    const auto loaded = store.load();
    REQUIRE(loaded.theme == ThemeMode::Light);
    REQUIRE(loaded.scalePercent == 125);
    REQUIRE(loaded.databasePath == "/tmp/catalog.sqlite");

    file.replaceWithText("{\"theme\":\"strange\",\"scale\":999}");
    const auto invalid = store.load();
    REQUIRE(invalid.theme == ThemeMode::System);
    REQUIRE(invalid.scalePercent == 100);
    file.replaceWithText("not json");
    REQUIRE(store.load().theme == ThemeMode::System);
    REQUIRE(AppSettings::clampScale(75) == 75);
    REQUIRE(AppSettings::clampScale(200) == 200);
    REQUIRE(AppSettings::clampScale(0) == 100);
    REQUIRE(AppSettings::clampScale(1000) == 100);
}
TEST_CASE("Independent settings updates do not overwrite each other", "[settings]")
{
    TestDirectory t; auto file = t.dir.getChildFile("settings.json");
    SettingsStore store(file);
    AppSettings initial;
    initial.theme = ThemeMode::Light; initial.scalePercent = 125;
    initial.databasePath = "/catalogs/main.sqlite";
    initial.windowX = 120; initial.windowY = 80; initial.windowWidth = 1100; initial.windowHeight = 700;
    store.save(initial);
    store.update([](AppSettings& saved) { saved.theme = ThemeMode::Dark; saved.scalePercent = 100; });
    auto afterTheme = store.load();
    REQUIRE(afterTheme.theme == ThemeMode::Dark);
    REQUIRE(afterTheme.windowX == 120); REQUIRE(afterTheme.windowWidth == 1100);
    REQUIRE(afterTheme.databasePath == "/catalogs/main.sqlite");
    store.update([](AppSettings& saved) { saved.windowWidth = 900; saved.windowX = 40; });
    auto afterWindow = store.load();
    REQUIRE(afterWindow.theme == ThemeMode::Dark);
    REQUIRE(afterWindow.windowWidth == 900); REQUIRE(afterWindow.windowX == 40);
    REQUIRE(afterWindow.databasePath == "/catalogs/main.sqlite");
}
TEST_CASE("Page size persists and invalid values fall back", "[settings][paging]")
{
    TestDirectory t; auto file = t.dir.getChildFile("settings.json");
    SettingsStore store(file);
    AppSettings settings; settings.pageSize = 0;
    store.save(settings);
    REQUIRE(store.load().pageSize == 0);
    settings.pageSize = 100; store.save(settings);
    REQUIRE(store.load().pageSize == 100);
    file.replaceWithText("{\"pageSize\":7}");
    REQUIRE(store.load().pageSize == 10000);
}

TEST_CASE("Search history round-trips and is capped at ten entries", "[settings][search]")
{
    TestDirectory t;
    SettingsStore store(t.dir.getChildFile("settings.json"));
    AppSettings settings; settings.databasePath = "/catalog.sqlite";
    for (int i = 0; i < 12; ++i) settings.searchHistory.add("query " + juce::String(i));
    while (settings.searchHistory.size() > 10) settings.searchHistory.remove(10);
    store.save(settings);
    const auto loaded = store.load();
    REQUIRE(loaded.searchHistory.size() == 10);
    REQUIRE(loaded.searchHistory[0] == "query 0");
}
TEST_CASE("Preview toggles round-trip and defaults are stable", "[settings][preview]")
{
    TestDirectory t;
    SettingsStore store(t.dir.getChildFile("preview.json"));
    const auto defaults = store.load();
    REQUIRE_FALSE(defaults.midiPreview);
    REQUIRE_FALSE(defaults.skipSilence);
    REQUIRE(defaults.autoPreview);
    REQUIRE_FALSE(defaults.loopPreview);
    REQUIRE(defaults.rexGate);
    AppSettings settings;
    settings.midiPreview = true;
    settings.skipSilence = true;
    settings.autoPreview = false;
    settings.loopPreview = true;
    settings.rexGate = false;
    store.save(settings);
    const auto loaded = store.load();
    REQUIRE(loaded.midiPreview);
    REQUIRE(loaded.skipSilence);
    REQUIRE_FALSE(loaded.autoPreview);
    REQUIRE(loaded.loopPreview);
    REQUIRE_FALSE(loaded.rexGate);
    // Malformed values keep the defaults instead of being read as true.
    REQUIRE(store.file().replaceWithText("{\"midiPreview\":\"yes\",\"autoPreview\":1}"));
    const auto malformed = store.load();
    REQUIRE_FALSE(malformed.midiPreview);
    REQUIRE(malformed.autoPreview);
}
TEST_CASE("Favorite folders are global and round-trip through settings", "[settings][favorites]")
{
    TestDirectory t;
    SettingsStore store(t.dir.getChildFile("favorites.json"));
    REQUIRE(store.load().favoriteFolders.isEmpty());
    store.update([](AppSettings& saved)
    {
        saved.favoriteFolders.add("/lib/Drums");
        saved.favoriteFolders.add("");
        saved.favoriteFolders.add("/lib/Bass");
    });
    auto loaded = store.load();
    REQUIRE(loaded.favoriteFolders.size() == 2);
    REQUIRE(loaded.favoriteFolders[0] == "/lib/Drums");
    REQUIRE(loaded.favoriteFolders[1] == "/lib/Bass");
    // An unrelated update must keep the favorite list.
    store.update([](AppSettings& saved) { saved.hideEmpty = true; });
    loaded = store.load();
    REQUIRE(loaded.favoriteFolders.size() == 2);
    REQUIRE(loaded.hideEmpty);
    store.update([](AppSettings& saved) { saved.favoriteFolders.remove(0); });
    REQUIRE(store.load().favoriteFolders.size() == 1);
    REQUIRE(store.load().favoriteFolders[0] == "/lib/Bass");
    store.update([](AppSettings& saved) { saved.favoritesPanelHeight = 320; });
    REQUIRE(store.load().favoritesPanelHeight == 320);
    store.file().replaceWithText("{\"favoritesPanelHeight\":-4}");
    REQUIRE(store.load().favoritesPanelHeight == 200);
}

TEST_CASE("Folder display names round-trip without touching the real path", "[settings][folder-alias]")
{
    TestDirectory t;
    SettingsStore store(t.dir.getChildFile("aliases.json"));
    REQUIRE(store.load().folderAliases.size() == 0);
    store.update([](AppSettings& saved)
    {
        saved.folderAliases.set("/lib/Drums and Percussion", "Drums");
        saved.folderAliases.set("/lib/Bass", "Low end");
    });
    auto loaded = store.load();
    REQUIRE(loaded.folderAliases.size() == 2);
    REQUIRE(loaded.folderAliases.getValue("/lib/Drums and Percussion", {}) == "Drums");
    REQUIRE(loaded.folderAliases.getValue("/lib/Bass", {}) == "Low end");
    // An unrelated settings writer must keep the alias table.
    store.update([](AppSettings& saved) { saved.hideEmpty = false; });
    loaded = store.load();
    REQUIRE(loaded.folderAliases.size() == 2);
    REQUIRE_FALSE(loaded.hideEmpty);
    store.update([](AppSettings& saved) { saved.folderAliases.remove(juce::StringRef("/lib/Bass")); });
    loaded = store.load();
    REQUIRE(loaded.folderAliases.size() == 1);
    REQUIRE(loaded.folderAliases.getValue("/lib/Drums and Percussion", {}) == "Drums");
}

TEST_CASE("Show ignored persists and defaults to off", "[settings][ignore]")
{
    TestDirectory t; SettingsStore store(t.dir.getChildFile("settings.json"));
    REQUIRE_FALSE(store.load().showIgnored);
    store.update([](AppSettings& s) { s.showIgnored = true; });
    REQUIRE(store.load().showIgnored);
}

TEST_CASE("Split defaults persist and clamp to the supported ranges", "[settings][split]")
{
    TestDirectory t; SettingsStore store(t.dir.getChildFile("settings.json"));
    auto loaded = store.load();
    REQUIRE(loaded.splitThresholdDb == -60.0);
    REQUIRE(loaded.splitMinSilenceMs == 250);
    REQUIRE(loaded.splitMinPartMs == 100);
    REQUIRE(loaded.splitSameFolder);
    REQUIRE_FALSE(loaded.splitMoveToTrash);
    store.update([](AppSettings& s)
    {
        s.splitThresholdDb = -45.0; s.splitMinSilenceMs = 800;
        s.splitMinPartMs = 200; s.splitSameFolder = false; s.splitMoveToTrash = true;
    });
    loaded = store.load();
    REQUIRE(loaded.splitThresholdDb == -45.0);
    REQUIRE(loaded.splitMinSilenceMs == 800);
    REQUIRE(loaded.splitMinPartMs == 200);
    REQUIRE_FALSE(loaded.splitSameFolder);
    REQUIRE(loaded.splitMoveToTrash);
}

TEST_CASE("Extract defaults persist", "[settings][extract]")
{
    TestDirectory t; SettingsStore store(t.dir.getChildFile("settings.json"));
    auto loaded = store.load();
    REQUIRE(loaded.extractSameFolder);
    REQUIRE_FALSE(loaded.extractNormalize);
    REQUIRE(loaded.extractTuneToSong);
    REQUIRE(loaded.extractOutputFolder.isEmpty());
    store.update([](AppSettings& s)
    {
        s.extractSameFolder = false; s.extractNormalize = true;
        s.extractTuneToSong = false;
        s.extractOutputFolder = "/tmp/extract-out";
    });
    loaded = store.load();
    REQUIRE_FALSE(loaded.extractSameFolder);
    REQUIRE(loaded.extractNormalize);
    REQUIRE_FALSE(loaded.extractTuneToSong);
    REQUIRE(loaded.extractOutputFolder == "/tmp/extract-out");
}
