#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cstdint>
#include <functional>
#include <vector>
#include "core/sampler/SamplerTypes.h"

namespace sonicator
{
// A short-time Fourier transform reduced to what a screen can show: a dB matrix of
// time columns and frequency rows, plus the mapping the renderer needs for its labels.
// Everything here is message/worker thread code: no audio thread, no UI types.
struct SpectrogramParams
{
    int fftSize = 2048;      // power of two, clamped to 256..8192
    int columns = 0;         // requested pixel columns; 0 means min(1024, 2 x rows)
    int rows = 0;            // requested pixel rows; 0 means 256
    int maxFrames = 8192;    // cap on analysed frames; beyond it the view is an overview
    double minFrequency = 20.0;  // lower edge of the log scale, Hz
    double floorDb = -90.0;      // quietest displayed level
    bool logFrequency = true;
};

// `db` is row-major, row 0 at the lowest displayed frequency, and normalised so the loudest
// bin of this view is 0 dB. Any failed read leaves `columns == 0`, which the caller treats as
// "no result"; a cancelled job therefore never publishes a partial grid.
struct SpectrogramGrid
{
    int columns = 0, rows = 0;
    double floorDb = -90.0;
    double startSeconds = 0.0, endSeconds = 0.0;
    double minFrequency = 0.0, maxFrequency = 0.0;
    bool logFrequency = true;
    // True when the hop had to exceed a quarter of the FFT size to stay inside maxFrames, so
    // time detail is aggregated rather than sampled densely.
    bool overview = false;
    std::vector<float> db;

    float at(int row, int column) const
    {
        if (row < 0 || row >= rows || column < 0 || column >= columns) return (float) floorDb;
        return db[(size_t) row * (size_t) columns + (size_t) column];
    }
    bool valid() const { return columns > 0 && rows > 0 && db.size() == (size_t) columns * (size_t) rows; }
};

// Fills `out` with `numFrames` frames of every channel starting at `startFrame`. Out-of-range
// frames are zero-filled by the reader, so the analysis never reads past the source. Returning
// false cancels the computation and the grid comes back invalid.
using SpectrogramFrameReader =
    std::function<bool(int64_t startFrame, int numFrames, int numChannels, juce::AudioBuffer<float>& out)>;

// Frequency position of a row in 0..1 (0 = bottom of the spectrum), or -1 when out of range.
double spectrogramFrequencyPosition(const SpectrogramGrid&, double frequencyHz);

// Hoisted so tests and the service budget the same way.
int spectrogramFrameCount(int64_t rangeFrames, int hopFrames);

SpectrogramGrid computeSpectrogram(const SpectrogramFrameReader& read, int sampleRate, int channels,
                                   int64_t firstFrame, int64_t lastFrame, const SpectrogramParams&);

// Convenience for sources already decoded in memory (containers, edits, sampler sources).
SpectrogramGrid computeSpectrogram(const SampleAudio& audio, const SpectrogramParams&);
} // namespace sonicator
