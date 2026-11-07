#include <catch2/catch_test_macros.hpp>
#include "core/settings/AppSettings.h"
#include "TestHelpers.h"
using namespace sonicator;
TEST_CASE("Hide empty defaults off and survives unrelated settings updates", "[settings][hide-empty]")
{
    TestDirectory t;
    const auto file = t.dir.getChildFile("preferences.json");
    SettingsStore store(file);
    REQUIRE_FALSE(store.load().hideEmpty);
    REQUIRE(file.replaceWithText("{\"hideEmpty\":\"false\"}"));
    REQUIRE_FALSE(store.load().hideEmpty); // malformed strings are not interpreted as true
    store.update([](AppSettings& saved) { saved.hideEmpty = true; });
    store.update([](AppSettings& saved) { saved.windowWidth = 1200; saved.theme = ThemeMode::Light; });
    REQUIRE(store.load().hideEmpty);
    store.update([](AppSettings& saved) { saved.hideEmpty = false; });
    REQUIRE_FALSE(store.load().hideEmpty);
    REQUIRE(store.load().windowWidth == 1200);
    REQUIRE(store.load().theme == ThemeMode::Light);
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
