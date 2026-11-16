#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/match/MatchPolicy.h"
#include "core/match/OfflineMatchRenderer.h"
#include "core/audio/SonicatorFormats.h"
#include "TestHelpers.h"
#include <cmath>
#include <thread>

using namespace sonicator;
using sonicator::plugin::HostTempoValue;

namespace
{
constexpr double testRate = 44100.0;

juce::File writeTone (const juce::File& file, double frequency, double seconds, int channels,
                      bool invertedRight = false, bool silence = false)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    REQUIRE (stream != nullptr);
    auto options = juce::AudioFormatWriterOptions {}
        .withSampleRate (testRate)
        .withNumChannels (channels)
        .withBitsPerSample (24);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, options);
    REQUIRE (writer != nullptr);
    const int64_t frames = (int64_t) std::llround (seconds * testRate);
    juce::AudioBuffer<float> buffer (channels, (int) juce::jmin<int64_t> (frames, 4096));
    int64_t done = 0;
    while (done < frames)
    {
        const int count = (int) juce::jmin<int64_t> (buffer.getNumSamples(), frames - done);
        for (int i = 0; i < count; ++i)
        {
            const double t = (double) (done + i) / testRate;
            const float value = silence ? 0.0f : (float) (0.5 * std::sin (juce::MathConstants<double>::twoPi * frequency * t));
            buffer.setSample (0, i, value);
            if (channels > 1) buffer.setSample (1, i, invertedRight ? -value : value);
        }
        REQUIRE (writer->writeFromFloatArrays (buffer.getArrayOfReadPointers(), channels, count));
        done += count;
    }
    REQUIRE (writer->flush());
    writer.reset();
    return file;
}

double estimateFrequency (const juce::File& file, double startSeconds, double durationSeconds)
{
    juce::AudioFormatManager formats;
    registerSonicatorFormats (formats);
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    REQUIRE (reader != nullptr);
    juce::AudioBuffer<float> buffer (1, (int) (durationSeconds * reader->sampleRate));
    reader->read (&buffer, 0, buffer.getNumSamples(),
                  (int64_t) (startSeconds * reader->sampleRate), true, false);
    int crossings = 0;
    const auto* samples = buffer.getReadPointer (0);
    for (int i = 1; i < buffer.getNumSamples(); ++i)
        if ((samples[i - 1] <= 0.0f && samples[i] > 0.0f) || (samples[i - 1] >= 0.0f && samples[i] < 0.0f))
            ++crossings;
    return (double) crossings / 2.0 / durationSeconds;
}

double centsOff (double measured, double expected)
{
    return 1200.0 * std::log2 (measured / expected);
}

MatchSource makeSource (const juce::File& file, MatchStructure structure = MatchStructure::loop)
{
    MatchSource source;
    source.identity = "test:" + file.getFullPathName();
    source.sourceFrames = (int64_t) std::llround (8.0 * testRate);
    source.sampleRate = testRate;
    source.channels = 1;
    source.structure = structure;
    source.bpm = 120.0;
    source.bpmState = MetadataState::known;
    return source;
}

MatchRenderRequest makeRequest (const juce::File& sourceFile, const juce::File& output,
                                const MatchSource& source, const MatchPlan& plan)
{
    MatchRenderRequest request;
    request.sourceFile = sourceFile;
    request.source = source;
    request.plan = plan;
    request.key = makeMatchResultKey (source, plan, sourceFile.getSize(),
                                      sourceFile.getLastModificationTime().toMilliseconds());
    request.outputFile = output;
    request.sourceSize = sourceFile.getSize();
    request.sourceModified = sourceFile.getLastModificationTime().toMilliseconds();
    return request;
}

PluginMatchPreferences bpmOnly()
{
    PluginMatchPreferences prefs;
    prefs.matchBpm = true;
    return prefs;
}

PluginMatchPreferences scaleOnly (int tonic)
{
    PluginMatchPreferences prefs;
    prefs.matchScale = true;
    prefs.targetTonic = tonic;
    prefs.targetMode = TargetMode::minor;
    return prefs;
}
} // namespace

TEST_CASE ("Match BPM renders an eight second loop to 7.5 s and keeps the pitch",
           "[plugin-match][render]")
{
    TestDirectory directory;
    const auto output = directory.dir.getChildFile ("out").getChildFile ("bpm.wav");
    auto source = makeSource (writeTone (directory.dir.getChildFile ("loop.wav"), 440.0, 8.0, 1));
    const auto plan = resolveMatch (source, bpmOnly(), HostTempoValue { true, 128.0, 1 });
    REQUIRE (plan.action == MatchAction::transform);

    const auto result = renderMatch (makeRequest (directory.dir.getChildFile ("loop.wav"), output, source, plan));
    INFO (result.error);
    REQUIRE (result.success);
    CHECK (result.frames == 330750);
    CHECK (result.sampleRate == Catch::Approx (44100.0));
    CHECK (result.channels == 1);

    juce::AudioFormatManager formats;
    registerSonicatorFormats (formats);
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (output));
    REQUIRE (reader != nullptr);
    CHECK (reader->lengthInSamples == 330750);
    CHECK (reader->bitsPerSample == 24);
    CHECK (reader->sampleRate == Catch::Approx (44100.0));

    const auto frequency = estimateFrequency (output, 2.0, 1.0);
    CHECK (std::abs (centsOff (frequency, 440.0)) < 5.0);
}

TEST_CASE ("Match Scale keeps the frame count and moves the pitch", "[plugin-match][render]")
{
    TestDirectory directory;
    const auto sourceFile = writeTone (directory.dir.getChildFile ("tone.wav"), 440.0, 2.0, 1);
    auto source = makeSource (sourceFile);
    source.sourceFrames = (int64_t) std::llround (2.0 * testRate);
    source.keyTonic = 9;
    source.keyMode = "minor";
    source.keyState = MetadataState::known;

    const auto plan = resolveMatch (source, scaleOnly (0), HostTempoValue { true, 120.0, 1 });
    REQUIRE (plan.action == MatchAction::transform);
    CHECK (plan.semitones == Catch::Approx (3.0));
    CHECK (plan.targetFrames == source.sourceFrames);

    const auto output = directory.dir.getChildFile ("scale.wav");
    const auto result = renderMatch (makeRequest (sourceFile, output, source, plan));
    INFO (result.error);
    REQUIRE (result.success);
    CHECK (result.frames == source.sourceFrames);
    const auto frequency = estimateFrequency (output, 0.5, 0.5);
    CHECK (std::abs (centsOff (frequency, 440.0 * std::pow (2.0, 3.0 / 12.0))) < 5.0);
}

TEST_CASE ("Both matches apply one length and one pitch change", "[plugin-match][render]")
{
    TestDirectory directory;
    const auto sourceFile = writeTone (directory.dir.getChildFile ("combo.wav"), 440.0, 8.0, 1);
    auto source = makeSource (sourceFile);
    source.keyTonic = 9;
    source.keyMode = "minor";
    source.keyState = MetadataState::known;
    auto prefs = bpmOnly();
    prefs.matchScale = true;
    prefs.targetTonic = 0;
    const auto plan = resolveMatch (source, prefs, HostTempoValue { true, 128.0, 1 });
    REQUIRE (plan.action == MatchAction::transform);

    const auto output = directory.dir.getChildFile ("combo-out.wav");
    const auto result = renderMatch (makeRequest (sourceFile, output, source, plan));
    INFO (result.error);
    REQUIRE (result.success);
    CHECK (result.frames == 330750);
    const auto frequency = estimateFrequency (output, 2.0, 1.0);
    CHECK (std::abs (centsOff (frequency, 440.0 * std::pow (2.0, 3.0 / 12.0))) < 5.0);
}

TEST_CASE ("Stereo stays coherent and silence stays silent", "[plugin-match][render]")
{
    TestDirectory directory;
    const auto sourceFile = writeTone (directory.dir.getChildFile ("stereo.wav"), 220.0, 2.0, 2, true);
    auto source = makeSource (sourceFile);
    source.sourceFrames = (int64_t) std::llround (2.0 * testRate);
    source.channels = 2;
    const auto plan = resolveMatch (source, bpmOnly(), HostTempoValue { true, 120.0, 1 });
    REQUIRE (plan.action == MatchAction::transform);

    const auto output = directory.dir.getChildFile ("stereo-out.wav");
    const auto result = renderMatch (makeRequest (sourceFile, output, source, plan));
    INFO (result.error);
    REQUIRE (result.success);
    juce::AudioFormatManager formats;
    registerSonicatorFormats (formats);
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (output));
    REQUIRE (reader != nullptr);
    CHECK (reader->numChannels == 2);
    juce::AudioBuffer<float> block (2, 1024);
    reader->read (&block, 0, 1024, reader->lengthInSamples / 2, true, true);
    for (int i = 0; i < 1024; ++i)
        CHECK (std::abs (block.getSample (0, i) + block.getSample (1, i)) < 1.0e-4f);

    const auto silentFile = writeTone (directory.dir.getChildFile ("silence.wav"), 440.0, 1.0, 1, false, true);
    auto silentSource = makeSource (silentFile, MatchStructure::oneShot);
    silentSource.sourceFrames = (int64_t) testRate;
    PluginMatchPreferences prefs;
    prefs.matchScale = true;
    prefs.targetTonic = 0;
    silentSource.pitchMidi = 60;
    silentSource.hasPitch = true;
    silentSource.pitchState = MetadataState::known;
    const auto silentPlan = resolveMatch (silentSource, prefs, HostTempoValue { true, 120.0, 1 });
    REQUIRE (silentPlan.action == MatchAction::transform);
    const auto silentOutput = directory.dir.getChildFile ("silence-out.wav");
    const auto silentResult = renderMatch (makeRequest (silentFile, silentOutput, silentSource, silentPlan));
    INFO (silentResult.error);
    REQUIRE (silentResult.success);
    std::unique_ptr<juce::AudioFormatReader> silentReader (formats.createReaderFor (silentOutput));
    REQUIRE (silentReader != nullptr);
    juce::AudioBuffer<float> silentBlock (1, (int) silentReader->lengthInSamples);
    silentReader->read (&silentBlock, 0, silentBlock.getNumSamples(), 0, true, false);
    CHECK (silentBlock.getMagnitude (0, silentBlock.getNumSamples()) < 1.0e-6f);
}

TEST_CASE ("A published file is reused only after its content is validated", "[plugin-match][render]")
{
    TestDirectory directory;
    const auto sourceFile = writeTone (directory.dir.getChildFile ("reuse.wav"), 330.0, 2.0, 1);
    auto source = makeSource (sourceFile);
    source.sourceFrames = (int64_t) std::llround (2.0 * testRate);
    const auto plan = resolveMatch (source, bpmOnly(), HostTempoValue { true, 120.0, 1 });
    const auto output = directory.dir.getChildFile ("reuse-out.wav");
    const auto first = renderMatch (makeRequest (sourceFile, output, source, plan));
    REQUIRE (first.success);
    const auto firstModified = output.getLastModificationTime().toMilliseconds();

    const auto second = renderMatch (makeRequest (sourceFile, output, source, plan));
    REQUIRE (second.success);
    CHECK (second.file == output);
    CHECK (output.getLastModificationTime().toMilliseconds() == firstModified);

    // A foreign file with the right name is not a cache hit.
    const auto corrupt = directory.dir.getChildFile ("corrupt.wav");
    corrupt.replaceWithText ("not a wav at all");
    const auto corruptResult = renderMatch (makeRequest (sourceFile, corrupt, source, plan));
    CHECK_FALSE (corruptResult.success);
    CHECK (corrupt.loadFileAsString() == "not a wav at all");
}

TEST_CASE ("A failed write never publishes a partial file", "[plugin-match][render]")
{
    TestDirectory directory;
    const auto sourceFile = writeTone (directory.dir.getChildFile ("disk.wav"), 330.0, 1.0, 1);
    auto source = makeSource (sourceFile);
    source.sourceFrames = (int64_t) testRate;
    const auto plan = resolveMatch (source, bpmOnly(), HostTempoValue { true, 120.0, 1 });
    const auto output = directory.dir.getChildFile ("disk-out.wav");
    auto request = makeRequest (sourceFile, output, source, plan);
    request.openStream = [] (const juce::File&) -> std::unique_ptr<juce::OutputStream> { return nullptr; };
    const auto result = renderMatch (request);
    CHECK_FALSE (result.success);
    CHECK_FALSE (output.existsAsFile());
}

namespace
{
class FailingWriteStream final : public juce::OutputStream
{
public:
    explicit FailingWriteStream (const juce::File& file)
        : stream (file.createOutputStream())
    {
    }
    void flush() override { if (stream != nullptr) stream->flush(); }
    bool setPosition (juce::int64 newPosition) override
    {
        return stream != nullptr && stream->setPosition (newPosition);
    }
    juce::int64 getPosition() override { return stream != nullptr ? stream->getPosition() : 0; }
    bool write (const void* data, size_t size) override
    {
        if (stream == nullptr) return false;
        if (written + (juce::int64) size > limit) return false;
        if (! stream->write (data, size)) return false;
        written += (juce::int64) size;
        return true;
    }
private:
    std::unique_ptr<juce::FileOutputStream> stream;
    juce::int64 written = 0;
    const juce::int64 limit = 4096;
};
} // namespace

TEST_CASE ("A full disk fails before writing and a failed write never publishes",
           "[plugin-match][render]")
{
    TestDirectory directory;
    const auto sourceFile = writeTone (directory.dir.getChildFile ("full.wav"), 330.0, 2.0, 1);
    auto source = makeSource (sourceFile);
    source.sourceFrames = (int64_t) std::llround (2.0 * testRate);
    const auto plan = resolveMatch (source, bpmOnly(), HostTempoValue { true, 120.0, 1 });

    const auto noSpace = directory.dir.getChildFile ("no-space.wav");
    auto request = makeRequest (sourceFile, noSpace, source, plan);
    request.availableBytes = 1024;
    const auto noSpaceResult = renderMatch (request);
    CHECK_FALSE (noSpaceResult.success);
    CHECK (noSpaceResult.error.contains ("disk space"));
    CHECK_FALSE (noSpace.existsAsFile());

    const auto failedWrite = directory.dir.getChildFile ("failed-write.wav");
    auto writeRequest = makeRequest (sourceFile, failedWrite, source, plan);
    writeRequest.openStream = [] (const juce::File& part) -> std::unique_ptr<juce::OutputStream>
    {
        return std::make_unique<FailingWriteStream> (part);
    };
    const auto writeResult = renderMatch (writeRequest);
    CHECK_FALSE (writeResult.success);
    CHECK_FALSE (failedWrite.existsAsFile());
}

TEST_CASE ("A corrupt published file is bypassed without deleting it", "[plugin-match][render]")
{
    TestDirectory directory;
    const auto root = directory.dir.getChildFile ("cache");
    const juce::String key = "key";
    const auto plain = matchOutputFile (key, "stem", root);
    REQUIRE (plain.getParentDirectory().createDirectory());
    plain.replaceWithText ("garbage");

    const auto chosen = findUsableMatchOutput (key, "stem", root, 100, 44100.0, 1);
    REQUIRE (chosen != juce::File());
    CHECK (chosen != plain);
    CHECK (chosen.getFileName().contains ("-r1"));
    CHECK (plain.loadFileAsString() == "garbage"); // project media is never deleted

    for (int attempt = 1; attempt < 8; ++attempt)
    {
        const auto file = plain.getSiblingFile ("stem-r" + juce::String (attempt) + "-match.wav");
        file.replaceWithText ("garbage");
    }
    CHECK (findUsableMatchOutput (key, "stem", root, 100, 44100.0, 1) == juce::File());
}

TEST_CASE ("The duration limit is measured in seconds, not 44.1 kHz frames",
           "[plugin-match][render]")
{
    TestDirectory directory;
    const auto sourceFile = writeTone (directory.dir.getChildFile ("limit.wav"), 330.0, 2.0, 1);
    auto source = makeSource (sourceFile);
    source.sourceFrames = (int64_t) std::llround (2.0 * testRate);
    const auto plan = resolveMatch (source, bpmOnly(), HostTempoValue { true, 120.0, 1 });

    auto request = makeRequest (sourceFile, directory.dir.getChildFile ("limit-out.wav"), source, plan);
    request.maximumSeconds = 1.0;
    const auto refused = renderMatch (request);
    CHECK_FALSE (refused.success);
    CHECK (refused.error.contains ("duration limit"));

    auto allowed = makeRequest (sourceFile, directory.dir.getChildFile ("limit-ok.wav"), source, plan);
    const auto rendered = renderMatch (allowed);
    INFO (rendered.error);
    CHECK (rendered.success);
}

TEST_CASE ("A source changed during preparation is never published", "[plugin-match][render]")
{
    TestDirectory directory;
    const auto sourceFile = writeTone (directory.dir.getChildFile ("changed.wav"), 330.0, 1.0, 1);
    auto source = makeSource (sourceFile);
    source.sourceFrames = (int64_t) testRate;
    const auto plan = resolveMatch (source, bpmOnly(), HostTempoValue { true, 120.0, 1 });
    const auto output = directory.dir.getChildFile ("changed-out.wav");
    auto request = makeRequest (sourceFile, output, source, plan);
    request.openStream = [sourceFile] (const juce::File& part)
    {
        sourceFile.appendData ("x", 1);
        return std::unique_ptr<juce::OutputStream> (part.createOutputStream().release());
    };
    const auto result = renderMatch (request);
    CHECK_FALSE (result.success);
    CHECK_FALSE (output.existsAsFile());
}

TEST_CASE ("Two renderers with one key publish one valid file", "[plugin-match][render]")
{
    TestDirectory directory;
    const auto sourceFile = writeTone (directory.dir.getChildFile ("race.wav"), 330.0, 1.0, 1);
    auto source = makeSource (sourceFile);
    source.sourceFrames = (int64_t) testRate;
    const auto plan = resolveMatch (source, bpmOnly(), HostTempoValue { true, 120.0, 1 });
    const auto output = directory.dir.getChildFile ("race-out.wav");
    MatchRenderResult first, second;
    std::thread a ([&] { first = renderMatch (makeRequest (sourceFile, output, source, plan)); });
    std::thread b ([&] { second = renderMatch (makeRequest (sourceFile, output, source, plan)); });
    a.join();
    b.join();
    INFO (first.error);
    INFO (second.error);
    CHECK (first.success);
    CHECK (second.success);
    juce::String error;
    CHECK (validateMatchOutput (output, plan.targetFrames, testRate, 1, error));
}
