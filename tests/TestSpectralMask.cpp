#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/spectral/SpectralMask.h"

using namespace sonicator;
using Catch::Approx;

namespace
{
SpectralMask rectangleMask()
{
    SpectralMask mask;
    mask.setWindow(0.0, 1.0, 24000.0);
    mask.addRectangle(0.2, 0.4, 200.0, 2000.0, false);
    return mask;
}
} // namespace

TEST_CASE("A rectangle covers exactly its time and frequency band", "[spectral][mask]")
{
    const auto mask = rectangleMask();
    REQUIRE_FALSE(mask.empty());
    REQUIRE(mask.contains(0.3, 1000.0));
    REQUIRE_FALSE(mask.contains(0.1, 1000.0));
    REQUIRE_FALSE(mask.contains(0.5, 1000.0));
    REQUIRE_FALSE(mask.contains(0.3, 100.0));
    REQUIRE_FALSE(mask.contains(0.3, 4000.0));
    REQUIRE(mask.lowFrequency() > 150.0);
    REQUIRE(mask.lowFrequency() < 260.0);
    REQUIRE(mask.highFrequency() > 1800.0);
    REQUIRE(mask.highFrequency() < 2300.0);
}

TEST_CASE("Subtracting the same rectangle empties the mask", "[spectral][mask]")
{
    auto mask = rectangleMask();
    mask.addRectangle(0.2, 0.4, 200.0, 2000.0, true);
    REQUIRE(mask.empty());
}

TEST_CASE("Growing the window resamples the existing cells", "[spectral][mask]")
{
    auto mask = rectangleMask();
    mask.setWindow(0.0, 2.0, 24000.0);
    REQUIRE_FALSE(mask.empty());
    REQUIRE(mask.contains(0.3, 1000.0));
    REQUIRE_FALSE(mask.contains(1.5, 1000.0));
}

TEST_CASE("A lasso polygon fills the same band", "[spectral][mask]")
{
    SpectralMask mask;
    mask.setWindow(0.0, 1.0, 24000.0);
    const std::vector<juce::Point<double>> points {
        { 0.2, 200.0 }, { 0.4, 200.0 }, { 0.4, 2000.0 }, { 0.2, 2000.0 }
    };
    mask.addPolygon(points, false);
    REQUIRE_FALSE(mask.empty());
    REQUIRE(mask.contains(0.3, 1000.0));
    REQUIRE_FALSE(mask.contains(0.3, 6000.0));
    REQUIRE_FALSE(mask.contains(0.1, 1000.0));
}

TEST_CASE("A magic-wand raster maps grid cells into the canonical mask", "[spectral][mask]")
{
    SpectrogramGrid grid;
    grid.rows = 16;
    grid.columns = 16;
    grid.startSeconds = 0.0;
    grid.endSeconds = 1.0;
    grid.minFrequency = 20.0;
    grid.maxFrequency = 20000.0;
    grid.logFrequency = true;
    grid.floorDb = -90.0;
    grid.db.assign(16 * 16, -90.0f);
    std::vector<uint8_t> cells(16 * 16, 0);
    for (int row = 6; row < 9; ++row)
        for (int column = 4; column < 12; ++column)
        {
            cells[(size_t) row * 16 + (size_t) column] = 1;
            grid.db[(size_t) row * 16 + (size_t) column] = 0.0f;
        }

    SpectralMask mask;
    mask.setWindow(0.0, 1.0, 20000.0);
    mask.addRaster(grid, cells, false);
    REQUIRE_FALSE(mask.empty());
    // Grid row 7 sits at 20 * (1000)^(7.5/16) ≈ 510 Hz; column 8 at 0.5 s.
    REQUIRE(mask.contains(0.5, 500.0));
    REQUIRE_FALSE(mask.contains(0.1, 500.0));
    REQUIRE_FALSE(mask.contains(0.5, 5000.0));

    mask.addRaster(grid, cells, true);
    REQUIRE(mask.empty());
}
