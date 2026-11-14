#pragma once
#include <juce_core/juce_core.h>

namespace sonicator::updates
{
// The newest published release on the project's GitHub page.
struct Release
{
    juce::String tag;         // as published, e.g. "v0.2.0"
    juce::String url;         // the release page
    bool published = false;   // a release exists to compare against
    bool newer = false;       // that release is above the running version
    juce::String error;       // non-empty when the check itself failed
};

// Compares dotted version numbers and ignores a leading "v". Returns -1, 0 or 1.
int compareVersions(const juce::String& a, const juce::String& b);

// Reads the answer of GitHub's releases endpoint. Kept apart from the request so the rules can be
// tested without a network: an empty list means the project has published nothing yet.
Release parseLatestRelease(const juce::String& json, const juce::String& currentVersion);

// Blocking GitHub request. Call it from a worker thread, never from the message thread.
Release checkForUpdate(const juce::String& currentVersion);

// The page a release check starts from.
juce::String releasesPageUrl();
}
