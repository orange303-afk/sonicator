#include "AnalysisAudio.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cmath>
namespace sonicator {
AnalysisAudio readAnalysisAudio(const juce::File& file, const AnalysisAudioOptions& options,
                                const CancelCheck& cancel)
{
    AnalysisAudio out;
    if (cancel && cancel()) { out.error = "Cancelled"; return out; }
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0 || reader->lengthInSamples <= 0
        || reader->numChannels == 0 || reader->numChannels > 64)
    {
        out.error = "unsupported, empty or corrupt audio file";
        return out;
    }
    const int64_t byTime = (int64_t) std::llround(options.maxSeconds * reader->sampleRate);
    const int64_t byMemory = options.maxDecodedBytes / (4 * (int64_t) reader->numChannels);
    const int64_t length = std::min({ reader->lengthInSamples, byTime, byMemory });
    if (length <= 0) { out.error = "empty audio file"; return out; }
    out.buffer.setSize((int) reader->numChannels, (int) length);
    if (! reader->read(&out.buffer, 0, (int) length, 0, true, true))
    {
        out.buffer.setSize(0, 0);
        out.error = "audio decode failed";
        return out;
    }
    if (cancel && cancel())
    {
        out.buffer.setSize(0, 0);
        out.error = "Cancelled";
        return out;
    }
    out.sampleRate = reader->sampleRate;
    out.truncated = reader->lengthInSamples > length;
    return out;
}
} // namespace sonicator
