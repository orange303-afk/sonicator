#include "CommandRouter.h"
#include "ShortcutBindings.h"

namespace sonicator
{
namespace
{
bool scopeServes(CommandScope command, CommandScope panel)
{
    return command == panel || command == CommandScope::app;
}
}

juce::String CommandRouter::resolve(const juce::KeyPress& pressed, const CommandFocus& focus) const
{
    if (focus.modal || focus.capturing || focus.textInput) return {};
    if (! pressed.isValid()) return {};
    const auto effective = profile != nullptr ? profile() : ShortcutProfile {};
    if (! effective.readable) return {};

    juce::String appMatch;
    for (const auto& command : registry.list())
    {
        if (! scopeServes(command.scope, focus.panel)) continue;
        bool matches = false;
        for (const auto& binding : effectiveShortcuts(command, effective))
            if (shortcutMatches(binding, pressed)) { matches = true; break; }
        if (! matches) continue;
        if (! registry.state(command.id).canRun()) continue;
        if (command.scope == focus.panel) return command.id;
        if (appMatch.isEmpty()) appMatch = command.id;
    }
    return appMatch;
}

bool CommandRouter::dispatch(const juce::KeyPress& pressed, const CommandFocus& focus)
{
    const auto id = resolve(pressed, focus);
    if (id.isEmpty()) return false;
    return registry.invoke(id);
}
} // namespace sonicator
