#include "SpectrogramService.h"
#include "core/audio/SonicatorFormats.h"
#include "core/spectral/SpectrogramCache.h"
#include <algorithm>
#include <cmath>

namespace sonicator
{
SpectrogramService::SpectrogramService(juce::File folder)
    : cacheFolder(std::move(folder)), worker([this] { run(); })
{
}

SpectrogramService::~SpectrogramService()
{
    {
        const std::lock_guard<std::mutex> guard(lock);
        stopping = true;
        ++generation;
        pending.reset();
    }
    wake.notify_all();
    if (worker.joinable()) worker.join();
}

juce::String SpectrogramService::sourceKey(const juce::File& file)
{
    return file.getFullPathName() + "|" + juce::String(file.getSize()) + "|"
        + juce::String((juce::int64) file.getLastModificationTime().toMilliseconds());
}

void SpectrogramService::prepare(SpectrogramRequest request)
{
    {
        const std::lock_guard<std::mutex> guard(lock);
        pending = std::move(request);
        ++generation;
        // The previous picture stays in `grid` on purpose: the card keeps the old image while
        // the new one is computed and switches when it arrives with a matching key.
        current.phase = Phase::computing;
        current.key = pending->key;
        current.error.clear();
    }
    wake.notify_one();
}

void SpectrogramService::cancel()
{
    {
        const std::lock_guard<std::mutex> guard(lock);
        ++generation;
        pending.reset();
        current = {};
    }
    wake.notify_all();
}

SpectrogramService::State SpectrogramService::state() const
{
    const std::lock_guard<std::mutex> guard(lock);
    return current;
}

void SpectrogramService::publish(Phase phase, std::shared_ptr<const SpectrogramGrid> grid,
                                 const juce::String& key, const juce::String& error)
{
    const std::lock_guard<std::mutex> guard(lock);
    current.phase = phase;
    current.grid = std::move(grid);
    current.key = key;
    current.error = error;
}

void SpectrogramService::run()
{
    for (;;)
    {
        SpectrogramRequest request;
        std::uint64_t mine = 0;
        {
            std::unique_lock<std::mutex> waiting(lock);
            wake.wait(waiting, [this] { return stopping || pending.has_value(); });
            if (stopping) return;
            request = std::move(*pending);
            pending.reset();
            mine = generation.load();
        }

        const auto alive = [this, mine] { return generation.load() == mine; };
        SpectrogramGrid grid;
        juce::String error;

        if (request.endSeconds <= request.startSeconds)
        {
            error = "There is nothing to draw in this range";
        }
        else if (request.audio != nullptr)
        {
            const auto& audio = *request.audio;
            if (audio.sampleRate <= 0 || audio.frames <= 0)
                error = "This source has no decoded audio";
            else
            {
                const int channels = audio.right.empty() ? 1 : 2;
                const auto read = [&audio, channels, &alive](int64_t start, int count,
                                                             int requestedChannels,
                                                             juce::AudioBuffer<float>& out)
                {
                    if (! alive() || requestedChannels != channels) return false;
                    out.clear();
                    for (int channel = 0; channel < channels; ++channel)
                    {
                        auto* destination = out.getWritePointer(channel);
                        for (int i = 0; i < count; ++i)
                        {
                            const auto frame = start + i;
                            if (frame < 0 || frame >= audio.frames) continue;
                            destination[i] = channel == 0 ? audio.left[(size_t) frame]
                                                          : audio.right[(size_t) frame];
                        }
                    }
                    return true;
                };
                const auto rate = (double) audio.sampleRate;
                const auto first = juce::jlimit<int64_t>(
                    0, audio.frames - 1, (int64_t) std::llround(request.startSeconds * rate));
                const auto last = juce::jlimit<int64_t>(
                    first + 1, audio.frames, (int64_t) std::llround(request.endSeconds * rate));
                grid = computeSpectrogram(read, audio.sampleRate, channels, first, last,
                                          request.params);
            }
        }
        else if (request.file.existsAsFile())
        {
            juce::AudioFormatManager formats;
            registerSonicatorFormats(formats);
            std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(request.file));
            if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
            {
                error = "This file cannot be read for the spectrum";
            }
            else
            {
                const int channels = juce::jlimit(1, 2, (int) reader->numChannels);
                auto* readerPointer = reader.get();
                const auto read = [readerPointer, channels, &alive](int64_t start, int count,
                                                                    int requestedChannels,
                                                                    juce::AudioBuffer<float>& out)
                {
                    if (! alive() || requestedChannels != channels) return false;
                    out.clear();
                    readerPointer->read(&out, 0, count, start, channels >= 1, channels >= 2);
                    return true;
                };
                const auto rate = reader->sampleRate;
                const auto first = juce::jlimit<int64_t>(
                    0, reader->lengthInSamples - 1,
                    (int64_t) std::llround(request.startSeconds * rate));
                const auto last = juce::jlimit<int64_t>(
                    first + 1, reader->lengthInSamples,
                    (int64_t) std::llround(request.endSeconds * rate));
                grid = computeSpectrogram(read, (int) std::lround(rate), channels, first, last,
                                          request.params);
            }
        }
        else
        {
            error = "Select a sample to preview";
        }

        if (! alive()) continue;
        if (error.isEmpty() && grid.valid() && request.cacheOverview
            && request.cacheSource.existsAsFile() && cacheFolder != juce::File())
            writeSpectrumCache(request.cacheSource, grid, cacheFolder);
        if (error.isNotEmpty())
            publish(Phase::failed, nullptr, request.key, error);
        else if (! grid.valid())
            publish(Phase::failed, nullptr, request.key, "The spectrum could not be computed");
        else
            publish(Phase::ready, std::make_shared<const SpectrogramGrid>(std::move(grid)),
                    request.key, {});
    }
}
} // namespace sonicator
