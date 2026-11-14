#include <catch2/catch_test_macros.hpp>
#include "ui/shell/PanelLayout.h"
#include "core/settings/AppSettings.h"
#include "TestHelpers.h"

using namespace sonicator;

namespace
{
void requireDisjoint(const PanelLayoutBounds& layout)
{
    const std::array<juce::Rectangle<int>, 3> panels { layout.folders, layout.library, layout.analysis };
    for (auto& panel : panels)
    {
        REQUIRE(panel.getWidth() >= 1);
        REQUIRE(panel.getHeight() >= 1);
    }
    for (size_t i = 0; i < panels.size(); ++i)
        for (size_t j = i + 1; j < panels.size(); ++j)
            REQUIRE_FALSE(panels[i].intersects(panels[j]));
    REQUIRE_FALSE(layout.preview.intersects(layout.folders));
    REQUIRE_FALSE(layout.preview.intersects(layout.library));
    REQUIRE_FALSE(layout.preview.intersects(layout.analysis));
}
}

TEST_CASE("The default layout matches the previous adaptive widths", "[layout]")
{
    REQUIRE(defaultPanelLayout(1400).foldersWidth == 240);
    REQUIRE(defaultPanelLayout(1400).analysisWidth == 270);
    REQUIRE(defaultPanelLayout(900).foldersWidth == 190);
    REQUIRE(defaultPanelLayout(900).analysisWidth == 230);
    REQUIRE(defaultPanelLayout(1400).previewHeight == minPreviewHeight);
}

TEST_CASE("A roomy window keeps the preferred widths and three dividers", "[layout]")
{
    PanelLayoutPreferences preferred;
    preferred.foldersWidth = 300;
    preferred.analysisWidth = 340;
    preferred.previewHeight = 200;
    const auto layout = computePanelLayout({ 0, 0, 1600, 900 }, preferred);
    REQUIRE_FALSE(layout.compact);
    REQUIRE(layout.folders.getWidth() == 300);
    REQUIRE(layout.analysis.getWidth() == 340);
    REQUIRE(layout.preview.getHeight() == 200);
    REQUIRE(layout.foldersDivider.getWidth() == panelDividerThickness);
    REQUIRE(layout.analysisDivider.getWidth() == panelDividerThickness);
    REQUIRE(layout.previewDivider.getHeight() == panelDividerThickness);
    REQUIRE(layout.folders.getHeight() == layout.library.getHeight());
    REQUIRE(layout.preview.getWidth() == 1600);
    requireDisjoint(layout);
}

TEST_CASE("Zero preference falls back to the adaptive default", "[layout]")
{
    PanelLayoutPreferences preferred;
    const auto layout = computePanelLayout({ 0, 0, 900, 700 }, preferred);
    REQUIRE(layout.folders.getWidth() == 190);
    REQUIRE(layout.analysis.getWidth() == 230);
}

TEST_CASE("Shrinking the window does not overwrite the preference", "[layout]")
{
    PanelLayoutPreferences preferred;
    preferred.foldersWidth = 400;
    preferred.analysisWidth = 400;
    const auto small = computePanelLayout({ 0, 0, 900, 600 }, preferred);
    REQUIRE(small.compact);
    REQUIRE(small.folders.getWidth() < 400);
    REQUIRE(preferred.foldersWidth == 400); // compute takes a const reference
    const auto large = computePanelLayout({ 0, 0, 1800, 900 }, preferred);
    REQUIRE(large.folders.getWidth() == 400);
    REQUIRE(large.analysis.getWidth() == 400);
}

TEST_CASE("A tiny workspace still yields non-overlapping panels", "[layout]")
{
    PanelLayoutPreferences preferred;
    preferred.foldersWidth = 600;
    preferred.analysisWidth = 600;
    preferred.previewHeight = 600;
    for (const auto size : { juce::Point<int>(120, 90), juce::Point<int>(60, 40), juce::Point<int>(20, 12) })
    {
        INFO("workspace " << size.x << "x" << size.y);
        const auto layout = computePanelLayout({ 0, 0, size.x, size.y }, preferred);
        // The extreme cases clamp each panel to one pixel rather than overlapping.
        REQUIRE(layout.folders.getWidth() >= 0);
        REQUIRE(layout.analysis.getWidth() >= 0);
        REQUIRE(layout.library.getWidth() >= 0);
        REQUIRE_FALSE(layout.folders.intersects(layout.library));
        REQUIRE_FALSE(layout.library.intersects(layout.analysis));
        REQUIRE_FALSE(layout.preview.intersects(layout.library));
        REQUIRE(layout.preview.getHeight() >= 0);
    }
}

TEST_CASE("Negative or absurd layout values stay safe", "[layout][settings]")
{
    TestDirectory t;
    const auto file = t.dir.getChildFile("layout.json");
    REQUIRE(file.replaceWithText("{\"layoutStandalone\":{\"foldersWidth\":-500,\"previewHeight\":-9}}"));
    SettingsStore store(file);
    const auto layout = store.load().standaloneLayout;
    const auto bounds = computePanelLayout({ 0, 0, 1000, 700 }, layout);
    REQUIRE(bounds.folders.getWidth() > 0);
    REQUIRE(bounds.library.getWidth() > 0);
}

TEST_CASE("Panel sizes persist separately for standalone and plugin", "[layout][settings]")
{
    TestDirectory t;
    SettingsStore store(t.dir.getChildFile("layout.json"));
    store.update([](AppSettings& saved)
    {
        saved.standaloneLayout.foldersWidth = 320;
        saved.standaloneLayout.previewHeight = 220;
        saved.pluginLayout.foldersWidth = 180;
    });
    const auto loaded = store.load();
    REQUIRE(loaded.standaloneLayout.foldersWidth == 320);
    REQUIRE(loaded.standaloneLayout.previewHeight == 220);
    REQUIRE(loaded.pluginLayout.foldersWidth == 180);
    REQUIRE(loaded.pluginLayout.previewHeight == 156);
}
