#include "EblAudioFormat.h"
#include "EblReader.h"
#include <algorithm>
#include <vector>
namespace sonicator {
namespace {
constexpr uint64_t maxDecodedBytes = 128ull << 20;
constexpr uint64_t readCapSlack = 64 * 1024;

// The decoded sample is already int16 PCM; the reader only serves it to JUCE.
class EblAudioReader final : public juce::AudioFormatReader
{
public:
    EblAudioReader(EblSample sample, juce::InputStream* stream)
        : juce::AudioFormatReader(stream, "E-mu EBL"), sample(std::move(sample))
    {
        sampleRate = this->sample.sampleRate;
        bitsPerSample = 16;
        usesFloatingPointData = false;
        lengthInSamples = this->sample.frames;
        numChannels = (unsigned int) this->sample.channels;
    }
    bool readSamples(int* const* destChannels, int numDestChannels, int startOffsetInDestBuffer,
                     juce::int64 startSampleInFile, int numSamples) override
    {
        const auto available = lengthInSamples - startSampleInFile;
        const int toRead = (int) juce::jlimit<juce::int64>(0, numSamples, available);
        for (int channel = 0; channel < numDestChannels; ++channel)
        {
            if (destChannels[channel] == nullptr) continue;
            const int sourceChannel = juce::jmin(channel, (int) numChannels - 1);
            for (int i = 0; i < toRead; ++i)
            {
                const auto frame = startSampleInFile + i;
                destChannels[channel][startOffsetInDestBuffer + i] =
                    (int) sample.pcm[(size_t) (frame * numChannels + (unsigned int) sourceChannel)] << 16;
            }
            juce::zeromem(destChannels[channel] + startOffsetInDestBuffer + toRead,
                          (size_t) (numSamples - toRead) * sizeof(int));
        }
        return true;
    }
private:
    EblSample sample;
};
} // namespace

EblAudioFormat::EblAudioFormat() : juce::AudioFormat("E-mu EBL", ".ebl") {}
juce::Array<int> EblAudioFormat::getPossibleSampleRates() { return {}; }
juce::Array<int> EblAudioFormat::getPossibleBitDepths() { return { 16 }; }

juce::AudioFormatReader* EblAudioFormat::createReaderFor(juce::InputStream* sourceStream,
                                                         bool deleteStreamIfOpeningFails)
{
    auto refuse = [sourceStream, deleteStreamIfOpeningFails]() -> juce::AudioFormatReader*
    {
        if (deleteStreamIfOpeningFails) delete sourceStream;
        return nullptr;
    };
    if (sourceStream == nullptr) return nullptr;
    const auto total = sourceStream->getTotalLength();
    if (total <= 0 || (uint64_t) total > maxDecodedBytes + readCapSlack) return refuse();
    sourceStream->setPosition(0);
    std::vector<std::byte> bytes((size_t) total);
    size_t done = 0;
    while (done < bytes.size())
    {
        const int n = sourceStream->read(bytes.data() + done,
                                         (int) std::min<size_t>(bytes.size() - done, 1u << 20));
        if (n <= 0) break;
        done += (size_t) n;
    }
    if (done != bytes.size()) return refuse();
    auto parsed = parseEbl(bytes, maxDecodedBytes);
    if (! parsed.ok()) return refuse();
    return new EblAudioReader(std::move(*parsed.sample), sourceStream);
}

std::unique_ptr<juce::AudioFormatWriter> EblAudioFormat::createWriterFor(
    std::unique_ptr<juce::OutputStream>&, const juce::AudioFormatWriterOptions&)
{
    return nullptr;
}
} // namespace sonicator
