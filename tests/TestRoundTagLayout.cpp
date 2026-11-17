#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "ui/library/RoundTagMenu.h"

using namespace sonicator;
using Catch::Approx;

namespace
{
std::vector<RoundTagItem> makeItems(int count, const juce::String& prefix)
{
    std::vector<RoundTagItem> out;
    for (int i = 0; i < count; ++i)
    {
        RoundTagItem item;
        item.tagId = i + 1;
        item.label = prefix + juce::String(i);
        item.colour = juce::Colours::orange;
        out.push_back(item);
    }
    return out;
}

juce::Font testFont() { return juce::Font(juce::FontOptions(10.5f)); }
}

TEST_CASE("Round tag layout uses two orbits", "[tags][round]")
{
    const auto geometry = layoutRoundTagMenu({ 400, 400 }, { 0, 0, 800, 800 },
                                             makeItems(8, "In"), makeItems(8, "Out"), testFont());
    REQUIRE(geometry.pills.size() == 16);
    REQUIRE(geometry.pills[0].centre.getDistanceFrom(geometry.centre) == Approx(122.0f).margin(1.5f));
    REQUIRE(geometry.pills[8].centre.getDistanceFrom(geometry.centre) == Approx(200.0f).margin(1.5f));
    REQUIRE(geometry.pills[0].angle == Approx(-juce::MathConstants<float>::halfPi).margin(0.001f));
    // The outer orbit starts half a step after the inner one.
    REQUIRE(geometry.pills[8].angle
            == Approx(-juce::MathConstants<float>::halfPi + juce::MathConstants<float>::pi / 8.0f)
                   .margin(0.001f));
}

TEST_CASE("Round tag layout keeps every pill inside the window", "[tags][round]")
{
    const auto geometry = layoutRoundTagMenu({ 10, 10 }, { 0, 0, 800, 800 },
                                             makeItems(8, "In"), makeItems(7, "Out"), testFont());
    const auto safe = juce::Rectangle<int>(0, 0, 800, 800).reduced(12).toFloat();
    for (const auto& pill : geometry.pills)
        REQUIRE(safe.contains(pill.bounds));
    REQUIRE(safe.contains(geometry.hub));
}

TEST_CASE("Round tag layout distributes short lists evenly", "[tags][round]")
{
    const auto geometry = layoutRoundTagMenu({ 400, 400 }, { 0, 0, 900, 900 },
                                             makeItems(3, "In"), makeItems(1, "Out"), testFont());
    REQUIRE(geometry.pills.size() == 4);
    REQUIRE(geometry.pills[1].angle == Approx(-juce::MathConstants<float>::halfPi
                                              + juce::MathConstants<float>::twoPi / 3.0f).margin(0.001f));
    // One outer pill sits opposite the first inner one, not on top of it.
    REQUIRE(geometry.pills[3].angle == Approx(juce::MathConstants<float>::halfPi).margin(0.001f));
}

TEST_CASE("Round tag nearest ignores the centre and far reach", "[tags][round]")
{
    const auto geometry = layoutRoundTagMenu({ 400, 400 }, { 0, 0, 900, 900 },
                                             makeItems(8, "In"), makeItems(8, "Out"), testFont());
    REQUIRE(roundTagNearest(geometry.pills, geometry.pills[0].centre) == 0);
    REQUIRE(roundTagNearest(geometry.pills, geometry.centre) == -1);
    REQUIRE(roundTagNearest(geometry.pills, { -500.0f, -500.0f }) == -1);
}

TEST_CASE("Round tag layout leaves room for hover growth at the editor edge", "[tags][round]")
{
    const auto geometry = layoutRoundTagMenu({ 0, 0 }, { 0, 0, 880, 580 },
                                             makeItems(8, "In"), makeItems(7, "Out"), testFont());
    const auto area = juce::Rectangle<float>(0.0f, 0.0f, 880.0f, 580.0f);
    for (const auto& pill : geometry.pills)
    {
        auto grown = pill.bounds.withSizeKeepingCentre(pill.bounds.getWidth() * 1.26f,
                                                       pill.bounds.getHeight() * 1.26f);
        grown.translate(std::cos(pill.angle) * 13.0f, std::sin(pill.angle) * 13.0f);
        REQUIRE(area.contains(grown));
    }
}

namespace
{
juce::MouseEvent mouseClick(RoundTagMenu& menu, juce::Point<float> position,
                            juce::ModifierKeys mods = {})
{
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position, mods,
                            1.0f, 0, 0, 0, 0, &menu, &menu, now, position, now, 1, false);
}
}

TEST_CASE("Round tag menu applies one pick and closes", "[tags][round]")
{
    RoundTagMenu menu;
    menu.setAnimationDuration(0);
    int picks = 0;
    menu.onTagPicked = [&picks](int64_t) { ++picks; };
    const auto geometry = layoutRoundTagMenu({ 400, 400 }, { 0, 0, 900, 900 },
                                             makeItems(8, "In"), makeItems(7, "Out"), testFont());
    menu.open(geometry, "3 files", "TAGS");
    const auto target = geometry.pills[1].centre;
    menu.mouseDown(mouseClick(menu, target));
    menu.mouseDown(mouseClick(menu, target));
    REQUIRE(picks == 1);
    REQUIRE_FALSE(menu.isOpen());
}

TEST_CASE("Round tag menu dismisses on a right click instead of picking", "[tags][round]")
{
    RoundTagMenu menu;
    menu.setAnimationDuration(0);
    int picks = 0, dismissals = 0;
    menu.onTagPicked = [&picks](int64_t) { ++picks; };
    menu.onDismissed = [&dismissals] { ++dismissals; };
    const auto geometry = layoutRoundTagMenu({ 400, 400 }, { 0, 0, 900, 900 },
                                             makeItems(8, "In"), makeItems(7, "Out"), testFont());
    menu.open(geometry, "3 files", "TAGS");
    menu.mouseDown(mouseClick(menu, geometry.pills[1].centre,
                              juce::ModifierKeys::rightButtonModifier));
    REQUIRE(picks == 0);
    REQUIRE(dismissals == 1);
}
