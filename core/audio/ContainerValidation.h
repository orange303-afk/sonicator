#pragma once
#include <juce_core/juce_core.h>
namespace sonicator {
// Physical chunk-boundary validation for WAV/AIFF. Other codecs remain decoder-validated.
juce::String validateContainer(const juce::File&);
}
