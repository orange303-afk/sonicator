#pragma once
#include <juce_core/juce_core.h>
#include <optional>
namespace sonicator {
struct MusicalKey
{
    int tonic = -1;          // 0 = C .. 11 = B
    juce::String mode;       // "major", "minor" or "unknown"
    juce::String canonical;  // e.g. "C# minor"
};
// Parses a complete token such as "C#min", "Bbmaj", "keyAmin", "G", "F#m".
// Returns nullopt for anything that is not a complete key token, so words such as
// "Cataract" or "Amen" never become a key. A missing mode is reported as major only
// when the caller explicitly treats a bare note as a key (ID3 TKEY); callers that
// parse filenames must require a mode suffix themselves.
std::optional<MusicalKey> parseKeyToken(const juce::String& token);
// True when the text is a complete key token (used for INFO IKEY, TKEY and similar fields).
std::optional<MusicalKey> parseKeyValue(const juce::String& value);
juce::String canonicalKeyName(int tonic, const juce::String& mode);
// "A minor", or just "A" when only the tonic is known. Empty for an invalid tonic.
juce::String keyDisplayName(int tonic, const juce::String& mode);
// MIDI note name in the selection bar, e.g. 69 -> "A4", 61 -> "C#4". Empty outside 0..127.
juce::String midiNoteName(int midi);
// Signed cents for display, e.g. +12.4 -> "+12c", -0.2 -> "0c".
juce::String formatCents(double cents);
struct NoteName
{
    int tonic = -1;   // 0 = C .. 11 = B
    int octave = -1;  // octave written by the user, not assumed
    int midi = -1;    // MIDI note number
};
// Parses a complete note token with an octave, e.g. C4, F#3, Bb2. Returns nullopt for words.
std::optional<NoteName> parseNoteName(const juce::String& token);
// Root note for a displayed key name (C4 = 60). Accepts "C", "C#", "Bb", "A minor"; -1 when the
// name does not start with a note.
int rootMidiFromKey(const juce::String& key);
} // namespace sonicator
