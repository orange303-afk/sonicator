#pragma once
#include "core/database/FolderIndex.h"
#include <juce_events/juce_events.h>
#include <functional>

namespace sonicator
{
// Builds the folder structure for the library and the favorites trees on a worker thread, so
// neither start-up nor the end of a scan blocks the message thread. Requests made while a build
// runs are coalesced: the newest one runs when the current one finishes, older ones are dropped.
class FolderIndexRunner final : private juce::Thread
{
public:
    using Callback = std::function<void(FolderIndex)>;

    explicit FolderIndexRunner(SampleRepository& repository)
        : juce::Thread("Folder index"), repo(repository) {}

    ~FolderIndexRunner() override
    {
        signalThreadShouldExit();
        stopThread(10000);
    }

    void request(FolderIndexRequest inputs, Callback callback)
    {
        const juce::ScopedLock lock(mutex);
        pending = std::move(inputs);
        pendingCallback = std::move(callback);
        hasPending = true;
        if (running) return;
        running = startThread();
    }

private:
    void run() override
    {
        for (;;)
        {
            FolderIndexRequest inputs;
            Callback callback;
            {
                const juce::ScopedLock lock(mutex);
                if (! hasPending) { running = false; return; }
                inputs = std::move(pending);
                callback = std::move(pendingCallback);
                pendingCallback = nullptr;
                hasPending = false;
            }

            auto index = buildFolderIndex(repo, inputs, [this] { return threadShouldExit(); });
            if (threadShouldExit())
            {
                const juce::ScopedLock lock(mutex);
                running = false;
                return;
            }

            // The answer is delivered on the message thread; a callback whose owner is gone has
            // already been dropped by its SafePointer. A shared pointer keeps the index to one copy.
            auto payload = std::make_shared<FolderIndex>(std::move(index));
            juce::MessageManager::callAsync([callback = std::move(callback), payload]
            {
                if (callback) callback(std::move(*payload));
            });
        }
    }

    SampleRepository& repo;
    juce::CriticalSection mutex;
    FolderIndexRequest pending;
    Callback pendingCallback;
    bool hasPending = false;
    bool running = false;
};
} // namespace sonicator
