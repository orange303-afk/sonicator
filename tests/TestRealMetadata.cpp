#include <catch2/catch_test_macros.hpp>
#include "core/smart/AbletonAsd.h"
#include "core/smart/AiffMetadata.h"
#include "core/smart/Rex2Metadata.h"
#include "core/smart/WavMetadata.h"
#include "core/sf2/Sf2Bank.h"
#include <iostream>
#include <deque>
#include <map>
using namespace sonicator;
namespace {
std::vector<juce::File> sampleFiles(const juce::File& root, const juce::StringArray& extensions,
                                    int perExtension)
{
    std::vector<juce::File> found;
    std::map<juce::String, int> counts;
    int remaining = extensions.size() * perExtension;
    std::deque<juce::File> pending { root };
    while (! pending.empty() && remaining > 0)
    {
        const auto directory = pending.front();
        pending.pop_front();
        juce::Array<juce::File> children;
        directory.findChildFiles(children, juce::File::findFilesAndDirectories, false);
        for (const auto& child : children)
        {
            if (child.isDirectory()) { pending.push_back(child); continue; }
            if (child.getFileName().startsWith("._")) continue;
            const auto extension = child.getFileExtension().toLowerCase();
            if (! extensions.contains(extension)) continue;
            if (counts[extension] >= perExtension) continue;
            ++counts[extension];
            --remaining;
            found.push_back(child);
            if (remaining <= 0) break;
        }
    }
    return found;
}
bool validBpm(const std::vector<SmartCandidate>& candidates)
{
    for (const auto& c : candidates)
        if (c.field == SmartField::Bpm && c.status == CandidateStatus::Valid)
            if (! std::isfinite(c.numericValue) || c.numericValue < 20 || c.numericValue > 400) return false;
    return true;
}
}
// Opt-in. Reads a real library only; never writes beside it.
TEST_CASE("Real-library metadata extraction stays finite and bounded", "[.real-metadata]")
{
    const auto rootPath = juce::SystemStats::getEnvironmentVariable("SONICATOR_METADATA_ROOT", {});
    REQUIRE(rootPath.isNotEmpty());
    juce::File root(rootPath);
    REQUIRE(root.isDirectory());
    const auto files = sampleFiles(root, { ".wav", ".aif", ".aiff", ".rx2", ".asd", ".sf2" }, 120);
    REQUIRE(files.size() > 20);
    int wav = 0, aiff = 0, rx2 = 0, asd = 0, sf2 = 0, emptyBanks = 0, drumBanks = 0, presetCount = 0, errors = 0, bpm = 0;
    int playableSlices = 0;
    for (const auto& file : files)
    {
        const auto extension = file.getFileExtension().toLowerCase();
        std::vector<SmartCandidate> candidates;
        if (extension == ".wav")
        {
            auto result = extractWavMetadata(file);
            candidates = std::move(result.candidates);
            if (result.error.isNotEmpty()) ++errors;
            ++wav;
        }
        else if (extension == ".aif" || extension == ".aiff")
        {
            auto result = extractAiffMetadata(file);
            candidates = std::move(result.candidates);
            if (result.error.isNotEmpty()) ++errors;
            ++aiff;
        }
        else if (extension == ".rx2")
        {
            auto result = extractRex2Metadata(file);
            candidates = std::move(result.candidates);
            if (result.error.isNotEmpty()) ++errors;
            playableSlices += result.playableSlices;
            ++rx2;
        }
        else if (extension == ".asd")
        {
            auto result = extractAsdMetadata(file);
            candidates = std::move(result.candidates);
            ++asd;
        }
        else if (extension == ".sf2")
        {
            Sf2Bank bank; juce::String error;
            const bool ok = bank.load(file, error);
            if (! ok)
            {
                // The E-MU collection contains a few valid 370-byte stub banks with no PCM.
                INFO(file.getFullPathName() + " :: " + error);
                REQUIRE((error.contains("no samples") || error.contains("no playable presets")));
                ++emptyBanks;
                ++sf2;
                continue;
            }
            if (bank.firstDrumPreset() >= 0) ++drumBanks;
            presetCount += (int) bank.presets().size();
            ++sf2;
        }
        REQUIRE(validBpm(candidates));
        for (const auto& c : candidates)
            if (c.field == SmartField::Key && c.status == CandidateStatus::Valid)
                REQUIRE(c.textValue.isNotEmpty());
            else if (c.field == SmartField::RootNote && c.status == CandidateStatus::Valid)
                REQUIRE((c.numericValue >= 0 && c.numericValue <= 127));
            else if (c.field == SmartField::Bpm && c.status == CandidateStatus::Valid)
                ++bpm;
    }
    std::cout << "real metadata: wav=" << wav << " aiff=" << aiff << " rx2=" << rx2
              << " asd=" << asd << " sf2=" << sf2 << " empty_sf2=" << emptyBanks << " drum_banks=" << drumBanks
              << " sf2_presets=" << presetCount << " structural_errors=" << errors
              << " valid_bpm_candidates=" << bpm << " rex_playable_slices=" << playableSlices << std::endl;
    REQUIRE(wav > 0);
    REQUIRE(rx2 > 0);
    REQUIRE(sf2 > 0);
}
