#include <catch2/catch_test_macros.hpp>
#include "ui/commands/CommandRegistry.h"

using namespace sonicator;

namespace
{
CommandSpec spec(juce::String id, CommandScope scope = CommandScope::app)
{
    CommandSpec s;
    s.id = std::move(id);
    s.name = "Name";
    s.description = "Description";
    s.category = "Category";
    s.scope = scope;
    return s;
}
}

TEST_CASE("Command ids are unique and non-empty", "[commands][registry]")
{
    CommandRegistry registry;
    registry.add(spec("library.selectAll"), {}, [] {});
    REQUIRE_THROWS_AS(registry.add(spec("library.selectAll"), {}, [] {}), std::invalid_argument);
    REQUIRE_THROWS_AS(registry.add(spec(""), {}, [] {}), std::invalid_argument);
    REQUIRE(registry.size() == 1);
}

TEST_CASE("Registry finds commands by stable id", "[commands][registry]")
{
    CommandRegistry registry;
    registry.add(spec("app.settings"), {}, [] {});
    const auto* found = registry.find("app.settings");
    REQUIRE(found != nullptr);
    REQUIRE(found->id == "app.settings");
    REQUIRE(registry.find("app.missing") == nullptr);
    REQUIRE_FALSE(registry.contains("app.missing"));
    REQUIRE(registry.list().size() == 1);
}

TEST_CASE("Unknown commands report a reason and never run", "[commands][registry]")
{
    CommandRegistry registry;
    int runs = 0;
    const auto state = registry.state("nope");
    REQUIRE_FALSE(state.available);
    REQUIRE(state.reason.isNotEmpty());
    REQUIRE_FALSE(registry.invoke("nope"));
    REQUIRE(runs == 0);
}

TEST_CASE("Disabled or unavailable commands never run", "[commands][registry]")
{
    CommandRegistry registry;
    int runs = 0;
    bool ready = false;
    registry.add(spec("waveform.normalize"), [&ready] { return ready ? CommandState::ready()
                                                                   : CommandState::blocked("Job running"); },
                 [&runs] { ++runs; });
    REQUIRE_FALSE(registry.invoke("waveform.normalize"));
    REQUIRE(runs == 0);
    REQUIRE(registry.state("waveform.normalize").reason == "Job running");
    ready = true;
    REQUIRE(registry.invoke("waveform.normalize"));
    REQUIRE(runs == 1);
}

TEST_CASE("A command whose state turns unavailable between menu and click does not run", "[commands][registry]")
{
    CommandRegistry registry;
    int runs = 0;
    bool available = true;
    registry.add(spec("library.addLastTag"),
                 [&available] { return available ? CommandState::ready()
                                                : CommandState::unavailable("No tag"); },
                 [&runs] { ++runs; });
    available = false;
    REQUIRE_FALSE(registry.invoke("library.addLastTag"));
    REQUIRE(runs == 0);
}

TEST_CASE("Replace updates a command in place", "[commands][registry]")
{
    CommandRegistry registry;
    int first = 0, second = 0;
    registry.add(spec("app.settings"), {}, [&first] { ++first; });
    registry.replace(spec("app.settings"), {}, [&second] { ++second; });
    REQUIRE(registry.size() == 1);
    REQUIRE(registry.invoke("app.settings"));
    REQUIRE(first == 0);
    REQUIRE(second == 1);
}

TEST_CASE("Command specs keep their platform defaults and menu ids", "[commands][registry]")
{
    CommandRegistry registry;
    auto s = spec("waveform.save", CommandScope::waveform);
    s.legacyMenuId = 16;
    s.destructive = true;
    s.defaults.push_back(juce::KeyPress('s', juce::ModifierKeys::commandModifier, 0));
    registry.add(s, {}, [] {});
    const auto* found = registry.find("waveform.save");
    REQUIRE(found != nullptr);
    REQUIRE(found->legacyMenuId == 16);
    REQUIRE(found->destructive);
    REQUIRE(found->defaults.size() == 1);
    REQUIRE(found->scope == CommandScope::waveform);
}
