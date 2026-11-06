#include <catch2/catch_test_macros.hpp>
#include "core/settings/AppSettings.h"
#include "TestHelpers.h"
using namespace sonicator;
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
