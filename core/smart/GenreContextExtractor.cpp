#include "GenreContextExtractor.h"

namespace sonicator {
namespace {

struct SubgenreRule {
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

struct TopGenreRule {
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

struct GenreAlias {
    const char* canonical;
    const char* aliases; // pipe-separated
};

constexpr GenreAlias genreAliases[] = {
    { "Drum & Bass", "drum & bass|drum and bass|drum&bass|drumandbass|dnb" },
    { "Trap", "trap|drill" },
    { "Hip-Hop", "hip-hop|hip hop|hiphop|boombap|boom bap|rap" },
    { "Techno", "techno|minimal techno" },
    { "House", "house|tech house|deep house|electro house|progressive house|acid house" },
    { "Lo-Fi", "lo-fi|lofi|lo fi" },
    { "Synthwave", "synthwave|retrowave|vaporwave|chillwave" },
    { "Dubstep", "dubstep|riddim" },
    { "Ambient", "ambient|drone" },
    { "Electronic", "electronic|electro|edm" },
    { "Rock", "rock" },
    { "Pop", "pop" },
    { "Funk", "funk" },
    { "Jazz", "jazz" },
    { "Reggae", "reggae|dub|dancehall" }
};

bool matchSubgenreAlias(const juce::String& text, juce::String& outGenre, juce::String& outSubgenre)
{
    const auto lower = text.trim().toLowerCase();
    if (lower.isEmpty()) return false;

    for (const auto& rule : subgenreRules)
    {
        for (const auto& alias : juce::StringArray::fromTokens(rule.aliases, "|", ""))
        {
            if (lower == alias.trim().toLowerCase())
            {
                outSubgenre = juce::String(rule.canonicalSubgenre);
                outGenre = juce::String(rule.parentGenre);
                return true;
            }
        }
    }
    return false;
}

bool matchTopGenreAlias(const juce::String& text, juce::String& outGenre)
{
    const auto lower = text.trim().toLowerCase();
    if (lower.isEmpty()) return false;

    for (const auto& rule : topGenreRules)
    {
        for (const auto& alias : juce::StringArray::fromTokens(rule.aliases, "|", ""))
        {
            if (lower == alias.trim().toLowerCase())
            {
                outGenre = juce::String(rule.canonicalGenre);
                return true;
            }
        }
    }
    return false;
}

juce::StringArray tokenizeFolder(const juce::String& folderName)
{
    juce::String clean = folderName.replace("&", " and ");
    juce::String spaced;
    for (int i = 0; i < clean.length(); ++i)
    {
        const auto ch = clean[i];
        if (juce::CharacterFunctions::isLetterOrDigit(ch))
            spaced += ch;
        else
            spaced += ' ';
    }
    return juce::StringArray::fromTokens(spaced, " ", "");
}

bool analyzeFolderTokens(const juce::StringArray& tokens,
                         juce::String& outGenre,
                         juce::String& outSubgenre,
                         bool& isSubgenre)
{
    if (tokens.isEmpty()) return false;

    // Check n-grams from length 4 down to 1
    const int maxLen = juce::jmin(4, tokens.size());
    for (int len = maxLen; len >= 1; --len)
    {
        for (int start = 0; start <= tokens.size() - len; ++start)
        {
            juce::String phrase;
            juce::String joined;
            for (int i = 0; i < len; ++i)
            {
                if (i > 0) phrase += " ";
                phrase += tokens[start + i];
                joined += tokens[start + i];
            }

            juce::String g, sg;
            if (matchSubgenreAlias(phrase, g, sg) || matchSubgenreAlias(joined, g, sg))
            {
                outGenre = g;
                outSubgenre = sg;
                isSubgenre = true;
                return true;
            }
        }
    }

    // If no subgenre matched, check top-level genres
    for (int len = maxLen; len >= 1; --len)
    {
        for (int start = 0; start <= tokens.size() - len; ++start)
        {
            juce::String phrase;
            juce::String joined;
            for (int i = 0; i < len; ++i)
            {
                if (i > 0) phrase += " ";
                phrase += tokens[start + i];
                joined += tokens[start + i];
            }

            juce::String g;
            if (matchTopGenreAlias(phrase, g) || matchTopGenreAlias(joined, g))
            {
                outGenre = g;
                outSubgenre = {};
                isSubgenre = false;
                return true;
            }
        }
    }

    return false;
}

} // namespace

GenreContextResult GenreContextExtractor::extract(const juce::File& file,
                                                 const std::vector<SmartCandidate>& existingCandidates)
{
    // 1. First check existingCandidates for ID3 or INFO genre candidates
    for (const auto& c : existingCandidates)
    {
        if (c.status == CandidateStatus::Invalid || c.status == CandidateStatus::Unsupported)
            continue;
        if (c.textValue.trim().isEmpty())
            continue;

        const bool isNonGenreField = (c.field == SmartField::Bpm || c.field == SmartField::Key
                                      || c.field == SmartField::Pitch || c.field == SmartField::RootNote);
        if (isNonGenreField)
            continue;

        const bool isId3 = c.source.equalsIgnoreCase("id3");
        const bool isInfoOrMeta = c.source.equalsIgnoreCase("info") || c.source.equalsIgnoreCase("metadata");
        const bool isGenreDetail = c.detail.containsIgnoreCase("genre")
                                    || c.detail.startsWithIgnoreCase("TCON")
                                    || c.detail.startsWithIgnoreCase("IGNR");

        if (isId3 || isInfoOrMeta || isGenreDetail)
        {
            GenreContextResult res;
            res.confidence = 0.9;
            res.source = isId3 ? "id3" : "metadata";

            juce::String genre, subgenre;
            if (matchSubgenreAlias(c.textValue, genre, subgenre))
            {
                res.genre = genre;
                res.subgenre = subgenre;
            }
            else if (matchTopGenreAlias(c.textValue, genre))
            {
                res.genre = genre;
                res.subgenre = {};
            }
            else
            {
                auto norm = normalizeGenreToken(c.textValue);
                if (norm.isNotEmpty())
                {
                    res.genre = norm;
                    res.subgenre = {};
                }
                else
                {
                    res.genre = c.textValue.trim();
                    res.subgenre = {};
                }
            }
            return res;
        }
    }

    // 2. Traverse parent folder names from root down to file's parent
    juce::String fullPath = file.getFullPathName().replaceCharacter('\\', '/');
    const int lastSlash = fullPath.lastIndexOfChar('/');
    const juce::String parentPath = (lastSlash >= 0) ? fullPath.substring(0, lastSlash) : fullPath;
    const auto folderParts = juce::StringArray::fromTokens(parentPath, "/", "");

    juce::String currentGenre;
    juce::String currentSubgenre;
    bool found = false;

    for (const auto& part : folderParts)
    {
        if (part.length() <= 2 && part.endsWith(":"))
            continue; // Skip Windows drive letters like "C:"

        const auto tokens = tokenizeFolder(part);
        juce::String folderGenre, folderSubgenre;
        bool isSub = false;

        if (analyzeFolderTokens(tokens, folderGenre, folderSubgenre, isSub))
        {
            found = true;
            if (isSub)
            {
                currentGenre = folderGenre;
                currentSubgenre = folderSubgenre;
            }
            else
            {
                if (currentSubgenre.isEmpty() || currentGenre.isEmpty())
                {
                    currentGenre = folderGenre;
                }
            }
        }
    }

    if (found)
    {
        GenreContextResult res;
        res.genre = currentGenre;
        res.subgenre = currentSubgenre;
        res.confidence = 0.85; // >= 0.8
        res.source = "path";
        return res;
    }

    // 3. None found: empty result with 0.0 confidence
    return {};
}

juce::String GenreContextExtractor::normalizeGenreToken(const juce::String& token)
{
    const auto clean = token.trim().toLowerCase();
    if (clean.isEmpty()) return {};

    for (const auto& item : genreAliases)
    {
        for (const auto& alias : juce::StringArray::fromTokens(item.aliases, "|", ""))
        {
            if (clean == alias.trim().toLowerCase())
                return juce::String(item.canonical);
        }
    }
    return {};
}

juce::StringArray GenreContextExtractor::canonicalGenres()
{
    return {
        "Electronic",
        "House",
        "Techno",
        "Hip-Hop",
        "Drum & Bass",
        "Trap",
        "Dubstep",
        "Ambient",
        "Lo-Fi",
        "Synthwave",
        "Rock",
        "Pop",
        "Funk",
        "Jazz",
        "Reggae"
    };
}

} // namespace sonicator
