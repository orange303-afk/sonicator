#pragma once
#include "core/smart/SmartTypes.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace sonicator {

struct GenreContextResult {
    juce::String genre;
    juce::String subgenre;
    double confidence = 0.0;
    juce::String source; // "path", "metadata", "id3"
};

class GenreContextExtractor {
public:
    static GenreContextResult extract(const juce::File& file, const std::vector<SmartCandidate>& existingCandidates = {});
    static juce::String normalizeGenreToken(const juce::String& token);
    static juce::StringArray canonicalGenres();
};

} // namespace sonicator
