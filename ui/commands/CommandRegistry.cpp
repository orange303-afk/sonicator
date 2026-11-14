#include "CommandRegistry.h"
#include <stdexcept>

namespace sonicator
{
void CommandRegistry::add(CommandSpec spec, std::function<CommandState()> state,
                          std::function<void()> invokeFn)
{
    if (spec.id.isEmpty())
        throw std::invalid_argument("Command id must not be empty");
    if (find(spec.id) != nullptr)
        throw std::invalid_argument("Command already registered: " + spec.id.toStdString());
    entries.push_back({ std::move(spec), std::move(state), std::move(invokeFn) });
}

void CommandRegistry::replace(CommandSpec spec, std::function<CommandState()> state,
                              std::function<void()> invokeFn)
{
    if (spec.id.isEmpty())
        throw std::invalid_argument("Command id must not be empty");
    if (auto* existing = findEntry(spec.id))
    {
        existing->spec = std::move(spec);
        existing->state = std::move(state);
        existing->invoke = std::move(invokeFn);
        return;
    }
    entries.push_back({ std::move(spec), std::move(state), std::move(invokeFn) });
}

CommandRegistry::Entry* CommandRegistry::findEntry(const juce::String& id)
{
    for (auto& entry : entries)
        if (entry.spec.id == id) return &entry;
    return nullptr;
}

const CommandRegistry::Entry* CommandRegistry::findEntry(const juce::String& id) const
{
    for (const auto& entry : entries)
        if (entry.spec.id == id) return &entry;
    return nullptr;
}

const CommandSpec* CommandRegistry::find(const juce::String& id) const
{
    const auto* entry = findEntry(id);
    return entry != nullptr ? &entry->spec : nullptr;
}

std::vector<CommandSpec> CommandRegistry::list() const
{
    std::vector<CommandSpec> specs;
    specs.reserve(entries.size());
    for (const auto& entry : entries) specs.push_back(entry.spec);
    return specs;
}

CommandState CommandRegistry::state(const juce::String& id) const
{
    const auto* entry = findEntry(id);
    if (entry == nullptr)
        return CommandState::unavailable("Unknown command");
    if (entry->state == nullptr)
        return CommandState::ready();
    return entry->state();
}

bool CommandRegistry::invoke(const juce::String& id)
{
    auto* entry = findEntry(id);
    if (entry == nullptr || entry->invoke == nullptr) return false;
    // The state is re-read here, not at menu build time: a job may have started meanwhile.
    if (entry->state != nullptr && ! entry->state().canRun()) return false;
    entry->invoke();
    return true;
}

bool CommandRegistry::contains(const juce::String& id) const
{
    return findEntry(id) != nullptr;
}
} // namespace sonicator
