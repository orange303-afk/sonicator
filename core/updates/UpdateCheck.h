#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace sonicator::updates
{
struct Asset
{
    juce::String name;
    juce::String url; // browser_download_url
};

// The newest published release on the project's GitHub page.
struct Release
{
    juce::String tag;         // as published, e.g. "v0.2.0"
    juce::String url;         // the release page
    juce::String notes;       // the release body: what changed in this version, Markdown
    std::vector<Asset> assets;
    bool published = false;   // a release exists to compare against
    bool newer = false;       // that release is above the running version
    juce::String error;       // non-empty when the check itself failed
};

enum class Platform { macOS, windows, linux };

// Compares dotted version numbers and ignores a leading "v". Returns -1, 0 or 1.
int compareVersions(const juce::String& a, const juce::String& b);

// Reads the answer of GitHub's releases endpoint. Kept apart from the request so the rules can be
// tested without a network: an empty list means the project has published nothing yet.
Release parseLatestRelease(const juce::String& json, const juce::String& currentVersion);

// Blocking GitHub request. Call it from a worker thread, never from the message thread.
Release checkForUpdate(const juce::String& currentVersion);

// The installer for this platform in the release: .pkg on macOS (universal first), .msi on
// Windows, AppImage before .deb on Linux. Falls back to the release page when none matches.
juce::String downloadUrlFor(const Release&, Platform);
Platform currentPlatform();

// The page a release check starts from.
juce::String releasesPageUrl();
// The author's store, offered as a second download source.
juce::String gumroadUrl();
} // namespace sonicator::updates
