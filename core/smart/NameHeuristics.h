#pragma once
#include "SmartTypes.h"
namespace sonicator {
// Extracts candidates from the file name and the nearest folder only. The folder is marked
// with source "folder" so the resolver can keep it as context rather than proof.
std::vector<SmartCandidate> extractNameCandidates(const juce::String& path);
// Canonical type for a single token ("kick", "kicks", "hihat"), or an empty string.
juce::String canonicalSmartTypeToken(const juce::String& token);
} // namespace sonicator
