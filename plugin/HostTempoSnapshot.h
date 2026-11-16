#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <cmath>
#include <optional>

namespace sonicator::plugin
{
// The value a listener sees. invalid means the host does not provide a usable tempo; a UI must
// present that honestly instead of falling back to 120 BPM. revision grows on every publish, so
// a reader can tell a fresh value from the one it already handled.
struct HostTempoValue
{
    bool valid = false;
    double bpm = 0.0;
    juce::uint64 revision = 0;
};

// One coherent word shared between the audio thread and the UI. A tempo, its validity and the
// revision live in the same 64-bit value, so a reader never sees a new tempo with an old flag.
// The audio thread only stores; the message thread only loads.
class HostTempoSnapshot
{
public:
    static_assert (std::atomic<juce::uint64>::is_always_lock_free,
                   "Host tempo must be published without a lock");

    // bpm is nullopt when the host explicitly has no valid tempo, or the tempo is not finite or
    // not positive. Publishing keeps the revision moving even for an invalid value.
    void publish (std::optional<double> bpm) noexcept
    {
        const auto previous = packed.load (std::memory_order_relaxed);
        const juce::uint64 revision = ((previous >> revisionShift) + 1) & revisionMask;
        const bool valid = bpm.has_value() && std::isfinite (*bpm) && *bpm > 0.0;
        juce::uint64 millis = 0;
        if (valid)
        {
            const double scaled = std::round (*bpm * 1000.0);
            if (scaled <= 0.0 || scaled > (double) 0xffffffffull) return publish (std::nullopt);
            millis = (juce::uint64) scaled;
        }
        const juce::uint64 next = (revision << revisionShift)
                                | (valid ? validMask : 0)
                                | millis;
        packed.store (next, std::memory_order_release);
    }

    HostTempoValue read() const noexcept
    {
        const auto value = packed.load (std::memory_order_acquire);
        HostTempoValue result;
        result.valid = (value & validMask) != 0;
        result.revision = value >> revisionShift;
        result.bpm = result.valid ? (double) (value & 0xffffffffull) / 1000.0 : 0.0;
        return result;
    }

private:
    static constexpr int revisionShift = 33;
    static constexpr juce::uint64 revisionMask = 0x7fffffffull;
    static constexpr juce::uint64 validMask = 1ull << 32;
    std::atomic<juce::uint64> packed { 0 };
};
} // namespace sonicator::plugin
