#pragma once
#include "core/export/AudioExporter.h"
#include "core/spectral/SpectralMask.h"

namespace sonicator
{
// Longer selections are refused explicitly: the inverse STFT holds the whole range in memory.
constexpr double maxSpectralExportSeconds = 60.0;

// Hann STFT (2048, hop 512), every bin scaled by the mask with a one-row feather, overlap-add
// synthesis normalised by the sum of squared windows. The output has exactly the input length, so
// a fragment dropped into a DAW keeps its place; samples outside the mask are silence.
bool renderMaskedBuffer(const juce::AudioBuffer<float>& input, double sampleRate,
                        const SpectralMask& mask, double startSeconds,
                        juce::AudioBuffer<float>& output, juce::String& error,
                        const CancelCheck& cancel = {});

// Decodes the range (file or decoded snapshot), masks it and writes the WAV through
// AudioExporter::exportBuffer. Runs on the selection exporter's worker, never on the message
// thread and never on the audio callback.
ExportResult exportSpectralSelection(const juce::File& sourceFile, SampleAudioPtr audio,
                                     double start, double end, const SpectralMask& mask,
                                     const juce::String& stem, int bitsPerSample,
                                     bool floatingPoint, const juce::File& folder,
                                     CancelCheck cancel = {});
} // namespace sonicator
