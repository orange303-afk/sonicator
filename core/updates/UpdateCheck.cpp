#include "UpdateCheck.h"

namespace sonicator::updates
{
namespace
{
constexpr const char* api = "https://api.github.com/repos/orange303-afk/sonicator/releases";
constexpr const char* releasesPage = "https://github.com/orange303-afk/sonicator/releases";
constexpr const char* gumroad = "https://ilyaorange.gumroad.com";

// "v0.2.0" → { 0, 2, 0 }. A suffix such as "-beta.2" stops that part at its digits, so a stable
// release still compares above nothing while the build metadata cannot make it look newer.
std::vector<int> versionParts(const juce::String& text)
{
    auto value = text.trim();
    if (value.startsWithIgnoreCase("v")) value = value.substring(1);

    std::vector<int> parts;
    for (const auto& piece : juce::StringArray::fromTokens(value, ".", ""))
    {
        int number = 0;
        for (const auto character : piece)
        {
            if (! juce::CharacterFunctions::isDigit(character)) break;
            number = number * 10 + (character - '0');
        }
        parts.push_back(number);
        // "1.0.0-beta.2" is a pre-release of 1.0.0, not a version above it.
        if (! piece.containsOnly("0123456789")) break;
    }
    return parts;
}
} // namespace

int compareVersions(const juce::String& a, const juce::String& b)
{
    const auto left = versionParts(a);
    const auto right = versionParts(b);
    const auto count = juce::jmax(left.size(), right.size());
    for (size_t i = 0; i < count; ++i)
    {
        const int l = i < left.size() ? left[i] : 0;
        const int r = i < right.size() ? right[i] : 0;
        if (l != r) return l < r ? -1 : 1;
    }
    return 0;
}

Release parseLatestRelease(const juce::String& json, const juce::String& currentVersion)
{
    Release release;

    juce::var parsed;
    if (const auto result = juce::JSON::parse(json, parsed); result.failed())
    {
        release.error = "GitHub sent an answer the app could not read.";
        return release;
    }

    const auto* list = parsed.getArray();
    if (list == nullptr)
    {
        release.error = "GitHub sent an answer the app could not read.";
        return release;
    }
    if (list->isEmpty())
        return release;   // nothing published yet, which is not an error

    const auto* item = list->getFirst().getDynamicObject();
    if (item == nullptr)
    {
        release.error = "GitHub sent an answer the app could not read.";
        return release;
    }

    release.tag = item->getProperty("tag_name").toString().trim();
    release.url = item->getProperty("html_url").toString().trim();
    // The release body is the "what changed" text GitHub shows on the release page. It arrives in
    // the same answer as the tag, so the dialog needs no second request.
    const auto body = item->getProperty("body");
    if (body.isString()) release.notes = body.toString().trim();
    if (release.tag.isEmpty())
    {
        release.error = "The newest release has no version number.";
        return release;
    }
    if (release.url.isEmpty())
        release.url = releasesPage;

    if (const auto* assets = item->getProperty("assets").getArray())
        for (const auto& value : *assets)
            if (const auto* asset = value.getDynamicObject())
            {
                const auto name = asset->getProperty("name").toString().trim();
                const auto url = asset->getProperty("browser_download_url").toString().trim();
                if (name.isNotEmpty() && url.isNotEmpty())
                    release.assets.push_back({ name, url });
            }

    release.published = true;
    release.newer = compareVersions(release.tag, currentVersion) > 0;
    return release;
}

Release checkForUpdate(const juce::String& currentVersion)
{
    Release release;

    const juce::URL url(juce::String(api) + "?per_page=1");
    const auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
        .withConnectionTimeoutMs(8000)
        .withNumRedirectsToFollow(5)
        .withExtraHeaders("User-Agent: Sonicator/" + currentVersion + "\r\n"
                          "Accept: application/vnd.github+json\r\n");

    auto stream = url.createInputStream(options);
    if (stream == nullptr)
    {
        release.error = "Could not reach GitHub. Check the connection and try again.";
        return release;
    }

    if (auto* web = dynamic_cast<juce::WebInputStream*>(stream.get()); web != nullptr)
    {
        const int status = web->getStatusCode();
        if (status == 403 || status == 429)
        {
            release.error = "GitHub is limiting requests right now. Try again a little later.";
            return release;
        }
        if (status != 200)
        {
            release.error = "GitHub answered with status " + juce::String(status) + ".";
            return release;
        }
    }

    const auto body = stream->readEntireStreamAsString();
    if (body.isEmpty())
    {
        release.error = "GitHub returned an empty answer.";
        return release;
    }
    return parseLatestRelease(body, currentVersion);
}

juce::String downloadUrlFor(const Release& release, const Platform platform)
{
    juce::String best;
    for (const auto& asset : release.assets)
    {
        const auto& name = asset.name;
        if (platform == Platform::macOS && name.endsWithIgnoreCase(".pkg"))
        {
            // The universal package runs on both architectures; another slice is the fallback.
            if (best.isEmpty() || name.containsIgnoreCase("universal")) best = asset.url;
        }
        else if (platform == Platform::windows && name.endsWithIgnoreCase(".msi"))
        {
            return asset.url;
        }
        else if (platform == Platform::linux)
        {
            if (name.endsWithIgnoreCase(".AppImage")) return asset.url;
            if (name.endsWithIgnoreCase(".deb") && best.isEmpty()) best = asset.url;
        }
    }
    return best.isNotEmpty() ? best : release.url;
}

Platform currentPlatform()
{
#if JUCE_WINDOWS
    return Platform::windows;
#elif JUCE_LINUX || JUCE_BSD
    return Platform::linux;
#else
    return Platform::macOS;
#endif
}

juce::String releasesPageUrl()
{
    return releasesPage;
}

juce::String gumroadUrl()
{
    return gumroad;
}
} // namespace sonicator::updates
