#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "ui/library/TapTempoDialog.h"

using namespace sonicator;
using Catch::Approx;

TEST_CASE("Space taps and does not start playback", "[bpm][ui]")
{
    TapTempoDialog dialog("kick.wav");
    int64_t now = 0;
    dialog.setClockForTest([&now] { return now; });
    bool loopStarted = false;
    dialog.onStartLoop = [&loopStarted] { loopStarted = true; };
    for (int i = 0; i < 3; ++i)
    {
        dialog.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey));
        now += 500;
    }
    REQUIRE(dialog.currentBpm() == Approx(120.0).margin(1.0));
    REQUIRE_FALSE(loopStarted);
}

TEST_CASE("Return applies the current value", "[bpm][ui]")
{
    TapTempoDialog dialog("kick.wav");
    int64_t now = 0;
    dialog.setClockForTest([&now] { return now; });
    for (int i = 0; i < 3; ++i)
    {
        dialog.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey));
        now += 500;
    }
    double applied = 0;
    dialog.onApply = [&applied](double bpm) { applied = bpm; };
    dialog.keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    REQUIRE(applied == Approx(120.0).margin(1.0));
}

TEST_CASE("Escape cancels without applying", "[bpm][ui]")
{
    TapTempoDialog dialog("kick.wav");
    bool cancelled = false, applied = false;
    dialog.onCancel = [&cancelled] { cancelled = true; };
    dialog.onApply = [&applied](double) { applied = true; };
    dialog.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    REQUIRE(cancelled);
    REQUIRE_FALSE(applied);
}

TEST_CASE("Apply does nothing before three taps", "[bpm][ui]")
{
    TapTempoDialog dialog("kick.wav");
    bool applied = false;
    dialog.onApply = [&applied](double) { applied = true; };
    dialog.keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    REQUIRE_FALSE(applied);
}
