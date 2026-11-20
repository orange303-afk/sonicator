#include <catch2/catch_test_macros.hpp>
#include "ui/library/DuplicateCleanupDialog.h"
#include "TestHelpers.h"
#include <vector>

using namespace sonicator;

namespace
{
juce::File writeBytes(const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    file.replaceWithText("duplicate bytes");
    return file;
}
SampleRepository::DuplicateGroup makeGroup(const juce::File& first, const juce::File& second)
{
    SampleRepository::DuplicateGroup group;
    group.hash = "same";
    group.size = first.getSize();
    group.members.push_back({ 1, first.getFullPathName(), first.getSize(), false });
    group.members.push_back({ 2, second.getFullPathName(), second.getSize(), false });
    return group;
}
} // namespace

TEST_CASE("Selecting a copy in the duplicate window asks for an audition", "[ui][duplicates]")
{
    TestDirectory t;
    const auto first = writeBytes(t.dir.getChildFile("a/kick.wav"));
    const auto second = writeBytes(t.dir.getChildFile("b/kick copy.wav"));
    std::vector<SampleRepository::DuplicateGroup> groups { makeGroup(first, second) };
    DuplicateCleanupDialog dialog(groups);

    juce::File auditioned;
    int stops = 0;
    dialog.onAudition = [&auditioned](const juce::File& file) { auditioned = file; };
    dialog.onStopAudition = [&stops] { ++stops; };

    dialog.selectRowForTest(1);
    REQUIRE(auditioned == second);
    REQUIRE(dialog.auditionedRow() == 1);

    // A second click on the sounding copy stops it.
    dialog.selectRowForTest(1);
    REQUIRE(stops == 1);
    REQUIRE(dialog.auditionedRow() == -1);

    dialog.selectRowForTest(0);
    REQUIRE(auditioned == first);
    REQUIRE(dialog.auditionedRow() == 0);
}

TEST_CASE("A missing copy is not auditioned", "[ui][duplicates]")
{
    TestDirectory t;
    const auto first = writeBytes(t.dir.getChildFile("a/snare.wav"));
    const auto missing = t.dir.getChildFile("b/snare copy.wav");
    auto group = makeGroup(first, missing);
    group.members[1].missing = true;
    std::vector<SampleRepository::DuplicateGroup> groups { group };
    DuplicateCleanupDialog dialog(groups);

    int calls = 0;
    dialog.onAudition = [&calls](const juce::File&) { ++calls; };
    dialog.selectRowForTest(1);
    REQUIRE(calls == 0);
    REQUIRE(dialog.auditionedRow() == -1);
}
