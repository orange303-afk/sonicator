#pragma once
#include "core/sampler/SamplerTypes.h"
#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

namespace sonicator::edit
{
// Half-open frame range, end exclusive.
struct FrameRange
{
    int64_t start = 0, end = 0;
    bool valid() const { return start >= 0 && end > start; }
    int64_t length() const { return valid() ? end - start : 0; }
};

// Sampler metadata that follows the audio and must move with it when frames are removed.
struct FileMarkers
{
    int rootKey = 60;
    // False when the source declared no sampler root; a saved file then gets no smpl chunk rather
    // than an invented C4.
    bool hasRootKey = false;
    bool hasLoop = false;
    int64_t loopStart = 0, loopEndExclusive = 0;
    std::vector<int64_t> cuePositions;
    bool operator==(const FileMarkers&) const = default;
};

enum class EditOperation
{
    deleteRegion, cropToSelection, silenceRegion, reverse,
    fadeIn, fadeOut, normalize, gain, toMono, removeDcOffset, trimSilence
};

struct EditRequest
{
    EditOperation operation = EditOperation::reverse;
    FrameRange range;          // ignored by toMono and trimSilence
    float gainDb = 0.0f;       // gain only
};

// Destructive edit session over one mono/stereo source. Every applied change is snapshotted, so undo
// restores the exact previous state; nothing touches the disk until the owner writes it out.
class AudioEditBuffer
{
public:
    static std::optional<AudioEditBuffer> load(const juce::File&, juce::String& error);
    static AudioEditBuffer fromSampleAudio(const SampleAudio&, FileMarkers markers = {});

    int channels() const { return right.empty() ? 1 : 2; }
    int sampleRate() const { return sampleRateValue; }
    int bitsPerSample() const { return bitsPerSampleValue; }
    bool floatingPoint() const { return floatingPointValue; }
    int64_t frames() const { return (int64_t) left.size(); }
    const std::vector<float>& channel(int index) const;
    const FileMarkers& markers() const { return markerData; }
    // Metadata the reader produced (INFO, bext, acid, ...); the writer passes it through and only
    // rewrites the sampler and cue keys that edits can move.
    const juce::StringPairArray& sourceMetadata() const { return metadata; }
    // Immutable copy for playback and export; the audio thread may keep it while edits continue.
    SampleAudioPtr snapshot() const;
    FrameRange whole() const { return { 0, frames() }; }
    // The selection clamped to the buffer, or the whole buffer when there is no selection.
    FrameRange resolve(const FrameRange& selection) const;
    // False when the operation cannot change this buffer, so the caller does not mark it dirty.
    bool apply(const EditRequest&);
    bool undo();
    bool redo();
    bool canUndo() const { return historyIndex > 0; }
    bool canRedo() const { return historyIndex + 1 < history.size(); }
    // True from the first applied change until undo reaches the base state or the owner saves.
    // Tracking it separately keeps a large snapshot that was evicted from the cap from reporting
    // a modified file as clean.
    bool isModified() const { return modified; }
    void clearHistory();
    size_t historyBytes() const;
    // Test hook: the production cap is 256 MB, too large to exercise the eviction path cheaply.
    void setHistoryLimitForTest(size_t bytes) { historyLimit = bytes; }

private:
    struct State
    {
        std::vector<float> left, right;
        FileMarkers markerData;
    };
    std::vector<float> left, right;
    int sampleRateValue = 0, bitsPerSampleValue = 16;
    bool floatingPointValue = false;
    FileMarkers markerData;
    juce::StringPairArray metadata;
    std::vector<State> history;
    size_t historyIndex = 0;
    size_t historyLimit = (size_t) 256 * 1024 * 1024;
    bool modified = false;

    State currentState() const;
    void restore(const State&);
    void recordState();
};
} // namespace sonicator::edit
