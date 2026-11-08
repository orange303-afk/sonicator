#include "TrackerSongRenderer.h"
#include "TrackerFormats.h"
#include <xmp.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace sonicator {
namespace {
constexpr int chunkFrames = 1024;
constexpr int64_t maxModuleBytes = 64 * 1024 * 1024;

juce::String trackerFieldText(const char* text, size_t maxLength)
{
    juce::String result;
    for (size_t i = 0; i < maxLength && text[i] != 0; ++i)
    {
        const auto c = (unsigned char) text[i];
        result << juce::String::charToString(c < 0x20 ? 0x20 : (juce::juce_wchar) c);
    }
    return result.trim();
}

int safeCapacity(double rate)
{
    const double wanted = juce::jlimit(8192.0, 262144.0, rate * 2.0);
    return (int) std::lround(wanted);
}
} // namespace

struct TrackerSongRenderer::Impl
{
    xmp_context ctx = nullptr;
    std::vector<uint8_t> bytes;
    std::thread worker;
    std::mutex waitMutex;
    std::condition_variable waitCondition;
    std::atomic<bool> stopThread {false}, playing {false}, ended {false}, loopEnabled {false};
    std::atomic<double> position {0};
    std::atomic<int> underruns {0};
    std::atomic<double> rate {44100.0};
    juce::AudioBuffer<float> ring;
    juce::AbstractFifo fifo { 1 };
    std::vector<float> carry; // interleaved stereo frames left over from one libxmp tick
    int carryFrames = 0;
    int capacity = 1;

    // Worker-only: pull ticks until the requested frame count is filled or the module ends.
    void fillChunk(float* destination, int frames)    {
        int written = 0;
        if (carryFrames > 0)
        {
            const int take = juce::jmin(carryFrames, frames);
            std::copy_n(carry.data(), (size_t) take * 2, destination);
            written += take;
            if (take < carryFrames)
            {
                std::copy(carry.begin() + (ptrdiff_t) take * 2, carry.begin() + (ptrdiff_t) carryFrames * 2,
                          carry.begin());
                carryFrames -= take;
            }
            else carryFrames = 0;
        }
        while (written < frames && ! ended.load(std::memory_order_relaxed))
        {
            if (xmp_play_frame(ctx) < 0)
            {
                if (loopEnabled.load(std::memory_order_relaxed))
                {
                    xmp_seek_time(ctx, 0);
                    continue;
                }
                ended.store(true, std::memory_order_relaxed);
                break;
            }
            xmp_frame_info info {};
            xmp_get_frame_info(ctx, &info);
            position.store((double) info.time / 1000.0, std::memory_order_relaxed);
            if (info.buffer_size <= 0 || info.buffer == nullptr) continue;
            const int available = info.buffer_size / 4; // interleaved stereo int16
            if (available <= 0) continue;
            const int take = juce::jmin(available, frames - written);
            const auto* source = static_cast<const int16_t*>(info.buffer);
            for (int i = 0; i < take * 2; ++i)
                destination[(size_t) written * 2 + (size_t) i] = (float) source[i] / 32768.0f;
            if (take < available)
            {
                carry.resize((size_t) (available - take) * 2);
                for (int i = 0; i < (available - take) * 2; ++i)
                    carry[(size_t) i] = (float) source[(ptrdiff_t) take * 2 + i] / 32768.0f;
                carryFrames = available - take;
            }
            written += take;
        }
        if (written < frames)
            std::fill_n(destination + (size_t) written * 2, (size_t) (frames - written) * 2, 0.0f);
    }

    void run()
    {
        std::vector<float> chunk((size_t) chunkFrames * 2, 0.0f);
        std::unique_lock lock(waitMutex);
        while (! stopThread.load(std::memory_order_acquire))
        {
            if (! playing.load(std::memory_order_relaxed)
                || ended.load(std::memory_order_relaxed)
                || fifo.getFreeSpace() < chunkFrames)
            {
                waitCondition.wait_for(lock, std::chrono::milliseconds(4));
                continue;
            }
            lock.unlock();
            fillChunk(chunk.data(), chunkFrames);
            lock.lock();
            if (stopThread.load(std::memory_order_acquire)) break;
            int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
            fifo.prepareToWrite(chunkFrames, start1, size1, start2, size2);
            // De-interleave explicitly: the chunk is L,R,L,R…, so a contiguous copy would read
            // past the scratch buffer and put unrelated memory into the ring.
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* destination = ring.getWritePointer(ch);
                for (int i = 0; i < size1; ++i)
                    destination[start1 + i] = chunk[(size_t) i * 2 + (size_t) ch];
                for (int i = 0; i < size2; ++i)
                    destination[start2 + i] = chunk[(size_t) (size1 + i) * 2 + (size_t) ch];
            }
            fifo.finishedWrite(size1 + size2);
        }
    }
};

TrackerSongRenderer::TrackerSongRenderer() : impl(std::make_unique<Impl>()) {}
TrackerSongRenderer::~TrackerSongRenderer() { unload(); }

void TrackerSongRenderer::unload()
{
    if (impl == nullptr) return;
    impl->playing.store(false, std::memory_order_relaxed);
    if (impl->worker.joinable())
    {
        impl->stopThread.store(true, std::memory_order_release);
        impl->waitCondition.notify_all();
        impl->worker.join();
    }
    if (impl->ctx != nullptr)
    {
        xmp_end_player(impl->ctx);
        xmp_release_module(impl->ctx);
        xmp_free_context(impl->ctx);
        impl->ctx = nullptr;
    }
    loaded.store(false, std::memory_order_release);
    impl->bytes.clear();
    impl->fifo.reset();
    impl->carry.clear();
    impl->carryFrames = 0;
    impl->position.store(0);
    duration.store(0);
    moduleTitle.clear();
    moduleFormat.clear();
    moduleChannels = 0;
}

bool TrackerSongRenderer::load(const juce::File& file, double sampleRate, juce::String& error)
{
    unload();
    error.clear();
    if (! readWholeFile(file, maxModuleBytes, impl->bytes, error)) return false;
    impl->rate.store((std::isfinite(sampleRate) && sampleRate >= 8000.0 && sampleRate <= 192000.0)
        ? sampleRate : 44100.0);
    impl->ctx = xmp_create_context();
    if (impl->ctx == nullptr) { error = "Cannot create the tracker player"; return false; }
    if (xmp_load_module_from_memory(impl->ctx, impl->bytes.data(), (long) impl->bytes.size()) != 0)
    {
        xmp_free_context(impl->ctx);
        impl->ctx = nullptr;
        error = "The tracker player cannot load this module";
        return false;
    }
    if (xmp_start_player(impl->ctx, (int) std::lround(impl->rate.load()), 0) != 0)
    {
        xmp_release_module(impl->ctx);
        xmp_free_context(impl->ctx);
        impl->ctx = nullptr;
        error = "The tracker player cannot start at this sample rate";
        return false;
    }
    xmp_module_info module {};
    xmp_get_module_info(impl->ctx, &module);
    if (module.mod != nullptr)
    {
        moduleTitle = trackerFieldText(module.mod->name, 32);
        moduleFormat = trackerFieldText(module.mod->type, 32);
        moduleChannels = module.mod->chn;
    }
    xmp_frame_info frame {};
    xmp_get_frame_info(impl->ctx, &frame);
    duration.store(frame.total_time > 0 ? (double) frame.total_time / 1000.0 : 0.0);
    impl->capacity = safeCapacity(impl->rate.load());
    impl->ring.setSize(2, impl->capacity, false, true, false);
    impl->fifo.setTotalSize(impl->capacity);
    impl->fifo.reset();
    impl->ring.clear();
    impl->carryFrames = 0;
    impl->position.store(0);
    impl->ended.store(false);
    impl->playing.store(false);
    impl->stopThread.store(false, std::memory_order_release);
    impl->worker = std::thread([this] { impl->run(); });
    loaded.store(true, std::memory_order_release);
    return true;
}

void TrackerSongRenderer::prepare(double sampleRate)
{
    if (! hasModule() || impl->ctx == nullptr) return;
    if (! std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 192000.0) return;
    if (std::abs(sampleRate - impl->rate.load()) < 0.5) return;
    impl->playing.store(false);
    if (impl->worker.joinable())
    {
        impl->stopThread.store(true, std::memory_order_release);
        impl->waitCondition.notify_all();
        impl->worker.join();
    }
    xmp_end_player(impl->ctx);
    impl->rate.store(sampleRate);
    if (xmp_start_player(impl->ctx, (int) std::lround(impl->rate.load()), 0) != 0)
    {
        loaded.store(false);
        return;
    }
    impl->capacity = safeCapacity(impl->rate.load());
    impl->ring.setSize(2, impl->capacity, false, true, false);
    impl->fifo.setTotalSize(impl->capacity);
    impl->fifo.reset();
    impl->ring.clear();
    impl->carryFrames = 0;
    impl->position.store(0);
    impl->ended.store(false);
    impl->stopThread.store(false, std::memory_order_release);
    impl->worker = std::thread([this] { impl->run(); });
}

void TrackerSongRenderer::setLooping(bool enabled)
{
    impl->loopEnabled.store(enabled, std::memory_order_relaxed);
}

void TrackerSongRenderer::play()
{
    if (! hasModule()) return;
    impl->playing.store(true);
    impl->waitCondition.notify_all();
    // Prime a few device blocks before starting so the first callback is not an underrun.
    for (int i = 0; i < 25 && impl->fifo.getNumReady() < 4096
         && ! impl->ended.load(std::memory_order_relaxed); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
}

void TrackerSongRenderer::pause()
{
    impl->playing.store(false);
}

void TrackerSongRenderer::stop()
{
    if (! hasModule()) return;
    impl->playing.store(false);
    if (impl->worker.joinable())
    {
        impl->stopThread.store(true, std::memory_order_release);
        impl->waitCondition.notify_all();
        impl->worker.join();
    }
    impl->fifo.reset();
    impl->ring.clear();
    impl->carryFrames = 0;
    impl->ended.store(false);
    impl->position.store(0);
    if (impl->ctx != nullptr) xmp_set_position(impl->ctx, 0);
    impl->stopThread.store(false, std::memory_order_release);
    impl->worker = std::thread([this] { impl->run(); });
}

void TrackerSongRenderer::seek(double seconds)
{
    if (! hasModule() || ! std::isfinite(seconds)) return;
    const double target = juce::jlimit(0.0, length(), seconds);
    const bool wasPlaying = impl->playing.load(std::memory_order_relaxed);
    impl->playing.store(false);
    if (impl->worker.joinable())
    {
        impl->stopThread.store(true, std::memory_order_release);
        impl->waitCondition.notify_all();
        impl->worker.join();
    }
    impl->fifo.reset();
    impl->ring.clear();
    impl->carryFrames = 0;
    impl->ended.store(false);
    if (impl->ctx != nullptr) xmp_seek_time(impl->ctx, (int) std::lround(target * 1000.0));
    impl->position.store(target);
    impl->stopThread.store(false, std::memory_order_release);
    impl->worker = std::thread([this] { impl->run(); });
    impl->playing.store(wasPlaying);
    impl->waitCondition.notify_all();
    if (wasPlaying)
        for (int i = 0; i < 25 && impl->fifo.getNumReady() < 4096
             && ! impl->ended.load(std::memory_order_relaxed); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
}

bool TrackerSongRenderer::isPlaying() const
{
    if (! hasModule()) return false;
    if (! impl->playing.load(std::memory_order_relaxed)) return false;
    return ! impl->ended.load(std::memory_order_relaxed) || impl->fifo.getNumReady() > 0;
}

bool TrackerSongRenderer::isFinished() const
{
    return hasModule() && impl->ended.load(std::memory_order_relaxed)
        && impl->fifo.getNumReady() == 0;
}

double TrackerSongRenderer::position() const
{
    if (! hasModule()) return 0;
    // The ring is ahead of what the listener has heard; subtract the frames still buffered.
    const double buffered = (double) impl->fifo.getNumReady() / juce::jmax(1.0, impl->rate.load());
    return juce::jlimit(0.0, length(), impl->position.load(std::memory_order_relaxed) - buffered);
}

int TrackerSongRenderer::underruns() const { return impl->underruns.load(std::memory_order_relaxed); }

void TrackerSongRenderer::render(juce::AudioBuffer<float>& out, int start, int count)
{
    juce::ScopedNoDenormals noDenormals;
    out.clear(start, count);
    if (! hasModule() || count <= 0) return;
    // Pause is immediate: the preallocated ring keeps its audio for resume, but it is not audible
    // and its position is not consumed while playback is stopped.
    if (! impl->playing.load(std::memory_order_relaxed)) return;
    int remaining = count, produced = 0;
    while (remaining > 0)
    {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        impl->fifo.prepareToRead(remaining, start1, size1, start2, size2);
        const int available = size1 + size2;
        if (available <= 0) break;
        for (int ch = 0; ch < out.getNumChannels(); ++ch)
        {
            const auto* source = impl->ring.getReadPointer(juce::jmin(ch, 1));
            if (size1 > 0) out.copyFrom(ch, start + produced, source + start1, size1);
            if (size2 > 0) out.copyFrom(ch, start + produced + size1, source + start2, size2);
        }
        impl->fifo.finishedRead(available);
        produced += available;
        remaining -= available;
    }
    if (remaining > 0) impl->underruns.fetch_add(1, std::memory_order_relaxed);
}
} // namespace sonicator
