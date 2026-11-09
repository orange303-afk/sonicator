#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/export/AudioExporter.h"
#include "core/export/SelectionExporter.h"
#include "core/scanner/FileScanner.h"
#include "TestHelpers.h"
#include <cmath>
#include <limits>
#include <atomic>
#include <condition_variable>
#include <thread>
using namespace sonicator;
namespace
{
juce::File writeWav(const juce::File& file, int channels, int bits, bool floating, int frames)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    juce::WavAudioFormat wav;
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate(44100.0)
        .withNumChannels(channels).withBitsPerSample(bits)
        .withSampleFormat(floating ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                   : juce::AudioFormatWriterOptions::SampleFormat::integral);
    auto writer = wav.createWriterFor(stream, options);
    if (writer == nullptr) return {};
    juce::AudioBuffer<float> buffer(channels, frames);
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < frames; ++i)
            buffer.setSample(c, i, (c % 2 == 0 ? 1.0f : -1.0f) * 0.5f
                * std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * (float) i / 44100.0f));
    writer->writeFromAudioSampleBuffer(buffer, 0, frames);
    return file;
}
juce::AudioFormatManager& formats()
{
    static juce::AudioFormatManager manager;
    static const bool registered = [] { manager.registerBasicFormats(); return true; }();
    juce::ignoreUnused(registered);
    return manager;
}
std::unique_ptr<juce::AudioFormatReader> readerFor(const juce::File& file)
{
    return std::unique_ptr<juce::AudioFormatReader>(formats().createReaderFor(file));
}
}
TEST_CASE("Export preserves rate, channels, bit depth, frame count and samples", "[export]")
{
    TestDirectory t;
    auto source = t.wave("source.wav", 440, 2, 44100 * 2);
    const auto size = source.getSize();
    const auto modified = source.getLastModificationTime().toMilliseconds();
    const auto hash = FileScanner::computeFileHash(source);
    const auto destination = t.dir.getChildFile("out.wav");
    AudioExporter exporter;
    const auto result = exporter.exportRange(source, 0.2, 0.5, destination);
    REQUIRE(result.success); REQUIRE(result.error.isEmpty()); REQUIRE(result.file == destination);
    auto input = readerFor(source); auto output = readerFor(destination);
    REQUIRE(input != nullptr); REQUIRE(output != nullptr);
    REQUIRE(output->sampleRate == 44100);
    REQUIRE(output->numChannels == 2);
    REQUIRE(output->bitsPerSample == 16);
    REQUIRE_FALSE(output->usesFloatingPointData);
    REQUIRE(output->lengthInSamples == 13230);
    juce::AudioBuffer<float> expected(2, 2), actual(2, 2);
    REQUIRE(input->read(&expected, 0, 2, 8820, true, true));
    REQUIRE(output->read(&actual, 0, 2, 0, true, true));
    for (int c = 0; c < 2; ++c)
        REQUIRE(actual.getSample(c, 0) == Catch::Approx(expected.getSample(c, 0)).margin(1.0 / 32768.0));
    REQUIRE(input->read(&expected, 0, 2, 22049, true, true));
    REQUIRE(output->read(&actual, 0, 2, 13229, true, true));
    for (int c = 0; c < 2; ++c)
        REQUIRE(actual.getSample(c, 1) == Catch::Approx(expected.getSample(c, 1)).margin(1.0 / 32768.0));
    REQUIRE(source.getSize() == size);
    REQUIRE(source.getLastModificationTime().toMilliseconds() == modified);
    REQUIRE(FileScanner::computeFileHash(source) == hash);
}
TEST_CASE("Export clamps ranges to the file and rejects invalid sources", "[export]")
{
    TestDirectory t;
    auto source = t.wave("src.wav", 440, 1, 44100);
    const auto folder = t.dir.getChildFile("exports");
    AudioExporter exporter;
    const auto clamped = folder.getChildFile("clamped.wav");
    const auto result = exporter.exportRange(source, -1.0, 0.1, clamped);
    REQUIRE(result.success);
    auto reader = readerFor(clamped); REQUIRE(reader != nullptr);
    REQUIRE(reader->lengthInSamples == 4410);
    const auto rejected = folder.getChildFile("rejected.wav");
    REQUIRE_FALSE(exporter.exportRange(source, 0.5, 0.5, rejected).success);
    REQUIRE_FALSE(exporter.exportRange(source, 0.7, 0.2, rejected).success);
    REQUIRE_FALSE(exporter.exportRange(source, std::numeric_limits<double>::quiet_NaN(), 0.2, rejected).success);
    REQUIRE_FALSE(exporter.exportRange(folder.getChildFile("missing.wav"), 0.0, 0.2, rejected).success);
    REQUIRE_FALSE(rejected.existsAsFile());
    REQUIRE_FALSE(juce::File(rejected.getFullPathName() + ".part").existsAsFile());
}
TEST_CASE("Export maps integer, float and compressed sources to documented WAV formats", "[export]")
{
    TestDirectory t;
    const auto folder = t.dir.getChildFile("exports");
    folder.createDirectory();
    AudioExporter exporter;
    auto int24 = writeWav(folder.getChildFile("int24.wav"), 2, 24, false, 4410);
    REQUIRE(int24.existsAsFile());
    auto int24Out = folder.getChildFile("int24-out.wav");
    REQUIRE(exporter.exportRange(int24, 0.0, 0.05, int24Out).success);
    auto int24Reader = readerFor(int24Out); REQUIRE(int24Reader != nullptr);
    REQUIRE(int24Reader->sampleRate == 44100); REQUIRE(int24Reader->numChannels == 2);
    REQUIRE(int24Reader->bitsPerSample == 24); REQUIRE_FALSE(int24Reader->usesFloatingPointData);
    auto floatIn = writeWav(folder.getChildFile("float.wav"), 2, 32, true, 4410);
    REQUIRE(floatIn.existsAsFile());
    auto floatOut = folder.getChildFile("float-out.wav");
    REQUIRE(exporter.exportRange(floatIn, 0.0, 0.05, floatOut).success);
    auto floatReader = readerFor(floatOut); REQUIRE(floatReader != nullptr);
    REQUIRE(floatReader->bitsPerSample == 32); REQUIRE(floatReader->usesFloatingPointData);
    for (const auto* name : { "tone.flac", "tone.mp3" })
    {
        auto compressed = juce::File(SONICATOR_FIXTURE_DIR).getChildFile(name);
        REQUIRE(compressed.existsAsFile());
        auto compressedReader = readerFor(compressed); REQUIRE(compressedReader != nullptr);
        const double end = juce::jmin(0.1, (double) compressedReader->lengthInSamples / compressedReader->sampleRate);
        auto compressedOut = folder.getChildFile(juce::String(name) + "-out.wav");
        REQUIRE(exporter.exportRange(compressed, 0.0, end, compressedOut).success);
        auto converted = readerFor(compressedOut); REQUIRE(converted != nullptr);
        REQUIRE(converted->sampleRate == compressedReader->sampleRate);
        REQUIRE(converted->numChannels == compressedReader->numChannels);
        REQUIRE(converted->bitsPerSample == 24);
        REQUIRE_FALSE(converted->usesFloatingPointData);
    }
}
TEST_CASE("Export refuses more than 64 channels without a partial file", "[export]")
{
    TestDirectory t;
    auto wide = t.wave("wide.wav", 440, 65, 100);
    const auto destination = t.dir.getChildFile("exports/wide-out.wav");
    const auto result = AudioExporter().exportRange(wide, 0.0, 0.001, destination);
    REQUIRE_FALSE(result.success);
    REQUIRE(result.error.contains("64"));
    REQUIRE_FALSE(destination.existsAsFile());
    REQUIRE_FALSE(juce::File(destination.getFullPathName() + ".part").existsAsFile());
}
TEST_CASE("Export names are sanitized, bounded and unique", "[export]")
{
    REQUIRE(AudioExporter::sanitizeStem("../../Kick 01:*?") == "Kick_01");
    REQUIRE(AudioExporter::sanitizeStem("") == "audio");
    REQUIRE(AudioExporter::sanitizeStem(juce::String::repeatedString("a", 200)).length() == 48);
    REQUIRE(AudioExporter::rangeSuffix(0.25, 1.1) == "0250-1100");
    TestDirectory t;
    auto source = t.wave("Kick 01.wav", 440, 1, 44100);
    const auto folder = t.dir.getChildFile("exports");
    AudioExporter exporter;
    const auto first = exporter.exportSelection(source, 0.25, 1.1, folder);
    const auto second = exporter.exportSelection(source, 0.25, 1.1, folder);
    REQUIRE(first.success); REQUIRE(second.success);
    REQUIRE(first.file.getParentDirectory() == folder);
    REQUIRE(second.file.getParentDirectory() == folder);
    REQUIRE(first.file.getFileName().startsWith("Kick_01_0250-1100_"));
    REQUIRE(second.file.getFileName().startsWith("Kick_01_0250-1100_"));
    REQUIRE(first.file != second.file);
    REQUIRE(first.file.existsAsFile()); REQUIRE(second.file.existsAsFile());
    REQUIRE_FALSE(first.file.getFileName().contains("/"));
}
TEST_CASE("Long exports run in bounded chunks and cancel cleanly", "[export]")
{
    TestDirectory t;
    auto source = t.wave("long.wav", 440, 1, 44100 * 10);
    const auto destination = t.dir.getChildFile("exports/long-out.wav");
    AudioExporter exporter;
    int checks = 0;
    const auto completed = exporter.exportRange(source, 0.0, 10.0, destination,
        [&] { ++checks; return false; });
    REQUIRE(completed.success);
    const auto frames = (juce::int64) 44100 * 10;
    REQUIRE(checks == (int) ((frames + AudioExporter::exportChunkFrames - 1) / AudioExporter::exportChunkFrames));
    const auto cancelledDestination = t.dir.getChildFile("exports/cancelled.wav");
    const auto cancelled = exporter.exportRange(source, 0.0, 10.0, cancelledDestination,
        [] { return true; });
    REQUIRE_FALSE(cancelled.success);
    REQUIRE(cancelled.error.containsIgnoreCase("cancel"));
    REQUIRE_FALSE(cancelledDestination.existsAsFile());
    REQUIRE_FALSE(juce::File(cancelledDestination.getFullPathName() + ".part").existsAsFile());
}
TEST_CASE("Selection export prepares a ready WAV in the background", "[export][worker]")
{
    TestDirectory t;
    auto source = t.wave("prep.wav", 440, 1, 44100 * 2);
    SelectionExporter exporter(t.dir.getChildFile("exports"));
    exporter.prepare(source, 0.2, 0.5);
    REQUIRE(exporter.waitUntilFinished(5000));
    const auto last = exporter.state();
    REQUIRE(last.isReady());
    REQUIRE(last.generation == 1);
    REQUIRE(last.start == Catch::Approx(0.2));
    REQUIRE(last.end == Catch::Approx(0.5));
    auto reader = readerFor(last.file); REQUIRE(reader != nullptr);
    REQUIRE(reader->lengthInSamples == 13230);
}
TEST_CASE("A newer selection supersedes an older preparation", "[export][worker]")
{
    TestDirectory t;
    auto source = t.wave("supersede.wav", 440, 1, 44100 * 2);
    SelectionExporter exporter(t.dir.getChildFile("exports"));
    exporter.prepare(source, 0.1, 0.2);
    exporter.prepare(source, 0.6, 0.9);
    REQUIRE(exporter.waitUntilFinished(5000));
    const auto last = exporter.state();
    REQUIRE(last.isReady());
    REQUIRE(last.generation == 2);
    REQUIRE(last.start == Catch::Approx(0.6));
    REQUIRE(last.end == Catch::Approx(0.9));
}
TEST_CASE("A failed preparation exposes an error and no file", "[export][worker]")
{
    TestDirectory t;
    SelectionExporter exporter(t.dir.getChildFile("exports"));
    exporter.prepare(t.dir.getChildFile("missing.wav"), 0.0, 1.0);
    REQUIRE(exporter.waitUntilFinished(5000));
    const auto last = exporter.state();
    REQUIRE(last.status == SelectionExporter::Status::failed);
    REQUIRE(last.error.isNotEmpty());
    REQUIRE(last.file == juce::File());
}
TEST_CASE("Cancelling a preparation clears its state", "[export][worker]")
{
    TestDirectory t;
    auto source = t.wave("cancel.wav", 440, 1, 44100);
    SelectionExporter exporter(t.dir.getChildFile("exports"));
    exporter.prepare(source, 0.0, 0.5);
    exporter.cancel();
    REQUIRE(exporter.waitUntilFinished(5000));
    REQUIRE(exporter.state().status == SelectionExporter::Status::idle);
}
TEST_CASE("Finalized WAV validation rejects a truncated export", "[export][finalize]")
{
    TestDirectory t;
    auto source = t.wave("finalize.wav", 440, 2, 44100);
    const auto folder = t.dir.getChildFile("exports");
    AudioExporter exporter;
    const auto result = exporter.exportRange(source, 0.0, 0.2, folder.getChildFile("final.wav"));
    REQUIRE(result.success);
    REQUIRE(AudioExporter::validateWavOutput(result.file, 8820, 44100, 2, 16, false).isEmpty());
    juce::MemoryBlock bytes;
    REQUIRE(result.file.loadFileAsData(bytes));
    REQUIRE(bytes.getSize() > 100);
    bytes.setSize(bytes.getSize() - 100);
    const auto truncated = folder.getChildFile("truncated.wav");
    REQUIRE(truncated.replaceWithData(bytes.getData(), bytes.getSize()));
    REQUIRE(AudioExporter::validateWavOutput(truncated, 8820, 44100, 2, 16, false).isNotEmpty());
}
TEST_CASE("Replacing a selection does not block the caller on an in-flight export", "[export][worker]")
{
    TestDirectory t;
    auto source = t.wave("blocking.wav", 440, 1, 44100);
    const auto folder = t.dir.getChildFile("exports");
    std::mutex gateMutex;
    std::condition_variable gate;
    bool started = false, release = false;
    SelectionExporter exporter(folder, [&](const juce::File& file, double start, double end,
                                           const juce::File& destination, CancelCheck)
    {
        {
            std::unique_lock lock(gateMutex);
            started = true;
            gate.notify_all();
            gate.wait(lock, [&] { return release; });
        }
        return AudioExporter().exportSelection(file, start, end, destination, {});
    });
    exporter.prepare(source, 0.0, 0.1);
    {
        std::unique_lock lock(gateMutex);
        REQUIRE(gate.wait_for(lock, std::chrono::seconds(5), [&] { return started; }));
    }
    std::atomic<bool> returned { false };
    std::thread replacer([&] { exporter.prepare(source, 0.5, 0.6); returned = true; });
    const auto deadline = juce::Time::getMillisecondCounter() + 500;
    while (! returned && juce::Time::getMillisecondCounter() < deadline) juce::Thread::sleep(5);
    const bool quick = returned;
    {
        std::lock_guard lock(gateMutex);
        release = true;
    }
    gate.notify_all();
    replacer.join();
    REQUIRE(quick);
    REQUIRE(exporter.waitUntilFinished(5000));
    const auto state = exporter.state();
    REQUIRE(state.generation == 2);
    REQUIRE(state.start == Catch::Approx(0.5));
    REQUIRE(state.end == Catch::Approx(0.6));
    REQUIRE(state.isReady());
}
TEST_CASE("A REX2 selection renders decoded PCM to a WAV", "[export][rex]")
{
    TestDirectory t;
    Rex2Audio audio;
    audio.channels = 2;
    audio.sampleRate = 44100;
    audio.frames = 44100;
    audio.left.resize((size_t) audio.frames);
    audio.right.resize((size_t) audio.frames);
    for (int64_t i = 0; i < audio.frames; ++i)
    {
        const auto value = 0.6f * std::sin(2.0f * juce::MathConstants<float>::pi * 120.0f
                                           * (float) i / 44100.0f);
        audio.left[(size_t) i] = value;
        audio.right[(size_t) i] = -value;
    }
    auto source = t.dir.getChildFile("loop.rx2");
    REQUIRE(source.replaceWithText("synthetic rex marker"));
    const auto folder = t.dir.getChildFile("rex-exports");
    const auto result = AudioExporter().exportRexSelection(audio, source, 0.25, 0.5, folder);
    REQUIRE(result.success);
    auto reader = readerFor(result.file);
    REQUIRE(reader != nullptr);
    REQUIRE(reader->lengthInSamples == 11025);
    REQUIRE(reader->numChannels == 2);
    REQUIRE(reader->sampleRate == Catch::Approx(44100.0));
    REQUIRE(reader->bitsPerSample == 24);
    juce::AudioBuffer<float> first(2, 1);
    REQUIRE(reader->read(first.getArrayOfWritePointers(), 2, 0, 1));
    REQUIRE(first.getSample(0, 0) > 0.0f);
    REQUIRE(first.getSample(1, 0) < 0.0f);
}
TEST_CASE("A per-request exporter renders a REX selection in the background", "[export][rex]")
{
    TestDirectory t;
    Rex2Audio audio;
    audio.channels = 1;
    audio.sampleRate = 44100;
    audio.frames = 44100;
    audio.left.assign((size_t) audio.frames, 0.25f);
    auto source = t.dir.getChildFile("worker.rx2");
    REQUIRE(source.replaceWithText("synthetic rex marker"));
    const auto folder = t.dir.getChildFile("rex-worker-exports");
    SelectionExporter exporter(folder);
    exporter.prepare(source, 0.1, 0.3,
        [audio](const juce::File& file, double start, double end,
                const juce::File& destination, CancelCheck cancel)
        {
            return AudioExporter().exportRexSelection(audio, file, start, end, destination, cancel);
        });
    REQUIRE(exporter.waitUntilFinished(5000));
    const auto state = exporter.state();
    REQUIRE(state.isReady());
    auto reader = readerFor(state.file);
    REQUIRE(reader != nullptr);
    REQUIRE(reader->lengthInSamples == 8820);
    REQUIRE(reader->numChannels == 1);
}

TEST_CASE("Range export applies an optional linear fade at both ends", "[export][fade]")
{
    TestDirectory t;
    auto source = t.dir.getChildFile("steady.wav");
    source.replaceWithText("");
    {
        std::unique_ptr<juce::OutputStream> stream(source.createOutputStream());
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}
            .withSampleRate(44100.0).withNumChannels(1).withBitsPerSample(16));
        juce::AudioBuffer<float> constant(1, 44100);
        constant.clear();
        for (int i = 0; i < constant.getNumSamples(); ++i) constant.setSample(0, i, 0.5f);
        writer->writeFromAudioSampleBuffer(constant, 0, constant.getNumSamples());
    }
    auto out = t.dir.getChildFile("faded.wav");
    AudioExporter exporter;
    REQUIRE(exporter.exportRange(source, 0.0, 1.0, out, {}, 0.01).success);
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(out));
    REQUIRE(reader != nullptr);
    juce::AudioBuffer<float> first(1, 1), mid(1, 1), last(1, 1);
    auto readOne = [&](juce::AudioBuffer<float>& buffer, juce::int64 position)
    {
        reader->read(&buffer, 0, 1, position, true, true);
    };
    readOne(first, 0);
    readOne(mid, 22050);
    readOne(last, reader->lengthInSamples - 1);
    REQUIRE(std::abs(first.getSample(0, 0)) < 0.01f);
    REQUIRE(std::abs(last.getSample(0, 0)) < 0.01f);
    REQUIRE(std::abs(mid.getSample(0, 0)) > 0.45f);
}

TEST_CASE("Silence detection sees every channel and keeps tiny non-zero audio", "[export][extract]")
{
    SampleAudio mono;
    mono.sampleRate = 44100; mono.channels = 1; mono.frames = 4;
    mono.left = { 0.0f, 0.0f, 0.0f, 0.0f };
    REQUIRE(AudioExporter::isEntirelySilent(mono));
    mono.left[2] = 1.0e-7f;
    REQUIRE_FALSE(AudioExporter::isEntirelySilent(mono));

    SampleAudio stereo;
    stereo.sampleRate = 44100; stereo.channels = 2; stereo.frames = 3;
    stereo.left = { 0.25f, 0.0f, 0.0f };
    stereo.right = { 0.0f, 0.0f, 0.0f };
    REQUIRE_FALSE(AudioExporter::isEntirelySilent(stereo));
    stereo.left.assign(3, 0.0f);
    REQUIRE(AudioExporter::isEntirelySilent(stereo));
}

TEST_CASE("Embedded sample export writes WAV with and without peak normalisation", "[export][extract]")
{
    TestDirectory t;
    SampleAudio audio;
    audio.sampleRate = 44100; audio.channels = 2; audio.bitsPerSample = 16;
    audio.frames = 4;
    audio.left = { 0.1f, -0.2f, 0.05f, 0.0f };
    audio.right = { 0.2f, -0.4f, 0.1f, 0.0f };
    AudioExporter exporter;
    const auto plain = t.dir.getChildFile("export/sample.wav");
    auto result = exporter.exportSampleFile(audio, plain, false);
    REQUIRE(result.success); REQUIRE(result.file == plain); REQUIRE(result.error.isEmpty());
    REQUIRE(result.file.getParentDirectory().isDirectory());
    auto reader = readerFor(plain);
    REQUIRE(reader != nullptr);
    REQUIRE(reader->numChannels == 2);
    REQUIRE(reader->bitsPerSample == 16);
    REQUIRE(reader->lengthInSamples == 4);
    juce::AudioBuffer<float> actual(2, 4);
    REQUIRE(reader->read(&actual, 0, 4, 0, true, true));
    REQUIRE(actual.getSample(0, 1) == Catch::Approx(-0.2f).margin(1.0 / 32768.0));
    REQUIRE(actual.getSample(1, 1) == Catch::Approx(-0.4f).margin(1.0 / 32768.0));

    const auto normalised = t.dir.getChildFile("export/normalised.wav");
    result = exporter.exportSampleFile(audio, normalised, true);
    REQUIRE(result.success);
    auto normReader = readerFor(normalised);
    REQUIRE(normReader != nullptr);
    juce::AudioBuffer<float> norm(2, 4);
    REQUIRE(normReader->read(&norm, 0, 4, 0, true, true));
    const float target = std::pow(10.0f, -0.1f / 20.0f);
    REQUIRE(norm.getMagnitude(0, 4) == Catch::Approx(target).margin(1.5f / 32768.0f));
    REQUIRE(norm.getSample(0, 1) == Catch::Approx(-0.5f * target).margin(1.5f / 32768.0f));
    REQUIRE(norm.getSample(1, 1) == Catch::Approx(-1.0f * target).margin(1.5f / 32768.0f));
}

TEST_CASE("Embedded sample export never overwrites and cancels without a partial file", "[export][extract]")
{
    TestDirectory t;
    SampleAudio audio;
    audio.sampleRate = 44100; audio.channels = 1; audio.frames = 3;
    audio.left = { 0.1f, 0.2f, 0.3f };
    AudioExporter exporter;
    const auto destination = t.dir.getChildFile("sample.wav");
    destination.replaceWithText("occupied");
    auto result = exporter.exportSampleFile(audio, destination, false);
    REQUIRE_FALSE(result.success);
    REQUIRE(destination.loadFileAsString() == "occupied");
    REQUIRE_FALSE(juce::File(destination.getFullPathName() + ".part").existsAsFile());

    const auto cancelled = t.dir.getChildFile("cancelled.wav");
    result = exporter.exportSampleFile(audio, cancelled, false, [] { return true; });
    REQUIRE_FALSE(result.success);
    REQUIRE(result.error.containsIgnoreCase("cancel"));
    REQUIRE_FALSE(cancelled.existsAsFile());
    REQUIRE_FALSE(juce::File(cancelled.getFullPathName() + ".part").existsAsFile());
}

TEST_CASE("A mid-write cancellation closes the stream and removes the part", "[export][extract]")
{
    TestDirectory t;
    SampleAudio audio;
    audio.sampleRate = 44100; audio.channels = 1;
    audio.frames = AudioExporter::exportChunkFrames * 3 + 17;
    audio.left.assign((size_t) audio.frames, 0.25f);
    const auto destination = t.dir.getChildFile("mid-cancel.wav");
    int calls = 0;
    const auto result = AudioExporter().exportSampleFile(audio, destination, false,
        [&] { return ++calls >= 3; });
    REQUIRE_FALSE(result.success);
    REQUIRE(result.error.containsIgnoreCase("cancel"));
    REQUIRE_FALSE(destination.existsAsFile());
    REQUIRE_FALSE(juce::File(destination.getFullPathName() + ".part").existsAsFile());
}
