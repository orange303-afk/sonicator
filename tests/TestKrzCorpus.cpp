#include <catch2/catch_test_macros.hpp>
#include "core/kurzweil/KrzFile.h"
#include "core/kurzweil/KrzInstrument.h"
#include "TestHelpers.h"
#include <map>

namespace {
juce::File environmentDirectory(const char* name)
{
    const auto value = juce::SystemStats::getEnvironmentVariable(name, {});
    return value.isEmpty() ? juce::File() : juce::File(value);
}

std::vector<juce::File> bankFiles(const juce::File& root)
{
    std::vector<juce::File> out;
    for (const auto& entry : juce::RangedDirectoryIterator(root, true, "*", juce::File::findFiles))
    {
        const auto file = entry.getFile();
        if (file.hasFileExtension("krz;k25;k26")) out.push_back(file);
    }
    return out;
}
} // namespace

TEST_CASE("Every Kurzweil bank in the local corpus opens and decodes", "[.krz-corpus]")
{
    const auto root = environmentDirectory("SONICATOR_KRZ_DIR");
    if (! root.isDirectory()) SKIP("SONICATOR_KRZ_DIR is not set to a directory");
    const auto files = bankFiles(root);
    REQUIRE(files.size() > 100);
    int banks = 0, withoutPcm = 0, sets = 0, samples = 0, rejected = 0, explained = 0;
    int defectiveRate = 0, playable = 0;
    juce::StringArray unplayable;
    juce::StringArray problems;
    std::map<int, int> rates;
    for (const auto& file : files)
    {
        if (sonicator::krz::continuationPartNumber(file) > 1) { ++sets; continue; }
        sonicator::krz::Bank bank;
        juce::String error;
        if (! sonicator::krz::readBank(file, bank, error))
        {
            if (error.containsIgnoreCase("SROM")) { ++withoutPcm; continue; }
            ++rejected;
            problems.add(file.getFileName() + ": " + error);
            continue;
        }
        ++banks;
        if (! bank.missingPart.isEmpty()) ++explained;
        if (! bank.hasPcm()) { ++withoutPcm; continue; }
        sonicator::FileInstrument instrument;
        juce::String loadError;
        if (sonicator::loadKrzInstrument(file, instrument, loadError)) ++playable;
        else if (loadError.startsWith("This bank uses the K2000 factory ROM samples"))
        {
            CHECK_FALSE(bank.hasPcm());   // the ROM message is only for banks without sample data
            ++explained;
        }
        else if (loadError.startsWith("The bank's samples cannot be decoded")) ++defectiveRate;
        else unplayable.add(file.getFileName() + ": " + loadError);
        for (const auto& sample : bank.samples)
        {
            for (int header = 0; header < (int) sample.headers.size(); ++header)
            {
                if (! sample.headers[(size_t) header].ramBased) continue;
                sonicator::krz::DecodedSample decoded;
                juce::String decodeError;
                if (! sonicator::krz::decodeHeader(bank, sample, header, decoded, decodeError))
                {
                    if (bank.missingPart.isNotEmpty()) ++explained;
                    else if (decodeError.containsIgnoreCase("no usable rate")) ++defectiveRate;
                    else problems.add(file.getFileName() + "#" + juce::String(sample.id) + "."
                                      + juce::String(header) + ": " + decodeError);
                    continue;
                }
                const int rate = decoded.audio->sampleRate;
                if (rate < 1000 || rate > 192000)
                    problems.add(file.getFileName() + ": rate " + juce::String(rate) + " is implausible");
                ++rates[rate];
                ++samples;
            }
        }
    }
    INFO("files " << files.size() << ", banks " << banks << ", without PCM " << withoutPcm
                  << ", sets " << sets << ", samples " << samples << ", rejected " << rejected
                  << ", explained " << explained);
    INFO(problems.joinIntoString("\n"));
    INFO("playable banks " << playable << "; unplayable: " << unplayable.joinIntoString("\n"));
    CHECK(rejected == 0);
    CHECK(problems.isEmpty());
    CHECK(unplayable.isEmpty());
    CHECK(playable > 150);
}

TEST_CASE("Official Kurzweil archives open or explain why not", "[.krz-factory]")
{
    const auto root = environmentDirectory("SONICATOR_KRZ_FACTORY_DIR");
    if (! root.isDirectory()) SKIP("SONICATOR_KRZ_FACTORY_DIR is not set to a directory");
    const auto files = bankFiles(root);
    REQUIRE(files.size() > 50);
    int banks = 0, romOnly = 0, playable = 0, srom = 0, rejected = 0;
    juce::StringArray problems;
    for (const auto& file : files)
    {
        if (sonicator::krz::continuationPartNumber(file) > 1) continue;
        sonicator::krz::Bank bank;
        juce::String error;
        if (! sonicator::krz::readBank(file, bank, error))
        {
            if (error.containsIgnoreCase("SROM")) { ++srom; continue; }
            ++rejected;
            problems.add(file.getFileName() + ": " + error);
            continue;
        }
        ++banks;
        sonicator::FileInstrument instrument;
        if (sonicator::loadKrzInstrument(file, instrument, error)) { ++playable; continue; }
        if (error.startsWith("This bank uses the K2000 factory ROM samples")) { ++romOnly; continue; }
        problems.add(file.getFileName() + ": " + error);
    }
    INFO("files " << files.size() << ", banks " << banks << ", playable " << playable
                  << ", ROM only " << romOnly << ", SROM " << srom << ", rejected " << rejected);
    INFO(problems.joinIntoString("\n"));
    CHECK(rejected == 0);
    CHECK(problems.isEmpty());
    CHECK(banks > 100);
}
