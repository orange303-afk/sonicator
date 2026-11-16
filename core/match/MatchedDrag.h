#pragma once
#include <juce_core/juce_core.h>

namespace sonicator
{
// One decision shared by every drag entry point (library row, cloud point, preview waveform).
// A pending or blocked Match never returns a file: the OS gesture is not started with the
// original pretending to be the matched result, and a gesture cannot be replayed from a late
// callback, so the user repeats it after Ready.
struct MatchedDragDecision
{
    enum class Status { bypass, ready, pending, blocked };
    Status status = Status::bypass;
    juce::File file;
    juce::String message;

    bool handled() const { return status != Status::bypass; }
    bool startsDrag() const { return status == Status::ready; }
};

inline MatchedDragDecision resolveMatchedDrag (bool matchEnabled, int itemCount, bool sameSource,
                                               bool ready, const juce::File& preparedFile,
                                               const juce::String& blockReason = {})
{
    MatchedDragDecision decision;
    if (! matchEnabled) return decision; // the existing drag path runs untouched
    decision.status = MatchedDragDecision::Status::pending;
    if (itemCount != 1)
    {
        decision.status = MatchedDragDecision::Status::blocked;
        decision.message = "Select one sample for Match";
        return decision;
    }
    if (! sameSource)
    {
        decision.status = MatchedDragDecision::Status::blocked;
        decision.message = "Preview the sample before dragging its matched result";
        return decision;
    }
    if (ready && preparedFile.existsAsFile())
    {
        decision.status = MatchedDragDecision::Status::ready;
        decision.file = preparedFile;
        return decision;
    }
    if (blockReason.isNotEmpty())
    {
        decision.status = MatchedDragDecision::Status::blocked;
        decision.message = blockReason;
        return decision;
    }
    decision.message = "Preparing matched audio...";
    return decision;
}
} // namespace sonicator
