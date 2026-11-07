#pragma once
#include <juce_core/juce_core.h>
#include <vector>
namespace sonicator {
enum class SearchKind
{
    Text,             // plain substring in folded name + path
    Glob,             // file-name mask with * and ?
    GlobOr,           // several masks written with |
    Format,           // ext:rx2 / format:wav
    TypeOrText,       // bare known type ("pad"): smart type or file-name text
    TypeOnly,         // type:pad
    KeyOrText,        // bare key/note token ("Amin", "C4"): smart key or text
    KeyOnly,          // key:Am
    NoteOnly          // note:C4: sampler root note, excluding defaults
};
struct SearchTerm
{
    SearchKind kind = SearchKind::Text;
    juce::String text;   // folded or original value, depending on kind
    int tonic = -1;      // 0 = C .. 11 = B for key/note terms
    int midi = -1;       // MIDI note for NoteOnly
    juce::String mode;   // major/minor when the user named one
    juce::StringArray values; // formats or masks when the term carries several alternatives
};
// Splits a query into terms. Plain words keep the existing "all tokens must match" behaviour.
std::vector<SearchTerm> parseSearchTerms(const juce::String& query);
// Converts a * / ? mask into a LIKE pattern with backslash escaping.
juce::String globToLike(const juce::String& mask);
} // namespace sonicator
