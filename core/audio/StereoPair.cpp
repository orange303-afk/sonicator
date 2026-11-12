#include "StereoPair.h"
#include "core/audio/SonicatorFormats.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <vector>
namespace sonicator {
StereoName parseStereoName(const juce::String& fileName)
{
    StereoName result;
    result.base = fileName;
    const auto extension = fileName.fromLastOccurrenceOf(".", true, false);
    auto stem = extension.isNotEmpty() ? fileName.dropLastCharacters(extension.length()) : fileName;
    const auto lower = stem.toLowerCase();
    struct Marker { const char* text; int side; };
    const Marker markers[] = { { "-l", -1 }, { "-r", 1 }, { " l", -1 }, { " r", 1 },
                               { ".l", -1 }, { ".r", 1 }, { "_l", -1 }, { "_r", 1 } };
    for (const auto& marker : markers)
    {
        if (! lower.endsWith(marker.text)) continue;
        result.base = stem.dropLastCharacters((int) std::strlen(marker.text)).trim();
        result.side = result.base.isEmpty() ? 0 : marker.side;
        result.suffix = stem.substring(stem.length() - (int) std::strlen(marker.text));
        return result;
    }
    return result;
}

namespace {
std::unique_ptr<juce::AudioFormatReader> openMono(const juce::File& file, int64_t maxFrames,
                                                 juce::String& error)
{
    juce::AudioFormatManager formats;
    registerSonicatorFormats(formats);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr) { error = "Cannot decode " + file.getFileName(); return nullptr; }
    if (reader->numChannels != 1) { error = file.getFileName() + " is not mono"; return nullptr; }
    if (! (reader->sampleRate > 0) || reader->lengthInSamples <= 0)
    {
        error = file.getFileName() + " has no audio frames";
        return nullptr;
    }
    if (reader->lengthInSamples > maxFrames)
    {
        error = file.getFileName() + " exceeds the stereo pair limit";
        return nullptr;
    }
    return reader;
}
bool readMono(juce::AudioFormatReader& reader, std::vector<float>& destination, juce::String& error)
{
    const auto frames = reader.lengthInSamples;
    destination.resize((size_t) frames);
    juce::AudioBuffer<float> buffer(1, (int) juce::jmin<int64_t>(65536, frames));
    for (int64_t offset = 0; offset < frames; offset += 65536)
    {
        const auto count = (int) juce::jmin<int64_t>(65536, frames - offset);
        buffer.clear();
        if (! reader.read(&buffer, 0, count, offset, true, false))
        {
            error = "Cannot read " + reader.getFormatName();
            return false;
        }
        for (int i = 0; i < count; ++i)
        {
            const auto value = buffer.getSample(0, i);
            if (! std::isfinite(value)) { error = "Stereo pair contains a non-finite sample"; return false; }
            destination[(size_t) (offset + i)] = value;
        }
    }
    return true;
}
} // namespace

StereoPairResult loadStereoPair(const juce::File& selected, int64_t maxDecodedBytes)
{
    StereoPairResult result;
    const auto name = parseStereoName(selected.getFileName());
    if (name.side == 0 || name.base.isEmpty()) return result;
    result.paired = true;
    const auto parent = selected.getParentDirectory();
    const auto extension = selected.getFileName().fromLastOccurrenceOf(".", true, false);
    juce::File partner;
    // Try the exact complementary name with the same marker style and case, then scan.
    const auto last = name.suffix[name.suffix.length() - 1];
    const bool upper = juce::CharacterFunctions::isUpperCase(last);
    const auto complement = name.suffix.dropLastCharacters(1)
        + (name.side < 0 ? (upper ? "R" : "r") : (upper ? "L" : "l"));
    const auto candidateName = name.base + complement + extension;
    partner = parent.getChildFile(candidateName);
    if (! partner.existsAsFile())
    {
        for (const auto& entry : juce::RangedDirectoryIterator(parent, false, "*.wav",
                                                               juce::File::findFiles,
                                                               juce::File::FollowSymlinks::no))
        {
            const auto other = parseStereoName(entry.getFile().getFileName());
            if (other.side == -name.side && other.base.equalsIgnoreCase(name.base))
            {
                partner = entry.getFile();
                break;
            }
        }
    }
    if (! partner.existsAsFile())
    {
        result.error = "No complementary L/R file found for " + selected.getFileName();
        return result;
    }
    const auto maxFrames = maxDecodedBytes / 8; // two float channels plus temporary buffers
    juce::String error;
    const auto& leftFile = name.side < 0 ? selected : partner;
    const auto& rightFile = name.side < 0 ? partner : selected;
    auto leftReader = openMono(leftFile, maxFrames, error);
    if (leftReader == nullptr) { result.error = error; return result; }
    auto rightReader = openMono(rightFile, maxFrames, error);
    if (rightReader == nullptr) { result.error = error; return result; }
    if (leftReader->sampleRate != rightReader->sampleRate
        || leftReader->lengthInSamples != rightReader->lengthInSamples)
    {
        result.error = "Stereo pair differs in length or sample rate";
        return result;
    }
    std::vector<float> left, right;
    if (! readMono(*leftReader, left, error) || ! readMono(*rightReader, right, error))
    {
        result.error = error;
        return result;
    }
    auto audio = std::make_shared<SampleAudio>();
    audio->sampleRate = (int) juce::jlimit(1.0, 1000000.0, leftReader->sampleRate);
    audio->channels = 2;
    audio->bitsPerSample = 16;
    audio->frames = leftReader->lengthInSamples;
    audio->left = std::move(left);
    audio->right = std::move(right);
    result.audio = std::move(audio);
    result.ok = true;
    return result;
}
} // namespace sonicator
