#include <catch2/catch_test_macros.hpp>
#include "core/spectral/SpectralAnalysisService.h"
#include "TestHelpers.h"

using namespace sonicator;

namespace
{
struct CacheFolderScope
{
    explicit CacheFolderScope(const juce::File& folder) { setSpectrumCacheFolderForTest(folder); }
    ~CacheFolderScope() { setSpectrumCacheFolderForTest({}); }
};
} // namespace

TEST_CASE("Spectral analysis caches every file and skips a fresh one", "[spectral][analysis]")
{
    TestDirectory t;
    CacheFolderScope scope(t.dir.getChildFile("spectra"));
    const auto a = t.wave("a.wav", 440.0, 1, 22050);
    const auto b = t.wave("b.wav", 880.0, 1, 22050);
    SpectralAnalysisService service;

    service.start({ a, b });
    REQUIRE(pumpUntil([&] { return ! service.isRunning(); }, 30000));
    SpectralAnalysisService::Progress summary;
    REQUIRE(service.takeFinished(summary));
    REQUIRE(summary.total == 2);
    REQUIRE(summary.done == 2);
    REQUIRE(summary.failed == 0);
    REQUIRE(spectrumCacheIsFresh(a));
    REQUIRE(spectrumCacheIsFresh(b));

    service.start({ a, b });
    REQUIRE(pumpUntil([&] { return ! service.isRunning(); }, 30000));
    REQUIRE(service.takeFinished(summary));
    REQUIRE(summary.skipped == 2);
    REQUIRE(summary.failed == 0);
}

TEST_CASE("A file that cannot be read counts as failed, not as cached", "[spectral][analysis]")
{
    TestDirectory t;
    CacheFolderScope scope(t.dir.getChildFile("spectra"));
    const auto good = t.wave("good.wav", 440.0, 1, 22050);
    const auto missing = t.dir.getChildFile("missing.wav");
    SpectralAnalysisService service;
    service.start({ good, missing });
    REQUIRE(pumpUntil([&] { return ! service.isRunning(); }, 30000));
    SpectralAnalysisService::Progress summary;
    REQUIRE(service.takeFinished(summary));
    REQUIRE(summary.failed == 1);
    REQUIRE(spectrumCacheIsFresh(good));
    REQUIRE_FALSE(spectrumCacheIsFresh(missing));
}
