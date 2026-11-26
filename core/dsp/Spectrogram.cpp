#include "Spectrogram.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>

namespace sonicator
{
namespace
{
int clampInt(int value, int low, int high) { return value < low ? low : (value > high ? high : value); }
double clampDouble(double value, double low, double high)
{
    return value < low ? low : (value > high ? high : value);
}

// Smallest order whose FFT is at least `fftSize`; 8 is 256, the analysis floor.
int fftOrderFor(int fftSize)
{
    int order = 8; // 256
    while ((1 << order) < fftSize && order < 13) ++order;
    return order;
}

double frequencyAtRatio(double minFrequency, double maxFrequency, double ratio, bool logScale)
{
    if (logScale && minFrequency > 0.0 && maxFrequency > minFrequency)
        return minFrequency * std::pow(maxFrequency / minFrequency, ratio);
    return minFrequency + (maxFrequency - minFrequency) * ratio;
}

struct RowBins
{
    int low = 0, high = 0;
};
} // namespace

double spectrogramFrequencyPosition(const SpectrogramGrid& grid, double frequencyHz)
{
    if (grid.rows <= 0 || grid.maxFrequency <= grid.minFrequency) return -1.0;
    if (frequencyHz < grid.minFrequency || frequencyHz > grid.maxFrequency) return -1.0;
    if (grid.logFrequency && grid.minFrequency > 0.0)
        return std::log(frequencyHz / grid.minFrequency) / std::log(grid.maxFrequency / grid.minFrequency);
    return (frequencyHz - grid.minFrequency) / (grid.maxFrequency - grid.minFrequency);
}

int spectrogramFrameCount(int64_t rangeFrames, int hopFrames)
{
    if (rangeFrames <= 0 || hopFrames <= 0) return 0;
    return (int) ((rangeFrames - 1) / hopFrames + 1);
}

SpectrogramGrid computeSpectrogram(const SpectrogramFrameReader& read, int sampleRate, int channels,
                                   int64_t firstFrame, int64_t lastFrame, const SpectrogramParams& params)
{
    SpectrogramGrid grid;
    if (! read || sampleRate <= 0 || channels <= 0 || lastFrame <= firstFrame) return grid;

    const int fftSize = 1 << fftOrderFor(clampInt(params.fftSize, 256, 8192));
    const int rows = clampInt(params.rows > 0 ? params.rows : 256, 8, 1024);
    const int columns = clampInt(params.columns > 0 ? params.columns : juce::jmin(1024, 2 * rows), 16, 8192);
    const int maxFrames = clampInt(params.maxFrames, 1, 65536);
    const double nyquist = sampleRate * 0.5;
    const double maxFrequency = nyquist;
    const double minFrequency = params.logFrequency
        ? clampDouble(params.minFrequency, nyquist / 1000.0, nyquist)
        : 0.0;

    const int64_t rangeFrames = lastFrame - firstFrame;
    const double idealHop = (double) rangeFrames / (double) columns;
    const double denseHop = (double) (fftSize / 4);
    int hop = 1;
    bool overview = false;
    if (idealHop <= denseHop)
        hop = (int) juce::jmax<int64_t>(1, (int64_t) std::floor(idealHop));
    else
    {
        hop = fftSize / 4;
        if (rangeFrames > (int64_t) hop * maxFrames)
        {
            hop = (int) std::ceil((double) rangeFrames / (double) maxFrames);
            overview = true;
        }
    }
    const int frameCount = spectrogramFrameCount(rangeFrames, hop);
    if (frameCount <= 0) return grid;

    const int bins = fftSize / 2 + 1;
    const double binHz = (double) sampleRate / (double) fftSize;
    std::vector<RowBins> rowBins((size_t) rows);
    for (int row = 0; row < rows; ++row)
    {
        const double lowRatio = (double) row / (double) rows;
        const double highRatio = (double) (row + 1) / (double) rows;
        const double lowHz = frequencyAtRatio(minFrequency, maxFrequency, lowRatio, params.logFrequency);
        const double highHz = frequencyAtRatio(minFrequency, maxFrequency, highRatio, params.logFrequency);
        int lowBin = (int) std::floor(lowHz / binHz);
        int highBin = (int) std::ceil(highHz / binHz) - 1;
        // A row narrower than one FFT bin still shows the bin it falls into, so a 20 Hz log
        // floor does not produce empty rows at the bottom.
        if (highBin < lowBin) highBin = lowBin;
        rowBins[(size_t) row] = { clampInt(lowBin, 0, bins - 1), clampInt(highBin, 0, bins - 1) };
    }

    juce::dsp::FFT fft(fftOrderFor(fftSize));
    std::vector<float> scratch((size_t) fftSize * 2, 0.0f);
    std::vector<float> window((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * (float) i / (float) (fftSize - 1));

    const size_t cells = (size_t) rows * (size_t) columns;
    std::vector<float> peak(cells, 0.0f);
    std::vector<float> binPower((size_t) bins, 0.0f);
    std::vector<float> rowPower((size_t) rows, 0.0f);
    double reference = 0.0;

    const auto analyseFrame = [&](int frame, const juce::AudioBuffer<float>& source, int offset)
    {
        std::fill(rowPower.begin(), rowPower.end(), 0.0f);
        const int available = source.getNumSamples() - offset;
        for (int channel = 0; channel < channels; ++channel)
        {
            std::fill(scratch.begin(), scratch.end(), 0.0f);
            const auto* input = source.getReadPointer(channel);
            for (int i = 0; i < fftSize && i < available; ++i)
                scratch[(size_t) i] = input[offset + i] * window[(size_t) i];
            fft.performFrequencyOnlyForwardTransform(scratch.data());
            // A one-sided magnitude spectrum, squared and summed across channels: anti-phase
            // stereo still has energy here, exactly as the catalog analyzer expects.
            for (int bin = 0; bin < bins; ++bin)
            {
                const float magnitude = scratch[(size_t) bin];
                binPower[(size_t) bin] = channel == 0 ? magnitude * magnitude
                                                      : binPower[(size_t) bin] + magnitude * magnitude;
            }
            for (int row = 0; row < rows; ++row)
            {
                const auto range = rowBins[(size_t) row];
                float best = 0.0f;
                for (int bin = range.low; bin <= range.high; ++bin)
                    best = juce::jmax(best, binPower[(size_t) bin]);
                rowPower[(size_t) row] += best;
            }
        }
        const int column = juce::jlimit(0, columns - 1, (int) ((int64_t) frame * columns / frameCount));
        for (int row = 0; row < rows; ++row)
        {
            const float power = rowPower[(size_t) row];
            reference = juce::jmax(reference, (double) power);
            auto& cell = peak[(size_t) row * (size_t) columns + (size_t) column];
            cell = juce::jmax(cell, power);
        }
    };

    // Blocks keep a whole-file view (large hop, many frames) from issuing thousands of small
    // seeks. Overlapping hops stay one frame per read, where the ranges are short anyway.
    const int framesPerBlock = hop >= fftSize ? juce::jlimit(1, 64, (1 << 20) / juce::jmax(1, hop)) : 1;
    juce::AudioBuffer<float> block(channels, fftSize);
    for (int blockStart = 0; blockStart < frameCount; blockStart += framesPerBlock)
    {
        const int blockEnd = juce::jmin(frameCount, blockStart + framesPerBlock);
        const int64_t readStart = firstFrame + (int64_t) blockStart * hop;
        const int64_t readEnd = juce::jmin(lastFrame,
            firstFrame + (int64_t) (blockEnd - 1) * hop + fftSize);
        const int count = (int) juce::jmax<int64_t>(0, readEnd - readStart);
        if (count <= 0) break;
        block.setSize(channels, count, false, false, true);
        block.clear();
        if (! read(readStart, count, channels, block)) return {}; // cancelled or unreadable
        for (int frame = blockStart; frame < blockEnd; ++frame)
            analyseFrame(frame, block, (int) ((int64_t) (frame - blockStart) * hop));
    }

    grid.columns = columns;
    grid.rows = rows;
    grid.floorDb = params.floorDb < -10.0 ? params.floorDb : -90.0;
    grid.startSeconds = (double) firstFrame / (double) sampleRate;
    grid.endSeconds = (double) lastFrame / (double) sampleRate;
    grid.minFrequency = minFrequency;
    grid.maxFrequency = maxFrequency;
    grid.logFrequency = params.logFrequency;
    grid.overview = overview;
    grid.db.assign(cells, (float) grid.floorDb);
    if (reference > 0.0)
    {
        const double floorPower = std::pow(10.0, grid.floorDb / 10.0);
        for (int row = 0; row < rows; ++row)
            for (int column = 0; column < columns; ++column)
            {
                const double power = juce::jmax((double) peak[(size_t) row * (size_t) columns + (size_t) column],
                                                floorPower * reference);
                const double db = 10.0 * std::log10(power / reference);
                grid.db[(size_t) row * (size_t) columns + (size_t) column] =
                    (float) juce::jlimit(grid.floorDb, 0.0, db);
            }
    }
    return grid;
}

SpectrogramGrid computeSpectrogram(const SampleAudio& audio, const SpectrogramParams& params)
{
    if (audio.sampleRate <= 0 || audio.frames <= 0) return {};
    const int channels = audio.right.empty() ? 1 : 2;
    const auto read = [&audio, channels](int64_t start, int count, int requestedChannels,
                                         juce::AudioBuffer<float>& out)
    {
        if (requestedChannels != channels) return false;
        out.clear();
        for (int channel = 0; channel < channels; ++channel)
        {
            auto* destination = out.getWritePointer(channel);
            for (int i = 0; i < count; ++i)
            {
                const int64_t frame = start + i;
                if (frame < 0 || frame >= audio.frames) continue;
                destination[i] = channel == 0 ? audio.left[(size_t) frame] : audio.right[(size_t) frame];
            }
        }
        return true;
    };
    return computeSpectrogram(read, audio.sampleRate, channels, 0, audio.frames, params);
}
} // namespace sonicator
