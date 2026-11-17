#include "PlatformName.h"

namespace sonicator::contribute
{
juce::String platformName()
{
   #if JUCE_WINDOWS
    return "win";
   #elif JUCE_LINUX
    return "linux";
   #else
    return "mac";
   #endif
}
} // namespace sonicator::contribute
