#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
namespace sonicator {
// The one registration every reader path uses: JUCE's basic formats plus Sonicator's own.
void registerSonicatorFormats(juce::AudioFormatManager& manager);
} // namespace sonicator
