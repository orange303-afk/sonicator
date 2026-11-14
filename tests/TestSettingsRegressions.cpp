#include <catch2/catch_test_macros.hpp>
#include "ui/MainComponent.h"
#include "ui/preview/EditCommands.h"
#include "ui/preview/PreviewPanel.h"
#include "ui/settings/SettingsPanel.h"
#include "ui/settings/HotkeysPanel.h"
#include "ui/commands/ShortcutBindings.h"
#include "TestHelpers.h"

using namespace sonicator;

namespace
{
SettingsPanel* openEmbeddedSettings(MainComponent& app)
{
    juce::TextButton* button = nullptr;
    for (int i = 0; i < app.getNumChildComponents() && button == nullptr; ++i)
        if (auto* candidate = dynamic_cast<juce::TextButton*>(app.getChildComponent(i)))
            if (candidate->getButtonText() == "Settings") button = candidate;
    if (button == nullptr) return nullptr;
    button->onClick();
    auto* overlay = app.findChildWithID("settingsOverlay");
    if (overlay == nullptr) return nullptr;
    return dynamic_cast<SettingsPanel*>(findDescendantWithID(*overlay, "settingsPanel"));
}
}

TEST_CASE("A rebound waveform key changes the menu hint", "[settings][hotkeys][regression]")
{
    // The menu reads the live profile, so a hint can never advertise a key that does nothing.
    const auto defaultText = editCommandShortcutText(EditAction::save);
    REQUIRE(defaultText.isNotEmpty());
    ShortcutProfile profile;
    profile.overrides["waveform.save"] = { ShortcutKey { 'j', true, false, false, false } };
    setEditCommandProfileProvider([&profile] { return profile; });
    const auto rebound = editCommandShortcutText(EditAction::save);
    REQUIRE(rebound != defaultText);
    REQUIRE(rebound.containsIgnoreCase("j"));
    // An explicitly removed key shows no hint at all.
    profile.overrides["waveform.save"] = {};
    REQUIRE(editCommandShortcutText(EditAction::save).isEmpty());
    setEditCommandProfileProvider(nullptr);
    REQUIRE(editCommandShortcutText(EditAction::save) == defaultText);
}

TEST_CASE("Escape clears the selection and stops the transport", "[settings][regression]")
{
    TestDirectory t;
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"));
    auto file = preview.loadFile(t.wave("escape.wav", 440, 2, 44100 * 2));
    REQUIRE(file);
    REQUIRE(engine.isPlaying());
    preview.selectRange(0.2, 0.4);
    REQUIRE(preview.selectedRange().valid());
    preview.stopAndClearSelection();
    REQUIRE_FALSE(engine.isPlaying());
    REQUIRE_FALSE(preview.selectedRange().valid());
}

TEST_CASE("The selected Settings section is remembered", "[settings][regression]")
{
    TestDirectory t;
    const auto settingsFile = t.dir.getChildFile("settings.json");
    const auto dbPath = t.dir.getChildFile("catalog.sqlite");
    {
        Database db(dbPath);
        SampleRepository repo(db);
        repo.createTables();
    }
    AudioEngine engine(false);
    MainComponent app(dbPath, false, {}, settingsFile, &engine, true);
    app.setSize(1400, 900);
    auto* panel = openEmbeddedSettings(app);
    REQUIRE(panel != nullptr);
    panel->selectSection(SettingsSection::hotkeys);
    AppSettings stored = SettingsStore(settingsFile).load();
    REQUIRE(stored.lastSettingsSection == "hotkeys");
    panel->selectSection(SettingsSection::about);
    REQUIRE(SettingsStore(settingsFile).load().lastSettingsSection == "about");
}

TEST_CASE("Leaving Hotkeys cancels a running capture", "[settings][regression]")
{
    CommandRegistry registry;
    CommandSpec spec;
    spec.id = "waveform.normalize";
    spec.name = "Normalize";
    spec.category = "Waveform edit";
    spec.scope = CommandScope::waveform;
    spec.defaults = { juce::KeyPress('n') };
    registry.add(spec, {}, [] {});
    SettingsPanel panel(AppSettings{}, juce::File("/tmp/catalog.sqlite"), false);
    panel.setCommandRegistry(&registry);
    panel.selectSection(SettingsSection::hotkeys);
    auto* hotkeys = dynamic_cast<HotkeysPanel*>(findDescendantWithID(panel, "hotkeysPanel"));
    REQUIRE(hotkeys != nullptr);
    hotkeys->selectCommandRow(0);
    hotkeys->beginCapture();
    REQUIRE(hotkeys->isCapturing());
    panel.selectSection(SettingsSection::general);
    panel.selectSection(SettingsSection::hotkeys);
    REQUIRE_FALSE(hotkeys->isCapturing());
}

TEST_CASE("A restored Audio page builds the embedded selector", "[settings][regression]")
{
    AppSettings settings;
    settings.lastSettingsSection = "audioMidi";
    SettingsPanel panel(settings, juce::File("/tmp/catalog.sqlite"), false);
    REQUIRE(panel.selectedSection() == SettingsSection::audioMidi);
    int built = 0;
    panel.setAudioSettingsFactory([&built]
    {
        ++built;
        auto component = std::make_unique<juce::Component>();
        component->setComponentID("settingsAudioPage");
        return component;
    });
    REQUIRE(built == 1);
    REQUIRE(findDescendantWithID(panel, "settingsAudioPage") != nullptr);
}

TEST_CASE("Reset layout stores the Favorites height too", "[settings][regression]")
{
    TestDirectory t;
    AppSettings settings;
    settings.favoritesPanelHeight = 333;
    settings.standaloneLayout.favoritesHeight = 333;
    const auto settingsFile = t.dir.getChildFile("settings.json");
    SettingsStore(settingsFile).save(settings);
    MainComponent app(t.dir.getChildFile("catalog.sqlite"), false, settings, settingsFile);
    app.setSize(1500, 900);
    app.resetLayout();
    const auto stored = SettingsStore(settingsFile).load();
    REQUIRE(stored.favoritesPanelHeight == defaultPanelLayout(1500).favoritesHeight);
    REQUIRE(stored.standaloneLayout.favoritesHeight == defaultPanelLayout(1500).favoritesHeight);
}

TEST_CASE("A conflicting stored binding is reported, not run twice", "[settings][regression]")
{
    CommandRegistry registry;
    CommandSpec waveform;
    waveform.id = "waveform.normalize";
    waveform.name = "Normalize";
    waveform.category = "Waveform edit";
    waveform.scope = CommandScope::waveform;
    waveform.defaults = { juce::KeyPress('n') };
    registry.add(waveform, {}, [] {});
    CommandSpec app;
    app.id = "app.quick";
    app.name = "Quick";
    app.category = "Application";
    app.scope = CommandScope::app;
    registry.add(app, {}, [] {});
    ShortcutProfile profile;
    profile.overrides["app.quick"] = { ShortcutKey { 'n', false, false, false, false } };
    juce::StringArray diagnostics;
    const auto safeProfile = sanitizedProfile(registry, profile, &diagnostics);
    REQUIRE_FALSE(diagnostics.isEmpty());
    CommandRouter router(registry, [&safeProfile] { return safeProfile; });
    CommandFocus focus;
    focus.panel = CommandScope::waveform;
    REQUIRE(router.resolve(juce::KeyPress('n'), focus) == "waveform.normalize");
    REQUIRE(router.resolve(juce::KeyPress('n'), CommandFocus { CommandScope::library }) != "app.quick");
}
