#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/smart/TapTempo.h"
#include "core/smart/KeyNames.h"

using namespace sonicator;
using Catch::Approx;

TEST_CASE("Taps at 120 BPM give 120", "[bpm]")
{
    TapTempo tempo;
    tempo.tap(0);
    tempo.tap(500);
    tempo.tap(1000);
    tempo.tap(1500);
    REQUIRE(tempo.hasValue());
    REQUIRE(tempo.bpm() == Approx(120.0).margin(0.5));
    REQUIRE(tempo.tapCount() == 4);
}

TEST_CASE("Two taps are not enough", "[bpm]")
{
    TapTempo tempo;
    tempo.tap(0);
    tempo.tap(500);
    REQUIRE_FALSE(tempo.hasValue());
    REQUIRE(tempo.bpm() == 0.0);
}

TEST_CASE("A late tap is dropped as an outlier", "[bpm]")
{
    TapTempo tempo;
    for (const int64_t time : { 0, 500, 1000, 2100, 2600 }) tempo.tap(time);
    REQUIRE(tempo.hasValue());
    REQUIRE(tempo.bpm() == Approx(120.0).margin(15.0));
}

TEST_CASE("A three second pause resets the taps", "[bpm]")
{
    TapTempo tempo;
    tempo.tap(0);
    tempo.tap(500);
    tempo.tap(4000);
    REQUIRE(tempo.tapCount() == 1);
    REQUIRE_FALSE(tempo.hasValue());
}

TEST_CASE("Jitter stays close", "[bpm]")
{
    TapTempo tempo;
    int64_t time = 0;
    for (const int64_t interval : { 495, 505, 497, 503, 500 })
    {
        tempo.tap(time);
        time += interval;
    }
    REQUIRE(tempo.bpm() == Approx(120.0).margin(2.0));
}

TEST_CASE("Key names map to root notes", "[bpm]")
{
    REQUIRE(rootMidiFromKey("C") == 60);
    REQUIRE(rootMidiFromKey("C#") == 61);
    REQUIRE(rootMidiFromKey("F#") == 66);
    REQUIRE(rootMidiFromKey("Bb") == 70);
    REQUIRE(rootMidiFromKey("A minor") == 69);
    REQUIRE(rootMidiFromKey("") == -1);
    REQUIRE(rootMidiFromKey("?") == -1);
}

TEST_CASE("A late third tap still gives a value", "[bpm]")
{
    TapTempo tempo;
    tempo.tap(0);
    tempo.tap(500);
    tempo.tap(1600);
    REQUIRE(tempo.hasValue());
    REQUIRE(tempo.bpm() == Approx(120.0).margin(1.0));
}
