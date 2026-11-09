#pragma once
#include <juce_core/juce_core.h>
#include <vector>
namespace sonicator {
struct ReasonReference
{
    juce::String name, refill, message;
    juce::File file;
    bool isLocal() const { return file != juce::File(); }
};
struct ReasonPatch
{
    juce::String name, message, error;
    juce::StringArray devices;
    std::vector<ReasonReference> references;
};
ReasonPatch readReasonPatch(const juce::File&, bool resolveSamples = true);
}
