#pragma once
#include "core/database/AssetCompatibility.h"
#include <juce_core/juce_core.h>
namespace sonicator {
// Physical chunk-boundary validation for WAV/AIFF. Other codecs remain decoder-validated.
// When compatibility is supplied it receives a verdict for structural failures only: an
// unreadable file stays unknown, a truncated payload is invalid, an unsupported RF64 layout is
// unsupported. Callers must not infer a verdict from the message text.
juce::String validateContainer(const juce::File&, AssetCompatibility* compatibility = nullptr);
} // namespace sonicator
