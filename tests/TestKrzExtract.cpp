#include <catch2/catch_test_macros.hpp>
#include "core/extract/ExtractService.h"
#include "core/extract/EmbeddedSamples.h"
#include "core/export/AudioExporter.h"
#include "core/kurzweil/KrzFile.h"
#include "TestHelpers.h"
#include "KrzTestFile.h"
#include <algorithm>
using namespace sonicator;
using namespace krztest;

namespace {
std::vector<juce::File> wavsIn(const juce::File& folder)
{
    std::vector<juce::File> out;
    if (! folder.isDirectory()) return out;
    for (const auto& entry : juce::RangedDirectoryIterator(folder, false, "*.wav", juce::File::findFiles))
        out.push_back(entry.getFile());
    std::sort(out.begin(), out.end(), [] (const juce::File& a, const juce::File& b)
              { return a.getFileName() < b.getFileName(); });
    return out;
}
BankSpec extractBank()
{
    BankSpec spec;
    SampleSpec looped; looped.id = 200; looped.name = "Guitar E1";
    HeaderSpec header; header.rootKey = 40; header.looping = true;
    header.startWord = 0; header.loopWord = 2; header.endWord = 5;
    looped.headers = { header };
    SampleSpec oneShot; oneShot.id = 201; oneShot.name = "Clap";
    HeaderSpec flat; flat.rootKey = 60; flat.startWord = 6; flat.endWord = 9;
    oneShot.headers = { flat };
    spec.samples = { looped, oneShot };
    spec.pcm = { 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, -1000, -2000, -3000, -4000 };
    return spec;
}
} // namespace

TEST_CASE("KRZ Extract writes each sample with its root key and loop", "[krz][extract]")
{
    TestDirectory dir;
    auto file = writeBank(dir.dir, "bank.krz", extractBank());
    ExtractOptions options;
    options.sameFolder = true;
    ExtractService service;
    REQUIRE(service.start({ file }, options));
    REQUIRE(service.waitUntilFinished(20000));
    const auto results = service.results();
    REQUIRE(results.size() == 1);
    CHECK(results[0].state == ExtractFileState::extracted);
    REQUIRE(results[0].written == 2);
    auto wavs = wavsIn(results[0].folder);
    REQUIRE(wavs.size() == 2);
    CHECK(wavs[0].getFileName() == "001 Guitar E1.wav");
    CHECK(wavs[1].getFileName() == "002 Clap.wav");

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(wavs[0]));
    REQUIRE(reader != nullptr);
    CHECK(reader->lengthInSamples == 6);
    CHECK(reader->sampleRate == 25000);
    const auto& metadata = reader->metadataValues;
    CHECK(metadata.getValue("MidiUnityNote", {}) == "40");
    CHECK(metadata.getValue("NumSampleLoops", {}) == "1");
    CHECK(metadata.getValue("Loop0Start", {}) == "2");
    CHECK(metadata.getValue("Loop0End", {}) == "5");

    std::unique_ptr<juce::AudioFormatReader> flat(formats.createReaderFor(wavs[1]));
    REQUIRE(flat != nullptr);
    CHECK(flat->lengthInSamples == 4);
    CHECK(flat->metadataValues.getValue("Loop0Start", {}) == "");
    CHECK(flat->metadataValues.getValue("MidiUnityNote", {}) == "60");
}

TEST_CASE("KRZ Extract keeps a stereo sample object as one stereo WAV", "[krz][extract]")
{
    TestDirectory dir;
    BankSpec spec;
    SampleSpec stereo; stereo.id = 200; stereo.name = "Pad"; stereo.stereo = true;
    HeaderSpec left; left.rootKey = 60; left.startWord = 0; left.endWord = 3;
    HeaderSpec right = left; right.startWord = 4; right.endWord = 7;
    stereo.headers = { left, right };
    spec.samples = { stereo };
    spec.pcm = { 1000, 2000, 3000, 4000, -1000, -2000, -3000, -4000 };
    auto file = writeBank(dir.dir, "stereo.krz", spec);
    ExtractOptions options;
    ExtractService service;
    REQUIRE(service.start({ file }, options));
    REQUIRE(service.waitUntilFinished(20000));
    auto wavs = wavsIn(service.results()[0].folder);
    REQUIRE(wavs.size() == 1);
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(wavs[0]));
    REQUIRE(reader != nullptr);
    CHECK(reader->numChannels == 2);
    CHECK(reader->lengthInSamples == 4);
}

TEST_CASE("KRZ Extract reports a missing part", "[krz][extract]")
{
    TestDirectory dir;
    BankSpec spec;
    SampleSpec sample; sample.id = 200; sample.name = "Needs Part";
    HeaderSpec header; header.startWord = 0; header.endWord = 99;
    sample.headers = { header };
    spec.samples = { sample };
    spec.pcm = { 1, 2, 3, 4 };
    auto file = writeBank(dir.dir, "gone.kr1.krz", spec);
    ExtractOptions options;
    ExtractService service;
    REQUIRE(service.start({ file }, options));
    REQUIRE(service.waitUntilFinished(20000));
    const auto results = service.results();
    REQUIRE(results.size() == 1);
    CHECK(results[0].problems.joinIntoString("|").contains("gone.kr2.krz"));
}

TEST_CASE("KRZ banks are embedded sample sources, ordinary audio is not", "[krz][extract]")
{
    TestDirectory dir;
    BankSpec spec;
    SampleSpec sample; sample.id = 200;
    HeaderSpec header; header.startWord = 0; header.endWord = 1;
    sample.headers = { header };
    spec.samples = { sample };
    spec.pcm = { 1, 2 };
    auto bank = writeBank(dir.dir, "bank.krz", spec);
    auto wave = dir.wave("plain.wav", 440, 1, 100);
    CHECK(isEmbeddedSampleSource(bank));
    CHECK_FALSE(isEmbeddedSampleSource(wave));
    const auto sources = embeddedSources({ bank, wave });
    REQUIRE(sources.size() == 1);
    CHECK(sources[0] == bank);
}

TEST_CASE("A sample exported without sampler metadata has no smpl chunk", "[krz][extract][export]")
{
    TestDirectory dir;
    SampleAudio audio;
    audio.sampleRate = 44100;
    audio.channels = 1;
    audio.frames = 64;
    audio.left.assign(64, 0.5f);
    const auto destination = dir.dir.getChildFile("plain.wav");
    AudioExporter exporter;
    REQUIRE(exporter.exportSampleFile(audio, destination, false).success);
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(destination));
    REQUIRE(reader != nullptr);
    CHECK(reader->metadataValues.getValue("Loop0Start", {}) == "");
    CHECK(reader->metadataValues.getValue("MidiUnityNote", {}) == "");
}

TEST_CASE("KRZ Extract explains a bank that only references the factory ROM", "[krz][extract]")
{
    TestDirectory dir;
    BankSpec spec;
    SampleSpec rom; rom.id = 200; rom.name = "Rom Sample";
    HeaderSpec header; header.startWord = 0; header.endWord = 100;
    rom.headers = { header };
    spec.samples = { rom };
    auto file = writeBank(dir.dir, "rom.krz", spec);
    ExtractOptions options;
    ExtractService service;
    REQUIRE(service.start({ file }, options));
    REQUIRE(service.waitUntilFinished(20000));
    const auto results = service.results();
    REQUIRE(results.size() == 1);
    CHECK(results[0].written == 0);
    CHECK(results[0].problems.joinIntoString("|").containsIgnoreCase("factory ROM"));
}
