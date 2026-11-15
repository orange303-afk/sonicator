#pragma once
#include "MatchTypes.h"
#include "core/scanner/FileScanner.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>
#include <memory>

namespace sonicator
{
// Everything the renderer needs. The source file is read-only; the plan already decided the
// ratios, so the renderer never guesses. `key` names the immutable output; `outputFile` is the
// final path under exports/match.
struct MatchRenderRequest
{
    juce::File sourceFile;
    MatchSource source;
    MatchPlan plan;
    juce::String key;
    juce::File outputFile;
    int64_t sourceSize = -1;
    int64_t sourceModified = -1;
    // Test seam for an unwritable disk. Empty uses a normal file stream.
    std::function<std::unique_ptr<juce::OutputStream> (const juce::File&)> openStream;
};

struct MatchRenderResult
{
    bool success = false;
    bool cancelled = false;
    juce::File file;
    int64_t frames = 0;
    double sampleRate = 0.0;
    int channels = 0;
    int targetTonic = -1;
    juce::String targetMode;
    juce::String error;
};

// Offline R3 render with an immutable, atomically published PCM24 WAV at the source rate.
// Returns a failure instead of a short or padded result when the length cannot be met within
// one millisecond. A previous file at the destination is never overwritten.
MatchRenderResult renderMatch (const MatchRenderRequest& request, CancelCheck cancel = {});

// The published output root: ~/Library/Sonicator/exports/match. Never cleaned automatically.
juce::File matchExportRoot();

// Algorithm identity is part of the key: a dependency or renderer change must not reuse an old
// file that was produced by different DSP.
juce::String matchAlgorithmVersion();

// Stable key over catalog identity, source fingerprint, metadata revision, range, format and both
// transforms. Two instances that ask for the same result share one file.
juce::String makeMatchResultKey (const MatchSource& source, const MatchPlan& plan,
                                 int64_t sourceSize, int64_t sourceModified);

// <root>/<key>/<stem>-match.wav, created by the renderer only.
juce::File matchOutputFile (const juce::String& key, const juce::String& sourceStem,
                            const juce::File& root = {});

// Reopens a published file and checks its format and frame count. Used before reusing a cached
// result: a truncated or foreign file with the right name is not Ready.
bool validateMatchOutput (const juce::File& file, int64_t expectedFrames, double sampleRate,
                          int channels, juce::String& error);
} // namespace sonicator
