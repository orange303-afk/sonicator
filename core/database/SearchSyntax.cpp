#include "SearchSyntax.h"
#include "core/smart/KeyNames.h"
#include "core/smart/NameHeuristics.h"
namespace sonicator {
juce::String globToLike(const juce::String& mask)
{
    juce::String out;
    for (auto c : mask)
    {
        if (c == '%' || c == '_' || c == '\\') out += "\\";
        if (c == '*') out += "%";
        else if (c == '?') out += "_";
        else out += juce::String::charToString(c);
    }
    return out;
}
std::vector<SearchTerm> parseSearchTerms(const juce::String& query)
{
    std::vector<SearchTerm> terms;
    for (const auto& raw : juce::StringArray::fromTokens(query.trim(), " \t\r\n", ""))
    {
        if (raw.isEmpty()) continue;
        const auto lower = raw.toLowerCase();
        SearchTerm term;
        auto formatValue = [](juce::String value)
        {
            value = value.trim().toLowerCase();
            while (value.startsWithChar('.') || value.startsWithChar('*') || value.startsWithChar('?'))
                value = value.substring(1);
            return value;
        };
        if (lower.startsWith("ext:") || lower.startsWith("format:"))
        {
            auto value = formatValue(raw.fromFirstOccurrenceOf(":", false, false));
            if (value.isEmpty()) continue;
            term.kind = SearchKind::Format;
            term.values = juce::StringArray::fromTokens(value, ",/|", "");
            if (term.values.isEmpty()) continue;
            term.text = term.values[0];
        }
        else if (lower.startsWith("type:"))
        {
            auto value = raw.fromFirstOccurrenceOf(":", false, false).trim().toLowerCase();
            if (value.isEmpty()) continue;
            const auto canonical = canonicalSmartTypeToken(value);
            term.kind = SearchKind::TypeOnly;
            term.text = canonical.isNotEmpty() ? canonical : value;
        }
        else if (lower.startsWith("key:"))
        {
            const auto value = raw.fromFirstOccurrenceOf(":", false, false);
            if (auto key = parseKeyValue(value))
            {
                term.kind = SearchKind::KeyOnly;
                term.tonic = key->tonic; term.mode = key->mode;
                term.text = value.toLowerCase();
            }
            else
            {
                term.kind = SearchKind::Text; term.text = raw;
            }
        }
        else if (lower.startsWith("note:"))
        {
            const auto value = raw.fromFirstOccurrenceOf(":", false, false);
            if (auto note = parseNoteName(value))
            {
                term.kind = SearchKind::NoteOnly;
                term.tonic = note->tonic; term.midi = note->midi;
                term.text = value.toLowerCase();
            }
            else
            {
                term.kind = SearchKind::Text; term.text = raw;
            }
        }
        else if (raw.startsWithIgnoreCase("*.") && raw.containsChar('/'))
        {
            // *.rex/rx2/sf2 — the compact form used in the library UI.
            auto parts = juce::StringArray::fromTokens(raw, "/", "");
            term.kind = SearchKind::Format;
            for (int i = 0; i < parts.size(); ++i)
            {
                auto part = formatValue(parts[i]);
                if (part.isNotEmpty()) term.values.add(part);
            }
            if (term.values.isEmpty()) continue;
            term.text = term.values[0];
        }
        else if (raw.containsAnyOf("*?"))
        {
            if (raw.containsChar('|'))
            {
                auto parts = juce::StringArray::fromTokens(raw, "|", "");
                bool allMasks = parts.size() > 1;
                for (const auto& part : parts)
                    if (! part.containsAnyOf("*?")) allMasks = false;
                if (allMasks)
                {
                    term.kind = SearchKind::GlobOr; term.values = parts; term.text = parts[0];
                }
                else
                {
                    term.kind = SearchKind::Text; term.text = raw;
                }
            }
            else
            {
                term.kind = SearchKind::Glob; term.text = raw;
            }
        }
        else if (auto type = canonicalSmartTypeToken(raw); type.isNotEmpty())
        {
            term.kind = SearchKind::TypeOrText; term.text = type;
        }
        else if (raw.length() >= 2)
        {
            if (auto note = parseNoteName(raw))
            {
                term.kind = SearchKind::KeyOrText;
                term.tonic = note->tonic; term.midi = note->midi;
                term.text = lower;
            }
            else if (auto key = parseKeyValue(raw))
            {
                term.kind = SearchKind::KeyOrText;
                term.tonic = key->tonic; term.mode = key->mode;
                term.text = lower;
            }
            else
            {
                term.kind = SearchKind::Text; term.text = raw;
            }
        }
        else
        {
            term.kind = SearchKind::Text; term.text = raw;
        }
        terms.push_back(std::move(term));
    }
    return terms;
}
} // namespace sonicator
