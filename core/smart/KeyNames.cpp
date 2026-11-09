#include "KeyNames.h"
#include <cmath>
namespace sonicator {
namespace {
const char* const sharpNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
int letterTonic(juce::juce_wchar c)
{
    switch (c)
    {
        case 'C': return 0;
        case 'D': return 2;
        case 'E': return 4;
        case 'F': return 5;
        case 'G': return 7;
        case 'A': return 9;
        case 'B': return 11;
        default: return -1;
    }
}
} // namespace
juce::String canonicalKeyName(int tonic, const juce::String& mode)
{
    if (tonic < 0 || tonic > 11) return {};
    return juce::String(sharpNames[tonic]) + " " + (mode.isEmpty() ? "unknown" : mode);
}
juce::String keyDisplayName(int tonic, const juce::String& mode)
{
    if (tonic < 0 || tonic > 11) return {};
    const juce::String name(sharpNames[tonic]);
    if (mode.isEmpty() || mode == "unknown") return name;
    return name + " " + mode;
}
juce::String midiNoteName(int midi)
{
    if (midi < 0 || midi > 127) return {};
    return juce::String(sharpNames[midi % 12]) + juce::String(midi / 12 - 1);
}
juce::String formatCents(double cents)
{
    if (! std::isfinite(cents)) return {};
    const int rounded = (int) std::lround(cents);
    if (rounded == 0) return "0c";
    return (rounded > 0 ? "+" : "") + juce::String(rounded) + "c";
}
std::optional<MusicalKey> parseKeyToken(const juce::String& token)
{
    auto text = token.trim();
    if (text.isEmpty()) return std::nullopt;
    // A common prefix in sample names: keyC#min, key_Amin, key-Gmaj.
    if (text.startsWithIgnoreCase("key"))
    {
        text = text.substring(3);
        while (text.startsWithChar('-') || text.startsWithChar('_') || text.startsWithChar(' '))
            text = text.substring(1);
    }
    text = text.replace(" ", "").replace("-", "").replace("_", "");
    if (text.isEmpty() || text.length() > 8) return std::nullopt;
    const auto upperLetter = juce::CharacterFunctions::toUpperCase(text[0]);
    const int tonic = letterTonic(upperLetter);
    if (tonic < 0) return std::nullopt;
    int index = 1;
    int accidental = 0;
    juce::juce_wchar accidentalChar = 0;
    if (index < text.length() && (text[index] == '#' || text[index] == 'b'))
    {
        accidentalChar = text[index];
        accidental = accidentalChar == '#' ? 1 : -1;
        ++index;
    }
    auto modeText = text.substring(index).toLowerCase();
    juce::String mode;
    if (modeText.isEmpty()) mode = "major";
    else if (modeText == "maj" || modeText == "major" || modeText == "maj7") mode = "major";
    else if (modeText == "min" || modeText == "minor" || modeText == "m" || modeText == "min7" || modeText == "m7")
        mode = "minor";
    else if (modeText == "dim" || modeText == "aug" || modeText == "sus2" || modeText == "sus4")
        mode = "unknown";
    else return std::nullopt;
    const int value = (tonic + accidental + 12) % 12;
    MusicalKey key;
    key.tonic = value;
    key.mode = mode;
    key.canonical = juce::String::charToString(upperLetter)
                  + (accidentalChar != 0 ? juce::String::charToString(accidentalChar) : juce::String())
                  + " " + mode;
    return key;
}
std::optional<MusicalKey> parseKeyValue(const juce::String& value)
{
    return parseKeyToken(value);
}
std::optional<NoteName> parseNoteName(const juce::String& token)
{
    auto text = token.trim();
    if (text.length() < 2 || text.length() > 3) return std::nullopt;
    const auto letter = juce::CharacterFunctions::toUpperCase(text[0]);
    int tonic = letterTonic(letter);
    if (tonic < 0) return std::nullopt;
    int index = 1, accidental = 0;
    if (index < text.length() && (text[index] == '#' || text[index] == 'b'))
    {
        accidental = text[index] == '#' ? 1 : -1;
        ++index;
    }
    if (index >= text.length() || ! juce::CharacterFunctions::isDigit(text[index]))
        return std::nullopt;
    const int octave = text[index] - '0';
    if (octave > 9) return std::nullopt;
    if (index + 1 != text.length()) return std::nullopt;
    NoteName note;
    note.tonic = (tonic + accidental + 12) % 12;
    note.octave = octave;
    note.midi = juce::jlimit(0, 127, (octave + 1) * 12 + note.tonic);
    return note;
}
} // namespace sonicator
