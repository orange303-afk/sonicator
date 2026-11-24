#pragma once
#include <juce_core/juce_core.h>

namespace sonicator::pathrelocation {
// Catalog paths may come from another operating system, so both '/' and '\\' are separators here.
// These helpers never touch the filesystem.
juce::String trimTrailingSeparators(const juce::String& path);
juce::String withSeparator(const juce::String& root, juce::juce_wchar separator);
// The same path with every '/' and '\\' written as the platform separator. Used where two strings
// are compared for equality: JUCE's File comparison does not normalise separators, so "C:/a" and
// "C:\\a" are different Files until this is applied.
juce::String toNative(const juce::String& path);
bool isUnder(const juce::String& path, const juce::String& root);
juce::String relativeTo(const juce::String& path, const juce::String& root);
juce::String join(const juce::String& root, const juce::String& relative, juce::juce_wchar separator);
juce::String remap(const juce::String& path, const juce::String& oldRoot, const juce::String& newRoot,
                   juce::juce_wchar separator = juce::File::getSeparatorChar());
} // namespace sonicator::pathrelocation
