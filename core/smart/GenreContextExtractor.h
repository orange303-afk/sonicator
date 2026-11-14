#pragma once
#include "core/smart/SmartTypes.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace sonicator {

// The dictionary that turns a folder, file name or metadata string into one musical genre.
// Bump the version when the vocabulary or the matching rules change, so old rows are revisited.
inline constexpr int genreDictionaryVersion = 1;

struct GenreContextResult {
    juce::String genre;      // canonical family, e.g. "House"
    juce::String subgenre;   // specific style, e.g. "Deep House"; empty when only the family is known
    double confidence = 0.0;
    juce::String source;     // metadata, folder, filename
    juce::String matchedFragment; // the exact text that matched, kept for the tooltip
};

class GenreContextExtractor {
public:
    // The best single result, strongest source first: metadata, then the nearest folders, then the
    // file name. Used where one value is enough (acoustic features); the catalog stores separate
    // candidates so the resolver can detect a conflict.
    static GenreContextResult extract(const juce::File& file,
                                      const std::vector<SmartCandidate>& existingCandidates = {});
    // Folder and file-name candidates only. Metadata candidates are produced by the container
    // readers as SmartField::Genre and are already in the analysis list.
    static std::vector<SmartCandidate> candidates(const juce::File& file);
    // Canonical display value and family for one raw string. False when it is not a genre.
    static bool interpret(const juce::String& raw, juce::String& display, juce::String& family);
    static juce::String normalizeGenreToken(const juce::String& token);
    static juce::StringArray canonicalGenres();
};

} // namespace sonicator
