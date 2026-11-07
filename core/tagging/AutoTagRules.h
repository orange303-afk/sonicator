#pragma once
#include "AutoTagTypes.h"
#include <set>
namespace sonicator {
// Pure, deterministic instrument-only rules. They never read files, SQLite or the UI; the caller
// supplies stored evidence and the mapping to existing tags. A file with competing or weak
// evidence is skipped rather than confidently mistagged.
class AutoTagRules
{
public:
    static AutoTagDecision evaluate(const AutoTagInput&, const AutoTagOptions&);
    // Canonical whitelist class for one exact token ("kick", "bd", "tambourine"), or empty.
    static juce::String canonicalTypeForToken(const juce::String& token);
    // Tokens split on non-alphanumerics; "#" is kept so note-like tokens stay whole.
    static juce::StringArray splitTokens(const juce::String& text);
    // Canonical instrument classes AutoTag may propose, in stable display order.
    static juce::StringArray supportedTypes();
};
} // namespace sonicator
