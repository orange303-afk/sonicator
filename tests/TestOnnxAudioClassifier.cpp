#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/ml/MelSpectrogram.h"
#include "core/ml/OnnxAudioClassifier.h"
#include <vector>
#include <cstdint>
#include <cmath>
#include <thread>
#include <future>

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

// Builds a minimal valid ONNX model protobuf:
// Input: 'input' [1, numClasses]
// Output: 'output' [1, numClasses]
// Op: 'Identity'
std::vector<uint8_t> createMinimalOnnxModel(int numClasses = 11)
{
    // OperatorSetIdProto: domain = 1 (string), version = 2 (int64)
    std::vector<uint8_t> opset;
    appendStringField(opset, 1, "");
    appendVarintField(opset, 2, 17);

    // Tensor shape:
    // Dimension 0: 1
    std::vector<uint8_t> dim0;
    appendVarintField(dim0, 1, 1);
    // Dimension 1: numClasses
    std::vector<uint8_t> dim1;
    appendVarintField(dim1, 1, numClasses);

    std::vector<uint8_t> shape;
    appendMessageField(shape, 1, dim0);
    appendMessageField(shape, 1, dim1);

    // TensorTypeProto: elem_type = 1 (FLOAT, field 1), shape (field 2)
    std::vector<uint8_t> tensorType;
    appendVarintField(tensorType, 1, 1); // FLOAT
    appendMessageField(tensorType, 2, shape);

    // TypeProto: tensor_type = field 1
    std::vector<uint8_t> typeProto;
    appendMessageField(typeProto, 1, tensorType);

    // ValueInfoProto for 'input'
    std::vector<uint8_t> inputVal;
    appendStringField(inputVal, 1, "input");
    appendMessageField(inputVal, 2, typeProto);

    // ValueInfoProto for 'output'
    std::vector<uint8_t> outputVal;
    appendStringField(outputVal, 1, "output");
    appendMessageField(outputVal, 2, typeProto);

    // NodeProto:
    // input = "input" (field 1)
    // output = "output" (field 2)
    // name = "identity0" (field 3)
    // op_type = "Identity" (field 4)
    std::vector<uint8_t> node;
    appendStringField(node, 1, "input");
    appendStringField(node, 2, "output");
    appendStringField(node, 3, "identity0");
    appendStringField(node, 4, "Identity");

    // GraphProto:
    // node (field 1)
    // name (field 2)
    // input (field 11)
    // output (field 12)
    std::vector<uint8_t> graph;
    appendMessageField(graph, 1, node);
    appendStringField(graph, 2, "test_graph");
    appendMessageField(graph, 11, inputVal);
    appendMessageField(graph, 12, outputVal);

    // ModelProto:
    // ir_version: field 1 = 8
    // producer_name: field 2 = "sonicator"
    // graph: field 7
    // opset_import: field 8
    std::vector<uint8_t> model;
    appendVarintField(model, 1, 8);
    appendStringField(model, 2, "sonicator");
    appendMessageField(model, 7, graph);
    appendMessageField(model, 8, opset);

    return model;
}

juce::AudioBuffer<float> createSineBuffer(double freqHz, double sampleRate, int numSamples)
{
    juce::AudioBuffer<float> buf(1, numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        double phase = 2.0 * juce::MathConstants<double>::pi * freqHz * i / sampleRate;
        buf.setSample(0, i, static_cast<float>(std::sin(phase)));
    }
    return buf;
}

} // namespace

TEST_CASE("MelSpectrogram dimensions and calculations", "[ml]")
{
    SECTION("Frame count calculation")
    {
        REQUIRE(sonicator::MelSpectrogram::calculateNumFrames(0, 1024, 512) == 0);
        REQUIRE(sonicator::MelSpectrogram::calculateNumFrames(512, 1024, 512) == 0);
        REQUIRE(sonicator::MelSpectrogram::calculateNumFrames(1023, 1024, 512) == 0);
        REQUIRE(sonicator::MelSpectrogram::calculateNumFrames(1024, 1024, 512) == 1);
        REQUIRE(sonicator::MelSpectrogram::calculateNumFrames(1536, 1024, 512) == 2);
        REQUIRE(sonicator::MelSpectrogram::calculateNumFrames(2048, 1024, 512) == 3);
        REQUIRE(sonicator::MelSpectrogram::calculateNumFrames(4096, 1024, 512) == 7);
        REQUIRE(sonicator::MelSpectrogram::calculateNumFrames(4096, 0, 512) == 0);
        REQUIRE(sonicator::MelSpectrogram::calculateNumFrames(4096, 1024, 0) == 0);
    }

    SECTION("Empty or too short buffer")
    {
        juce::AudioBuffer<float> emptyBuf(1, 0);
        auto mel = sonicator::MelSpectrogram::computeLogMel(emptyBuf, 44100.0);
        REQUIRE(mel.empty());

        juce::AudioBuffer<float> shortBuf(1, 512);
        mel = sonicator::MelSpectrogram::computeLogMel(shortBuf, 44100.0);
        REQUIRE(mel.empty());

        juce::AudioBuffer<float> zeroChBuf(0, 1024);
        mel = sonicator::MelSpectrogram::computeLogMel(zeroChBuf, 44100.0);
        REQUIRE(mel.empty());
    }

    SECTION("Output size matches melBands x numFrames")
    {
        const int numSamples = 4096;
        const int melBands = 64;
        const int fftSize = 1024;
        const int hopSize = 512;
        int expectedFrames = sonicator::MelSpectrogram::calculateNumFrames(numSamples, fftSize, hopSize);
        REQUIRE(expectedFrames == 7);

        juce::AudioBuffer<float> buf(2, numSamples);
        buf.clear();

        auto mel = sonicator::MelSpectrogram::computeLogMel(buf, 44100.0, melBands, fftSize, hopSize);
        REQUIRE(mel.size() == static_cast<size_t>(melBands * expectedFrames));

        // Silent buffer produces zeroed mel values
        for (float val : mel)
            REQUIRE(val == 0.0f);
    }
}

TEST_CASE("MelSpectrogram frequency response", "[ml]")
{
    const double sampleRate = 44100.0;
    const int numSamples = 4096;
    const int melBands = 64;
    const int fftSize = 1024;
    const int hopSize = 512;
    int numFrames = sonicator::MelSpectrogram::calculateNumFrames(numSamples, fftSize, hopSize);

    auto lowBuf = createSineBuffer(100.0, sampleRate, numSamples);
    auto lowMel = sonicator::MelSpectrogram::computeLogMel(lowBuf, sampleRate, melBands, fftSize, hopSize);
    REQUIRE(lowMel.size() == static_cast<size_t>(melBands * numFrames));

    auto highBuf = createSineBuffer(8000.0, sampleRate, numSamples);
    auto highMel = sonicator::MelSpectrogram::computeLogMel(highBuf, sampleRate, melBands, fftSize, hopSize);
    REQUIRE(highMel.size() == static_cast<size_t>(melBands * numFrames));

    // Calculate total energy per mel band
    std::vector<float> lowBandEnergy(melBands, 0.0f);
    std::vector<float> highBandEnergy(melBands, 0.0f);

    for (int b = 0; b < melBands; ++b)
    {
        for (int f = 0; f < numFrames; ++f)
        {
            lowBandEnergy[b] += lowMel[b * numFrames + f];
            highBandEnergy[b] += highMel[b * numFrames + f];
        }
    }

    int lowPeakBand = static_cast<int>(std::distance(
        lowBandEnergy.begin(),
        std::max_element(lowBandEnergy.begin(), lowBandEnergy.end())));

    int highPeakBand = static_cast<int>(std::distance(
        highBandEnergy.begin(),
        std::max_element(highBandEnergy.begin(), highBandEnergy.end())));

    // 100 Hz must peak in a lower mel bin than 8000 Hz
    REQUIRE(lowPeakBand < highPeakBand);
    REQUIRE(lowPeakBand <= 15);
    REQUIRE(highPeakBand >= 40);

    // Check energy distribution: low frequency has far more energy in bottom half
    float lowBottomHalf = 0.0f;
    float lowTopHalf = 0.0f;
    for (int b = 0; b < melBands / 2; ++b)
        lowBottomHalf += lowBandEnergy[b];
    for (int b = melBands / 2; b < melBands; ++b)
        lowTopHalf += lowBandEnergy[b];
    REQUIRE(lowBottomHalf > lowTopHalf * 10.0f);

    // High frequency has far more energy in top half
    float highBottomHalf = 0.0f;
    float highTopHalf = 0.0f;
    for (int b = 0; b < melBands / 2; ++b)
        highBottomHalf += highBandEnergy[b];
    for (int b = melBands / 2; b < melBands; ++b)
        highTopHalf += highBandEnergy[b];
    REQUIRE(highTopHalf > highBottomHalf * 10.0f);
}

TEST_CASE("OnnxAudioClassifier lifecycle, error handling and classification", "[ml]")
{
    sonicator::OnnxAudioClassifier classifier;
    REQUIRE_FALSE(classifier.isLoaded());

    SECTION("Unloaded classifier returns empty predictions")
    {
        juce::AudioBuffer<float> buf = createSineBuffer(440.0, 44100.0, 4096);
        auto preds = classifier.classify(buf, 44100.0);
        REQUIRE(preds.empty());
    }

    SECTION("Corrupt data rejected")
    {
        std::vector<uint8_t> corrupt = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02 };
        REQUIRE_FALSE(classifier.loadModelFromMemory(corrupt.data(), corrupt.size()));
        REQUIRE_FALSE(classifier.isLoaded());
    }

    SECTION("Null or empty memory data rejected")
    {
        REQUIRE_FALSE(classifier.loadModelFromMemory(nullptr, 0));
        REQUIRE_FALSE(classifier.loadModelFromMemory(nullptr, 100));
        REQUIRE_FALSE(classifier.isLoaded());
    }

    SECTION("Non-existent file rejected")
    {
        juce::File nonexistent("/path/to/nonexistent/model.onnx");
        REQUIRE_FALSE(classifier.loadModel(nonexistent));
        REQUIRE_FALSE(classifier.isLoaded());
    }

    SECTION("Supported classes are canonical 11 musical categories")
    {
        auto classes = sonicator::OnnxAudioClassifier::supportedClasses();
        REQUIRE(classes.size() == 11);
        REQUIRE(classes[0] == "Guitar");
        REQUIRE(classes[1] == "Piano");
        REQUIRE(classes[2] == "Pad");
        REQUIRE(classes[3] == "Lead");
        REQUIRE(classes[4] == "Strings");
        REQUIRE(classes[5] == "Brass");
        REQUIRE(classes[6] == "Vocal");
        REQUIRE(classes[7] == "Bass");
        REQUIRE(classes[8] == "Kick");
        REQUIRE(classes[9] == "Snare");
        REQUIRE(classes[10] == "Hat");
    }

    SECTION("Valid minimal model loading from memory and classification")
    {
        auto modelBytes = createMinimalOnnxModel(11);
        REQUIRE(classifier.loadModelFromMemory(modelBytes.data(), modelBytes.size()));
        REQUIRE(classifier.isLoaded());

        juce::AudioBuffer<float> buf = createSineBuffer(440.0, 44100.0, 4096);

        // Top 3 predictions
        auto preds3 = classifier.classify(buf, 44100.0, 3);
        REQUIRE(preds3.size() == 3);
        REQUIRE(preds3[0].probability >= preds3[1].probability);
        REQUIRE(preds3[1].probability >= preds3[2].probability);
        REQUIRE(preds3[0].probability > 0.0f);
        REQUIRE_FALSE(preds3[0].className.isEmpty());

        // Top 1 prediction
        auto preds1 = classifier.classify(buf, 44100.0, 1);
        REQUIRE(preds1.size() == 1);
        REQUIRE(preds1[0].className == preds3[0].className);
        REQUIRE_THAT(preds1[0].probability, Catch::Matchers::WithinRel(preds3[0].probability, 1e-4f));

        // Invalid buffer or sample rate yields empty predictions
        juce::AudioBuffer<float> emptyBuf(1, 0);
        REQUIRE(classifier.classify(emptyBuf, 44100.0).empty());
        REQUIRE(classifier.classify(buf, 0.0).empty());
        REQUIRE(classifier.classify(buf, -1.0).empty());
        REQUIRE(classifier.classify(buf, 44100.0, 0).empty());
    }

    SECTION("Loading model from file")
    {
        auto modelBytes = createMinimalOnnxModel(11);
        juce::File tempModel = juce::File::createTempFile(".onnx");
        REQUIRE(tempModel.replaceWithData(modelBytes.data(), modelBytes.size()));

        REQUIRE(classifier.loadModel(tempModel));
        REQUIRE(classifier.isLoaded());

        juce::AudioBuffer<float> buf = createSineBuffer(1000.0, 44100.0, 4096);
        auto preds = classifier.classify(buf, 44100.0, 3);
        REQUIRE(preds.size() == 3);
        REQUIRE(preds[0].probability > 0.0f);

        tempModel.deleteFile();
    }

    SECTION("Thread safety for concurrent classify calls")
    {
        auto modelBytes = createMinimalOnnxModel(11);
        REQUIRE(classifier.loadModelFromMemory(modelBytes.data(), modelBytes.size()));

        juce::AudioBuffer<float> buf = createSineBuffer(440.0, 44100.0, 4096);

        std::vector<std::future<std::vector<sonicator::MlPrediction>>> futures;
        for (int i = 0; i < 8; ++i)
        {
            futures.push_back(std::async(std::launch::async, [&classifier, &buf]() {
                return classifier.classify(buf, 44100.0, 3);
            }));
        }

        for (auto& fut : futures)
        {
            auto res = fut.get();
            REQUIRE(res.size() == 3);
            REQUIRE(res[0].probability > 0.0f);
        }
    }
}
