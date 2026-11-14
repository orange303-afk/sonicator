#include <catch2/catch_test_macros.hpp>
#include "ui/commands/CommandRouter.h"
#include "ui/commands/ShortcutBindings.h"

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

TEST_CASE("Text input and open dialogs never reach the command router", "[commands][routing]")
{
    CommandRegistry registry;
    int runs = 0;
    registry.add(makeSpec("waveform.normalize", CommandScope::waveform, { key('n') }),
                 {}, [&runs] { ++runs; });
    CommandRouter router(registry, [] { return ShortcutProfile {}; });
    CommandFocus focus;
    focus.panel = CommandScope::waveform;
    focus.textInput = true;
    REQUIRE(router.resolve(key('n'), focus).isEmpty());
    REQUIRE_FALSE(router.dispatch(key('n'), focus));
    focus.textInput = false;
    focus.modal = true;
    REQUIRE_FALSE(router.dispatch(key('n'), focus));
    focus.modal = false;
    focus.capturing = true;
    REQUIRE_FALSE(router.dispatch(key('n'), focus));
    focus.capturing = false;
    REQUIRE(router.dispatch(key('n'), focus));
    REQUIRE(runs == 1);
}

TEST_CASE("Only the focused panel and application commands are reachable", "[commands][routing]")
{
    CommandRegistry registry;
    int libraryRuns = 0, waveformRuns = 0;
    registry.add(makeSpec("library.star", CommandScope::library, { key('s', juce::ModifierKeys::altModifier) }),
                 {}, [&libraryRuns] { ++libraryRuns; });
    registry.add(makeSpec("waveform.silence", CommandScope::waveform,
                          { key('s', juce::ModifierKeys::altModifier) }),
                 {}, [&waveformRuns] { ++waveformRuns; });
    CommandRouter router(registry, [] { return ShortcutProfile {}; });
    CommandFocus library;
    library.panel = CommandScope::library;
    REQUIRE(router.dispatch(key('s', juce::ModifierKeys::altModifier), library));
    REQUIRE(libraryRuns == 1);
    REQUIRE(waveformRuns == 0);

    CommandFocus waveform;
    waveform.panel = CommandScope::waveform;
    REQUIRE(router.dispatch(key('s', juce::ModifierKeys::altModifier), waveform));
    REQUIRE(waveformRuns == 1);
    REQUIRE(libraryRuns == 1);
}

TEST_CASE("A panel binding wins over an application binding of the same key", "[commands][routing]")
{
    CommandRegistry registry;
    int panelRuns = 0, appRuns = 0;
    registry.add(makeSpec("app.settings", CommandScope::app, { key(',', juce::ModifierKeys::commandModifier) }),
                 {}, [&appRuns] { ++appRuns; });
    registry.add(makeSpec("library.comma", CommandScope::library,
                          { key(',', juce::ModifierKeys::commandModifier) }),
                 {}, [&panelRuns] { ++panelRuns; });
    CommandRouter router(registry, [] { return ShortcutProfile {}; });
    CommandFocus focus;
    focus.panel = CommandScope::library;
    REQUIRE(router.resolve(key(',', juce::ModifierKeys::commandModifier), focus) == "library.comma");
    REQUIRE(router.dispatch(key(',', juce::ModifierKeys::commandModifier), focus));
    REQUIRE(panelRuns == 1);
    REQUIRE(appRuns == 0);
}

TEST_CASE("A bound but unavailable command does not consume the key", "[commands][routing]")
{
    CommandRegistry registry;
    registry.add(makeSpec("library.normalizeSelected", CommandScope::library,
                          { key('n', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier) }),
                 [] { return CommandState::unavailable("No selection"); }, [] {});
    CommandRouter router(registry, [] { return ShortcutProfile {}; });
    CommandFocus focus;
    focus.panel = CommandScope::library;
    REQUIRE(router.resolve(key('n', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier),
                           focus).isEmpty());
    REQUIRE_FALSE(router.dispatch(key('n', juce::ModifierKeys::commandModifier
                                      | juce::ModifierKeys::shiftModifier), focus));
}

TEST_CASE("A remapped key is the one the router runs", "[commands][routing]")
{
    CommandRegistry registry;
    int runs = 0;
    registry.add(makeSpec("waveform.normalize", CommandScope::waveform, { key('n') }),
                 {}, [&runs] { ++runs; });
    ShortcutProfile profile;
    profile.overrides["waveform.normalize"] = storedKeysFromKeyPresses({ key('k', juce::ModifierKeys::altModifier) });
    CommandRouter router(registry, [&profile] { return profile; });
    CommandFocus focus;
    focus.panel = CommandScope::waveform;
    REQUIRE_FALSE(router.dispatch(key('n'), focus));
    REQUIRE(router.dispatch(key('k', juce::ModifierKeys::altModifier), focus));
    REQUIRE(runs == 1);
}

TEST_CASE("An unreadable profile runs nothing", "[commands][routing]")
{
    CommandRegistry registry;
    int runs = 0;
    registry.add(makeSpec("waveform.normalize", CommandScope::waveform, { key('n') }),
                 {}, [&runs] { ++runs; });
    ShortcutProfile profile;
    profile.readable = false;
    CommandRouter router(registry, [&profile] { return profile; });
    CommandFocus focus;
    focus.panel = CommandScope::waveform;
    REQUIRE_FALSE(router.dispatch(key('n'), focus));
    REQUIRE(runs == 0);
}
