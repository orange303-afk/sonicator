#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/dsp/AcousticFeatures.h"
#include "core/dsp/FastAcousticRules.h"
#include "core/smart/AcousticClassifierService.h"
#include "core/smart/SmartAnalysisService.h"
#include "core/smart/GenreContextExtractor.h"
#include "core/smart/SmartResolver.h"
#include "core/tagging/AutoTagRules.h"
#include "TestHelpers.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <vector>

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

std::vector<uint8_t> createBiasedOnnxModel(int targetClass, float biasAmount = 20.0f, int numClasses = 11)
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

    // Initializer for bias
    std::vector<float> biasVals(static_cast<size_t>(numClasses), 0.0f);
    if (targetClass >= 0 && targetClass < numClasses)
        biasVals[static_cast<size_t>(targetClass)] = biasAmount;

    std::vector<uint8_t> biasTensor;
    appendVarintField(biasTensor, 1, 1);
    appendVarintField(biasTensor, 1, numClasses);
    appendVarintField(biasTensor, 2, 1); // FLOAT
    appendBytesField(biasTensor, 9, biasVals.data(), biasVals.size() * sizeof(float));
    appendStringField(biasTensor, 8, "bias");

    std::vector<uint8_t> node;
    appendStringField(node, 1, "input");
    appendStringField(node, 1, "bias");
    appendStringField(node, 2, "output");
    appendStringField(node, 3, "add0");
    appendStringField(node, 4, "Add");

    std::vector<uint8_t> graph;
    appendMessageField(graph, 1, node);
    appendStringField(graph, 2, "test_graph");
    appendMessageField(graph, 5, biasTensor);
    appendMessageField(graph, 11, inputVal);
    appendMessageField(graph, 12, outputVal);

    std::vector<uint8_t> model;
    appendVarintField(model, 1, 8);
    appendStringField(model, 2, "sonicator");
    appendMessageField(model, 7, graph);
    appendMessageField(model, 8, opset);

    return model;
}

struct BiquadFilter {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    static BiquadFilter makeHighPass(float cutoffHz, float sampleRate, float Q = 0.7071f)
    {
        BiquadFilter f;
        float w0 = 2.0f * juce::MathConstants<float>::pi * cutoffHz / sampleRate;
        float cosw0 = std::cos(w0);
        float alpha = std::sin(w0) / (2.0f * Q);
        float a0 = 1.0f + alpha;
        f.b0 = ((1.0f + cosw0) / 2.0f) / a0;
        f.b1 = (-(1.0f + cosw0)) / a0;
        f.b2 = ((1.0f + cosw0) / 2.0f) / a0;
        f.a1 = (-2.0f * cosw0) / a0;
        f.a2 = (1.0f - alpha) / a0;
        return f;
    }

    float process(float in)
    {
        float out = b0 * in + z1;
        z1 = b1 * in - a1 * out + z2;
        z2 = b2 * in - a2 * out;
        return out;
    }
};

juce::AudioBuffer<float> makeKickVariation(int k, double sampleRate = 44100.0)
{
    float duration = 0.40f + static_cast<float>(k) * 0.015f;
    const int numSamples = static_cast<int>(sampleRate * duration);
    juce::AudioBuffer<float> buffer(1, numSamples);

    float startFreq = 110.0f + static_cast<float>(k) * 4.0f; // 110 - 146 Hz
    float endFreq = 42.0f + static_cast<float>(k % 4) * 2.5f; // 42 - 49.5 Hz
    float sweepRate = 28.0f + static_cast<float>(k) * 1.2f;
    float envDecay = 11.0f + static_cast<float>(k) * 0.6f;
    float clickAmp = 0.4f - static_cast<float>(k) * 0.015f;

    float phase = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        float freq = endFreq + (startFreq - endFreq) * std::exp(-t * sweepRate);
        phase += 2.0f * juce::MathConstants<float>::pi * freq / static_cast<float>(sampleRate);
        float env = std::exp(-t * envDecay);
        float sineVal = std::sin(phase);

        // Click transient in the first 2-3 ms
        float click = (i < 120) ? (clickAmp * std::exp(-t * 800.0f) * std::cos(2.0f * juce::MathConstants<float>::pi * 1200.0f * t)) : 0.0f;

        buffer.setSample(0, i, sineVal * env + click);
    }
    return buffer;
}

juce::AudioBuffer<float> makeSnareVariation(int k, double sampleRate = 44100.0)
{
    float duration = 0.28f + static_cast<float>(k) * 0.01f;
    const int numSamples = static_cast<int>(sampleRate * duration);
    juce::AudioBuffer<float> buffer(1, numSamples);

    float toneFreq = 180.0f + static_cast<float>(k) * 5.0f; // 180 - 225 Hz (~200Hz)
    float bodyMix = 0.38f + static_cast<float>(k % 3) * 0.02f;
    float noiseMix = 1.0f - bodyMix;
    float decayRate = 13.0f + static_cast<float>(k) * 0.7f;

    juce::Random rng(12345 + k * 101);
    for (int i = 0; i < numSamples; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        float body = std::sin(2.0f * juce::MathConstants<float>::pi * toneFreq * t);
        float noise = rng.nextFloat() * 2.0f - 1.0f;
        float env = std::exp(-t * decayRate);
        buffer.setSample(0, i, (bodyMix * body + noiseMix * noise) * env);
    }
    return buffer;
}

juce::AudioBuffer<float> makeHatVariation(int k, double sampleRate = 44100.0)
{
    float duration = 0.08f + static_cast<float>(k) * 0.01f; // 80ms - 170ms (< 200ms)
    const int numSamples = static_cast<int>(sampleRate * duration);
    juce::AudioBuffer<float> buffer(1, numSamples);

    float cutoff = 8200.0f + static_cast<float>(k) * 200.0f; // 8.2kHz - 10.0kHz
    auto hp1 = BiquadFilter::makeHighPass(cutoff, static_cast<float>(sampleRate));
    auto hp2 = BiquadFilter::makeHighPass(cutoff, static_cast<float>(sampleRate));
    float decayRate = 60.0f + static_cast<float>(k) * 6.0f;

    juce::Random rng(54321 + k * 73);
    for (int i = 0; i < numSamples; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        float rawNoise = rng.nextFloat() * 2.0f - 1.0f;
        float filtered = hp2.process(hp1.process(rawNoise));
        float env = std::exp(-t * decayRate);
        buffer.setSample(0, i, filtered * env);
    }
    return buffer;
}

juce::AudioBuffer<float> makeCymbalVariation(int k, double sampleRate = 44100.0)
{
    float duration = 1.6f + static_cast<float>(k) * 0.07f; // 1.6s - 2.23s (> 1.5s)
    const int numSamples = static_cast<int>(sampleRate * duration);
    juce::AudioBuffer<float> buffer(1, numSamples);

    float cutoff = 7200.0f + static_cast<float>(k) * 150.0f; // 7.2kHz - 8.55kHz
    auto hp1 = BiquadFilter::makeHighPass(cutoff, static_cast<float>(sampleRate));
    auto hp2 = BiquadFilter::makeHighPass(cutoff, static_cast<float>(sampleRate));
    float decayRate = 1.8f + static_cast<float>(k) * 0.15f;

    float p1 = 7600.0f + static_cast<float>(k) * 60.0f;
    float p2 = 9100.0f + static_cast<float>(k) * 80.0f;
    float p3 = 11400.0f + static_cast<float>(k) * 100.0f;

    juce::Random rng(98765 + k * 137);
    for (int i = 0; i < numSamples; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        float rawNoise = rng.nextFloat() * 2.0f - 1.0f;
        float filteredNoise = hp2.process(hp1.process(rawNoise));

        float partials = 0.2f * std::sin(2.0f * juce::MathConstants<float>::pi * p1 * t)
                       + 0.15f * std::sin(2.0f * juce::MathConstants<float>::pi * p2 * t)
                       + 0.1f * std::sin(2.0f * juce::MathConstants<float>::pi * p3 * t);

        float env = std::exp(-t * decayRate);
        buffer.setSample(0, i, (0.7f * filteredNoise + partials) * env);
    }
    return buffer;
}

juce::AudioBuffer<float> makeSubBassVariation(int k, double sampleRate = 44100.0)
{
    float duration = 1.2f + static_cast<float>(k) * 0.05f; // 1.2s - 1.65s (> 0.8s)
    const int numSamples = static_cast<int>(sampleRate * duration);
    juce::AudioBuffer<float> buffer(1, numSamples);

    float freq = 40.0f + static_cast<float>(k) * 2.1f; // 40.0Hz - 58.9Hz (pure 40-60Hz sine tone)
    float attackTime = 0.02f + static_cast<float>(k) * 0.002f; // ~20-38ms
    float sustainDecay = 0.5f + static_cast<float>(k) * 0.05f;

    for (int i = 0; i < numSamples; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        float env = (t < attackTime) ? (t / attackTime) : std::exp(-(t - attackTime) * sustainDecay);
        float val = std::sin(2.0f * juce::MathConstants<float>::pi * freq * t) * env;
        buffer.setSample(0, i, val);
    }
    return buffer;
}

juce::AudioBuffer<float> makeGuitarVariation(int k, double sampleRate = 44100.0)
{
    float duration = 1.0f + static_cast<float>(k) * 0.05f;
    const int numSamples = static_cast<int>(sampleRate * duration);
    juce::AudioBuffer<float> buffer(1, numSamples);

    float fundamental = 110.0f + static_cast<float>(k) * 22.0f; // 110 - 308 Hz
    float attackTime = 0.015f + static_cast<float>(k) * 0.0015f; // 15 - 28.5 ms
    float baseDecay = 3.5f + static_cast<float>(k) * 0.2f;

    for (int i = 0; i < numSamples; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        float attackEnv = (t < attackTime)
            ? (0.5f * (1.0f - std::cos(juce::MathConstants<float>::pi * t / attackTime)))
            : 1.0f;

        float h1 = std::sin(2.0f * juce::MathConstants<float>::pi * fundamental * t) * std::exp(-t * baseDecay);
        float h2 = 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * fundamental * 2.0f * t) * std::exp(-t * baseDecay * 1.5f);
        float h3 = 0.25f * std::sin(2.0f * juce::MathConstants<float>::pi * fundamental * 3.0f * t) * std::exp(-t * baseDecay * 2.2f);
        float h4 = 0.12f * std::sin(2.0f * juce::MathConstants<float>::pi * fundamental * 4.0f * t) * std::exp(-t * baseDecay * 3.0f);

        buffer.setSample(0, i, (h1 + h2 + h3 + h4) * attackEnv);
    }
    return buffer;
}

juce::AudioBuffer<float> makePadVariation(int k, double sampleRate = 44100.0)
{
    float duration = 1.8f + static_cast<float>(k) * 0.07f; // 1.8s - 2.43s
    const int numSamples = static_cast<int>(sampleRate * duration);
    juce::AudioBuffer<float> buffer(1, numSamples);

    float attackTime = 0.35f + static_cast<float>(k) * 0.03f; // 350ms - 620ms (> 300ms)
    float baseFreq = 220.0f + static_cast<float>(k) * 12.0f; // 220Hz - 328Hz
    float f1 = baseFreq;
    float f2 = baseFreq * 1.25f;
    float f3 = baseFreq * 1.50f;

    for (int i = 0; i < numSamples; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        float env = (t < attackTime)
            ? (0.5f * (1.0f - std::cos(juce::MathConstants<float>::pi * t / attackTime)))
            : (1.0f - 0.15f * ((t - attackTime) / (duration - attackTime)));

        float signal = 0.45f * std::sin(2.0f * juce::MathConstants<float>::pi * f1 * t)
                     + 0.35f * std::sin(2.0f * juce::MathConstants<float>::pi * f2 * t)
                     + 0.20f * std::sin(2.0f * juce::MathConstants<float>::pi * f3 * t);

        buffer.setSample(0, i, signal * env);
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

TEST_CASE("Acoustic classification accuracy across 50 realistic instrument variations", "[acceptance][acoustic][accuracy]")
{
    const double sampleRate = 44100.0;
    int correctCount = 0;
    const int totalPerClass = 10;

    // 1. Kicks: 10 variations
    for (int k = 0; k < totalPerClass; ++k)
    {
        auto buffer = makeKickVariation(k, sampleRate);
        const double duration = static_cast<double>(buffer.getNumSamples()) / sampleRate;
        auto desc = AcousticFeatures::extract(buffer, sampleRate);
        auto decision = FastAcousticRules::evaluate(desc, duration);

        if (decision.determined && decision.canonicalType == "Kick")
            correctCount++;

        auto candidates = AcousticClassifierService::analyzeBuffer(buffer, sampleRate, juce::File());
        const auto* c = findCandidate(candidates, SmartField::Type, "dsp_acoustic");
        REQUIRE(c != nullptr);
        REQUIRE(c->textValue == "Kick");
    }

    // 2. Snares: 10 variations
    for (int k = 0; k < totalPerClass; ++k)
    {
        auto buffer = makeSnareVariation(k, sampleRate);
        const double duration = static_cast<double>(buffer.getNumSamples()) / sampleRate;
        auto desc = AcousticFeatures::extract(buffer, sampleRate);
        auto decision = FastAcousticRules::evaluate(desc, duration);

        if (decision.determined && decision.canonicalType == "Snare")
            correctCount++;

        auto candidates = AcousticClassifierService::analyzeBuffer(buffer, sampleRate, juce::File());
        const auto* c = findCandidate(candidates, SmartField::Type, "dsp_acoustic");
        REQUIRE(c != nullptr);
        REQUIRE(c->textValue == "Snare");
    }

    // 3. Hi-Hats: 10 variations
    for (int k = 0; k < totalPerClass; ++k)
    {
        auto buffer = makeHatVariation(k, sampleRate);
        const double duration = static_cast<double>(buffer.getNumSamples()) / sampleRate;
        auto desc = AcousticFeatures::extract(buffer, sampleRate);
        auto decision = FastAcousticRules::evaluate(desc, duration);

        if (decision.determined && decision.canonicalType == "Hat")
            correctCount++;

        auto candidates = AcousticClassifierService::analyzeBuffer(buffer, sampleRate, juce::File());
        const auto* c = findCandidate(candidates, SmartField::Type, "dsp_acoustic");
        REQUIRE(c != nullptr);
        REQUIRE(c->textValue == "Hat");
    }

    // 4. Cymbals: 10 variations
    for (int k = 0; k < totalPerClass; ++k)
    {
        auto buffer = makeCymbalVariation(k, sampleRate);
        const double duration = static_cast<double>(buffer.getNumSamples()) / sampleRate;
        auto desc = AcousticFeatures::extract(buffer, sampleRate);
        auto decision = FastAcousticRules::evaluate(desc, duration);

        if (decision.determined && decision.canonicalType == "Cymbal")
            correctCount++;

        auto candidates = AcousticClassifierService::analyzeBuffer(buffer, sampleRate, juce::File());
        const auto* c = findCandidate(candidates, SmartField::Type, "dsp_acoustic");
        REQUIRE(c != nullptr);
        REQUIRE(c->textValue == "Cymbal");
    }

    // 5. Sub basses: 10 variations
    for (int k = 0; k < totalPerClass; ++k)
    {
        auto buffer = makeSubBassVariation(k, sampleRate);
        const double duration = static_cast<double>(buffer.getNumSamples()) / sampleRate;
        auto desc = AcousticFeatures::extract(buffer, sampleRate);
        auto decision = FastAcousticRules::evaluate(desc, duration);

        if (decision.determined && decision.canonicalType == "Sub")
            correctCount++;

        auto candidates = AcousticClassifierService::analyzeBuffer(buffer, sampleRate, juce::File());
        const auto* c = findCandidate(candidates, SmartField::Type, "dsp_acoustic");
        REQUIRE(c != nullptr);
        REQUIRE(c->textValue == "Sub");
    }

    // Verification: >= 90% accuracy (at least 45 out of 50 correct)
    REQUIRE(correctCount >= 45);

    // 6. Ambient Pads / Textures: 10 variations
    // Verify that Pads/Textures are correctly recognized as not drums (determined == false or pad candidate)
    for (int k = 0; k < totalPerClass; ++k)
    {
        auto buffer = makePadVariation(k, sampleRate);
        const double duration = static_cast<double>(buffer.getNumSamples()) / sampleRate;
        auto desc = AcousticFeatures::extract(buffer, sampleRate);
        auto decision = FastAcousticRules::evaluate(desc, duration);

        // Slow attack > 300ms and crest factor < 3.0 must ensure it is not recognized as drums
        REQUIRE((! decision.determined || decision.canonicalType == "Pad"));

        auto candidates = AcousticClassifierService::analyzeBuffer(buffer, sampleRate, juce::File());
        for (const auto& c : candidates)
        {
            if (c.field == SmartField::Type && c.source == "dsp_acoustic")
            {
                REQUIRE(c.textValue != "Kick");
                REQUIRE(c.textValue != "Snare");
                REQUIRE(c.textValue != "Hat");
                REQUIRE(c.textValue != "Cymbal");
                REQUIRE(c.textValue != "Sub");
            }
        }
    }
}

TEST_CASE("Acoustic classification benchmark speed is under 3.0 ms per buffer", "[.][benchmark][acceptance]")
{
    const double sampleRate = 44100.0;
    std::vector<juce::AudioBuffer<float>> buffers;
    buffers.reserve(100);

    for (int i = 0; i < 100; ++i)
    {
        switch (i % 3)
        {
            case 0: buffers.push_back(makeKickVariation(i % 10, sampleRate)); break;
            case 1: buffers.push_back(makeSnareVariation(i % 10, sampleRate)); break;
            default: buffers.push_back(makeHatVariation(i % 10, sampleRate)); break;
        }
    }

    // Warm up one iteration
    (void) AcousticClassifierService::analyzeBuffer(buffers[0], sampleRate, juce::File());

    const double startMs = juce::Time::getMillisecondCounterHiRes();

    for (int i = 0; i < 100; ++i)
    {
        auto candidates = AcousticClassifierService::analyzeBuffer(buffers[static_cast<size_t>(i)], sampleRate, juce::File());
        (void) candidates.size();
    }

    const double endMs = juce::Time::getMillisecondCounterHiRes();
    const double totalElapsedMs = endMs - startMs;
    const double avgMs = totalElapsedMs / 100.0;

    // JUCE_DEBUG is only defined in debug configurations, so this must stay preprocessor.
#if JUCE_DEBUG
    const double limitMs = 8.0;
#else
    const double limitMs = 3.0;
#endif
    REQUIRE(avgMs < limitMs);
}

TEST_CASE("Guitar variations and ML fallback predictions for non-drums", "[acceptance][acoustic][ml]")
{
    const double sampleRate = 44100.0;
    const int totalVariations = 10;

    // 1. Without model: verify Guitar variations are recognized as non-drums
    for (int k = 0; k < totalVariations; ++k)
    {
        auto buffer = makeGuitarVariation(k, sampleRate);
        const double duration = static_cast<double>(buffer.getNumSamples()) / sampleRate;
        auto desc = AcousticFeatures::extract(buffer, sampleRate);
        auto decision = FastAcousticRules::evaluate(desc, duration);

        REQUIRE_FALSE(decision.determined);
        REQUIRE(decision.canonicalType.isEmpty());
    }

    // 2. With Guitar model loaded into getSharedClassifier(): produces positive ML prediction "Guitar"
    {
        auto guitarModel = createBiasedOnnxModel(0); // Class 0 = "Guitar"
        auto& classifier = AcousticClassifierService::getSharedClassifier();
        REQUIRE(classifier.loadModelFromMemory(guitarModel.data(), guitarModel.size()));
        REQUIRE(classifier.isLoaded());

        for (int k = 0; k < totalVariations; ++k)
        {
            auto buffer = makeGuitarVariation(k, sampleRate);
            auto candidates = AcousticClassifierService::analyzeBuffer(buffer, sampleRate, juce::File());
            const auto* c = findCandidate(candidates, SmartField::Type, "ml_classifier");
            REQUIRE(c != nullptr);
            REQUIRE(c->field == SmartField::Type);
            REQUIRE(c->source == "ml_classifier");
            REQUIRE(c->textValue == "Guitar");
            REQUIRE(c->numericValue >= 0.50);
            REQUIRE(c->status == CandidateStatus::Valid);
        }
    }

    // 3. With Pad model loaded into getSharedClassifier(): produces positive ML prediction "Pad"
    {
        auto padModel = createBiasedOnnxModel(2); // Class 2 = "Pad"
        auto& classifier = AcousticClassifierService::getSharedClassifier();
        REQUIRE(classifier.loadModelFromMemory(padModel.data(), padModel.size()));
        REQUIRE(classifier.isLoaded());

        for (int k = 0; k < totalVariations; ++k)
        {
            auto buffer = makePadVariation(k, sampleRate);
            auto candidates = AcousticClassifierService::analyzeBuffer(buffer, sampleRate, juce::File());
            const auto* c = findCandidate(candidates, SmartField::Type, "ml_classifier");
            REQUIRE(c != nullptr);
            REQUIRE(c->field == SmartField::Type);
            REQUIRE(c->source == "ml_classifier");
            REQUIRE(c->textValue == "Pad");
            REQUIRE(c->numericValue >= 0.50);
            REQUIRE(c->status == CandidateStatus::Valid);
        }
    }
}

TEST_CASE("Acoustic classification end-to-end pipeline integration", "[acceptance][acoustic][pipeline]")
{
    TestDirectory t;
    const double sampleRate = 44100.0;
    auto kickBuffer = makeKickVariation(0, sampleRate);

    SECTION("Unnamed audio file with kick sound produces Type == Kick candidate via SmartAnalysisService::collect")
    {
        auto unnamedFile = t.dir.getChildFile("audio_01.wav");
        writeWavFile(unnamedFile, kickBuffer, sampleRate);

        auto collection = SmartAnalysisService::collect(recordFor(unnamedFile));
        const auto* kickCandidate = findCandidate(collection.candidates, SmartField::Type, "dsp_acoustic");
        REQUIRE(kickCandidate != nullptr);
        REQUIRE(kickCandidate->field == SmartField::Type);
        REQUIRE(kickCandidate->textValue == "Kick");
        REQUIRE(kickCandidate->extractorId == "dsp");
        REQUIRE(kickCandidate->numericValue >= 0.85);

        auto resolved = resolveSmart(collection.candidates);
        resolved.pipelineState = SmartPipelineState::Complete;
        resolved.extractorVersion = smartExtractorsVersion;
        resolved.resolverVersion = smartResolverVersion;
        REQUIRE(resolved.type == "Kick");
        REQUIRE(resolved.typeSource == "dsp_acoustic");
        REQUIRE(resolved.typeState == SmartState::Resolved);

        AutoTagInput in;
        in.sampleId = 1;
        in.filePath = unnamedFile.getFullPathName();
        in.result = resolved;
        in.candidates = collection.candidates;

        AutoTagOptions options;
        options.tagIds["Kick"] = 42;

        auto decision = AutoTagRules::evaluate(in, options);
        REQUIRE(decision.proposal.has_value());
        REQUIRE(decision.proposal->canonicalType == "Kick");
        REQUIRE(decision.proposal->rule == AutoTagRule::SmartType);
        REQUIRE(decision.proposal->tagId == 42);
    }

    SECTION("Folder /Samples/Trap/Kicks/audio01.wav produces Type: Kick and genre context Subgenre: Trap, Genre: Hip-Hop")
    {
        auto trapKickFile = t.dir.getChildFile("Samples")
                                 .getChildFile("Trap")
                                 .getChildFile("Kicks")
                                 .getChildFile("audio01.wav");
        writeWavFile(trapKickFile, kickBuffer, sampleRate);

        auto collection = SmartAnalysisService::collect(recordFor(trapKickFile));

        // 1. Acoustic / Type Kick candidate produced
        const auto* kickCandidate = findCandidate(collection.candidates, SmartField::Type, "dsp_acoustic");
        REQUIRE(kickCandidate != nullptr);
        REQUIRE(kickCandidate->field == SmartField::Type);
        REQUIRE(kickCandidate->textValue == "Kick");

        // 2. Genre candidate produced from path: Subgenre "Trap", Genre "Hip-Hop"
        const auto* genreCandidate = findCandidate(collection.candidates, SmartField::Type, "path");
        REQUIRE(genreCandidate != nullptr);
        REQUIRE(genreCandidate->field == SmartField::Type);
        REQUIRE(genreCandidate->textValue == "Trap");
        REQUIRE(genreCandidate->detail == "Hip-Hop");

        // 3. GenreContextExtractor produces both Subgenre "Trap" and Genre "Hip-Hop"
        auto genreResult = GenreContextExtractor::extract(trapKickFile, collection.candidates);
        REQUIRE(genreResult.subgenre == "Trap");
        REQUIRE(genreResult.genre == "Hip-Hop");
        REQUIRE(genreResult.source == "path");
        REQUIRE(genreResult.confidence >= 0.80);

        // 4. SmartResolver resolves Type as Kick
        auto resolved = resolveSmart(collection.candidates);
        resolved.pipelineState = SmartPipelineState::Complete;
        resolved.extractorVersion = smartExtractorsVersion;
        resolved.resolverVersion = smartResolverVersion;
        REQUIRE(resolved.type == "Kick");
        REQUIRE(resolved.typeState == SmartState::Resolved);
        REQUIRE(resolved.explanation.contains("genre context"));

        // 5. AutoTagRules proposes Kick tag with AutoTagRule::SmartType
        AutoTagInput in;
        in.sampleId = 2;
        in.filePath = trapKickFile.getFullPathName();
        in.result = resolved;
        in.candidates = collection.candidates;

        AutoTagOptions options;
        options.tagIds["Kick"] = 100;
        options.tagIds["Trap"] = 200;
        options.tagIds["Hip-Hop"] = 300;

        auto tagDecision = AutoTagRules::evaluate(in, options);
        REQUIRE(tagDecision.proposal.has_value());
        REQUIRE(tagDecision.proposal->canonicalType == "Kick");
        REQUIRE(tagDecision.proposal->rule == AutoTagRule::SmartType);
        REQUIRE(tagDecision.proposal->tagId == 100);
    }
}
