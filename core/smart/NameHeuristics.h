#pragma once
#include "SmartTypes.h"
namespace sonicator {
// Extracts candidates from the file name and the nearest folder only. The folder is marked
// with source "folder" so the resolver can keep it as context rather than proof.
std::vector<SmartCandidate> extractNameCandidates(const juce::String& path);
// Canonical type for a single token ("kick", "kicks", "hihat"), or an empty string.
juce::String canonicalSmartTypeToken(const juce::String& token);
// Every type name the heuristics know. A user type outside this list is "custom" for the intake.
juce::StringArray canonicalSmartTypeNames();
// Every type a person can pick in the Type menu, including labels that a file name cannot infer
// (Piano, Pluck, Stab). This is the built-in vocabulary the intake reports as standard.
juce::StringArray standardSampleTypeNames();
} // namespace sonicator
