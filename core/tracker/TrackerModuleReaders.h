#pragma once
#include "TrackerFormats.h"
namespace sonicator {
bool parseXm(const std::vector<uint8_t>& bytes, TrackerDocumentData& out, juce::String& error);
bool parseS3m(const std::vector<uint8_t>& bytes, TrackerDocumentData& out, juce::String& error);
bool parseXi(const std::vector<uint8_t>& bytes, TrackerDocumentData& out, juce::String& error);
} // namespace sonicator
