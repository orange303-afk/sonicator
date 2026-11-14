#include <catch2/catch_test_macros.hpp>
#include "ui/MainComponent.h"
#include "ui/library/SearchField.h"
#include "ui/shell/Theme.h"
#include "TestHelpers.h"

using namespace sonicator;

namespace
{
// Shared by every case in this file: the shell finds its buttons by text today, and the restyle
// keeps that text as the accessible name.
juce::Button* buttonWithText(juce::Component& parent, const juce::String& text)
{
    for (auto* child : parent.getChildren())
        if (auto* button = dynamic_cast<juce::Button*>(child))
            if (button->getButtonText() == text) return button;
    return nullptr;
}
}

TEST_CASE("The header keeps search and settings on the grid", "[ui][studio]")
{
    TestDirectory t;
    MainComponent app(t.dir.getChildFile("header.sqlite"), false);
    app.setSize(880, 580);
    app.resized();
    auto* search = dynamic_cast<SearchField*>(app.findChildWithID("search"));
    REQUIRE(search != nullptr);
    REQUIRE(search->getHeight() == 32);
    REQUIRE(search->leadingIcon() == ActionIcon::search);
    auto* settings = buttonWithText(app, "Settings");
    REQUIRE(settings != nullptr);
    REQUIRE(settings->getWidth() == 32);
    REQUIRE(settings->getHeight() == 32);
    REQUIRE(settings->getRight() <= app.getWidth() - 8);
    REQUIRE(search->getRight() < settings->getX());
    REQUIRE(search->getWidth() >= 300);
}
