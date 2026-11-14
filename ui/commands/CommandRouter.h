#pragma once
#include "CommandRegistry.h"
#include "core/settings/ShortcutPreferences.h"
#include <functional>

namespace sonicator
{
// What currently owns the keyboard. The router refuses to act while a dialog or the shortcut
// recorder is open, or while a text field has focus, so typing never normalizes or plays.
struct CommandFocus
{
    CommandScope panel = CommandScope::library;
    bool textInput = false;
    bool modal = false;
    bool capturing = false;
};

// Resolves a key press against the effective bindings and runs one command. It never installs a
// global hook: it is called from the focused component's keyPressed, and only inside Sonicator.
class CommandRouter
{
public:
    CommandRouter(const CommandRegistry& registry, std::function<ShortcutProfile()> profile)
        : registry(registry), profile(std::move(profile)) {}

    // The command id the key would run right now, or empty. Panel commands win over application
    // commands; only runnable commands are returned.
    juce::String resolve(const juce::KeyPress&, const CommandFocus&) const;
    // True only when a command actually ran.
    bool dispatch(const juce::KeyPress&, const CommandFocus&);

private:
    const CommandRegistry& registry;
    std::function<ShortcutProfile()> profile;
};
} // namespace sonicator
