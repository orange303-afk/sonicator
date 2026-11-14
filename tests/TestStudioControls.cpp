#include <catch2/catch_test_macros.hpp>
#include "ui/shell/ActionIcons.h"
#include "ui/shell/DesignTokens.h"
#include "ui/shell/StudioControls.h"
#include "ui/shell/Theme.h"

using namespace sonicator;

namespace
{
// juce::Image::operator== compares the shared image data, not the pixels, so every visual
// assertion here compares pixels explicitly.
bool samePixels(const juce::Image& a, const juce::Image& b)
{
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight()) return false;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) return false;
    return true;
}
}

// The Studio primitives keep the JUCE control types the shell already uses, so every test that
// finds a control by its text and every accessibility name survives the restyle. What changes is
// the paint: a property selects the surface, and the LookAndFeel draws it.
TEST_CASE("Icon buttons draw their icon instead of their text", "[ui][studio]")
{
    SonicatorLookAndFeel look;
    studio::IconButton settings(ActionIcon::settings, "Settings");
    studio::IconButton play(ActionIcon::settings, "Play");
    settings.setLookAndFeel(&look);
    play.setLookAndFeel(&look);
    settings.setSize(32, 32);
    play.setSize(32, 32);

    REQUIRE(settings.getButtonText() == "Settings");
    REQUIRE(play.getButtonText() == "Play");
    REQUIRE(settings.icon() == ActionIcon::settings);
    REQUIRE((int) settings.getProperties()["studioIcon"] == (int) ActionIcon::settings);

    // Different text, same icon: the text is an accessible name, not a label, so the two paints
    // are identical.
    REQUIRE(samePixels(settings.createComponentSnapshot(settings.getLocalBounds()),
                       play.createComponentSnapshot(play.getLocalBounds())));
}

TEST_CASE("Chips keep their label, toggle state and pill property", "[ui][studio]")
{
    SonicatorLookAndFeel look;
    studio::Chip chip("Starred");
    chip.setLookAndFeel(&look);
    chip.setSize(90, studio::chipHeight);
    REQUIRE(chip.getButtonText() == "Starred");
    REQUIRE((int) chip.getProperties()["studioChip"] == 1);

    const auto off = chip.createComponentSnapshot(chip.getLocalBounds());
    chip.setToggleState(true, juce::dontSendNotification);
    const auto on = chip.createComponentSnapshot(chip.getLocalBounds());
    REQUIRE_FALSE(samePixels(off, on));

    studio::Chip small("Loop", true);
    REQUIRE((int) small.getProperties()["studioChip"] == 2);
}

TEST_CASE("Check chips carry a tick that shows its state", "[ui][studio]")
{
    SonicatorLookAndFeel look;
    studio::CheckChip check("Hide empty");
    check.setLookAndFeel(&look);
    check.setSize(120, studio::chipHeight);
    REQUIRE(check.getButtonText() == "Hide empty");
    REQUIRE((int) check.getProperties()["studioCheckChip"] == 1);

    const auto off = check.createComponentSnapshot(check.getLocalBounds());
    check.setToggleState(true, juce::dontSendNotification);
    const auto on = check.createComponentSnapshot(check.getLocalBounds());
    REQUIRE_FALSE(samePixels(off, on));
}

TEST_CASE("Stat tiles carry a value and a caption", "[ui][studio]")
{
    studio::StatTile tile;
    tile.setStat({ "2.184 s", "Length" });
    REQUIRE(tile.stat().value == "2.184 s");
    REQUIRE(tile.stat().caption == "Length");
    REQUIRE(studio::StatTile::preferredHeight() >= 24);
    tile.setSize(120, studio::StatTile::preferredHeight());
    REQUIRE(tile.createComponentSnapshot(tile.getLocalBounds()).isValid());
}

TEST_CASE("The icon set has a left chevron and a more glyph", "[ui][studio]")
{
    for (const auto icon : { ActionIcon::chevronLeft, ActionIcon::more })
    {
        const auto path = actionIconPath(icon);
        REQUIRE_FALSE(path.isEmpty());
        REQUIRE(path.getBounds().getX() >= -0.05f);
        REQUIRE(path.getBounds().getRight() <= 1.05f);
    }
}
