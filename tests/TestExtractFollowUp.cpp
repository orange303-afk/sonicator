#include <catch2/catch_test_macros.hpp>
#include "core/extract/ExtractFollowUp.h"
using namespace sonicator;

TEST_CASE("Extracted folders inside the library are only scanned", "[extract][follow-up]")
{
    const auto plan = planExtractFollowUp({ "/lib/samples/disc samples" }, { "/lib" });
    REQUIRE(plan.newRoots.isEmpty());
    REQUIRE(plan.scanFolders == juce::StringArray({ "/lib/samples/disc samples" }));
}

TEST_CASE("Extracted folders outside the library become roots and are scanned", "[extract][follow-up]")
{
    const auto plan = planExtractFollowUp({ "/elsewhere/disc samples" }, { "/lib" });
    REQUIRE(plan.newRoots == juce::StringArray({ "/elsewhere/disc samples" }));
    REQUIRE(plan.scanFolders == juce::StringArray({ "/elsewhere/disc samples" }));
}

TEST_CASE("A root itself and duplicate or nested folders collapse", "[extract][follow-up]")
{
    juce::StringArray folders { "/lib", "/lib", "/elsewhere", "/elsewhere/disc samples", "/other/a", "/other/a/b" };
    const auto plan = planExtractFollowUp(folders, { "/lib" });
    // A listed root is still scanned when it was an output folder; nested folders collapse.
    REQUIRE(plan.scanFolders == juce::StringArray({ "/elsewhere", "/lib", "/other/a" }));
    REQUIRE(plan.newRoots == juce::StringArray({ "/elsewhere", "/other/a" }));
}
