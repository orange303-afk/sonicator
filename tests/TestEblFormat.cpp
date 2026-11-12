#include <catch2/catch_test_macros.hpp>
#include "core/audio/SonicatorFormats.h"
#include "core/dsp/AnalysisAudio.h"
#include "core/scanner/FileScanner.h"
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
