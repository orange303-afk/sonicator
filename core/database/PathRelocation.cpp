#include "PathRelocation.h"

namespace sonicator::pathrelocation {
namespace {
bool isSeparator(juce::juce_wchar c) { return c == '/' || c == '\\'; }

juce::String canonical(const juce::String& path)
{
    return path.replaceCharacter('\\', '/');
}

juce::String withNativeSeparators(const juce::String& relative, juce::juce_wchar separator)
{
    juce::String out;
    out.preallocateBytes((size_t) relative.length() * sizeof(juce::juce_wchar));
    for (auto c : relative)
        out += juce::String::charToString(isSeparator(c) ? separator : c);
    return out;
}
} // namespace

juce::String trimTrailingSeparators(const juce::String& path)
{
    auto out = path;
    while (out.length() > 1 && isSeparator(out[out.length() - 1]))
        out = out.dropLastCharacters(1);
    return out;
}

juce::String withSeparator(const juce::String& root, juce::juce_wchar separator)
{
    if (root.isEmpty()) return juce::String::charToString(separator);
    const auto trimmed = trimTrailingSeparators(root);
    if (trimmed.length() == 1 && isSeparator(trimmed[0])) return trimmed;
    return trimmed + juce::String::charToString(separator);
}

bool isUnder(const juce::String& path, const juce::String& root)
{
    const auto r = canonical(trimTrailingSeparators(root));
    if (r.isEmpty()) return false;
    const auto p = canonical(path);
    if (p == r) return true;
    if (! p.startsWith(r)) return false;
    if (r == "/") return true;
    return p[r.length()] == '/';
}

juce::String relativeTo(const juce::String& path, const juce::String& root)
{
    if (! isUnder(path, root)) return {};
    const auto r = trimTrailingSeparators(root);
    if (path.length() == r.length()) return {};
    if (r == "/") return path.substring(1);
    auto offset = r.length();
    if (offset < path.length() && isSeparator(path[offset])) ++offset;
    return path.substring(offset);
}

juce::String join(const juce::String& root, const juce::String& relative, juce::juce_wchar separator)
{
    const auto trimmed = trimTrailingSeparators(root);
    const auto native = withNativeSeparators(relative, separator);
    if (native.isEmpty()) return trimmed;
    if (trimmed.isEmpty()) return juce::String::charToString(separator) + native;
    if (trimmed.length() == 1 && isSeparator(trimmed[0])) return trimmed + native;
    return trimmed + juce::String::charToString(separator) + native;
}

juce::String remap(const juce::String& path, const juce::String& oldRoot, const juce::String& newRoot,
                   juce::juce_wchar separator)
{
    if (! isUnder(path, oldRoot)) return path;
    return join(newRoot, relativeTo(path, oldRoot), separator);
}
} // namespace sonicator::pathrelocation
