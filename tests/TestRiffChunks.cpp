#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/export/RiffChunks.h"
#include "core/smart/WavMetadata.h"
#include "TestHelpers.h"

using namespace sonicator;
using Catch::Approx;

namespace
{
juce::File writeWav(const juce::File& file, int frames = 4410)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    juce::WavAudioFormat wav;
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate(44100.0)
        .withNumChannels(1).withBitsPerSample(16);
    auto writer = wav.createWriterFor(stream, options);
    if (writer == nullptr) return {};
    juce::AudioBuffer<float> buffer(1, frames);
    for (int i = 0; i < frames; ++i) buffer.setSample(0, i, 0.25f);
    writer->writeFromAudioSampleBuffer(buffer, 0, frames);
    return file;
}

struct Chunk { juce::String id; juce::MemoryBlock payload; };

std::vector<Chunk> readChunks(const juce::File& file)
{
    juce::MemoryBlock data;
    std::vector<Chunk> chunks;
    if (! file.loadFileAsData(data)) return chunks;
    const auto* bytes = static_cast<const juce::uint8*>(data.getData());
    if (data.getSize() < 12 || juce::String::fromUTF8((const char*) bytes, 4) != "RIFF") return chunks;
    size_t offset = 12;
    auto le32 = [&](size_t o)
    {
        return (juce::uint32) bytes[o] | ((juce::uint32) bytes[o + 1] << 8)
            | ((juce::uint32) bytes[o + 2] << 16) | ((juce::uint32) bytes[o + 3] << 24);
    };
    while (offset + 8 <= data.getSize())
    {
        Chunk chunk;
        chunk.id = juce::String::fromUTF8((const char*) bytes + offset, 4);
        const auto size = (size_t) le32(offset + 4);
        if (offset + 8 + size > data.getSize()) break;
        chunk.payload.setSize(0);
        chunk.payload.append(bytes + offset + 8, size);
        chunks.push_back(std::move(chunk));
        offset += 8 + size + (size & 1u);
    }
    return chunks;
}

Chunk* findChunk(std::vector<Chunk>& chunks, const juce::String& id)
{
    for (auto& chunk : chunks) if (chunk.id == id) return &chunk;
    return nullptr;
}

bool hasValidBpm(const WavMetadataResult& result, double bpm)
{
    for (const auto& candidate : result.candidates)
        if (candidate.field == SmartField::Bpm && candidate.status == CandidateStatus::Valid)
            return candidate.numericValue == Approx(bpm).margin(0.01);
    return false;
}

int rootCandidate(const WavMetadataResult& result)
{
    for (const auto& candidate : result.candidates)
        if (candidate.field == SmartField::RootNote
            && (candidate.status == CandidateStatus::Valid
                || candidate.status == CandidateStatus::DefaultValue))
            return (int) std::lround(candidate.numericValue);
    return -1;
}
} // namespace

TEST_CASE("An acid chunk round-trips through our reader", "[bpm][riff]")
{
    TestDirectory t;
    const auto file = writeWav(t.dir.getChildFile("acid.wav"));
    REQUIRE(riff::insertOrReplaceChunk(file, "acid", riff::buildAcid(128.0, 4, 60)));
    const auto result = extractWavMetadata(file);
    REQUIRE(hasValidBpm(result, 128.0));
    REQUIRE(rootCandidate(result) == 60);
}

TEST_CASE("Replacing acid leaves a single chunk", "[bpm][riff]")
{
    TestDirectory t;
    const auto file = writeWav(t.dir.getChildFile("replace.wav"));
    REQUIRE(riff::insertOrReplaceChunk(file, "acid", riff::buildAcid(128.0, 4, -1)));
    REQUIRE(riff::insertOrReplaceChunk(file, "acid", riff::buildAcid(90.0, 3, -1)));
    auto chunks = readChunks(file);
    int count = 0;
    for (const auto& chunk : chunks) if (chunk.id == "acid") ++count;
    REQUIRE(count == 1);
    REQUIRE(hasValidBpm(extractWavMetadata(file), 90.0));
}

TEST_CASE("Inserting a chunk preserves the audio bytes", "[bpm][riff]")
{
    TestDirectory t;
    const auto file = writeWav(t.dir.getChildFile("audio.wav"));
    auto before = readChunks(file);
    const auto* dataBefore = findChunk(before, "data");
    REQUIRE(dataBefore != nullptr);
    const auto payloadBefore = dataBefore->payload;
    REQUIRE(riff::insertOrReplaceChunk(file, "acid", riff::buildAcid(120.0, 4, -1)));
    auto after = readChunks(file);
    const auto* dataAfter = findChunk(after, "data");
    REQUIRE(dataAfter != nullptr);
    REQUIRE(dataAfter->payload == payloadBefore);
}

TEST_CASE("buildSmplRoot keeps an existing loop", "[bpm][riff]")
{
    juce::MemoryBlock existing(60, true);
    auto* bytes = static_cast<juce::uint8*>(existing.getData());
    bytes[12] = 60;                          // unity note
    bytes[28] = 1;                           // one loop
    for (int i = 0; i < 24; ++i) bytes[36 + i] = (juce::uint8) (i + 1);
    auto updated = riff::buildSmplRoot(48, existing);
    REQUIRE(updated.getSize() == existing.getSize());
    const auto* updatedBytes = static_cast<const juce::uint8*>(updated.getData());
    REQUIRE(updatedBytes[12] == 48);
    for (int i = 0; i < 24; ++i) REQUIRE(updatedBytes[36 + i] == (juce::uint8) (i + 1));
}

TEST_CASE("removeChunk drops only the requested chunk", "[bpm][riff]")
{
    TestDirectory t;
    const auto file = writeWav(t.dir.getChildFile("remove.wav"));
    REQUIRE(riff::insertOrReplaceChunk(file, "acid", riff::buildAcid(100.0, 4, -1)));
    auto before = readChunks(file);
    int beforeCount = (int) before.size();
    REQUIRE(findChunk(before, "acid") != nullptr);
    REQUIRE(riff::removeChunk(file, "acid"));
    auto after = readChunks(file);
    REQUIRE(findChunk(after, "acid") == nullptr);
    REQUIRE((int) after.size() == beforeCount - 1);
    REQUIRE(findChunk(after, "data") != nullptr);
}

TEST_CASE("A BPM-only acid chunk writes no root", "[bpm][riff]")
{
    TestDirectory t;
    const auto file = writeWav(t.dir.getChildFile("noroot.wav"));
    REQUIRE(riff::insertOrReplaceChunk(file, "acid", riff::buildAcid(128.0, 4, -1)));
    const auto result = extractWavMetadata(file);
    REQUIRE(hasValidBpm(result, 128.0));
    REQUIRE(rootCandidate(result) < 0);
}
