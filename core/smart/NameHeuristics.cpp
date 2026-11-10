#include "NameHeuristics.h"
#include "KeyNames.h"
#include <cmath>
namespace sonicator {
namespace {
struct Alias { const char* canonical; const char* tokens; };
// Exact tokens, never substrings: "bd"/"sd"/"hh" are deliberately absent because the spike
// showed they match unrelated abbreviations inside this library.
constexpr Alias typeAliases[] = {
    { "kick", "kick|kickdrum|kik" },
    { "snare", "snare|snr" },
    { "rim", "rim|rimshot|sidestick" },
    { "clap", "clap|claps|handclap" },
    { "snap", "snap|snaps|fingersnap" },
    { "click", "click|clicks|tick" },
    { "blip", "blip|blips|bleep" },
    { "zap", "zap|zaps|laser" },
    { "glitch", "glitch|stutter" },
    { "hat", "hihat|hat|hats|openhat|closedhat" },
    { "shaker", "shaker|shakers" },
    { "tamb", "tamb|tambourine" },
    { "cowbell", "cowbell|cbell" },
    { "bongo", "bongo|bongos" },
    { "conga", "conga|congas|tumba" },
    { "timbale", "timbale|timbales" },
    { "agogo", "agogo" },
    { "clave", "clave|claves|woodblock" },
    { "cabasa", "cabasa|guiro" },
    { "triangle", "triangle" },
    { "djembe", "djembe" },
    { "tabla", "tabla" },
    { "tom", "tom|toms" },
    { "crash", "crash|crashes" },
    { "ride", "ride|rides" },
    { "cymbal", "cymbal|splash|china" },
    { "percussion", "perc|percussion" },
    { "bass", "bass|bassline" },
    { "sub", "sub|subbass" },
    { "808", "808|808bass|808sub" },
    { "reese", "reese|reesebass" },
    { "acid", "acid|303|tb303" },
    { "lead", "lead|arp|arpeggio|pluck|stab|riff" },
    { "pad", "pad|pads|atmos|atmosphere|texture|drone|ambient" },
    { "keys", "keys|piano|organ|rhodes|wurli|clav" },
    { "synth", "synth|chord|chords|poly" },
    { "guitar", "guitar|gtr" },
    { "vocal", "vocal|vocals|vox|choir|acapella" },
    { "fx", "fx|sfx|effect|effects|noise|sweep|impact|foley" },
    { "foley", "foley" },
    { "riser", "riser|sweep|upfilter|uplifter" },
    { "drop", "drop|fall|downfilter|downlifter" },
    { "brass", "brass|trumpet|sax|saxophone|horn|horns" },
    { "strings", "string|strings|violin|cello|viola" }
};
juce::StringArray splitTokens(const juce::String& text)
{
    juce::StringArray out;
    juce::String current;
    for (auto c : text)
    {
        if (juce::CharacterFunctions::isLetterOrDigit(c) || c == '#')
            current += juce::String::charToString(c);
        else if (current.isNotEmpty())
        {
            out.add(current);
            current.clear();
        }
    }
    if (current.isNotEmpty()) out.add(current);
    return out;
}
juce::String typeForToken(const juce::String& token)
{
    auto match = [](const juce::String& value)
    {
        for (const auto& alias : typeAliases)
            for (const auto& candidate : juce::StringArray::fromTokens(alias.tokens, "|", ""))
                if (value.equalsIgnoreCase(candidate)) return juce::String(alias.canonical);
        return juce::String();
    };
    auto exact = match(token);
    if (exact.isNotEmpty()) return exact;
    if (token.length() > 3 && token.endsWithIgnoreCase("s"))
        return match(token.dropLastCharacters(1)); // Kicks, Snares, Hats
    return {};
}
bool isNoteToken(const juce::String& token)
{
    if (token.isEmpty() || token.length() > 2) return false;
    const auto letter = juce::CharacterFunctions::toUpperCase(token[0]);
    if (letter < 'A' || letter > 'G') return false;
    return token.length() == 1 || token[1] == '#' || token[1] == 'b';
}
bool isModeWord(const juce::String& token)
{
    for (auto word : { "min", "maj", "minor", "major", "m", "dim", "aug", "maj7", "min7", "m7" })
        if (token.equalsIgnoreCase(word)) return true;
    return false;
}
bool hasModeSuffix(const juce::String& token)
{
    auto lower = token.toLowerCase();
    return lower.endsWith("min") || lower.endsWith("maj") || lower.endsWith("minor")
        || lower.endsWith("major") || lower.endsWith("maj7") || lower.endsWith("min7")
        || lower.endsWith("m7") || lower.endsWith("dim") || lower.endsWith("aug")
        || lower.endsWith("m");
}
void collect(const juce::String& text, const juce::String& source, std::vector<SmartCandidate>& out)
{
    auto tokens = splitTokens(text);
    // "Bb Minor" is one key written as two tokens; join it before matching.
    for (int i = 0; i + 1 < tokens.size(); ++i)
        if (isNoteToken(tokens[i]) && isModeWord(tokens[(size_t) i + 1]))
        {
            tokens.set(i, tokens[i] + tokens[(size_t) i + 1]);
            tokens.remove((size_t) i + 1);
        }
    for (int i = 0; i < tokens.size(); ++i)
    {
        const auto& token = tokens[(size_t) i];
        if (auto type = typeForToken(token); type.isNotEmpty())
        {
            SmartCandidate c; c.field = SmartField::Type; c.source = source;
            c.textValue = type; c.detail = token; out.push_back(c);
        }
        if (token.equalsIgnoreCase("one") && i + 1 < tokens.size()
            && tokens[(size_t) i + 1].equalsIgnoreCase("shot"))
        {
            SmartCandidate c; c.field = SmartField::Structure; c.source = source;
            c.textValue = "one-shot"; c.detail = token + "-" + tokens[(size_t) i + 1];
            out.push_back(c);
        }
        if (token.equalsIgnoreCase("oneshot") || token.equalsIgnoreCase("loop")
            || token.equalsIgnoreCase("loops") || token.equalsIgnoreCase("phrase")
            || token.equalsIgnoreCase("phrases"))
        {
            SmartCandidate c; c.field = SmartField::Structure; c.source = source;
            c.textValue = token.startsWithIgnoreCase("loop") ? "loop"
                         : (token.startsWithIgnoreCase("phrase") ? "phrase" : "one-shot");
            c.detail = token; out.push_back(c);
        }
        // 128bpm / 128BPM
        auto lower = token.toLowerCase();
        if (lower.endsWith("bpm") && lower.length() > 3)
        {
            auto number = lower.dropLastCharacters(3);
            if (number.containsOnly("0123456789"))
            {
                const double bpm = number.getDoubleValue();
                if (bpm >= 20 && bpm <= 400)
                {
                    SmartCandidate c; c.field = SmartField::Bpm; c.source = source;
                    c.numericValue = bpm; c.detail = token; out.push_back(c);
                }
            }
        }
        // "BPM 128" or "128 bpm" with a separate token.
        if (lower == "bpm")
        {
            auto addSeparate = [&](const juce::String& number)
            {
                if (! number.containsOnly("0123456789")) return;
                const double bpm = number.getDoubleValue();
                if (bpm >= 20 && bpm <= 400)
                {
                    SmartCandidate c; c.field = SmartField::Bpm; c.source = source;
                    c.numericValue = bpm; c.detail = number + " bpm"; out.push_back(c);
                }
            };
            if (i > 0) addSeparate(tokens[(size_t) i - 1]);
            if (i + 1 < tokens.size()) addSeparate(tokens[(size_t) i + 1]);
        }
        if (lower.startsWith("key") || hasModeSuffix(token))
        {
            if (auto key = parseKeyToken(token))
            {
                SmartCandidate c; c.field = SmartField::Key; c.source = source;
                c.textValue = key->canonical; c.detail = token; out.push_back(c);
            }
        }
    }
}
} // namespace
juce::String canonicalSmartTypeToken(const juce::String& token) { return typeForToken(token); }
std::vector<SmartCandidate> extractNameCandidates(const juce::String& path)
{
    std::vector<SmartCandidate> out;
    const juce::File file(path);
    collect(file.getFileNameWithoutExtension(), "filename", out);
    const auto folder = file.getParentDirectory().getFileName();
    if (folder.isNotEmpty()) collect(folder, "folder", out);
    return out;
}
} // namespace sonicator
