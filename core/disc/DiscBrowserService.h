#pragma once
#include "DiscFilesystem.h"
#include "DiscImageProbe.h"
#include "DiscAudioReader.h"
#include "CueSheet.h"
#include <condition_variable>
#include <mutex>
#include <thread>
#include <atomic>
namespace sonicator {
// One persistent worker for open/list/decode. Every completion carries the request generation;
// the owner ignores results from a superseded image or entry. The worker never touches the audio
// callback or the message thread.
class DiscBrowserService
{
public:
    enum class Kind { open, list, decode };
    struct Completion
    {
        Kind kind = Kind::open;
        DiscStatus status = DiscStatus::unknown;
        juce::String error;
        juce::uint64 generation = 0;
        juce::File file;
        DiscEntryId entry, root;
        DiscProbe probe;
        DiscListing listing;
        std::vector<CueTrack> cueTracks;
        SampleAudioPtr audio;
    };
    explicit DiscBrowserService(DiscLimits limits = {});
    ~DiscBrowserService();
    // trackNumber selects a data track from a multi-track cue; -1 picks the only data track.
    void requestOpen(const juce::File&, int trackNumber = -1);
    void requestList(DiscEntryId, bool recursive = false);
    void requestDecode(DiscEntryId, const juce::String& displayNameForHint, DiscEntryId partner = {});
    void close();
    // Invalidates pending work without closing the currently opened filesystem.
    void cancelPending();
    bool poll(Completion&);
private:
    struct Request
    {
        Kind kind = Kind::open;
        juce::File file;
        DiscEntryId entry, partner;
        juce::String hint;
        int trackNumber = -1;
        juce::uint64 generation = 0;
        bool recursive = false;
    };
    DiscLimits budget;
    std::mutex mutex;
    std::condition_variable workCondition, doneCondition;
    Request pending;
    bool hasPending = false, stopping = false;
    std::atomic<bool> closed { false };
    juce::uint64 generation = 0;
    std::vector<Completion> completed;
    std::thread worker;
    ByteSourcePtr source;
    std::unique_ptr<DiscFilesystem> filesystem;
    DiscEntryId rootId;
    void run();
    void handle(const Request&, Completion&);
    void handleCue(const Request&, Completion&, const juce::File& cue);
    CancelCheck cancelCheck(juce::uint64 forGeneration);
    void publish(Completion);
};
} // namespace sonicator
