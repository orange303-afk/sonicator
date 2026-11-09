#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/smart/AcousticClassifierService.h"
#include "core/smart/SmartAnalysisService.h"
#include "TestHelpers.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <vector>
#include <cstdint>

using namespace sonicator;

namespace {

void encodeVarint(std::vector<uint8_t>& out, uint64_t val)
{
    while (val > 0x7F) {
        out.push_back(static_cast<uint8_t>((val & 0x7F) | 0x80));
        val >>= 7;
    }
    out.push_back(static_cast<uint8_t>(val & 0x7F));
}

void appendTag(std::vector<uint8_t>& out, int fieldNum, int wireType)
{
    encodeVarint(out, (static_cast<uint64_t>(fieldNum) << 3) | static_cast<uint64_t>(wireType));
}

void appendVarintField(std::vector<uint8_t>& out, int fieldNum, uint64_t val)
{
    appendTag(out, fieldNum, 0);
    encodeVarint(out, val);
}

void appendBytesField(std::vector<uint8_t>& out, int fieldNum, const void* data, size_t len)
{
    appendTag(out, fieldNum, 2);
    encodeVarint(out, len);
    const auto* bytes = static_cast<const uint8_t*>(data);
    out.insert(out.end(), bytes, bytes + len);
}

void appendStringField(std::vector<uint8_t>& out, int fieldNum, const std::string& str)
{
    appendBytesField(out, fieldNum, str.data(), str.size());
}

void appendMessageField(std::vector<uint8_t>& out, int fieldNum, const std::vector<uint8_t>& msg)
{
    appendBytesField(out, fieldNum, msg.data(), msg.size());
}

std::vector<uint8_t> createMinimalOnnxModel(int numClasses = 11)
{
    std::vector<uint8_t> opset;
    appendStringField(opset, 1, "");
    appendVarintField(opset, 2, 17);

    std::vector<uint8_t> dim0;
    appendVarintField(dim0, 1, 1);
    std::vector<uint8_t> dim1;
    appendVarintField(dim1, 1, numClasses);

    std::vector<uint8_t> shape;
    appendMessageField(shape, 1, dim0);
    appendMessageField(shape, 1, dim1);

    std::vector<uint8_t> tensorType;
    appendVarintField(tensorType, 1, 1); // FLOAT
    appendMessageField(tensorType, 2, shape);

    std::vector<uint8_t> typeProto;
    appendMessageField(typeProto, 1, tensorType);

    std::vector<uint8_t> inputVal;
    appendStringField(inputVal, 1, "input");
    appendMessageField(inputVal, 2, typeProto);

    std::vector<uint8_t> outputVal;
    appendStringField(outputVal, 1, "output");
    appendMessageField(outputVal, 2, typeProto);

    std::vector<uint8_t> node;
    appendStringField(node, 1, "input");
    appendStringField(node, 2, "output");
    appendStringField(node, 3, "identity0");
    appendStringField(node, 4, "Identity");

    std::vector<uint8_t> graph;
    appendMessageField(graph, 1, node);
    appendStringField(graph, 2, "test_graph");
    appendMessageField(graph, 11, inputVal);
    appendMessageField(graph, 12, outputVal);

    std::vector<uint8_t> model;
    appendVarintField(model, 1, 8);
    appendStringField(model, 2, "sonicator");
    appendMessageField(model, 7, graph);
    appendMessageField(model, 8, opset);

    return model;
}

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

juce::AudioBuffer<float> makeSlowRampTone(double freqHz = 150.0, double sampleRate = 44100.0)
{
    const int numSamples = static_cast<int>(sampleRate * 0.5);
    juce::AudioBuffer<float> buffer(1, numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        float envelope = static_cast<float>(i) / static_cast<float>(numSamples - 1);
        float carrier = std::sin(2.0f * juce::MathConstants<float>::pi * static_cast<float>(freqHz) * static_cast<float>(i) / static_cast<float>(sampleRate));
        buffer.setSample(0, i, envelope * carrier);
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

TEST_CASE("AcousticClassifierService falls back to ML classifier when fast rules are indeterminate", "[smart][acoustic][ml]")
{
    auto modelBytes = createMinimalOnnxModel(11);
    auto& classifier = AcousticClassifierService::getSharedClassifier();
    REQUIRE(classifier.loadModelFromMemory(modelBytes.data(), modelBytes.size()));
    REQUIRE(classifier.isLoaded());

    const double sampleRate = 44100.0;
    const int numSamples = 1024;
    juce::AudioBuffer<float> buffer(1, numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        float s1 = std::sin(2.0f * juce::MathConstants<float>::pi * 40.0f * i / static_cast<float>(sampleRate));
        float s2 = std::sin(2.0f * juce::MathConstants<float>::pi * 60.0f * i / static_cast<float>(sampleRate));
        buffer.setSample(0, i, 0.5f * (s1 + s2));
    }
    auto candidates = AcousticClassifierService::analyzeBuffer(buffer, sampleRate, juce::File());

    const auto* c = findCandidate(candidates, SmartField::Type, "ml_classifier");
    REQUIRE(c != nullptr);
    REQUIRE(c->field == SmartField::Type);
    REQUIRE(c->source == "ml_classifier");
    REQUIRE(c->numericValue >= 0.50);
    REQUIRE(c->status == CandidateStatus::Valid);
    REQUIRE(c->detail.startsWith("ml_prob="));
    REQUIRE(c->textValue.isNotEmpty());
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

TEST_CASE("SmartAnalysisService::collect extracts genre even on corrupted audio or skipped DSP", "[smart][acoustic]")
{
    TestDirectory t;
    auto genreDir = t.dir.getChildFile("Techno");
    auto file = genreDir.getChildFile("corrupt.wav");
    file.replaceWithText("not real audio content");

    auto collection = SmartAnalysisService::collect(recordFor(file));

    const auto* genreCandidate = findCandidate(collection.candidates, SmartField::Type, "path");
    REQUIRE(genreCandidate != nullptr);
    REQUIRE(genreCandidate->textValue == "Techno");
    REQUIRE(genreCandidate->extractorId == "name");
}
