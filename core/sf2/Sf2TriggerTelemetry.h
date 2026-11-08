#pragma once
#include "core/sampler/SamplerTypes.h"
#include <atomic>
#include <cstdint>
namespace sonicator {
// Single-writer seqlock for the last source that actually started. The audio thread publishes a
// bounded POD snapshot; the UI reads it from a timer with retries. No locks, no allocation, no
// data race: payload fields are atomic with relaxed ordering and the sequence word carries
// release/acquire ordering.
class TriggerTelemetry
{
public:
    struct Snapshot
    {
        uint64_t generation = 0;
        SampleId sourceId = invalidSampleId;
        int layers = 0;
        uint64_t sequence = 0;
        bool valid = false;
    };
    void publish(uint64_t sourceGeneration, SampleId id, int layerCount)
    {
        const auto current = sequence.load(std::memory_order_relaxed);
        sequence.store(current + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        generation.store(sourceGeneration, std::memory_order_relaxed);
        source.store(id, std::memory_order_relaxed);
        layerTotal.store(layerCount, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        sequence.store(current + 2, std::memory_order_release);
    }
    Snapshot read() const
    {
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const auto first = sequence.load(std::memory_order_acquire);
            if ((first & 1u) != 0) continue;
            const Snapshot candidate { generation.load(std::memory_order_relaxed),
                                       source.load(std::memory_order_relaxed),
                                       layerTotal.load(std::memory_order_relaxed),
                                       first, first > 0 };
            std::atomic_thread_fence(std::memory_order_acquire);
            if (sequence.load(std::memory_order_relaxed) == first) return candidate;
        }
        return {};
    }
    // The output callback must be detached; used when a new instrument replaces the old one.
    void reset()
    {
        sequence.store(0, std::memory_order_relaxed);
        generation.store(0, std::memory_order_relaxed);
        source.store(invalidSampleId, std::memory_order_relaxed);
        layerTotal.store(0, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
    }
private:
    std::atomic<uint64_t> sequence { 0 };
    std::atomic<uint64_t> generation { 0 };
    std::atomic<int32_t> source { invalidSampleId };
    std::atomic<int> layerTotal { 0 };
};
} // namespace sonicator
