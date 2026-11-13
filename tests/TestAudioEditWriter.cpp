#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/edit/AudioEditWriter.h"
#include "TestHelpers.h"
#include <filesystem>

using namespace sonicator;
using namespace sonicator::edit;
using Catch::Approx;

namespace
{
juce::File writeAudio(const juce::File& file, juce::AudioFormat& format, int channels, int frames,
                      double rate, int bits)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate(rate)
        .withNumChannels(channels).withBitsPerSample(bits);
    auto writer = format.createWriterFor(stream, options);
    if (writer == nullptr) return {};
    juce::AudioBuffer<float> buffer(channels, frames);
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < frames; ++i)
            buffer.setSample(c, i, 0.25f * std::sin(2.0f * juce::MathConstants<float>::pi
                                                     * 440.0f * (float) i / (float) rate));
    writer->writeFromAudioSampleBuffer(buffer, 0, frames);
    return file;
}

AudioEditBuffer loadBuffer(const juce::File& file)
{
    juce::String error;
    auto buffer = AudioEditBuffer::load(file, error);
    REQUIRE(buffer.has_value());
    return *buffer;
}

std::unique_ptr<juce::AudioFormatReader> openReader(const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    return std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file));
}
} // namespace

TEST_CASE("Overwrite replaces a WAV atomically and keeps its format", "[edit][writer]")
{
    TestDirectory t;
    juce::WavAudioFormat wav;
    const auto file = writeAudio(t.dir.getChildFile("hires.wav"), wav, 2, 2000, 96000.0, 24);
    auto buffer = loadBuffer(file);
    REQUIRE(buffer.apply({ EditOperation::silenceRegion, { 0, 100 } }));
    const auto result = overwrite(file, buffer);
    REQUIRE(result.success);
    auto reader = openReader(file);
    REQUIRE(reader != nullptr);
    REQUIRE(reader->sampleRate == Approx(96000.0));
    REQUIRE(reader->bitsPerSample == 24);
    REQUIRE(reader->numChannels == 2);
    REQUIRE(reader->lengthInSamples == 2000);
    juce::AudioBuffer<float> readBack(2, 2000);
    REQUIRE(reader->read(&readBack, 0, 2000, 0, true, true));
    REQUIRE(readBack.getSample(0, 0) == Approx(buffer.channel(0)[0]).margin(1e-6));
    REQUIRE(readBack.getSample(0, 500) == Approx(buffer.channel(0)[500]).margin(1e-6));
}

#if ! JUCE_WINDOWS
TEST_CASE("A failed overwrite leaves the original byte-identical", "[edit][writer]")
{
    TestDirectory t;
    juce::WavAudioFormat wav;
    const auto folder = t.dir.getChildFile("locked");
    folder.createDirectory();
    const auto file = writeAudio(folder.getChildFile("keep.wav"), wav, 1, 1000, 44100.0, 16);
    const auto originalBytes = file.loadFileAsString();
    auto buffer = loadBuffer(file);
    REQUIRE(buffer.apply({ EditOperation::silenceRegion, { 0, 100 } }));

    std::filesystem::permissions(folder.getFullPathName().toStdString(),
                                 std::filesystem::perms::owner_read
                                     | std::filesystem::perms::owner_exec,
                                 std::filesystem::perm_options::replace);
    const auto result = overwrite(file, buffer);
    std::filesystem::permissions(folder.getFullPathName().toStdString(),
                                 std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace);
    REQUIRE_FALSE(result.success);
    REQUIRE(result.error.isNotEmpty());
    REQUIRE(file.loadFileAsString() == originalBytes);
    auto reader = openReader(file);
    REQUIRE(reader != nullptr);
    REQUIRE(reader->lengthInSamples == 1000);
}

TEST_CASE("Overwrite follows a symlink instead of replacing it", "[edit][writer]")
{
    TestDirectory t;
    juce::WavAudioFormat wav;
    const auto real = writeAudio(t.dir.getChildFile("real.wav"), wav, 1, 1000, 44100.0, 16);
    const auto link = t.dir.getChildFile("link.wav");
    REQUIRE(real.createSymbolicLink(link, true));
    auto buffer = loadBuffer(link);
    REQUIRE(buffer.apply({ EditOperation::cropToSelection, { 0, 400 } }));
    const auto result = overwrite(link, buffer);
    REQUIRE(result.success);
    REQUIRE(link.isSymbolicLink());
    auto reader = openReader(real);
    REQUIRE(reader != nullptr);
    REQUIRE(reader->lengthInSamples == 400);
}
#endif

TEST_CASE("Only WAV, AIFF and FLAC can be overwritten", "[edit][writer]")
{
    REQUIRE(canOverwriteInPlace(juce::File("/x/a.wav")));
    REQUIRE(canOverwriteInPlace(juce::File("/x/a.WAV")));
    REQUIRE(canOverwriteInPlace(juce::File("/x/a.aif")));
    REQUIRE(canOverwriteInPlace(juce::File("/x/a.aiff")));
    REQUIRE(canOverwriteInPlace(juce::File("/x/a.flac")));
    REQUIRE_FALSE(canOverwriteInPlace(juce::File("/x/a.mp3")));
    REQUIRE_FALSE(canOverwriteInPlace(juce::File("/x/a.ogg")));
    REQUIRE_FALSE(canOverwriteInPlace(juce::File("/x/a.rex")));
    REQUIRE_FALSE(canOverwriteInPlace(juce::File("/x/a.mp4")));
}

TEST_CASE("Copy names never overwrite", "[edit][writer]")
{
    TestDirectory t;
    const auto original = t.dir.getChildFile("Clap_15.wav");
    original.replaceWithText("x");
    auto first = uniqueCopyPath(original, "Clap_15");
    REQUIRE(first.getFileName() == "Clap_15 edit.wav");
    first.replaceWithText("x");
    auto second = uniqueCopyPath(original, "Clap_15");
    REQUIRE(second.getFileName() == "Clap_15 edit 2.wav");
    second.replaceWithText("x");
    REQUIRE(uniqueCopyPath(original, "Clap_15").getFileName() == "Clap_15 edit 3.wav");
}

TEST_CASE("Save copy of a WAV keeps the format and never overwrites", "[edit][writer]")
{
    TestDirectory t;
    juce::WavAudioFormat wav;
    const auto original = writeAudio(t.dir.getChildFile("kick.wav"), wav, 1, 1000, 44100.0, 16);
    auto buffer = loadBuffer(original);
    REQUIRE(buffer.apply({ EditOperation::cropToSelection, { 100, 600 } }));
    const auto result = saveCopy(original, "kick", buffer);
    REQUIRE(result.success);
    REQUIRE(result.file.existsAsFile());
    REQUIRE(result.file.getFileName() == "kick edit.wav");
    auto reader = openReader(result.file);
    REQUIRE(reader != nullptr);
    REQUIRE(reader->lengthInSamples == 500);
    auto originalReader = openReader(original);
    REQUIRE(originalReader->lengthInSamples == 1000);
}

TEST_CASE("Save copy of an AIFF keeps AIFF", "[edit][writer]")
{
    TestDirectory t;
    juce::AiffAudioFormat aiff;
    const auto original = writeAudio(t.dir.getChildFile("pad.aif"), aiff, 1, 1000, 44100.0, 16);
    auto buffer = loadBuffer(original);
    REQUIRE(buffer.apply({ EditOperation::cropToSelection, { 100, 600 } }));
    const auto result = saveCopy(original, "pad", buffer);
    INFO(result.error);
    REQUIRE(result.success);
    REQUIRE(result.file.getFileExtension() == ".aif");
    auto reader = openReader(result.file);
    REQUIRE(reader != nullptr);
    REQUIRE(reader->lengthInSamples == 500);
}

TEST_CASE("Writer metadata carries root key and loop", "[edit][writer]")
{
    TestDirectory t;
    juce::WavAudioFormat wav;
    const auto file = writeAudio(t.dir.getChildFile("loop.wav"), wav, 1, 2000, 44100.0, 16);
    FileMarkers markers;
    markers.rootKey = 48;
    markers.hasLoop = true;
    markers.loopStart = 100;
    markers.loopEndExclusive = 500;
    markers.cuePositions = { 250, 750 };
    auto buffer = loadBuffer(file);
    // Replace the buffer's markers through its public constructor path.
    SampleAudio audio;
    audio.sampleRate = buffer.sampleRate();
    audio.channels = buffer.channels();
    audio.bitsPerSample = buffer.bitsPerSample();
    audio.frames = buffer.frames();
    audio.left = buffer.channel(0);
    buffer = AudioEditBuffer::fromSampleAudio(audio, markers);
    const auto result = overwrite(file, buffer);
    REQUIRE(result.success);
    auto reader = openReader(file);
    REQUIRE(reader != nullptr);
    REQUIRE(reader->metadataValues.getValue("MidiUnityNote", "") == "48");
    REQUIRE(reader->metadataValues.getValue("NumSampleLoops", "") == "1");
    REQUIRE(reader->metadataValues.getValue("Loop0Start", "") == "100");
    REQUIRE(reader->metadataValues.getValue("Loop0End", "") == "499");
    REQUIRE(reader->metadataValues.getValue("NumCuePoints", "") == "2");
    REQUIRE(reader->metadataValues.getValue("Cue0Offset", "") == "250");
    REQUIRE(reader->metadataValues.getValue("Cue1Offset", "") == "750");
}
