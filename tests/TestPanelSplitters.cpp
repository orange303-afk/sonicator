#include <catch2/catch_test_macros.hpp>
#include "ui/MainComponent.h"
#include "ui/shell/PanelSplitter.h"
#include "ui/shell/PanelLayout.h"
#include "TestHelpers.h"

using namespace sonicator;

namespace
{
juce::MouseEvent makeMouseEvent(juce::Component& target, juce::Point<float> position,
                                juce::Point<float> downPosition, bool buttonDown)
{
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position,
                            juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target,
                            now, downPosition, now, 1, buttonDown);
}
}

TEST_CASE("A splitter reports a drag from its starting size", "[layout][splitter]")
{
    PanelSplitter splitter(PanelSplitter::Axis::columns, "folders");
    splitter.getPreferred = [] { return 240; };
    int began = 0, dragged = 0, committed = 0;
    splitter.onDragBegin = [&began](int base) { began = base; };
    splitter.onDrag = [&dragged](int delta) { dragged = delta; };
    splitter.onCommit = [&committed] { ++committed; };
    splitter.setBounds(0, 0, panelDividerThickness, 600);

    splitter.mouseDown(makeMouseEvent(splitter, { 4.0f, 4.0f }, { 4.0f, 4.0f }, true));
    REQUIRE(began == 240);
    splitter.mouseDrag(makeMouseEvent(splitter, { 44.0f, 4.0f }, { 4.0f, 4.0f }, true));
    REQUIRE(dragged == 40);
    splitter.mouseUp(makeMouseEvent(splitter, { 44.0f, 4.0f }, { 4.0f, 4.0f }, false));
    REQUIRE(committed == 1);
}

TEST_CASE("Double-clicking a splitter asks for a reset", "[layout][splitter]")
{
    PanelSplitter splitter(PanelSplitter::Axis::rows, "preview");
    int resets = 0;
    splitter.onReset = [&resets] { ++resets; };
    splitter.setBounds(0, 0, 600, panelDividerThickness);
    splitter.mouseDoubleClick(makeMouseEvent(splitter, { 4.0f, 4.0f }, { 4.0f, 4.0f }, false));
    REQUIRE(resets == 1);
}

TEST_CASE("The workspace uses the stored panel widths", "[layout][ui]")
{
    TestDirectory t;
    AppSettings settings;
    settings.standaloneLayout.foldersWidth = 300;
    settings.standaloneLayout.analysisWidth = 320;
    MainComponent app(t.dir.getChildFile("catalog.sqlite"), false, settings,
                      t.dir.getChildFile("settings.json"));
    app.setSize(1600, 900);
    REQUIRE(app.currentLayout().folders.getWidth() == 300);
}

TEST_CASE("Shrinking the window keeps the stored panel preference", "[layout][ui]")
{
    TestDirectory t;
    AppSettings settings;
    settings.standaloneLayout.foldersWidth = 400;
    const auto settingsFile = t.dir.getChildFile("settings.json");
    SettingsStore(settingsFile).save(settings);
    MainComponent app(t.dir.getChildFile("catalog.sqlite"), false, settings, settingsFile);
    app.setSize(1700, 950);
    REQUIRE(app.currentLayout().folders.getWidth() == 400);
    app.setSize(900, 620);
    REQUIRE(app.currentLayout().folders.getWidth() < 400);
    app.setSize(1700, 950);
    REQUIRE(app.currentLayout().folders.getWidth() == 400);
    // The stored preference was never replaced by the small window's effective size.
    AppSettings reloaded = SettingsStore(settingsFile).load();
    REQUIRE(reloaded.standaloneLayout.foldersWidth == 400);
}

TEST_CASE("Reset layout restores every panel and leaves other settings alone", "[layout][ui]")
{
    TestDirectory t;
    AppSettings settings;
    settings.standaloneLayout.foldersWidth = 420;
    settings.standaloneLayout.analysisWidth = 360;
    settings.standaloneLayout.previewHeight = 240;
    settings.shortcuts.mac.overrides["waveform.crop"] = { ShortcutKey { 'c', false, false, false, true } };
    const auto settingsFile = t.dir.getChildFile("settings.json");
    SettingsStore(settingsFile).save(settings);
    MainComponent app(t.dir.getChildFile("catalog.sqlite"), false, settings, settingsFile);
    app.setSize(1600, 900);
    app.resetLayout();
    const auto reloaded = SettingsStore(settingsFile).load();
    const auto defaults = defaultPanelLayout(1600);
    REQUIRE(reloaded.standaloneLayout.foldersWidth == defaults.foldersWidth);
    REQUIRE(reloaded.standaloneLayout.analysisWidth == defaults.analysisWidth);
    REQUIRE(reloaded.standaloneLayout.previewHeight == defaults.previewHeight);
    REQUIRE(reloaded.standaloneLayout.favoritesHeight == defaults.favoritesHeight);
    // Shortcuts, window geometry and the catalog path are not part of "Reset layout".
    REQUIRE(reloaded.shortcuts.mac.overrides.count("waveform.crop") == 1);
}

TEST_CASE("Resetting one panel leaves the others as they were", "[layout][ui]")
{
    TestDirectory t;
    AppSettings settings;
    settings.standaloneLayout.foldersWidth = 420;
    settings.standaloneLayout.analysisWidth = 360;
    const auto settingsFile = t.dir.getChildFile("settings.json");
    MainComponent app(t.dir.getChildFile("catalog.sqlite"), false, settings, settingsFile);
    app.setSize(1600, 900);
    app.resetPanelSize("folders");
    const auto reloaded = SettingsStore(settingsFile).load();
    REQUIRE(reloaded.standaloneLayout.foldersWidth == defaultPanelLayout(1600).foldersWidth);
    REQUIRE(reloaded.standaloneLayout.analysisWidth == 360);
}

TEST_CASE("The preview keeps a usable height and the dividers stay visible", "[layout][ui]")
{
    TestDirectory t;
    AppSettings settings;
    settings.standaloneLayout.previewHeight = 900; // absurd for the window
    MainComponent app(t.dir.getChildFile("catalog.sqlite"), false, settings,
                      t.dir.getChildFile("settings.json"));
    app.setSize(1400, 800);
    const auto* preview = findDescendantWithID(app, "preview");
    REQUIRE(preview != nullptr);
    REQUIRE(preview->getHeight() > 0);
    REQUIRE(preview->getHeight() < 800);
    const auto* divider = findDescendantWithID(app, "previewDivider");
    REQUIRE(divider != nullptr);
    REQUIRE(divider->isVisible());
}
