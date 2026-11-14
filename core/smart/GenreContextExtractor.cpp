#include "GenreContextExtractor.h"

namespace sonicator {
namespace {

struct SubgenreRule
{
    const char* canonicalSubgenre;
    const char* parentGenre;
    const char* aliases; // pipe-separated
};

constexpr SubgenreRule subgenreRules[] = {
    { "Trap", "Hip-Hop", "trap" },
    { "Drill", "Hip-Hop", "drill|uk drill|ny drill" },
    { "Boom Bap", "Hip-Hop", "boombap|boom bap" },
    { "Tech House", "House", "tech house|techhouse" },
    { "Deep House", "House", "deep house|deephouse" },
    { "Progressive House", "House", "progressive house|prog house" },
    { "Electro House", "House", "electro house" },
    { "Acid House", "House", "acid house" },
    { "Minimal Techno", "Techno", "minimal techno" },
    { "Liquid", "Drum & Bass", "liquid dnb|liquid drum and bass" },
    { "Neurofunk", "Drum & Bass", "neurofunk" },
    { "Retrowave", "Synthwave", "retrowave" },
    { "Vaporwave", "Synthwave", "vaporwave" },
    { "Riddim", "Dubstep", "riddim" }
};

struct TopGenreRule
{
    const char* canonicalGenre;
    const char* aliases; // pipe-separated
};

constexpr TopGenreRule topGenreRules[] = {
    { "Drum & Bass", "drum & bass|drum and bass|drum&bass|drumandbass|dnb" },
    { "Hip-Hop", "hip-hop|hip hop|hiphop" },
    { "Techno", "techno" },
    { "House", "house" },
    { "Lo-Fi", "lo-fi|lofi|lo fi" },
    { "Synthwave", "synthwave" },
    { "Dubstep", "dubstep" },
    { "Ambient", "ambient|drone" },
    { "Electronic", "electronic|edm" },
    { "Rock", "rock" },
    { "Pop", "pop" },
    { "Funk", "funk" },
    { "Jazz", "jazz" },
    { "Reggae", "reggae" }
};

bool matchSubgenreAlias(const juce::String& text, juce::String& outGenre, juce::String& outSubgenre)
{
    const auto lower = text.trim().toLowerCase();
    if (lower.isEmpty()) return false;
    for (const auto& rule : subgenreRules)
        for (const auto& alias : juce::StringArray::fromTokens(rule.aliases, "|", ""))
            if (lower == alias.trim().toLowerCase())
            {
                outSubgenre = juce::String(rule.canonicalSubgenre);
                outGenre = juce::String(rule.parentGenre);
                return true;
            }
    return false;
}

bool matchTopGenreAlias(const juce::String& text, juce::String& outGenre)
{
    const auto lower = text.trim().toLowerCase();
    if (lower.isEmpty()) return false;
    for (const auto& rule : topGenreRules)
        for (const auto& alias : juce::StringArray::fromTokens(rule.aliases, "|", ""))
            if (lower == alias.trim().toLowerCase())
            {
                outGenre = juce::String(rule.canonicalGenre);
                return true;
            }
    return false;
}

// Whole tokens only: "housekeeper" is one token and must never match "house".
juce::StringArray tokenize(const juce::String& text)
{
    juce::String clean = text.replace("&", " and ");
    juce::String spaced;
    for (int i = 0; i < clean.length(); ++i)
    {
        const auto ch = clean[i];
        spaced += juce::String::charToString(
            juce::CharacterFunctions::isLetterOrDigit(ch) ? ch : (juce::juce_wchar) ' ');
    }
    return juce::StringArray::fromTokens(spaced, " ", "");
}

// Longest phrase wins: "deep house" is tried before "house".
bool matchTokens(const juce::StringArray& tokens, juce::String& display, juce::String& family,
                 juce::String& fragment)
{
    const int maxLen = juce::jmin(4, tokens.size());
    for (int len = maxLen; len >= 1; --len)
        for (int start = 0; start <= tokens.size() - len; ++start)
        {
            juce::StringArray slice;
            for (int i = 0; i < len; ++i) slice.add(tokens[start + i]);
            const auto phrase = slice.joinIntoString(" ");
            juce::String sub, parent;
            if (matchSubgenreAlias(phrase, parent, sub))
            {
                display = sub;
                family = parent;
                fragment = phrase;
                return true;
            }
            juce::String top;
            if (matchTopGenreAlias(phrase, top))
            {
                display = top;
                family = top;
                fragment = phrase;
                return true;
            }
        }
    return false;
}

// A volume or drive name is never evidence of a genre.
bool isVolumeName(const juce::String& part)
{
    return part.isEmpty() || part.endsWith(":") || part.equalsIgnoreCase("Volumes")
        || part.equalsIgnoreCase("Users") || part.equalsIgnoreCase("home");
}

// juce::File does not treat a backslash as a separator on macOS, so the stem is taken from the
// normalized path instead of getFileNameWithoutExtension().
juce::String fileStem(const juce::File& file)
{
    const auto path = file.getFullPathName().replaceCharacter('\\', '/');
    const auto name = path.substring(path.lastIndexOfChar('/') + 1);
    return name.upToLastOccurrenceOf(".", false, false);
}

} // namespace

bool GenreContextExtractor::interpret(const juce::String& raw, juce::String& display, juce::String& family)
{
    juce::String sub, parent;
    if (matchSubgenreAlias(raw, parent, sub))
    {
        display = sub;
        family = parent;
        return true;
    }
    juce::String top;
    if (matchTopGenreAlias(raw, top))
    {
        display = top;
        family = top;
        return true;
    }
    // A metadata value the dictionary does not know is still a genre the exporter wrote, so it is
    // shown as written; only the family stays empty.
    const auto trimmed = raw.trim();
    if (trimmed.isEmpty()) return false;
    if (const auto normal = normalizeGenreToken(trimmed); normal.isNotEmpty())
    {
        display = normal;
        family = normal;
        return true;
    }
    return false;
}

std::vector<SmartCandidate> GenreContextExtractor::candidates(const juce::File& file)
{
    std::vector<SmartCandidate> out;
    auto make = [](const juce::String& source, const juce::String& display, const juce::String& family,
                   const juce::String& fragment, double confidence)
    {
        SmartCandidate c;
        c.field = SmartField::Genre;
        c.source = source;
        c.textValue = display;
        c.rawValue = fragment;
        c.detail = source + "=" + fragment
            + (family.isNotEmpty() && family != display ? " (" + family + ")" : "");
        c.numericValue = confidence;
        c.extractorId = "genre_" + source;
        c.extractorVersion = genreDictionaryVersion;
        c.limitation = source == "folder"
            ? "genre inferred from a folder name, not from the audio"
            : "genre inferred from the file name, not from the audio";
        c.status = CandidateStatus::Valid;
        return c;
    };

    // File name first: it is the most specific thing a person wrote about this file.
    if (const auto stem = fileStem(file); stem.isNotEmpty())
    {
        juce::String display, family, fragment;
        if (matchTokens(tokenize(stem), display, family, fragment))
            out.push_back(make("filename", display, family, fragment, 0.6));
    }

    // Only the two nearest folders. A library or drive name further up must not assign one genre
    // to every file below it. Splitting the full path by hand keeps Windows-style separators
    // working on macOS, where juce::File does not treat a backslash as a path separator.
    auto parts = juce::StringArray::fromTokens(
        file.getFullPathName().replaceCharacter('\\', '/'), "/", "");
    if (! parts.isEmpty()) parts.remove(parts.size() - 1); // the file itself
    int visited = 0;
    for (int i = parts.size() - 1; i >= 0 && visited < 2; --i)
    {
        const auto part = parts[i];
        if (isVolumeName(part)) break;
        ++visited;
        juce::String display, family, fragment;
        if (matchTokens(tokenize(part), display, family, fragment))
        {
            out.push_back(make("folder", display, family, fragment, 0.55));
            break; // the nearest folder that names a genre wins
        }
    }
    return out;
}

GenreContextResult GenreContextExtractor::extract(const juce::File& file,
                                                 const std::vector<SmartCandidate>& existingCandidates)
{
    // 1. Metadata written by the exporter.
    for (const auto& c : existingCandidates)
    {
        if (c.field != SmartField::Genre) continue;
        if (c.status != CandidateStatus::Valid || c.textValue.trim().isEmpty()) continue;
        juce::String display, family;
        if (! interpret(c.textValue, display, family)) display = c.textValue.trim();
        GenreContextResult res;
        res.genre = family.isNotEmpty() ? family : display;
        res.subgenre = display != res.genre ? display : juce::String();
        res.source = c.source == "id3" ? "id3"
            : (c.source == "folder" || c.source == "filename") ? c.source
            : "metadata";
        res.confidence = 0.9;
        res.matchedFragment = c.rawValue.isNotEmpty() ? c.rawValue : c.textValue;
        return res;
    }

    // 2. Name and nearest folders.
    for (const auto& c : candidates(file))
    {
        juce::String family = c.detail.contains("(") ? c.detail.fromFirstOccurrenceOf("(", false, false)
                                                            .upToFirstOccurrenceOf(")", false, false)
                                                     : c.textValue;
        GenreContextResult res;
        res.genre = family.isNotEmpty() ? family : c.textValue;
        res.subgenre = c.textValue != res.genre ? c.textValue : juce::String();
        res.source = c.source;
        res.confidence = c.source == "folder" ? 0.85 : 0.7;
        res.matchedFragment = c.rawValue;
        return res;
    }
    return {};
}

juce::String GenreContextExtractor::normalizeGenreToken(const juce::String& token)
{
    const auto clean = token.trim().toLowerCase();
    if (clean.isEmpty()) return {};
    juce::String sub, parent;
    if (matchSubgenreAlias(clean, parent, sub)) return sub;
    juce::String top;
    if (matchTopGenreAlias(clean, top)) return top;
    return {};
}

juce::StringArray GenreContextExtractor::canonicalGenres()
{
    return { "Electronic", "House", "Techno", "Hip-Hop", "Drum & Bass", "Trap", "Dubstep",
             "Ambient", "Lo-Fi", "Synthwave", "Rock", "Pop", "Funk", "Jazz", "Reggae" };
}

} // namespace sonicator
