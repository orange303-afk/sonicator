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
            R"([{"tag_name":"v0.2.0","html_url":"https://github.com/orange303-afk/sonicator/releases/tag/v0.2.0"}])",
            "0.1.0");
        REQUIRE(release.error.isEmpty());
        REQUIRE(release.published);
        REQUIRE(release.newer);
        REQUIRE(release.tag == "v0.2.0");
        REQUIRE(release.url == "https://github.com/orange303-afk/sonicator/releases/tag/v0.2.0");
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

