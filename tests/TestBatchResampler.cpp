#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/batch/BatchQuantizer.h"
#include "core/batch/BatchResampler.h"
#include "core/audio/SonicatorFormats.h"
#include "TestHelpers.h"

using namespace sonicator;
using namespace sonicator::batch;

namespace
{
juce::File writeWav(const juce::File& file, const std::vector<std::vector<float>>& channels,
                    double rate = 44100.0)
{
    file.deleteFile();
    file.getParentDirectory().createDirectory();
    juce::WavAudioFormat format;
    auto stream = std::unique_ptr<juce::OutputStream>(file.createOutputStream());
    REQUIRE(stream != nullptr);
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate(rate)
        .withNumChannels((int) channels.size()).withBitsPerSample(32)
        .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = format.createWriterFor(stream, options);
    REQUIRE(writer != nullptr);
    std::vector<const float*> pointers;
    for (const auto& channel : channels) pointers.push_back(channel.data());
    REQUIRE(writer->writeFromFloatArrays(pointers.data(), (int) pointers.size(),
                                         (int) channels.front().size()));
    writer.reset();
    stream.reset();
    return file;
}

SourceSnapshot snapshotFor(const juce::File& file)
{
    SourceSnapshot snapshot;
    snapshot.path = file;
    snapshot.identity = physicalIdentity(file);
    juce::AudioFormatManager formats;
    registerSonicatorFormats(formats);
    auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file));
    REQUIRE(reader != nullptr);
    snapshot.sampleRate = (int) reader->sampleRate;
    snapshot.channels = (int) reader->numChannels;
    snapshot.bits = (int) reader->bitsPerSample;
    snapshot.floating = reader->usesFloatingPointData;
    snapshot.frames = reader->lengthInSamples;
    return snapshot;
}

std::unique_ptr<PreparedAudio> prepare(const juce::File& file)
{
    Recipe recipe;
    auto prepared = prepareAudio(snapshotFor(file), recipe);
    REQUIRE(prepared.ok);
    return std::move(prepared.audio);
}

std::vector<std::vector<float>> readStream(ConvertedStream& stream, int blockSize = 777)
{
    std::vector<std::vector<float>> out((size_t) stream.channels());
    juce::AudioBuffer<float> block(stream.channels(), blockSize);
    int64_t total = 0;
    while (true)
    {
        const auto result = stream.readNext(blockSize, block);
        REQUIRE((result.state == ReadState::ok || result.state == ReadState::eof));
        if (result.frames <= 0) break;
        for (int channel = 0; channel < stream.channels(); ++channel)
            for (int n = 0; n < result.frames; ++n)
                out[(size_t) channel].push_back(block.getSample(channel, n));
        total += result.frames;
    }
    REQUIRE(total == stream.frames());
    return out;
}

std::vector<float> tone(double hz, double rate, int frames, float amplitude = 0.5f)
{
    std::vector<float> samples((size_t) frames);
    for (int i = 0; i < frames; ++i)
        samples[(size_t) i] = amplitude
            * (float) std::sin(2.0 * juce::MathConstants<double>::pi * hz * i / rate);
    return samples;
}

double rmsDb(const std::vector<float>& samples, size_t begin, size_t end)
{
    double sum = 0.0;
    for (size_t i = begin; i < end; ++i) sum += (double) samples[i] * samples[i];
    const auto rms = std::sqrt(sum / (double) std::max<size_t>(1, end - begin));
    return 20.0 * std::log10(std::max(1.0e-12, rms));
}

double peakDb(const std::vector<float>& samples, size_t begin, size_t end)
{
    float peak = 0.0f;
    for (size_t i = begin; i < end; ++i) peak = std::max(peak, std::abs(samples[i]));
    return 20.0 * std::log10(std::max(1.0e-12, (double) peak));
}
} // namespace

TEST_CASE("ConvertedStream keeps identity, mono/stereo mapping and exact lengths",
          "[batch][batch-src]")
{
    TestDirectory t;
    auto samples = tone(1000.0, 44100.0, 4096);
    const auto file = writeWav(t.dir.getChildFile("identity.wav"), { samples });
    auto identity = std::make_unique<ConvertedStream>(prepare(file), 44100, 1);
    REQUIRE(identity->frames() == 4096);
    const auto values = readStream(*identity);
    for (size_t i = 0; i < samples.size(); ++i)
        REQUIRE(values[0][i] == Catch::Approx(samples[i]).margin(1e-7));

    auto stereo = std::make_unique<ConvertedStream>(prepare(file), 44100, 2);
    const auto both = readStream(*stereo);
    REQUIRE(both.size() == 2);
    REQUIRE(both[0] == both[1]);

    const auto antiphaseFile = writeWav(t.dir.getChildFile("antiphase.wav"), { samples,
        [&samples] { std::vector<float> inverted(samples.size()); for (size_t i = 0; i < samples.size(); ++i) inverted[i] = -samples[i]; return inverted; }() });
    auto mono = std::make_unique<ConvertedStream>(prepare(antiphaseFile), 44100, 1);
    const auto mixed = readStream(*mono);
    for (float value : mixed[0]) REQUIRE(std::abs(value) < 1e-7f);

    REQUIRE(ConvertedStream::outputFrames(44100, 44100, 48000) == 48000);
    REQUIRE(ConvertedStream::outputFrames(1, 192000, 44100) == 1);
    REQUIRE(ConvertedStream::outputFrames(96000, 96000, 44100) == 44100);
}

TEST_CASE("Resampling produces the exact frame count and keeps an impulse in place",
          "[batch][batch-src]")
{
    TestDirectory t;
    std::vector<float> impulse(44100, 0.0f);
    for (int n = 0; n < 64; ++n)
        impulse[1000 + n] = (float) (0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * n / 63.0));
    const auto file = writeWav(t.dir.getChildFile("impulse.wav"), { impulse });
    auto stream = std::make_unique<ConvertedStream>(prepare(file), 48000, 1);
    REQUIRE(stream->frames() == 48000);
    const auto values = readStream(*stream, 1024);
    size_t peakIndex = 0;
    float peak = 0.0f;
    for (size_t i = 0; i < values[0].size(); ++i)
        if (std::abs(values[0][i]) > peak) { peak = std::abs(values[0][i]); peakIndex = i; }
    REQUIRE(peak > 0.8f);
    const auto expected = (size_t) std::llround(1031.0 * 48000.0 / 44100.0);
    REQUIRE(std::abs((int64_t) peakIndex - (int64_t) expected) <= 2);
}

TEST_CASE("Resampling suppresses out-of-band content and preserves in-band amplitude",
          "[batch][batch-src]")
{
    TestDirectory t;
    const auto highFile = writeWav(t.dir.getChildFile("30k.wav"), { tone(30000.0, 96000.0, 96000) },
                                   96000.0);
    auto down = std::make_unique<ConvertedStream>(prepare(highFile), 44100, 1);
    REQUIRE(down->frames() == 44100);
    const auto suppressed = readStream(*down, 4096);
    REQUIRE(rmsDb(suppressed[0], 4000, 40000) < -80.0);

    const auto toneFile = writeWav(t.dir.getChildFile("1k.wav"), { tone(1000.0, 48000.0, 48000) },
                                   48000.0);
    auto resampled = std::make_unique<ConvertedStream>(prepare(toneFile), 44100, 1);
    const auto steady = readStream(*resampled, 4096);
    REQUIRE(std::abs(peakDb(steady[0], 4000, 40000) - (-6.0206)) <= 0.1);

    const auto shortFile = writeWav(t.dir.getChildFile("one.wav"), { { 0.5f } }, 192000.0);
    auto one = std::make_unique<ConvertedStream>(prepare(shortFile), 48000, 1);
    REQUIRE(one->frames() == 1);
    const auto oneValues = readStream(*one);
    REQUIRE(oneValues[0].size() == 1);
}

TEST_CASE("Different read block partitions agree within the numeric tolerance",
          "[batch][batch-src]")
{
    TestDirectory t;
    const auto file = writeWav(t.dir.getChildFile("partition.wav"),
                               { tone(440.0, 44100.0, 20000) });
    auto a = std::make_unique<ConvertedStream>(prepare(file), 48000, 1);
    auto b = std::make_unique<ConvertedStream>(prepare(file), 48000, 1);
    const auto one = readStream(*a, 1);
    const auto many = readStream(*b, 16384);
    REQUIRE(one[0].size() == many[0].size());
    for (size_t i = 0; i < one[0].size(); ++i)
        REQUIRE(std::abs(one[0][i] - many[0][i]) <= 2e-6f);
}

TEST_CASE("Quantizer hits integer endpoints, avoids signed shifts and dithers once",
          "[batch][batch-quantizer]")
{
    const float positive[1] { 0.99999f };
    const float negative[1] { -1.0f };
    const float zero[1] { 0.0f };
    const float* input[1] { positive };
    int32_t destination[1] { 0 };
    int32_t* output[1] { destination };

    Quantizer quantizer;
    quantizer.reset(1, false, 16);
    quantizer.quantize(input, 1, 1, output);
    REQUIRE(destination[0] == (32767 << 16));

    const float* negativeInput[1] { negative };
    quantizer.quantize(negativeInput, 1, 1, output);
    REQUIRE(destination[0] == (int32_t) ((uint32_t) (-32768) << 16));

    const float* zeroInput[1] { zero };
    quantizer.reset(1, false, 24);
    quantizer.quantize(zeroInput, 1, 1, output);
    REQUIRE(destination[0] == 0);

    // TPDF mean and variance in target LSB units over 200k constant samples.
    // The TPDF distribution before rounding: mean 0 and variance 1/6 LSB^2.
    quantizer.reset(1234567, true, 24);
    double sum = 0.0, squares = 0.0;
    constexpr int count = 200000;
    for (int i = 0; i < count; ++i)
    {
        const double dither = quantizer.ditherSample(0);
        sum += dither;
        squares += dither * dither;
        REQUIRE(std::abs(dither) <= 1.0);
    }
    const double mean = sum / count;
    const double variance = squares / count - mean * mean;
    REQUIRE(std::abs(mean) < 0.02);
    REQUIRE(variance == Catch::Approx(1.0 / 6.0).epsilon(0.05));

    std::vector<float> silence(200000, 0.0f);
    std::vector<int32_t> quantized(silence.size());
    const float* silenceInput[1] { silence.data() };
    int32_t* quantizedOutput[1] { quantized.data() };
    quantizer.reset(1234567, true, 24);
    quantizer.quantize(silenceInput, 1, (int) silence.size(), quantizedOutput);
    bool allIntegers = true;
    for (auto value : quantized)
        if ((value & 0xff) != 0) allIntegers = false;
    REQUIRE(allIntegers);
}

TEST_CASE("Quantizer seeds are stable and channels are independent", "[batch][batch-quantizer]")
{
    std::vector<float> silence(4096, 0.0f);
    const float* input[2] { silence.data(), silence.data() };
    std::vector<int32_t> left(4096), right(4096), repeatLeft(4096), repeatRight(4096);
    int32_t* first[2] { left.data(), right.data() };
    int32_t* second[2] { repeatLeft.data(), repeatRight.data() };
    Quantizer a, b;
    a.reset(99, true, 16);
    b.reset(99, true, 16);
    a.quantize(input, 2, 4096, first);
    b.quantize(input, 2, 4096, second);
    REQUIRE(left == repeatLeft);
    REQUIRE(right == repeatRight);
    REQUIRE(left != right);

    REQUIRE_FALSE(automaticDither(Encoding::int24, SourceSnapshot { .bits = 16, .floating = false }, false));
    REQUIRE_FALSE(automaticDither(Encoding::int16, SourceSnapshot { .bits = 16, .floating = false }, false));
    REQUIRE(automaticDither(Encoding::int16, SourceSnapshot { .bits = 24, .floating = false }, false));
    REQUIRE(automaticDither(Encoding::int24, SourceSnapshot { .bits = 24, .floating = false }, true));
    REQUIRE_FALSE(automaticDither(Encoding::float32, SourceSnapshot { .bits = 32, .floating = true }, true));
}
