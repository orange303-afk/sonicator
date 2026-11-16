#pragma once
#include <juce_core/juce_core.h>
#include "core/match/MatchPreferences.h"

namespace sonicator::plugin
{
// What a DAW project remembers about a SONICATOR instance. The catalog itself is shared with the
// application and never stored here; only the window, the last selection and the per-instance
// match intent are per project. Host tempo is never stored: it belongs to the host, not the file.
struct PluginState
{
    // v2 added the match preferences. A v1 project has no such fields and loads with both off.
    static constexpr int currentVersion = 2;
    static constexpr int defaultWidth = 1180;
    static constexpr int defaultHeight = 720;
    static constexpr int minimumWidth = 880;
    static constexpr int minimumHeight = 580;
    static constexpr int maximumWidth = 3840;
    static constexpr int maximumHeight = 2160;
    // A host hands over whatever the project file contains. A real block is a few hundred bytes,
    // so anything larger than this is foreign data and is ignored instead of being parsed.
    static constexpr size_t maximumStateBytes = 64 * 1024;

    int version = currentVersion;
    juce::String lastFolder;
    juce::String lastSamplePath;
    int windowWidth = defaultWidth;
    int windowHeight = defaultHeight;
    PluginMatchPreferences match;

    // XML so a support bundle can be read by eye and older projects keep their known fields.
    // Unknown or foreign data yields the defaults instead of throwing at the host.
    juce::MemoryBlock toMemoryBlock() const;
    static PluginState fromMemoryBlock(const void* data, size_t sizeInBytes);
};
} // namespace sonicator::plugin
