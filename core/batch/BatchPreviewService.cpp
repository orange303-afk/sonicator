#include "BatchPreviewService.h"
#include "BatchPipeline.h"
#include "BatchResampler.h"
#include "core/audio/SonicatorFormats.h"

namespace sonicator::batch
{
namespace
{
bool writeFloatWav(const juce::File& file, const juce::AudioBuffer<float>& buffer, int rate)
{
    juce::WavAudioFormat format;
    auto stream = std::unique_ptr<juce::OutputStream>(file.createOutputStream());
    if (stream == nullptr) return false;
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate((double) rate)
        .withNumChannels(buffer.getNumChannels()).withBitsPerSample(32)
        .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = format.createWriterFor(stream, options);
    if (writer == nullptr) return false;
    const bool ok = writer->writeFromFloatArrays(buffer.getArrayOfReadPointers(),
                                                 buffer.getNumChannels(),
                                                 buffer.getNumSamples());
    writer.reset();
    stream.reset();
    return ok;
}

std::array<double, 2> measure(const juce::AudioBuffer<float>& buffer, float& peak)
{
    std::array<double, 2> sums { 0.0, 0.0 };
    std::array<double, 2> compensation { 0.0, 0.0 };
    peak = 0.0f;
    for (int channel = 0; channel < juce::jmin(2, buffer.getNumChannels()); ++channel)
        for (int n = 0; n < buffer.getNumSamples(); ++n)
        {
            const double value = buffer.getSample(channel, n);
            compensatedAdd(sums[(size_t) channel], compensation[(size_t) channel], value);
            peak = std::max(peak, (float) std::abs(value));
        }
    std::array<double, 2> means {};
    for (int channel = 0; channel < 2; ++channel)
        means[(size_t) channel] = buffer.getNumSamples() > 0
            ? (sums[(size_t) channel] + compensation[(size_t) channel]) / buffer.getNumSamples()
            : 0.0;
    return means;
}
} // namespace

BatchPreviewService::BatchPreviewService() : Thread("Sonicator batch preview") {}

BatchPreviewService::~BatchPreviewService()
{
    cancel();
    stopThread(10000);
}

bool BatchPreviewService::start(const SourceSnapshot& snapshot, const Recipe& newRecipe,
                                int newGeneration, juce::File targetFolder)
{
    if (running.load()) return false;
    if (snapshot.frames <= 0 || snapshot.sampleRate <= 0) return false;
    source = snapshot;
    recipe = newRecipe;
    generation = newGeneration;
    cancelled = false;
    folder = targetFolder.getFullPathName().isNotEmpty()
        ? targetFolder.getChildFile("preview-" + juce::Uuid().toDashedString())
        : juce::File::getSpecialLocation(juce::File::tempDirectory)
              .getChildFile("sonicator-batch-preview")
              .getChildFile("preview-" + juce::Uuid().toDashedString());
    folder.createDirectory();
    {
        std::lock_guard lock(resultMutex);
        result = {};
    }
    running = true;
    startThread();
    return true;
}

void BatchPreviewService::cancel() { cancelled = true; }

PreviewResult BatchPreviewService::takeResult()
{
    std::lock_guard lock(resultMutex);
    auto out = std::move(result);
    result = {};
    return out;
}

void BatchPreviewService::discardResult()
{
    std::lock_guard lock(resultMutex);
    if (result.dry.existsAsFile()) result.dry.deleteFile();
    if (result.wet.existsAsFile()) result.wet.deleteFile();
    result = {};
}

void BatchPreviewService::run()
{
    PreviewResult local;
    local.generation = generation;
    const auto checkCancel = [this] { return cancelled.load(); };
    try
    {
        auto prepared = prepareAudio(source, recipe, checkCancel);
        if (! prepared.ok)
        {
            local.ok = false;
            local.cancelled = prepared.code == ErrorCode::cancelled;
            local.code = prepared.code;
            local.error = prepared.error;
        }
        else
        {
            const auto inRate = prepared.audio->sampleRate();
            const auto inChannels = prepared.audio->channels();
            const auto startFrame = prepared.audio->startFrame();
            local.sourceStartFrame = startFrame;

            juce::AudioFormatManager formats;
            registerSonicatorFormats(formats);
            std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(source.path));
            if (reader == nullptr)
            {
                local.code = ErrorCode::decode_failed;
                local.error = "The source cannot be read for the dry preview";
            }
            else
            {
                const auto frames = (int) std::min<int64_t>(
                    previewFrameLimit(inRate), std::max<int64_t>(0, source.frames - startFrame));
                juce::AudioBuffer<float> dry(inChannels, std::max(1, frames));
                if (frames > 0
                    && reader->read(&dry, 0, frames, startFrame, true, true))
                {
                    local.dryInfo.channels = inChannels;
                    local.dryInfo.sampleRate = inRate;
                    local.dryInfo.frames = frames;
                    const auto means = measure(dry, local.dryInfo.peak);
                    local.dryInfo.dc = std::max(std::abs(means[0]), std::abs(means[1]));
                }
                const auto dryFile = folder.getChildFile("dry.wav");
                if (frames <= 0 || ! writeFloatWav(dryFile, dry, inRate))
                {
                    local.code = ErrorCode::encode_failed;
                    local.error = "The dry preview cannot be written";
                }
                else
                {
                    local.dry = dryFile;
                }
            }

            if (local.code == ErrorCode::none && ! checkCancel())
            {
                int outChannels = inChannels;
                switch (recipe.channels)
                {
                    case ChannelMode::sameAsSource: outChannels = inChannels; break;
                    case ChannelMode::mono: outChannels = 1; break;
                    case ChannelMode::stereo: outChannels = 2; break;
                }
                const int outRate = recipe.sampleRate > 0 ? recipe.sampleRate : inRate;
                ConvertedStream stream(std::move(prepared.audio), outRate, outChannels, checkCancel);
                const auto limit = (int) std::min<int64_t>(previewFrameLimit(outRate), stream.frames());
                juce::AudioBuffer<float> wet(outChannels, std::max(1, limit));
                int produced = 0;
                while (produced < limit && ! checkCancel())
                {
                    juce::AudioBuffer<float> block;
                    const auto read = stream.readNext(std::min(4096, limit - produced), block);
                    if (read.state != ReadState::ok || read.frames <= 0) break;
                    for (int channel = 0; channel < outChannels; ++channel)
                        wet.copyFrom(channel, produced, block, channel, 0, read.frames);
                    produced += read.frames;
                }
                if (produced <= 0)
                {
                    local.code = ErrorCode::encode_failed;
                    local.error = "The processed preview produced no audio";
                }
                else
                {
                    wet.setSize(outChannels, produced, true, false, true);
                    local.wetInfo.channels = outChannels;
                    local.wetInfo.sampleRate = outRate;
                    local.wetInfo.frames = produced;
                    const auto means = measure(wet, local.wetInfo.peak);
                    local.wetInfo.dc = std::max(std::abs(means[0]), std::abs(means[1]));
                    const auto wetFile = folder.getChildFile("wet.wav");
                    if (! writeFloatWav(wetFile, wet, outRate))
                    {
                        local.code = ErrorCode::encode_failed;
                        local.error = "The processed preview cannot be written";
                    }
                    else
                    {
                        local.wet = wetFile;
                    }
                }
            }
            local.ok = local.code == ErrorCode::none && local.dry.existsAsFile()
                && local.wet.existsAsFile();
        }
    }
    catch (const std::exception& error)
    {
        local.ok = false;
        local.code = ErrorCode::encode_failed;
        local.error = error.what();
    }
    if (checkCancel())
    {
        local.cancelled = true;
        local.ok = false;
        if (local.code == ErrorCode::none) local.code = ErrorCode::cancelled;
        if (local.dry.existsAsFile()) local.dry.deleteFile();
        if (local.wet.existsAsFile()) local.wet.deleteFile();
        folder.deleteRecursively();
    }
    if (! local.ok && ! local.cancelled)
    {
        if (local.dry.existsAsFile()) local.dry.deleteFile();
        if (local.wet.existsAsFile()) local.wet.deleteFile();
    }
    {
        std::lock_guard lock(resultMutex);
        result = std::move(local);
    }
    running = false;
}
} // namespace sonicator::batch
