#pragma once
#include <juce_core/juce_core.h>

namespace sonicator::pathrelocation {
// Catalog paths may come from another operating system, so both '/' and '\\' are separators here.
// These helpers never touch the filesystem.
juce::String trimTrailingSeparators(const juce::String& path);
juce::String withSeparator(const juce::String& root, juce::juce_wchar separator);
// Rewrites '/' to a Windows separator. On a POSIX separator the path is returned unchanged: there a
// backslash is an ordinary filename character, not another spelling of the separator.
juce::String normaliseSeparators(const juce::String& path, juce::juce_wchar separator);
// Half-open string range [lower, upper) that contains every descendant of `folder` and no sibling
// whose name merely starts with the folder name. The folder itself is bounded by its separator.
std::pair<juce::String, juce::String> folderRange(const juce::String& folder,
                                                 juce::juce_wchar separator);
bool isUnder(const juce::String& path, const juce::String& root);
juce::String relativeTo(const juce::String& path, const juce::String& root);
juce::String join(const juce::String& root, const juce::String& relative, juce::juce_wchar separator);
juce::String remap(const juce::String& path, const juce::String& oldRoot, const juce::String& newRoot,
                   juce::juce_wchar separator = juce::File::getSeparatorChar());
} // namespace sonicator::pathrelocation
