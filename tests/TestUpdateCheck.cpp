#include <catch2/catch_test_macros.hpp>
#include "core/updates/UpdateCheck.h"

using namespace sonicator;

TEST_CASE("Release tags compare by their dotted parts", "[updates]")
{
    using updates::compareVersions;
    REQUIRE(compareVersions("0.1.0", "0.1.0") == 0);
    REQUIRE(compareVersions("v0.1.0", "0.1.0") == 0);
    REQUIRE(compareVersions("v0.2.0", "0.1.0") > 0);
    REQUIRE(compareVersions("0.1.0", "v0.2.0") < 0);
    REQUIRE(compareVersions("0.10.0", "0.9.9") > 0);
    REQUIRE(compareVersions("2.0", "1.9.9") > 0);
    // A pre-release of the same version is not an update: the digits decide, the suffix does not.
    REQUIRE(compareVersions("1.0.0-beta.2", "1.0.0") == 0);
}

TEST_CASE("The newest published release is read from the GitHub answer", "[updates]")
{
    using updates::parseLatestRelease;

    SECTION("a newer release")
    {
        const auto release = parseLatestRelease(
            R"([{"tag_name":"v0.2.0","html_url":"https://github.com/orange303-afk/sonicator/releases/tag/v0.2.0",
                 "body":"## What changed\n\n- The Cloud map follows the filter.\n"}])",
            "0.1.0");
        REQUIRE(release.error.isEmpty());
        REQUIRE(release.published);
        REQUIRE(release.newer);
        REQUIRE(release.tag == "v0.2.0");
        REQUIRE(release.url == "https://github.com/orange303-afk/sonicator/releases/tag/v0.2.0");
        CHECK(release.notes == "## What changed\n\n- The Cloud map follows the filter.");
    }

    SECTION("a release without notes keeps the update usable")
    {
        const auto release = parseLatestRelease(
            R"([{"tag_name":"v0.2.0","html_url":"https://example.test"}])", "0.1.0");
        REQUIRE(release.published);
        REQUIRE(release.newer);
        CHECK(release.notes.isEmpty());
    }

    SECTION("notes that are not a string are ignored")
    {
        const auto release = parseLatestRelease(
            R"([{"tag_name":"v0.2.0","html_url":"https://example.test","body":{"unexpected":true}}])",
            "0.1.0");
        REQUIRE(release.published);
        CHECK(release.notes.isEmpty());
    }

    SECTION("the running version")
    {
        const auto release = parseLatestRelease(R"([{"tag_name":"0.1.0","html_url":"https://example.test"}])",
                                                "0.1.0");
        REQUIRE(release.published);
        REQUIRE_FALSE(release.newer);
    }

    SECTION("nothing published yet is not an error")
    {
        const auto release = parseLatestRelease("[]", "0.1.0");
        REQUIRE(release.error.isEmpty());
        REQUIRE_FALSE(release.published);
        REQUIRE_FALSE(release.newer);
    }

    SECTION("a broken answer is reported")
    {
        REQUIRE(parseLatestRelease("{not json", "0.1.0").error.isNotEmpty());
        REQUIRE(parseLatestRelease(R"([{"html_url":"https://example.test"}])", "0.1.0").error.isNotEmpty());
    }
}

TEST_CASE("Each platform gets its own installer from the release", "[updates]")
{
    using updates::downloadUrlFor;
    using updates::Platform;
    const auto release = updates::parseLatestRelease(R"([{
        "tag_name":"v0.2.0",
        "html_url":"https://example.test/tag",
        "assets":[
            {"name":"SONICATOR-0.2.0-Windows-x64.msi","browser_download_url":"https://example.test/win.msi"},
            {"name":"SONICATOR-0.2.0-macOS-x86_64-10.15.pkg","browser_download_url":"https://example.test/mac-intel.pkg"},
            {"name":"SONICATOR-0.2.0-macOS-universal.pkg","browser_download_url":"https://example.test/mac-universal.pkg"},
            {"name":"SONICATOR-0.2.0-Linux-x86_64.AppImage","browser_download_url":"https://example.test/linux.AppImage"},
            {"name":"SONICATOR-0.2.0-Linux-amd64.deb","browser_download_url":"https://example.test/linux.deb"}
        ]}])", "0.1.1");
    REQUIRE(release.assets.size() == 5);
    REQUIRE(downloadUrlFor(release, Platform::macOS) == "https://example.test/mac-universal.pkg");
    REQUIRE(downloadUrlFor(release, Platform::windows) == "https://example.test/win.msi");
    REQUIRE(downloadUrlFor(release, Platform::linux) == "https://example.test/linux.AppImage");

    updates::Release noAssets;
    noAssets.url = "https://example.test/page";
    REQUIRE(downloadUrlFor(noAssets, Platform::macOS) == "https://example.test/page");
    REQUIRE(downloadUrlFor(noAssets, Platform::windows) == "https://example.test/page");
}

