#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include "core/dsp/Spectrogram.h"

using namespace sonicator;
using Catch::Approx;

namespace
{
SampleAudio sineAudio(double frequencyHz, double seconds = 1.0, int sampleRate = 48000,
                      float amplitude = 0.8f, bool invertedRight = false)
{
    SampleAudio audio;
    audio.sampleRate = sampleRate;
    audio.channels = invertedRight ? 2 : 1;
    audio.frames = (int64_t) std::llround((double) sampleRate * seconds);
    audio.left.resize((size_t) audio.frames);
    for (int64_t i = 0; i < audio.frames; ++i)
        audio.left[(size_t) i] = amplitude
            * std::sin((float) (2.0 * juce::MathConstants<double>::pi * frequencyHz
                                * (double) i / (double) sampleRate));
    if (invertedRight)
    {
        audio.right.resize(audio.left.size());
        for (size_t i = 0; i < audio.left.size(); ++i) audio.right[i] = -audio.left[i];
    }
    return audio;
}

double rowFrequency(const SpectrogramGrid& grid, int row)
{
    return grid.minFrequency
        * std::pow(grid.maxFrequency / grid.minFrequency,
                   ((double) row + 0.5) / (double) grid.rows);
}

SpectrogramParams gridParams(int rows, int columns)
{
    SpectrogramParams params;
    params.rows = rows;
    params.columns = columns;
    params.fftSize = 2048;
    params.minFrequency = 20.0;
    params.floorDb = -90.0;
    params.logFrequency = true;
    return params;
}
} // namespace

TEST_CASE("A sine lands on the row that names its frequency", "[spectrogram]")
{
    const auto audio = sineAudio(440.0, 1.0, 48000);
    const auto grid = computeSpectrogram(audio, gridParams(128, 256));
    REQUIRE(grid.valid());
    REQUIRE(grid.columns == 256);
    REQUIRE(grid.rows == 128);

    int bestRow = 0;
    float best = grid.at(0, 0);
    for (int row = 0; row < grid.rows; ++row)
        for (int column = 0; column < grid.columns; ++column)
            if (grid.at(row, column) > best) { best = grid.at(row, column); bestRow = row; }
    REQUIRE(best == Approx(0.0).margin(1e-4));
    REQUIRE(rowFrequency(grid, bestRow) == Approx(440.0).margin(30.0));
}

TEST_CASE("The loudest bin normalises to 0 dB and silence sits on the floor", "[spectrogram]")
{
    const auto tone = computeSpectrogram(sineAudio(1000.0, 0.5), gridParams(64, 128));
    REQUIRE(tone.valid());
    float peak = -1000.0f;
    for (float value : tone.db) peak = juce::jmax(peak, value);
    REQUIRE(peak == Approx(0.0).margin(1e-4));

    SampleAudio silence;
    silence.sampleRate = 44100;
    silence.frames = 44100;
    silence.channels = 1;
    silence.left.assign((size_t) silence.frames, 0.0f);
    const auto quiet = computeSpectrogram(silence, gridParams(64, 128));
    REQUIRE(quiet.valid());
    for (float value : quiet.db)
    {
        REQUIRE(std::isfinite(value));
        REQUIRE(value == Approx((float) quiet.floorDb));
    }
}

TEST_CASE("Anti-phase stereo keeps its power", "[spectrogram]")
{
    const auto grid = computeSpectrogram(sineAudio(440.0, 0.5, 48000, 0.8f, true),
                                         gridParams(64, 128));
    REQUIRE(grid.valid());
    float peak = -1000.0f;
    for (float value : grid.db) peak = juce::jmax(peak, value);
    // The channels are summed in the power domain, so a phase-inverted pair cannot cancel.
    REQUIRE(peak == Approx(0.0).margin(1e-4));
}

TEST_CASE("Frequency positions follow the chart scale", "[spectrogram]")
{
    SpectrogramGrid grid;
    grid.rows = 100;
    grid.columns = 1;
    grid.db.assign(100, -90.0f);
    grid.minFrequency = 20.0;
    grid.maxFrequency = 20000.0;
    grid.logFrequency = true;
    const auto logPosition = spectrogramFrequencyPosition(grid, 1000.0);
    REQUIRE(logPosition == Approx(std::log(1000.0 / 20.0) / std::log(20000.0 / 20.0)).margin(1e-9));
    REQUIRE(spectrogramFrequencyPosition(grid, 10.0) == Approx(-1.0));
    REQUIRE(spectrogramFrequencyPosition(grid, 30000.0) == Approx(-1.0));

    // A linear chart starts at 0 Hz, as computeSpectrogram does.
    grid.logFrequency = false;
    grid.minFrequency = 0.0;
    REQUIRE(spectrogramFrequencyPosition(grid, 10000.0) == Approx(10000.0 / 20000.0).margin(1e-9));
}

TEST_CASE("A long range becomes a budgeted overview", "[spectrogram]")
{
    auto audio = sineAudio(220.0, 30.0, 8000);
    auto params = gridParams(48, 200);
    params.maxFrames = 40;
    const auto grid = computeSpectrogram(audio, params);
    REQUIRE(grid.valid());
    REQUIRE(grid.columns == 200);
    REQUIRE(grid.overview); // the hop had to grow beyond a quarter of the FFT
    REQUIRE(spectrogramFrameCount(1000, 10) == 100);
    REQUIRE(spectrogramFrameCount(1000, 400) == 3);
}

TEST_CASE("A short source at a long range still produces columns", "[spectrogram]")
{
    const auto audio = sineAudio(440.0, 0.05, 48000); // fewer frames than columns
    const auto grid = computeSpectrogram(audio, gridParams(32, 512));
    REQUIRE(grid.valid());
    REQUIRE(grid.columns == 512);
    REQUIRE(grid.endSeconds > grid.startSeconds);
}
