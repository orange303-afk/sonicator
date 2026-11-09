#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/smart/AcousticClassifierService.h"
#include "core/smart/SmartAnalysisService.h"
#include "TestHelpers.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>

using namespace sonicator;

namespace {

juce::AudioBuffer<float> makeSyntheticKick(double sampleRate = 44100.0)
{
    const int numSamples = static_cast<int>(sampleRate * 0.4);
    juce::AudioBuffer<float> buffer(1, numSamples);
    float phase = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        float freq = 50.0f + 110.0f * std::exp(-t * 30.0f);
        phase += 2.0f * juce::MathConstants<float>::pi * freq / static_cast<float>(sampleRate);
        float env = std::exp(-t * 12.0f);
        buffer.setSample(0, i, std::sin(phase) * env);
    }
    return buffer;
}

juce::AudioBuffer<float> makeSyntheticSnare(double sampleRate = 44100.0)
{
    const int numSamples = static_cast<int>(sampleRate * 0.3);
    juce::AudioBuffer<float> buffer(1, numSamples);
    juce::Random rng(12345);
    for (int i = 0; i < numSamples; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        float body = std::sin(2.0f * juce::MathConstants<float>::pi * 180.0f * t);
        float noise = rng.nextFloat() * 2.0f - 1.0f;
        float env = std::exp(-t * 15.0f);
        buffer.setSample(0, i, (0.4f * body + 0.6f * noise) * env);
    }
    return buffer;
}

void writeWavFile(const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(
        stream,
        juce::AudioFormatWriterOptions()
            .withSampleRate(sampleRate)
            .withNumChannels(buffer.getNumChannels())
            .withBitsPerSample(16)));
    if (writer)
        writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

SampleRecord recordFor(const juce::File& file)
{
    SampleRecord r;
    r.filePath = file.getFullPathName();
    r.fileName = file.getFileName();
    r.fileSize = file.getSize();
    r.dateModified = file.getLastModificationTime().toMilliseconds();
    r.fileHash = "h";
    r.version = basicFeaturesVersion;
    return r;
}

const SmartCandidate* findCandidate(const std::vector<SmartCandidate>& candidates,
                                    SmartField field,
                                    const juce::String& source)
{
    for (const auto& c : candidates)
    {
        if (c.field == field && c.source == source)
            return &c;
    }
    return nullptr;
}

} // namespace

TEST_CASE("AcousticClassifierService classifies synthetic kick buffer", "[smart][acoustic]")
{
    const double sampleRate = 44100.0;
    auto buffer = makeSyntheticKick(sampleRate);

    auto candidates = AcousticClassifierService::analyzeBuffer(buffer, sampleRate, juce::File());
    const auto* c = findCandidate(candidates, SmartField::Type, "dsp_acoustic");
    REQUIRE(c != nullptr);
    REQUIRE(c->field == SmartField::Type);
    REQUIRE(c->textValue == "Kick");
    REQUIRE(c->numericValue >= 0.85);
    REQUIRE(c->status == CandidateStatus::Valid);
    REQUIRE(c->detail.contains("Sub-dominant"));
}

TEST_CASE("AcousticClassifierService classifies synthetic snare buffer", "[smart][acoustic]")
{
    const double sampleRate = 44100.0;
    auto buffer = makeSyntheticSnare(sampleRate);

    auto candidates = AcousticClassifierService::analyzeBuffer(buffer, sampleRate, juce::File());
    const auto* c = findCandidate(candidates, SmartField::Type, "dsp_acoustic");
    REQUIRE(c != nullptr);
    REQUIRE(c->field == SmartField::Type);
    REQUIRE(c->textValue == "Snare");
    REQUIRE(c->numericValue >= 0.85);
    REQUIRE(c->status == CandidateStatus::Valid);
}

TEST_CASE("AcousticClassifierService extracts genre from path", "[smart][acoustic]")
{
    juce::AudioBuffer<float> emptyBuffer(1, 1024);
    emptyBuffer.clear();
    const juce::File file("/Volumes/Samples/Drum and Bass/break01.wav");

    auto candidates = AcousticClassifierService::analyzeBuffer(emptyBuffer, 44100.0, file);
    const auto* c = findCandidate(candidates, SmartField::Type, "path");
    REQUIRE(c != nullptr);
    REQUIRE(c->field == SmartField::Type);
    REQUIRE(c->textValue == "Drum & Bass");
    REQUIRE(c->numericValue >= 0.80);
    REQUIRE(c->status == CandidateStatus::Valid);
}

TEST_CASE("AcousticClassifierService::analyzeAudioAndContext handles missing/corrupt audio gracefully", "[smart][acoustic]")
{
    const juce::File file("/Volumes/Samples/Drum and Bass/nonexistent.wav");
    auto candidates = AcousticClassifierService::analyzeAudioAndContext(file, recordFor(file));

    const auto* c = findCandidate(candidates, SmartField::Type, "path");
    REQUIRE(c != nullptr);
    REQUIRE(c->textValue == "Drum & Bass");
}

TEST_CASE("SmartAnalysisService::collect integrates acoustic and genre candidates", "[smart][acoustic]")
{
    TestDirectory t;
    auto genreDir = t.dir.getChildFile("Drum and Bass");
    auto file = genreDir.getChildFile("audio_01.wav");

    auto kickBuffer = makeSyntheticKick(44100.0);
    writeWavFile(file, kickBuffer, 44100.0);

    auto collection = SmartAnalysisService::collect(recordFor(file));

    const auto* kickCandidate = findCandidate(collection.candidates, SmartField::Type, "dsp_acoustic");
    REQUIRE(kickCandidate != nullptr);
    REQUIRE(kickCandidate->textValue == "Kick");
    REQUIRE(kickCandidate->extractorId == "dsp");

    const auto* genreCandidate = findCandidate(collection.candidates, SmartField::Type, "path");
    REQUIRE(genreCandidate != nullptr);
    REQUIRE(genreCandidate->textValue == "Drum & Bass");
    REQUIRE(genreCandidate->extractorId == "name");
}
