#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "ui/waveform/WaveformView.h"

using namespace sonicator::waveview;
using Catch::Approx;

TEST_CASE("Zoom keeps the anchor under the same pixel", "[waveform]")
{
    auto view = whole(10.0);
    constexpr int width = 1000;
    for (int i = 0; i < 4; ++i)
    {
        view = zoomAround(view, 2.5, 2.0, 10.0, 0.001);
        const auto x = xAtTime(2.5, width, view);
        REQUIRE(timeAtX(x, width, view) == Approx(2.5).margin(1e-5));
    }
}

TEST_CASE("Zoom cannot go beyond the whole file", "[waveform]")
{
    const auto view = zoomAround(whole(10.0), 5.0, 0.5, 10.0, 0.001);
    REQUIRE(view.start == Approx(0.0));
    REQUIRE(view.end == Approx(10.0));
}

TEST_CASE("Zoom stops at one sample per pixel", "[waveform]")
{
    constexpr int width = 1000;
    constexpr double sampleRate = 44100.0;
    const auto minimum = (double) width / sampleRate;
    auto view = whole(1.0);
    for (int i = 0; i < 40; ++i) view = zoomAround(view, 0.5, 2.0, 1.0, minimum);
    REQUIRE(view.length() == Approx(minimum).margin(1e-9));
}

TEST_CASE("Pan clamps to file bounds", "[waveform]")
{
    View view { 1.0, 2.0 };
    auto left = panBy(view, -5.0, 10.0);
    REQUIRE(left.start == Approx(0.0));
    REQUIRE(left.end == Approx(1.0));
    auto right = panBy(view, 50.0, 10.0);
    REQUIRE(right.start == Approx(9.0));
    REQUIRE(right.end == Approx(10.0));
}

TEST_CASE("Time and pixel mapping round-trip", "[waveform]")
{
    for (const auto view : { whole(10.0), View { 2.0, 3.0 } })
    {
        const auto x = xAtTime(2.5, 1000, view);
        REQUIRE(timeAtX(x, 1000, view) == Approx(2.5).margin(1e-6));
    }
}

TEST_CASE("A view equal to the file is whole", "[waveform]")
{
    REQUIRE(isWholeFile(whole(10.0), 10.0));
    REQUIRE_FALSE(isWholeFile({ 0.0, 9.999 }, 10.0));
}
