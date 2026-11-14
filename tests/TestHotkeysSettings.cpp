#include <catch2/catch_test_macros.hpp>
#include "ui/settings/HotkeysPanel.h"
#include "ui/commands/CommandRouter.h"

using namespace sonicator;

namespace
{
CommandSpec command(juce::String id, juce::String name, juce::String category, CommandScope scope,
                    std::vector<juce::KeyPress> defaults = {})
{
    CommandSpec spec;
    spec.id = std::move(id);
    spec.name = std::move(name);
    spec.category = std::move(category);
    spec.scope = scope;
    spec.defaults = std::move(defaults);
    return spec;
}

juce::KeyPress key(int code, int modifiers = 0)
{
    return juce::KeyPress(code, juce::ModifierKeys(modifiers), 0);
}

struct Fixture
{
    CommandRegistry registry;
    HotkeysPanel panel { registry };
    ShortcutProfile published;
    int writes = 0;
    bool accept = true;

    Fixture()
    {
        registry.add(command("waveform.normalize", "Normalize", "Waveform edit", CommandScope::waveform,
                             { key('n') }), {}, [] {});
        registry.add(command("waveform.crop", "Crop", "Waveform edit", CommandScope::waveform,
                             { key('c') }), {}, [] {});
        registry.add(command("library.selectAll", "Select all", "Library", CommandScope::library,
                             { key('a', juce::ModifierKeys::commandModifier) }), {}, [] {});
        panel.reload();
        panel.setProfile(published);
        panel.onProfileChanged = [this](const ShortcutProfile& profile)
        {
            ++writes;
            if (! accept) return false;
            published = profile;
            return true;
        };
        panel.onConfirmResetAll = [](std::function<void(bool)> done) { done(true); };
        panel.setSize(520, 460);
    }
};

std::vector<juce::KeyPress> keysOf(const CommandRegistry& registry, const ShortcutProfile& profile,
                                   const juce::String& id)
{
    return effectiveShortcuts(*registry.find(id), profile);
}
}

TEST_CASE("The hotkey list shows every command and filters by id or name", "[settings][hotkeys]")
{
    Fixture f;
    REQUIRE(f.panel.commandRows() == 3);
    f.panel.setSearch("crop");
    REQUIRE(f.panel.commandRows() == 1);
    REQUIRE(f.panel.rowCommandId(0) == "waveform.crop");
    f.panel.setSearch("library");
    REQUIRE(f.panel.commandRows() == 1);
    REQUIRE(f.panel.rowCommandId(0) == "library.selectAll");
    f.panel.setSearch("waveform.normalize");
    REQUIRE(f.panel.commandRows() == 1);
    f.panel.setSearch("");
    REQUIRE(f.panel.commandRows() == 3);
}

TEST_CASE("Capturing a key adds it to the command and publishes the profile", "[settings][hotkeys]")
{
    Fixture f;
    f.panel.setProfile(f.published);
    f.panel.setSearch("normalize");
    f.panel.selectCommandRow(0);
    f.panel.beginCapture();
    REQUIRE(f.panel.isCapturing());
    REQUIRE(f.panel.captureKey(key('k', juce::ModifierKeys::altModifier)));
    REQUIRE_FALSE(f.panel.isCapturing());
    REQUIRE(f.panel.pendingKeys().size() == 1);
    f.panel.applyCapture();
    REQUIRE(f.writes == 1);
    const auto keys = keysOf(f.registry, f.published, "waveform.normalize");
    REQUIRE(keys.size() == 2);
    REQUIRE(shortcutMatches(keys[1], key('k', juce::ModifierKeys::altModifier)));
}

TEST_CASE("Escape cancels a capture and leaves the profile alone", "[settings][hotkeys]")
{
    Fixture f;
    f.panel.setSearch("normalize");
    f.panel.selectCommandRow(0);
    f.panel.beginCapture();
    REQUIRE(f.panel.captureKey(key(juce::KeyPress::escapeKey)));
    REQUIRE_FALSE(f.panel.isCapturing());
    REQUIRE(f.panel.pendingKeys().empty());
    REQUIRE(f.writes == 0);
    REQUIRE(f.published.overrides.empty());
}

TEST_CASE("A modifier-only press is swallowed while capturing", "[settings][hotkeys]")
{
    Fixture f;
    f.panel.setSearch("normalize");
    f.panel.selectCommandRow(0);
    f.panel.beginCapture();
    REQUIRE(f.panel.captureKey(juce::KeyPress(0, juce::ModifierKeys::shiftModifier, 0)));
    REQUIRE(f.panel.isCapturing());
    REQUIRE(f.panel.pendingKeys().empty());
}

TEST_CASE("A clash is reported and nothing is published until it is resolved", "[settings][hotkeys]")
{
    Fixture f;
    // Bind an application command's key that the library scope in this fixture can also see.
    f.registry.add(command("app.settings", "Settings", "App", CommandScope::app,
                           { key(',', juce::ModifierKeys::commandModifier) }), {}, [] {});
    f.panel.setProfile(f.published);
    f.panel.setSearch("normalize");
    f.panel.selectCommandRow(0);
    f.panel.beginCapture();
    f.panel.captureKey(key(',', juce::ModifierKeys::commandModifier));
    f.panel.applyCapture();
    REQUIRE(f.writes == 0);
    REQUIRE(f.panel.conflictText().contains("Settings"));
    REQUIRE(f.published.overrides.empty());
    f.panel.replaceConflicting();
    REQUIRE(f.writes == 1);
    REQUIRE(keysOf(f.registry, f.published, "waveform.normalize").size() == 2);
    REQUIRE(keysOf(f.registry, f.published, "app.settings").empty());
}

TEST_CASE("Cancelling after a clash keeps the previous bindings", "[settings][hotkeys]")
{
    Fixture f;
    f.registry.add(command("app.settings", "Settings", "App", CommandScope::app,
                           { key(',', juce::ModifierKeys::commandModifier) }), {}, [] {});
    f.panel.setProfile(f.published);
    f.panel.setSearch("normalize");
    f.panel.selectCommandRow(0);
    f.panel.beginCapture();
    f.panel.captureKey(key(',', juce::ModifierKeys::commandModifier));
    f.panel.applyCapture();
    REQUIRE_FALSE(f.panel.conflictText().isEmpty());
    f.panel.cancelCapture();
    REQUIRE(f.panel.conflictText().isEmpty());
    REQUIRE(f.writes == 0);
    REQUIRE(keysOf(f.registry, f.panel.profile(), "waveform.normalize").size() == 1);
}

TEST_CASE("Removing and resetting a command updates the profile", "[settings][hotkeys]")
{
    Fixture f;
    f.panel.setSearch("normalize");
    f.panel.selectCommandRow(0);
    f.panel.beginCapture();
    f.panel.captureKey(key('k', juce::ModifierKeys::altModifier));
    f.panel.applyCapture();
    REQUIRE(f.published.hasOverride("waveform.normalize"));
    f.panel.resetSelected();
    REQUIRE_FALSE(f.published.hasOverride("waveform.normalize"));
    REQUIRE(keysOf(f.registry, f.published, "waveform.normalize").size() == 1);
    f.panel.removeSelectedKeys();
    REQUIRE(f.published.hasOverride("waveform.normalize"));
    REQUIRE(keysOf(f.registry, f.published, "waveform.normalize").empty());
}

TEST_CASE("Reset all returns every command to its default after confirmation", "[settings][hotkeys]")
{
    Fixture f;
    f.panel.setSearch("normalize");
    f.panel.selectCommandRow(0);
    f.panel.beginCapture();
    f.panel.captureKey(key('k', juce::ModifierKeys::altModifier));
    f.panel.applyCapture();
    bool asked = false;
    f.panel.onConfirmResetAll = [&asked](std::function<void(bool)> done) { asked = true; done(false); };
    f.panel.resetAll();
    REQUIRE(asked);
    REQUIRE(f.published.hasOverride("waveform.normalize"));
    f.panel.onConfirmResetAll = [](std::function<void(bool)> done) { done(true); };
    f.panel.resetAll();
    REQUIRE(f.published.overrides.empty());
}

TEST_CASE("A rejected write keeps the previous profile", "[settings][hotkeys]")
{
    Fixture f;
    f.accept = false;
    f.panel.setSearch("normalize");
    f.panel.selectCommandRow(0);
    f.panel.beginCapture();
    f.panel.captureKey(key('k', juce::ModifierKeys::altModifier));
    f.panel.applyCapture();
    REQUIRE(f.writes == 1);
    REQUIRE(f.published.overrides.empty());
    REQUIRE(f.panel.statusText().contains("previous profile"));
}

TEST_CASE("The router runs a freshly captured binding and not the old one", "[settings][hotkeys]")
{
    Fixture f;
    f.panel.setSearch("normalize");
    f.panel.selectCommandRow(0);
    f.panel.beginCapture();
    f.panel.captureKey(key('k', juce::ModifierKeys::altModifier));
    f.panel.applyCapture();
    CommandRouter router(f.registry, [&f] { return f.published; });
    CommandFocus focus;
    focus.panel = CommandScope::waveform;
    REQUIRE(router.resolve(key('k', juce::ModifierKeys::altModifier), focus) == "waveform.normalize");
    // Adding a key keeps the command's existing default until the user removes it.
    REQUIRE(router.resolve(key('n'), focus) == "waveform.normalize");
}
