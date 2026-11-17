#pragma once
#include <juce_core/juce_core.h>
namespace sonicator::contribute
{
/** The `os` field the contribution intake accepts: "mac", "win" or "linux".
    Linux builds used to report themselves as macOS. Kept in one place so the client and the
    intake worker cannot drift apart silently. */
juce::String platformName();
} // namespace sonicator::contribute
