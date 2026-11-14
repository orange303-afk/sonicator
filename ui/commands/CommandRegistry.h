#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

namespace sonicator
{
// Where a command may run. Overlap decides whether one key may mean two things.
enum class CommandScope
{
    app,        // anywhere in the Sonicator window
    library,    // the file table has focus
    folders,    // the folder tree has focus
    waveform,   // the waveform/preview editor has focus
    discBrowser,// a container image is open
    settings    // a Settings page owns the keyboard
};

// Why a command cannot run right now. `available` is about the current object (no selection, no
// editable source); `enabled` is about a temporary condition (job running, busy catalog).
struct CommandState
{
    bool available = true;
    bool enabled = true;
    juce::String reason;

    static CommandState ready() { return {}; }
    static CommandState unavailable(juce::String why) { return { false, false, std::move(why) }; }
    static CommandState blocked(juce::String why) { return { true, false, std::move(why) }; }
    bool canRun() const { return available && enabled; }
};

struct CommandSpec
{
    juce::String id;          // stable, never translated, e.g. "waveform.normalize"
    juce::String name;        // display name, may be translated later
    juce::String description; // short explanation for Settings > Hotkeys
    juce::String category;    // grouping in Settings > Hotkeys
    CommandScope scope = CommandScope::app;
    std::vector<juce::KeyPress> defaults;
    int legacyMenuId = 0;     // the EditCommands popup id this command replaces, 0 when none
    bool destructive = false; // needs a confirmation or a safe writer before it runs
};

// The single catalogue of actions that buttons, menus and keys all call. Components register
// their own commands with a handler; nothing else hard-codes a shortcut or a menu label.
class CommandRegistry
{
public:
    // Throws std::invalid_argument for an empty or already registered id.
    void add(CommandSpec spec, std::function<CommandState()> state, std::function<void()> invoke);
    // Replaces a registration with the same id, for a widget rebuilt with the same lifetime owner.
    void replace(CommandSpec spec, std::function<CommandState()> state, std::function<void()> invoke);

    const CommandSpec* find(const juce::String& id) const;
    std::vector<CommandSpec> list() const;
    CommandState state(const juce::String& id) const;
    // Runs the command only when it is available and enabled; re-checks state at call time.
    bool invoke(const juce::String& id);
    bool contains(const juce::String& id) const;
    std::size_t size() const { return entries.size(); }
    void clear() { entries.clear(); }

private:
    struct Entry
    {
        CommandSpec spec;
        std::function<CommandState()> state;
        std::function<void()> invoke;
    };
    Entry* findEntry(const juce::String& id);
    const Entry* findEntry(const juce::String& id) const;
    std::vector<Entry> entries;
};
} // namespace sonicator
