#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/spectral/SpectrogramCache.h"
#include "TestHelpers.h"

using namespace sonicator;
using Catch::Approx;

namespace
{
struct CacheFolderScope
{
    explicit CacheFolderScope(const juce::File& folder) { setSpectrumCacheFolderForTest(folder); }
    ~CacheFolderScope() { setSpectrumCacheFolderForTest({}); }
};

SampleAudio tone(double hz = 440.0, double seconds = 0.5, int rate = 22050)
{
    SampleAudio audio;
    audio.sampleRate = rate;
    audio.channels = 1;
    audio.frames = (int64_t) std::llround(seconds * rate);
    audio.left.resize((size_t) audio.frames);
    for (int64_t i = 0; i < audio.frames; ++i)
        audio.left[(size_t) i] = (float) (0.8 * std::sin(2.0 * juce::MathConstants<double>::pi
                                                           * hz * (double) i / (double) rate));
    return audio;
}
} // namespace

TEST_CASE("A cached spectrogram round-trips its grid and PNG", "[spectral][cache]")
{
    TestDirectory t;
    CacheFolderScope scope(t.dir.getChildFile("spectra"));
    const auto file = t.wave("sine.wav", 440.0, 1, 22050);

    SpectrogramParams params;
    params.rows = 64;
    params.columns = 256;
    const auto grid = computeSpectrogram(tone(), params);
    REQUIRE(grid.valid());
    REQUIRE(writeSpectrumCache(file, grid));

    REQUIRE(spectrumCacheIsFresh(file));
    const auto cached = readSpectrumCache(file);
    REQUIRE(cached.has_value());
    REQUIRE(cached->grid.rows == grid.rows);
    REQUIRE(cached->grid.columns == grid.columns);
    REQUIRE(cached->grid.startSeconds == Approx(grid.startSeconds));
    REQUIRE(cached->grid.endSeconds == Approx(grid.endSeconds));
    REQUIRE(cached->image.isValid());
    // Stored as uint8 dB; the loudest bin still reads as the loudest.
    float originalPeak = -1000.0f, cachedPeak = -1000.0f;
    for (float value : grid.db) originalPeak = juce::jmax(originalPeak, value);
    for (float value : cached->grid.db) cachedPeak = juce::jmax(cachedPeak, value);
    REQUIRE(cachedPeak == Approx(originalPeak).margin(0.5));
}

TEST_CASE("A changed file invalidates its cached spectrogram", "[spectral][cache]")
{
    TestDirectory t;
    CacheFolderScope scope(t.dir.getChildFile("spectra"));
    const auto file = t.wave("sine.wav", 440.0, 1, 22050);
    SpectrogramParams params;
    params.rows = 32;
    params.columns = 128;
    REQUIRE(writeSpectrumCache(file, computeSpectrogram(tone(), params)));
    REQUIRE(spectrumCacheIsFresh(file));

    file.setLastModificationTime(juce::Time(juce::Time::currentTimeMillis() + 5000));
    REQUIRE_FALSE(spectrumCacheIsFresh(file));
    REQUIRE_FALSE(readSpectrumCache(file).has_value());
}

TEST_CASE("A truncated cache file is a miss, never a wrong picture", "[spectral][cache]")
{
    TestDirectory t;
    CacheFolderScope scope(t.dir.getChildFile("spectra"));
    const auto file = t.wave("sine.wav", 440.0, 1, 22050);
    SpectrogramParams params;
    params.rows = 32;
    params.columns = 128;
    REQUIRE(writeSpectrumCache(file, computeSpectrogram(tone(), params)));
    const auto sps = spectrumCacheFile(file, ".sps");
    REQUIRE(sps.existsAsFile());
    sps.replaceWithData("broken", 6);
    REQUIRE_FALSE(readSpectrumCache(file).has_value());
}

TEST_CASE("Pruning keeps the newest entries and drops both files", "[spectral][cache]")
{
    TestDirectory t;
    const auto folder = t.dir.getChildFile("spectra");
    CacheFolderScope scope(folder);
    SpectrogramParams params;
    params.rows = 32;
    params.columns = 128;
    const auto grid = computeSpectrogram(tone(), params);

    std::vector<juce::File> files;
    for (int i = 0; i < 3; ++i)
    {
        const auto file = t.wave("sine" + juce::String(i) + ".wav", 440.0, 1, 22050);
        REQUIRE(writeSpectrumCache(file, grid));
        REQUIRE(spectrumCacheFile(file, ".png").existsAsFile());
        // Pruning orders the cache files themselves, so age them directly.
        spectrumCacheFile(file, ".sps").setLastModificationTime(
            juce::Time(juce::Time::currentTimeMillis() + i * 1000));
        files.push_back(file);
    }
    REQUIRE(spectrumCacheFileCount(folder) == 3);
    pruneSpectrumCache(folder, 1, 1024ll * 1024 * 1024);
    REQUIRE(spectrumCacheFileCount(folder) == 1);
    // The newest entry survives together with its PNG.
    const auto& newest = files.back();
    REQUIRE(spectrumCacheFile(newest, ".sps").existsAsFile());
    REQUIRE(spectrumCacheFile(newest, ".png").existsAsFile());
    REQUIRE_FALSE(spectrumCacheFile(files.front(), ".sps").existsAsFile());
}
