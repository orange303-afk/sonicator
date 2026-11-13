#include "TrackerDocument.h"
#include "TrackerModReader.h"
#include "TrackerModuleReaders.h"
#include <cstring>
namespace sonicator {
namespace {
// A single decoded source is capped at 128 MiB of float PCM, matching the existing MIDI limit.
// The container itself is capped before it is read, so no dimension can be forced to overflow.
constexpr int64_t maxDecodedFrames = 128ll * 1024 * 1024 / (int64_t) sizeof(float);

bool decodePcm(const std::vector<uint8_t>& bytes, const TrackerSampleSlot& slot,
               SampleAudio& audio, juce::String& error)
{
    if (slot.frames <= 0) { error = "The sample is empty"; return false; }
    if (slot.frames > maxDecodedFrames) { error = "The sample exceeds the 128 MiB decoded limit"; return false; }
    const int64_t bytesPerFrame = slot.bitsPerSample == 16 ? 2 : 1;
    const int64_t needed = slot.frames * bytesPerFrame;
    if (slot.pcmOffset < 0 || needed < 0
        || slot.pcmOffset + needed > (int64_t) bytes.size())
    { error = "Sample data exceeds the file size"; return false; }
    const auto* source = bytes.data() + slot.pcmOffset;
    audio.sampleRate = slot.sampleRate;
    audio.channels = 1;
    audio.bitsPerSample = slot.bitsPerSample;
    audio.frames = slot.frames;
    audio.left.resize((size_t) slot.frames);
    if (slot.encoding == TrackerPcmEncoding::signedInt8)
    {
        for (int64_t i = 0; i < slot.frames; ++i)
            audio.left[(size_t) i] = (float) (int8_t) source[i] / 128.0f;
    }
    else if (slot.encoding == TrackerPcmEncoding::unsignedInt8)
    {
        for (int64_t i = 0; i < slot.frames; ++i)
            audio.left[(size_t) i] = ((float) source[i] - 128.0f) / 128.0f;
    }
    else if (slot.encoding == TrackerPcmEncoding::signedInt16LE
             || slot.encoding == TrackerPcmEncoding::unsignedInt16LE)
    {
        const float bias = slot.encoding == TrackerPcmEncoding::unsignedInt16LE ? 32768.0f : 0.0f;
        for (int64_t i = 0; i < slot.frames; ++i)
        {
            const auto raw = (uint16_t) source[i * 2] | ((uint16_t) source[i * 2 + 1] << 8);
            audio.left[(size_t) i] = ((float) raw - bias) / 32768.0f;
        }
    }
    else if (slot.encoding == TrackerPcmEncoding::deltaInt8)
    {
        int8_t accumulator = 0;
        for (int64_t i = 0; i < slot.frames; ++i)
        {
            accumulator = (int8_t) (accumulator + (int8_t) source[i]);
            audio.left[(size_t) i] = (float) accumulator / 128.0f;
        }
    }
    else // deltaInt16LE
    {
        int16_t accumulator = 0;
        for (int64_t i = 0; i < slot.frames; ++i)
        {
            const auto delta = (int16_t) ((uint16_t) source[i * 2]
                                          | ((uint16_t) source[i * 2 + 1] << 8));
            accumulator = (int16_t) (accumulator + delta);
            audio.left[(size_t) i] = (float) accumulator / 32768.0f;
        }
    }
    return true;
}
} // namespace

std::unique_ptr<TrackerDocument> TrackerDocument::open(const juce::File& file, juce::String& error)
{
    error.clear();
    std::vector<uint8_t> bytes;
    if (! readWholeFile(file, maxBytes, bytes, error)) return nullptr;
    TrackerDocumentData data;
    bool parsed = false;
    if (bytes.size() >= 21 && std::memcmp(bytes.data(), "Extended Instrument: ", 21) == 0)
        parsed = parseXi(bytes, data, error);
    else if (bytes.size() >= 17 && std::memcmp(bytes.data(), "Extended Module: ", 17) == 0)
        parsed = parseXm(bytes, data, error);
    else if (bytes.size() >= 48 && std::memcmp(bytes.data() + 44, "SCRM", 4) == 0)
        parsed = parseS3m(bytes, data, error);
    else
        parsed = parseMod(bytes, data, error);
    if (! parsed) return nullptr;
    if (data.info.formatName.isEmpty()) data.info.formatName = trackerFormatName(data.info.format);
    auto document = std::unique_ptr<TrackerDocument>(new TrackerDocument());
    document->sourceFile = file;
    document->data = std::move(data);
    document->cache.resize(document->data.slots.size());
    return document;
}

SampleAudioPtr TrackerDocument::sampleAudio(int slot, juce::String& error) const
{
    error.clear();
    if (slot < 0 || slot >= (int) data.slots.size())
    {
        error = "Sample slot is out of range";
        return nullptr;
    }
    if (cache[(size_t) slot] != nullptr) return cache[(size_t) slot];
    const auto& entry = data.slots[(size_t) slot];
    if (entry.empty) { error = "Sample slot is empty"; return nullptr; }
    if (entry.error.isNotEmpty()) { error = entry.error; return nullptr; }
    const int64_t decodedBytes = entry.frames * (int64_t) sizeof(float);
    if (cachedBytes + decodedBytes > maxCachedBytes)
    {
        error = "The decoded sample cache limit (256 MiB) is reached";
        return nullptr;
    }
    auto audio = std::make_shared<SampleAudio>();
    if (! decodePcm(data.bytes, entry, *audio, error))
    {
        return nullptr;
    }
    cache[(size_t) slot] = audio;
    cachedBytes += decodedBytes;
    return audio;
}
} // namespace sonicator
