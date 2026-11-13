#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/export/DragMetadata.h"
#include "core/export/RiffChunks.h"
#include "core/smart/WavMetadata.h"
#include "TestHelpers.h"

using namespace sonicator;
using Catch::Approx;

namespace
{
juce::File writeAudio(const juce::File& file, juce::AudioFormat& format, int frames, int bits)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate(44100.0)
        .withNumChannels(1).withBitsPerSample(bits);
    auto writer = format.createWriterFor(stream, options);
    if (writer == nullptr) return {};
    juce::AudioBuffer<float> buffer(1, frames);
    for (int i = 0; i < frames; ++i)
        buffer.setSample(0, i, 0.25f * std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f
                                                 * (float) i / 44100.0f));
    writer->writeFromAudioSampleBuffer(buffer, 0, frames);
    return file;
}

std::vector<juce::String> chunkIds(const juce::File& file)
{
    juce::MemoryBlock data;
    std::vector<juce::String> ids;
    if (! file.loadFileAsData(data) || data.getSize() < 12) return ids;
    const auto* bytes = static_cast<const juce::uint8*>(data.getData());
    auto le32 = [&](size_t o)
    {
        return (juce::uint32) bytes[o] | ((juce::uint32) bytes[o + 1] << 8)
            | ((juce::uint32) bytes[o + 2] << 16) | ((juce::uint32) bytes[o + 3] << 24);
    };
    size_t offset = 12;
    while (offset + 8 <= data.getSize())
    {
        const auto size = (size_t) le32(offset + 4);
        ids.push_back(juce::String::fromUTF8((const char*) bytes + offset, 4));
        offset += 8 + size + (size & 1u);
    }
    return ids;
}

bool hasId(const std::vector<juce::String>& ids, const juce::String& id)
{
    for (const auto& value : ids) if (value == id) return true;
    return false;
}

bool metadataHasBpm(const juce::File& file, double bpm)
{
    for (const auto& candidate : extractWavMetadata(file).candidates)
        if (candidate.field == SmartField::Bpm && candidate.status == CandidateStatus::Valid)
            return candidate.numericValue == Approx(bpm).margin(0.01);
    return false;
}
} // namespace

TEST_CASE("A WAV with BPM gets an acid chunk and keeps its audio", "[bpm][drag]")
{
    TestDirectory t;
    juce::WavAudioFormat wav;
    const auto source = writeAudio(t.dir.getChildFile("loop.wav"), wav, 88200, 16);
    juce::MemoryBlock originalBytes;
    REQUIRE(source.loadFileAsData(originalBytes));
    DragMetadata metadata;
    metadata.hasBpm = true;
    metadata.bpm = 128.0;
    juce::String error;
    const auto copy = materialiseWithMetadata(source, metadata, t.dir.getChildFile("cache"), error);
    REQUIRE(error.isEmpty());
    REQUIRE(copy.existsAsFile());
    REQUIRE(copy != source);
    REQUIRE(copy.getParentDirectory() != t.dir);
    REQUIRE(metadataHasBpm(copy, 128.0));
    juce::MemoryBlock currentBytes;
    REQUIRE(source.loadFileAsData(currentBytes));
    REQUIRE(currentBytes == originalBytes);
}

TEST_CASE("BPM only writes no smpl chunk", "[bpm][drag]")
{
    TestDirectory t;
    juce::WavAudioFormat wav;
    const auto source = writeAudio(t.dir.getChildFile("loop.wav"), wav, 44100, 16);
    DragMetadata metadata;
    metadata.hasBpm = true;
    metadata.bpm = 100.0;
    juce::String error;
    const auto copy = materialiseWithMetadata(source, metadata, t.dir.getChildFile("cache"), error);
    REQUIRE(error.isEmpty());
    REQUIRE(hasId(chunkIds(copy), "acid"));
    REQUIRE_FALSE(hasId(chunkIds(copy), "smpl"));
}

TEST_CASE("Root only writes smpl and no acid", "[bpm][drag]")
{
    TestDirectory t;
    juce::WavAudioFormat wav;
    const auto source = writeAudio(t.dir.getChildFile("loop.wav"), wav, 44100, 16);
    DragMetadata metadata;
    metadata.hasRoot = true;
    metadata.rootMidi = 60;
    juce::String error;
    const auto copy = materialiseWithMetadata(source, metadata, t.dir.getChildFile("cache"), error);
    REQUIRE(error.isEmpty());
    REQUIRE(hasId(chunkIds(copy), "smpl"));
    REQUIRE_FALSE(hasId(chunkIds(copy), "acid"));
}

TEST_CASE("An AIFF with BPM becomes a decodable WAV", "[bpm][drag]")
{
    TestDirectory t;
    juce::AiffAudioFormat aiff;
    const auto source = writeAudio(t.dir.getChildFile("pad.aif"), aiff, 44100, 16);
    DragMetadata metadata;
    metadata.hasBpm = true;
    metadata.bpm = 96.0;
    juce::String error;
    const auto copy = materialiseWithMetadata(source, metadata, t.dir.getChildFile("cache"), error);
    REQUIRE(error.isEmpty());
    REQUIRE(copy.getFileExtension() == ".wav");
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(copy));
    REQUIRE(reader != nullptr);
    REQUIRE(reader->sampleRate == Approx(44100.0));
    REQUIRE(metadataHasBpm(copy, 96.0));
}

TEST_CASE("No metadata returns the source path", "[bpm][drag]")
{
    TestDirectory t;
    juce::WavAudioFormat wav;
    const auto source = writeAudio(t.dir.getChildFile("plain.wav"), wav, 1000, 16);
    juce::String error;
    REQUIRE(materialiseWithMetadata(source, {}, t.dir.getChildFile("cache"), error) == source);
    REQUIRE(error.isEmpty());
}

TEST_CASE("A repeated call reuses the cache file", "[bpm][drag]")
{
    TestDirectory t;
    juce::WavAudioFormat wav;
    const auto source = writeAudio(t.dir.getChildFile("loop.wav"), wav, 44100, 16);
    DragMetadata metadata;
    metadata.hasBpm = true;
    metadata.bpm = 120.0;
    juce::String error;
    const auto first = materialiseWithMetadata(source, metadata, t.dir.getChildFile("cache"), error);
    REQUIRE(first.existsAsFile());
    const auto modified = first.getLastModificationTime();
    const auto second = materialiseWithMetadata(source, metadata, t.dir.getChildFile("cache"), error);
    REQUIRE(second == first);
    REQUIRE(second.getLastModificationTime() == modified);
}

TEST_CASE("An unreadable file reports an error", "[bpm][drag]")
{
    TestDirectory t;
    const auto source = t.dir.getChildFile("broken.wav");
    source.replaceWithText("not audio");
    DragMetadata metadata;
    metadata.hasBpm = true;
    metadata.bpm = 120.0;
    juce::String error;
    const auto result = materialiseWithMetadata(source, metadata, t.dir.getChildFile("cache"), error);
    REQUIRE_FALSE(result.existsAsFile());
    REQUIRE(error.isNotEmpty());
}
