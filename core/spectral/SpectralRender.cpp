#include "SpectralRender.h"
#include "core/audio/SonicatorFormats.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>

namespace sonicator
{
namespace
{
constexpr int fftSize = 2048;
constexpr int hopSize = 512;
constexpr int fftOrder = 11;

std::vector<float> hannWindow()
{
    std::vector<float> window((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f
            * std::cos(2.0f * juce::MathConstants<float>::pi * (float) i / (float) (fftSize - 1));
    return window;
}
} // namespace

bool renderMaskedBuffer(const juce::AudioBuffer<float>& input, double sampleRate,
                        const SpectralMask& mask, double startSeconds,
                        juce::AudioBuffer<float>& output, juce::String& error,
                        const CancelCheck& cancel)
{
    const int frames = input.getNumSamples();
    const int channels = juce::jlimit(1, 2, input.getNumChannels());
    if (frames <= 0 || channels <= 0 || sampleRate <= 0.0)
    {
        error = "There is no audio to render";
        return false;
    }
    if (mask.empty())
    {
        error = "The spectral selection is empty";
        return false;
    }

    juce::dsp::FFT fft(fftOrder);
    const auto window = hannWindow();
    output.setSize(channels, frames);
    output.clear();
    std::vector<float> weight((size_t) frames, 0.0f);
    std::vector<float> scratch((size_t) fftSize * 2, 0.0f);
    std::vector<float> gain((size_t) (fftSize / 2 + 1), 0.0f);
    const double binHz = sampleRate / (double) fftSize;

    // Frames start half a window before zero: every sample is covered by at least one window, and
    // the final division by the accumulated window energy makes the unmasked case exact.
    for (int frameStart = -fftSize / 2; frameStart < frames; frameStart += hopSize)
    {
        if (cancel && cancel())
        {
            error = "Export cancelled";
            return false;
        }
        const double time = startSeconds + ((double) frameStart + fftSize * 0.5) / sampleRate;
        const int column = mask.columnForTime(time);
        // A frame outside the mask's time window contributes nothing at all: keeping its window
        // energy in the overlap-add weight would dip the samples at the mask's time edges.
        if (column < 0) continue;
        for (int bin = 0; bin <= fftSize / 2; ++bin)
        {
            const double frequency = (double) bin * binHz;
            float value = 0.0f;
            if (column >= 0 && frequency >= mask.minFrequency && frequency <= mask.maxFrequency)
            {
                const int row = mask.rowForFrequency(frequency);
                if (mask.cell(row, column))
                    value = 1.0f;
                else if (mask.cell(row - 1, column) || mask.cell(row + 1, column))
                    value = 0.5f; // one-row feather keeps the band edge from ringing
            }
            gain[(size_t) bin] = value;
        }
        for (int channel = 0; channel < channels; ++channel)
        {
            std::fill(scratch.begin(), scratch.end(), 0.0f);
            for (int i = 0; i < fftSize; ++i)
            {
                const int index = frameStart + i;
                if (index < 0 || index >= frames) continue;
                scratch[(size_t) i] = input.getSample(channel, index) * window[(size_t) i];
            }
            fft.performRealOnlyForwardTransform(scratch.data(), true);
            for (int bin = 0; bin <= fftSize / 2; ++bin)
            {
                const float value = gain[(size_t) bin];
                if (bin == 0)
                    scratch[0] *= value;
                else if (bin == fftSize / 2)
                    scratch[1] *= value; // JUCE packs the Nyquist real into bin 0's imaginary slot
                else
                {
                    scratch[(size_t) (2 * bin)] *= value;
                    scratch[(size_t) (2 * bin + 1)] *= value;
                }
            }
            fft.performRealOnlyInverseTransform(scratch.data());
            auto* destination = output.getWritePointer(channel);
            for (int i = 0; i < fftSize; ++i)
            {
                const int index = frameStart + i;
                if (index < 0 || index >= frames) continue;
                destination[index] += scratch[(size_t) i] * window[(size_t) i];
                if (channel == 0) weight[(size_t) index] += window[(size_t) i] * window[(size_t) i];
            }
        }
    }

    for (int i = 0; i < frames; ++i)
    {
        const float energy = weight[(size_t) i];
        if (energy <= 1.0e-8f) continue;
        for (int channel = 0; channel < channels; ++channel)
            output.setSample(channel, i, output.getSample(channel, i) / energy);
    }
    return true;
}

ExportResult exportSpectralSelection(const juce::File& sourceFile, SampleAudioPtr audio,
                                     double start, double end, const SpectralMask& mask,
                                     const juce::String& stem, int bitsPerSample,
                                     bool floatingPoint, const juce::File& folder,
                                     CancelCheck cancel)
{
    if (! std::isfinite(start) || ! std::isfinite(end) || end <= start)
        return { false, {}, "The spectral selection has no time range" };
    if (end - start > maxSpectralExportSeconds)
        return { false, {}, "The spectral selection is longer than "
                                + juce::String((int) maxSpectralExportSeconds) + " s" };
    if (mask.empty())
        return { false, {}, "The spectral selection is empty" };
    if (cancel && cancel()) return { false, {}, "Export cancelled" };

    juce::AudioBuffer<float> input;
    int sampleRate = 0, channels = 0;
    int bits = bitsPerSample;
    bool floating = floatingPoint;
    int64_t first = 0, last = 0;

    if (audio != nullptr)
    {
        if (audio->sampleRate <= 0 || audio->frames <= 0)
            return { false, {}, "This source has no decoded audio" };
        sampleRate = audio->sampleRate;
        channels = audio->right.empty() ? 1 : 2;
        first = juce::jlimit<int64_t>(0, audio->frames - 1,
                                      (int64_t) std::llround(start * sampleRate));
        last = juce::jlimit<int64_t>(first + 1, audio->frames,
                                     (int64_t) std::llround(end * sampleRate));
        bits = audio->bitsPerSample;
        floating = audio->floatingPoint;
        input.setSize(channels, (int) (last - first));
        for (int channel = 0; channel < channels; ++channel)
        {
            auto* destination = input.getWritePointer(channel);
            for (int64_t i = 0; i < last - first; ++i)
                destination[i] = channel == 0 ? audio->left[(size_t) (first + i)]
                                              : audio->right[(size_t) (first + i)];
        }
    }
    else if (sourceFile.existsAsFile())
    {
        juce::AudioFormatManager formats;
        registerSonicatorFormats(formats);
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(sourceFile));
        if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
            return { false, {}, "This file cannot be read for the spectral selection" };
        sampleRate = (int) std::lround(reader->sampleRate);
        channels = juce::jlimit(1, 2, (int) reader->numChannels);
        first = juce::jlimit<int64_t>(0, reader->lengthInSamples - 1,
                                      (int64_t) std::llround(start * sampleRate));
        last = juce::jlimit<int64_t>(first + 1, reader->lengthInSamples,
                                     (int64_t) std::llround(end * sampleRate));
        bits = (int) reader->bitsPerSample;
        floating = false;
        input.setSize(channels, (int) (last - first));
        input.clear();
        reader->read(&input, 0, (int) (last - first), first, channels >= 1, channels >= 2);
    }
    else
    {
        return { false, {}, "Select a sample to preview" };
    }

    juce::AudioBuffer<float> rendered;
    juce::String error;
    if (! renderMaskedBuffer(input, (double) sampleRate, mask, (double) first / sampleRate,
                             rendered, error, cancel))
        return { false, {}, error };
    return AudioExporter().exportBuffer(rendered, sampleRate, bits, floating, stem, start, end,
                                        folder, cancel);
}
} // namespace sonicator
