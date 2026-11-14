#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "ui/shell/DesignTokens.h"
#include "ui/shell/ActionIcons.h"
#include "ui/shell/Theme.h"
#include <array>

using namespace sonicator;

TEST_CASE("Studio tokens keep one layout scale", "[ui][studio]")
{
    REQUIRE(studio::spacingUnit == 4);
    REQUIRE(studio::spacingTight == 8);
    REQUIRE(studio::spacing == 12);
    REQUIRE(studio::spacingWide == 16);
    REQUIRE(studio::spacingSection == 24);
    REQUIRE(studio::controlRadius < studio::panelRadius);
    REQUIRE(studio::panelRadius < studio::dialogRadius);
    REQUIRE(studio::navigationWidth >= 160);
    REQUIRE(studio::dividerThickness >= 6);
    REQUIRE(studio::controlHeight >= 24);
}

TEST_CASE("Studio typography is a readable hierarchy", "[ui][studio]")
{
    REQUIRE(studio::captionFont().getHeight() < studio::controlFont().getHeight());
    REQUIRE(studio::controlFont().getHeight() <= studio::bodyFont().getHeight());
    REQUIRE(studio::bodyFont().getHeight() < studio::pageTitleFont().getHeight());
    // The sizes are logical pixels; the interface scale multiplies them at paint time only.
    // Manrope's ascent+descent box runs taller than the system sans, so the nominal size is
    // scaled by the calibration and keeps the optical size the layout was measured with.
    REQUIRE(studio::bodyFont().getHeight() == Catch::Approx(14.0f * fonts::heightCalibration).margin(0.5));
    REQUIRE(studio::pageTitleFont().getHeight()
            == Catch::Approx(24.0f * fonts::heightCalibration).margin(0.5));
}

TEST_CASE("Every action icon is drawable inside its box", "[ui][studio]")
{
    const std::array<ActionIcon, 15> icons {
        ActionIcon::settings, ActionIcon::search, ActionIcon::play, ActionIcon::pause,
        ActionIcon::stop, ActionIcon::folder, ActionIcon::favorite, ActionIcon::reset,
        ActionIcon::close, ActionIcon::add, ActionIcon::remove, ActionIcon::chevronRight,
        ActionIcon::chevronLeft, ActionIcon::chevronDown, ActionIcon::more };
    for (const auto icon : icons)
    {
        const auto path = actionIconPath(icon);
        INFO("icon " << (int) icon);
        REQUIRE_FALSE(path.isEmpty());
        const auto bounds = path.getBounds();
        REQUIRE(bounds.getX() >= -0.05f);
        REQUIRE(bounds.getY() >= -0.05f);
        REQUIRE(bounds.getRight() <= 1.05f);
        REQUIRE(bounds.getBottom() <= 1.05f);
    }
}

TEST_CASE("Theme switch repaints custom painted controls", "[ui][studio]")
{
    const auto previous = theme::isDark;
    theme::apply(ThemeMode::Dark);
    const auto darkAccent = theme::accent;
    theme::apply(ThemeMode::Light);
    const auto lightAccent = theme::accent;
    REQUIRE(darkAccent != lightAccent);
    REQUIRE(theme::background != theme::panel);
    theme::apply(previous ? ThemeMode::Dark : ThemeMode::Light);
}

TEST_CASE("The table header fits its column without letter tracking", "[ui][studio]")
{
    REQUIRE(studio::tableHeaderFont().getExtraKerningFactor() == 0.0f);
    REQUIRE(studio::tableHeaderFont().getHeight() < studio::bodyFont().getHeight());
}
