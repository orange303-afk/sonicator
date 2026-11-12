#include <catch2/catch_test_macros.hpp>
#include "core/extract/ExtractCandidates.h"
#include "ui/MainComponent.h"
#include "TestHelpers.h"
using namespace sonicator;

namespace
{
juce::File touch(TestDirectory& t, const juce::String& name)
{
    auto file = t.dir.getChildFile(name);
    file.replaceWithText("x");
    return file;
}
SampleRecord recordFor(const juce::File& file)
{
    SampleRecord record;
    record.filePath = file.getFullPathName();
    record.fileName = file.getFileName();
    return record;
}
SampleRecord discEntryFor(const juce::File& image, const juce::String& name,
                          int64_t entry, int64_t partner = 0)
{
    SampleRecord record;
    record.filePath = image.getFullPathName() + "::/Volume/" + name;
    record.fileName = name;
    record.containerPath = image.getFullPathName();
    record.discEntry = juce::String(entry);
    if (partner != 0) record.discPartner = juce::String(partner);
    return record;
}
} // namespace

TEST_CASE("Disc extract sources are recognised by extension and existence", "[extract][candidates]")
{
    TestDirectory t;
    REQUIRE(isDiscExtractSource(touch(t, "a.cue")));
    REQUIRE(isDiscExtractSource(touch(t, "a.iso")));
    REQUIRE(isDiscExtractSource(touch(t, "a.bin")));
    REQUIRE_FALSE(isDiscExtractSource(touch(t, "a.wav")));
    REQUIRE_FALSE(isDiscExtractSource(t.dir));
    REQUIRE_FALSE(isDiscExtractSource(t.dir.getChildFile("missing.iso")));
}

TEST_CASE("Extract candidates group image rows and selected disc samples", "[extract][candidates]")
{
    TestDirectory t;
    const auto cue = touch(t, "one.cue");
    const auto iso = touch(t, "two.iso");
    const auto wave = touch(t, "plain.wav");
    const auto bank = touch(t, "bank.sf2");
    const auto ignoredWave = touch(t, "ignored.wav");

    std::vector<SampleRecord> selection;
    selection.push_back(recordFor(cue));                 // whole image
    selection.push_back(recordFor(iso));                 // whole image
    selection.push_back(discEntryFor(cue, "KICK", 11));  // selected sample
    selection.push_back(discEntryFor(cue, "PAIR", 12, 13)); // stereo pair
    selection.push_back(discEntryFor(iso, "BASS", 21));
    selection.push_back(recordFor(wave));                // not extractable
    selection.push_back(recordFor(bank));                // embedded source
    selection.push_back(recordFor(ignoredWave));
    selection.push_back(discEntryFor(cue, "BROKEN", 0)); // no id: skipped

    const auto ignoredPath = ignoredWave.getFullPathName();
    const auto candidates = collectExtractCandidates(selection, [&](const juce::String& path)
    {
        return path == ignoredPath;
    });

    REQUIRE(candidates.images.size() == 2);
    REQUIRE(candidates.embedded.size() == 1);
    REQUIRE(candidates.embedded.front() == bank);
    REQUIRE(candidates.entries.size() == 2);
    REQUIRE(candidates.entries[0].image == cue);
    REQUIRE(candidates.entries[0].entries.size() == 2);
    REQUIRE(candidates.entries[0].entries[0].id.value == 11);
    REQUIRE(candidates.entries[0].entries[1].id.value == 12);
    REQUIRE(candidates.entries[0].entries[1].partner.value == 13);
    REQUIRE(candidates.entries[1].image == iso);
    REQUIRE(candidates.entries[1].entries.size() == 1);
    REQUIRE(candidates.sourceCount() == 5);
    REQUIRE_FALSE(candidates.empty());
}

TEST_CASE("A selection without extractable rows yields no candidates", "[extract][candidates]")
{
    TestDirectory t;
    const auto wave = touch(t, "plain.wav");
    const auto missing = t.dir.getChildFile("gone.iso");
    std::vector<SampleRecord> selection { recordFor(wave), recordFor(missing) };
    REQUIRE(collectExtractCandidates(selection).empty());
}

TEST_CASE("The window accepts only disc images as file drops", "[extract][drop]")
{
    TestDirectory t;
    const auto cue = touch(t, "a.cue");
    const auto iso = touch(t, "b.iso");
    const auto wav = touch(t, "a.wav");
    REQUIRE(MainComponent::isDiscDropTarget({ cue.getFullPathName() }));
    REQUIRE(MainComponent::isDiscDropTarget({ wav.getFullPathName(), iso.getFullPathName() }));
    REQUIRE_FALSE(MainComponent::isDiscDropTarget({ wav.getFullPathName() }));
    REQUIRE_FALSE(MainComponent::isDiscDropTarget({ t.dir.getFullPathName() }));
    REQUIRE_FALSE(MainComponent::isDiscDropTarget({}));
}
