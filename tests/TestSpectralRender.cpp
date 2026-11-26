#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/audio/SonicatorFormats.h"
#include "core/spectral/SpectralRender.h"
#include "TestHelpers.h"

using namespace sonicator;
using Catch::Approx;

namespace
{
juce::AudioBuffer<float> toneBuffer(double hz, double seconds = 0.5, double rate = 44100.0,
                                    float amplitude = 0.5f)
{
    juce::AudioBuffer<float> buffer(1, (int) std::lround(seconds * rate));
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        buffer.setSample(0, i, amplitude * (float) std::sin(2.0 * juce::MathConstants<double>::pi
                                                            * hz * (double) i / rate));
    return buffer;
}

double magnitudeAt(const juce::AudioBuffer<float>& buffer, double rate, double hz)
{
    double real = 0.0, imaginary = 0.0;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto phase = 2.0 * juce::MathConstants<double>::pi * hz * (double) i / rate;
        real += buffer.getSample(0, i) * std::cos(phase);
        imaginary += buffer.getSample(0, i) * std::sin(phase);
    }
    return std::sqrt(real * real + imaginary * imaginary) / (double) buffer.getNumSamples();
}

SpectralMask fullBandMask(double start, double end, double nyquist)
{
    SpectralMask mask;
    mask.setWindow(start, end, nyquist);
    mask.addRectangle(start, end, 20.0, nyquist, false);
    return mask;
}
} // namespace

TEST_CASE("A full mask reconstructs the signal", "[spectral][render]")
{
    const auto input = toneBuffer(440.0, 0.3);
    const auto duration = 0.3;
    const auto mask = fullBandMask(0.0, duration, 22050.0);
    juce::AudioBuffer<float> output;
    juce::String error;
    REQUIRE(renderMaskedBuffer(input, 44100.0, mask, 0.0, output, error));
    REQUIRE(output.getNumSamples() == input.getNumSamples());
    double errorEnergy = 0.0, reference = 0.0;
    for (int i = 0; i < input.getNumSamples(); ++i)
    {
        const auto difference = (double) output.getSample(0, i) - input.getSample(0, i);
        errorEnergy += difference * difference;
        reference += (double) input.getSample(0, i) * input.getSample(0, i);
    }
    REQUIRE(reference > 0.0);
    REQUIRE(std::sqrt(errorEnergy / reference) < 0.02);
}

TEST_CASE("A band mask removes everything outside its frequency range", "[spectral][render]")
{
    auto input = toneBuffer(440.0, 0.4);
    const auto high = toneBuffer(3000.0, 0.4);
    for (int i = 0; i < input.getNumSamples(); ++i)
        input.setSample(0, i, input.getSample(0, i) + high.getSample(0, i));
    const auto lowBefore = magnitudeAt(input, 44100.0, 440.0);
    const auto highBefore = magnitudeAt(input, 44100.0, 3000.0);

    SpectralMask mask;
    mask.setWindow(0.0, 0.4, 22050.0);
    mask.addRectangle(0.0, 0.4, 300.0, 700.0, false);
    juce::AudioBuffer<float> output;
    juce::String error;
    REQUIRE(renderMaskedBuffer(input, 44100.0, mask, 0.0, output, error));
    const auto lowAfter = magnitudeAt(output, 44100.0, 440.0);
    const auto highAfter = magnitudeAt(output, 44100.0, 3000.0);
    REQUIRE(lowBefore > 0.1);
    REQUIRE(highBefore > 0.1);
    REQUIRE(lowAfter > lowBefore * 0.6);
    REQUIRE(highAfter < highBefore * 0.05);
}

TEST_CASE("An empty mask is refused, never exported as silence", "[spectral][render]")
{
    juce::AudioBuffer<float> output;
    juce::String error;
    SpectralMask empty;
    empty.setWindow(0.0, 0.2, 22050.0);
    REQUIRE_FALSE(renderMaskedBuffer(toneBuffer(440.0, 0.2), 44100.0, empty, 0.0, output, error));
    REQUIRE(error.isNotEmpty());
}

TEST_CASE("The masked selection is written as a WAV of the range length", "[spectral][render]")
{
    TestDirectory t;
    const auto file = t.wave("mix.wav", 440.0, 1, 44100);
    SpectralMask mask;
    mask.setWindow(0.2, 0.4, 22050.0);
    mask.addRectangle(0.2, 0.4, 300.0, 700.0, false);
    const auto result = exportSpectralSelection(file, {}, 0.2, 0.4, mask, "mix", 16, false,
                                                t.dir.getChildFile("exports"));
    REQUIRE(result.success);
    REQUIRE(result.file.existsAsFile());

    juce::AudioFormatManager formats;
    registerSonicatorFormats(formats);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(result.file));
    REQUIRE(reader != nullptr);
    REQUIRE(reader->sampleRate == Approx(44100.0));
    REQUIRE(reader->numChannels == 1);
    REQUIRE(reader->lengthInSamples == 8820); // 0.2 s at 44.1 kHz, both files have the same rate
}
