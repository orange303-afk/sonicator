#pragma once
#include <juce_core/juce_core.h>

namespace sonicator::plugin
{
// What a DAW project remembers about a SONICATOR instance. The catalog itself is shared with the
// application and never stored here; only the window and the last selection are per project.
struct PluginState
{
    static constexpr int currentVersion = 1;
    static constexpr int defaultWidth = 1180;
    static constexpr int defaultHeight = 720;
    static constexpr int minimumWidth = 880;
    static constexpr int minimumHeight = 580;
    static constexpr int maximumWidth = 3840;
    static constexpr int maximumHeight = 2160;

    int version = currentVersion;
    juce::String lastFolder;
    juce::String lastSamplePath;
    int windowWidth = defaultWidth;
    int windowHeight = defaultHeight;

    // XML so a support bundle can be read by eye and older projects keep their known fields.
    // Unknown or foreign data yields the defaults instead of throwing at the host.
    juce::MemoryBlock toMemoryBlock() const;
    static PluginState fromMemoryBlock(const void* data, size_t sizeInBytes);
};
} // namespace sonicator::plugin
