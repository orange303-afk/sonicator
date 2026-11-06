#pragma once
#include "core/database/SampleRecord.h"
#include "core/scanner/FileScanner.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
namespace sonicator {
class BasicAnalyzer
{
public:
    SampleRecord analyze(const juce::File&, CancelCheck = {}) const;
    SampleRecord analyzeAndHash(const juce::File&, CancelCheck = {}) const;
};
}
