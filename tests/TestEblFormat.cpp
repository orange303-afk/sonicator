#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/audio/SonicatorFormats.h"
#include "core/dsp/AnalysisAudio.h"
#include "core/database/Database.h"
#include "core/indexing/IndexingService.h"
#include "core/scanner/FileScanner.h"
#include "core/smart/SmartAnalysisService.h"
#include "core/smart/SmartResolver.h"
#include "EblTestFile.h"
#include "TestHelpers.h"
#include <cmath>
#include <memory>

namespace {
void checkSample(float actual, int16_t expected)
{
    CHECK(std::abs(actual - (float) expected / 32768.0f) <= 1e-6f);
}

juce::File writeEbl(TestDirectory& t, const juce::String& name, const ebltest::EblOptions& o)
{
    auto file = t.dir.getChildFile(name);
    const auto bytes = ebltest::makeEbl(o);
    file.replaceWithData(bytes.data(), bytes.size());
    return file;
}
} // namespace

TEST_CASE("EBL opens through the shared format manager", "[ebl][format]")
{
    TestDirectory t;
    auto o = ebltest::stereoOptions();
    o.stereoFrontSkew = true;
    auto file = writeEbl(t, "stereo.ebl", o);

    juce::AudioFormatManager formats;
    sonicator::registerSonicatorFormats(formats);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    REQUIRE(reader != nullptr);
    CHECK(reader->sampleRate == 47360);
    CHECK(reader->numChannels == 2);
    CHECK(reader->lengthInSamples == 4);
    CHECK(reader->bitsPerSample == 16);

    juce::AudioBuffer<float> buffer(2, 4);
    buffer.clear();
    REQUIRE(reader->read(&buffer, 0, 4, 0, true, true));
    for (int i = 0; i < 4; ++i)
    {
        checkSample(buffer.getSample(0, i), o.left[(size_t) i]);
        checkSample(buffer.getSample(1, i), o.right[(size_t) i]);
    }
}

TEST_CASE("EBL is a scanned audio format and analysis reads it", "[ebl][format]")
{
    TestDirectory t;
    auto o = ebltest::monoOptions();
    o.loop = std::pair<uint32_t, uint32_t> { 2, 6 };
    auto file = writeEbl(t, "mono.ebl", o);

    CHECK(sonicator::FileScanner::isSupportedFormat(file));
    CHECK_FALSE(sonicator::FileScanner::isMetadataOnlyFormat(file));

    const auto audio = sonicator::readAnalysisAudio(file);
    REQUIRE(audio.ok());
    CHECK(audio.sampleRate == 47360);
    CHECK(audio.buffer.getNumChannels() == 1);
    CHECK(audio.buffer.getNumSamples() == 8);
    for (int i = 0; i < 8; ++i)
        checkSample(audio.buffer.getSample(0, i), o.left[(size_t) i]);
}

TEST_CASE("EBL garbage is refused by the shared manager", "[ebl][format]")
{
    TestDirectory t;
    auto file = t.dir.getChildFile("broken.ebl");
    REQUIRE(file.replaceWithText("this is not an ebl file"));
    juce::AudioFormatManager formats;
    sonicator::registerSonicatorFormats(formats);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    CHECK(reader == nullptr);
}

TEST_CASE("EBL catalog rows are named from the header", "[ebl][catalog]")
{
    TestDirectory t;
    auto folder = t.dir.getChildFile("library");
    folder.createDirectory();
    auto named = ebltest::monoOptions();
    named.name = "org nod C +36";
    named.loop = std::pair<uint32_t, uint32_t> { 2, 6 };
    writeEbl(t, "library/named.ebl", named);
    auto blank = ebltest::monoOptions();
    blank.name = "     ";
    writeEbl(t, "library/blank.ebl", blank);

    sonicator::Database db(t.dir.getChildFile("catalog.sqlite"));
    sonicator::SampleRepository repo(db);
    repo.createTables();
    sonicator::IndexingService index(repo);
    REQUIRE(index.start(juce::StringArray { folder.getFullPathName() }));
    REQUIRE(index.waitUntilFinished(10000));
    REQUIRE(index.progress().failed == 0);

    const auto namedRow = repo.findByPath(folder.getChildFile("named.ebl").getFullPathName());
    REQUIRE(namedRow.has_value());
    CHECK(namedRow->fileName == "org nod C +36");
    CHECK(namedRow->duration > 0.0);
    CHECK(namedRow->sampleRate == 47360);
    CHECK(namedRow->numChannels == 1);
    CHECK(namedRow->error.isEmpty());

    const auto blankRow = repo.findByPath(folder.getChildFile("blank.ebl").getFullPathName());
    REQUIRE(blankRow.has_value());
    CHECK(blankRow->fileName == "blank.ebl");
    CHECK(blankRow->error.isEmpty());

    sonicator::LibraryFilter filter;
    filter.text = "org nod";
    const auto page = repo.query(filter);
    CHECK(page.total == 1);
    REQUIRE(page.samples.size() == 1);
    CHECK(page.samples.front().fileName == "org nod C +36");
}

TEST_CASE("EBL reaches the smart DSP pass instead of being unsupported", "[smart][ebl]")
{
    TestDirectory t;
    auto o = ebltest::monoOptions();
    o.rate = 44100;
    const int frames = 4 * 44100;
    o.left.assign((size_t) frames, 0);
    const double interval = 60.0 / 120.0;
    for (double time = 0.05; time < 4.0; time += interval)
    {
        const int start = (int) std::lround(time * 44100.0);
        for (int i = 0; i < 1058 && start + i < frames; ++i)
        {
            const double x = (double) i / 44100.0;
            const auto value = 0.8 * std::exp(-x / 0.03)
                * std::sin(2.0 * juce::MathConstants<double>::pi * 1200.0 * x);
            o.left[(size_t) (start + i)] = (int16_t) std::lround(value * 32767.0);
        }
    }
    const auto file = t.dir.getChildFile("drum_pattern.ebl");
    const auto bytes = ebltest::makeEbl(o);
    REQUIRE(file.replaceWithData(bytes.data(), bytes.size()));

    sonicator::SampleRecord record;
    record.filePath = file.getFullPathName();
    record.fileName = file.getFileName();
    record.fileSize = file.getSize();
    record.dateModified = file.getLastModificationTime().toMilliseconds();
    record.fileHash = "ebl";
    const auto collection = sonicator::SmartAnalysisService::collect(record);
    bool dspSeen = false;
    for (const auto& extraction : collection.extractions)
        if (extraction.source == "dsp")
        {
            dspSeen = true;
            INFO(extraction.error);
            CHECK(extraction.status == "ok");
        }
    CHECK(dspSeen);
    const auto result = sonicator::resolveSmart(collection.candidates);
    CHECK(result.structureSource == "dsp");
    CHECK(result.structure == "loop");
    CHECK(result.hasBpm);
    CHECK(result.bpm == Catch::Approx(120.0).epsilon(0.02));
}
