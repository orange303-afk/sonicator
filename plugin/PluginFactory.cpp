#include "plugin/PluginProcessor.h"

// The wrapper instantiates the processor through this factory. It lives apart from the processor
// so the test binary can compile the processor and editor without defining a second factory.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new sonicator::plugin::PluginProcessor();
}
