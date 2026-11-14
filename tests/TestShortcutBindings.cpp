#include <catch2/catch_test_macros.hpp>
#include "ui/commands/ShortcutBindings.h"
#include "core/settings/AppSettings.h"
#include "TestHelpers.h"

using namespace sonicator;

namespace
{
CommandSpec makeSpec(juce::String id, CommandScope scope, std::vector<juce::KeyPress> defaults = {})
{
    CommandSpec spec;
    spec.id = std::move(id);
    spec.name = spec.id;
    spec.category = "Test";
    spec.scope = scope;
    spec.defaults = std::move(defaults);
    return spec;
}

juce::KeyPress key(int code, int modifiers = 0)
{
    return juce::KeyPress(code, juce::ModifierKeys(modifiers), 0);
}
}

TEST_CASE("No override means the command's defaults", "[commands][bindings]")
{
    const auto spec = makeSpec("waveform.normalize", CommandScope::waveform, { key('n') });
    ShortcutProfile profile;
    const auto keys = effectiveShortcuts(spec, profile);
    REQUIRE(keys.size() == 1);
    REQUIRE(shortcutMatches(keys[0], key('N')));
}

TEST_CASE("An empty override removes every key deliberately", "[commands][bindings]")
{
    auto spec = makeSpec("waveform.normalize", CommandScope::waveform, { key('n') });
    ShortcutProfile profile;
    profile.overrides[spec.id] = {};
    REQUIRE(profile.hasOverride(spec.id));
    REQUIRE(effectiveShortcuts(spec, profile).empty());
}

TEST_CASE("Several keys per command round-trip", "[commands][bindings]")
{
    auto spec = makeSpec("waveform.deleteRegion", CommandScope::waveform,
                         { key(juce::KeyPress::backspaceKey), key(juce::KeyPress::deleteKey) });
    ShortcutProfile profile;
    profile.overrides[spec.id] = storedKeysFromKeyPresses(spec.defaults);
    const auto keys = effectiveShortcuts(spec, profile);
    REQUIRE(keys.size() == 2);
    REQUIRE(shortcutMatches(keys[0], key(juce::KeyPress::backspaceKey)));
    REQUIRE(shortcutMatches(keys[1], key(juce::KeyPress::deleteKey)));
}

TEST_CASE("Command and Control stay distinct", "[commands][bindings]")
{
    ShortcutKey command;
    command.keyCode = 'z';
    command.command = true;
    ShortcutKey control;
    control.keyCode = 'z';
    control.control = true;
    REQUIRE(command != control);
    const auto commandKey = keyPressFromShortcutKey(command);
    const auto controlKey = keyPressFromShortcutKey(control);
    REQUIRE(commandKey != controlKey);
}

TEST_CASE("Conflicts only exist inside overlapping scopes", "[commands][bindings]")
{
    REQUIRE(scopesOverlap(CommandScope::app, CommandScope::library));
    REQUIRE(scopesOverlap(CommandScope::app, CommandScope::waveform));
    REQUIRE(scopesOverlap(CommandScope::library, CommandScope::library));
    REQUIRE_FALSE(scopesOverlap(CommandScope::library, CommandScope::waveform));
    REQUIRE_FALSE(scopesOverlap(CommandScope::folders, CommandScope::discBrowser));
}

TEST_CASE("Duplicate keys in overlapping scopes are reported", "[commands][bindings]")
{
    CommandRegistry registry;
    registry.add(makeSpec("waveform.normalize", CommandScope::waveform, { key('n') }), {}, [] {});
    registry.add(makeSpec("library.normalizeSelected", CommandScope::library, { key('n') }), {}, [] {});
    registry.add(makeSpec("app.settings", CommandScope::app, { key(',', juce::ModifierKeys::commandModifier) }),
                 {}, [] {});
    ShortcutProfile profile;
    // Library and waveform do not overlap, so N may mean two things there.
    REQUIRE(conflictingCommands(registry, profile, "waveform.normalize", { key('n') }).empty());
    // An application command is reachable from every panel, so it conflicts with any panel key.
    registry.replace(makeSpec("app.quick", CommandScope::app, { key('n') }), {}, [] {});
    const auto conflicts = conflictingCommands(registry, profile, "waveform.normalize", { key('n') });
    REQUIRE(conflicts.size() == 1);
    REQUIRE(conflicts[0] == "app.quick");
}

TEST_CASE("A conflicting user binding is dropped, defaults are kept", "[commands][bindings]")
{
    CommandRegistry registry;
    registry.add(makeSpec("waveform.normalize", CommandScope::waveform, { key('n') }), {}, [] {});
    registry.add(makeSpec("app.quick", CommandScope::app, { key('q') }), {}, [] {});
    ShortcutProfile profile;
    // The user binds N to an application command, which is reachable from the waveform panel.
    profile.overrides["app.quick"] = storedKeysFromKeyPresses({ key('n') });
    juce::StringArray diagnostics;
    const auto sanitized = sanitizedProfile(registry, profile, &diagnostics);
    REQUIRE(diagnostics.size() == 1);
    REQUIRE(sanitized.overrides.count("app.quick") == 1);
    REQUIRE(sanitized.overrides.at("app.quick").empty());
    REQUIRE(shortcutMatches(effectiveShortcuts(*registry.find("waveform.normalize"), sanitized)[0], key('n')));
}

TEST_CASE("Unknown command ids survive sanitising", "[commands][bindings]")
{
    CommandRegistry registry;
    registry.add(makeSpec("waveform.normalize", CommandScope::waveform, { key('n') }), {}, [] {});
    ShortcutProfile profile;
    profile.overrides["component.removed.action"] = { ShortcutKey { 'k', false, false, false, true } };
    const auto sanitized = sanitizedProfile(registry, profile);
    REQUIRE(sanitized.overrides.count("component.removed.action") == 1);
    REQUIRE(sanitized.overrides.at("component.removed.action").size() == 1);
}

TEST_CASE("A newer shortcut schema is preserved and not executed", "[commands][bindings]")
{
    ShortcutProfile profile;
    profile.schemaVersion = 99;
    profile.readable = false;
    profile.overrides["waveform.normalize"] = { ShortcutKey { 'x', false, false, false, false } };
    CommandRegistry registry;
    registry.add(makeSpec("waveform.normalize", CommandScope::waveform, { key('n') }), {}, [] {});
    const auto sanitized = sanitizedProfile(registry, profile);
    REQUIRE(sanitized.overrides.count("waveform.normalize") == 1);
    REQUIRE(sanitized.overrides.at("waveform.normalize").size() == 1);
    REQUIRE(sanitized.overrides.at("waveform.normalize")[0].keyCode == 'x');
}

TEST_CASE("Shortcut preferences round-trip through JSON per platform", "[commands][bindings][settings]")
{
    ShortcutPreferences preferences;
    preferences.mac.overrides["waveform.save"] = { ShortcutKey { 's', true, false, false, false } };
    preferences.windows.overrides["waveform.save"] = { ShortcutKey { 's', false, true, false, false } };
    preferences.windows.overrides["waveform.crop"] = {};
    auto parsed = shortcutPreferencesFromVar(shortcutPreferencesToVar(preferences));
    REQUIRE(parsed.mac.overrides.at("waveform.save").size() == 1);
    REQUIRE(parsed.mac.overrides.at("waveform.save")[0].command);
    REQUIRE_FALSE(parsed.mac.overrides.at("waveform.save")[0].control);
    REQUIRE_FALSE(parsed.windows.overrides.at("waveform.save")[0].command);
    REQUIRE(parsed.windows.overrides.at("waveform.save")[0].control);
    REQUIRE(parsed.windows.overrides.at("waveform.crop").empty());
}

TEST_CASE("Malformed shortcut entries are skipped without losing valid ones", "[commands][bindings][settings]")
{
    auto* mac = new juce::DynamicObject();
    auto* bindings = new juce::DynamicObject();
    juce::Array<juce::var> valid;
    auto* good = new juce::DynamicObject();
    good->setProperty("key", (int) 'a');
    valid.add(juce::var(good));
    auto* bad = new juce::DynamicObject(); // no key code
    valid.add(juce::var(bad));
    valid.add("not an object");
    bindings->setProperty("waveform.crop", juce::var(valid));
    bindings->setProperty("waveform.broken", "not a list");
    mac->setProperty("version", 1);
    mac->setProperty("bindings", juce::var(bindings));
    auto* root = new juce::DynamicObject();
    root->setProperty("mac", juce::var(mac));
    juce::StringArray diagnostics;
    const auto parsed = shortcutPreferencesFromVar(juce::var(root), &diagnostics);
    REQUIRE(parsed.mac.overrides.count("waveform.crop") == 1);
    REQUIRE(parsed.mac.overrides.at("waveform.crop").size() == 1);
    REQUIRE(parsed.mac.overrides.count("waveform.broken") == 0);
    REQUIRE_FALSE(diagnostics.isEmpty());
}

TEST_CASE("Settings store keeps shortcuts and layout beside other fields", "[commands][settings]")
{
    TestDirectory t;
    SettingsStore store(t.dir.getChildFile("shortcuts.json"));
    store.update([](AppSettings& saved)
    {
        saved.theme = ThemeMode::Light;
        saved.shortcuts.mac.overrides["waveform.save"] = { ShortcutKey { 's', true, false, false, false } };
        saved.standaloneLayout.foldersWidth = 300;
        saved.lastSettingsSection = "hotkeys";
    });
    store.update([](AppSettings& saved) { saved.windowWidth = 1280; });
    const auto loaded = store.load();
    REQUIRE(loaded.theme == ThemeMode::Light);
    REQUIRE(loaded.windowWidth == 1280);
    REQUIRE(loaded.shortcuts.mac.overrides.count("waveform.save") == 1);
    REQUIRE(loaded.standaloneLayout.foldersWidth == 300);
    REQUIRE(loaded.pluginLayout.foldersWidth == 0);
    REQUIRE(loaded.lastSettingsSection == "hotkeys");
}

TEST_CASE("Absurd layout values fall back instead of collapsing a panel", "[commands][settings]")
{
    TestDirectory t;
    const auto file = t.dir.getChildFile("layout.json");
    REQUIRE(file.replaceWithText("{\"layoutStandalone\":{\"foldersWidth\":-500,\"analysisWidth\":99999,"
                                 "\"previewHeight\":1}}"));
    SettingsStore store(file);
    const auto loaded = store.load();
    REQUIRE(loaded.standaloneLayout.foldersWidth == 0);
    REQUIRE(loaded.standaloneLayout.analysisWidth == 0);
    REQUIRE(loaded.standaloneLayout.previewHeight == 156);
}
