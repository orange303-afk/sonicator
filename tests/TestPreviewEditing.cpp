#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "ui/preview/PreviewPanel.h"
#include "TestHelpers.h"

using namespace sonicator;
using Catch::Approx;

namespace
{
juce::File writeWav(const juce::File& file, int channels, int frames)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    juce::WavAudioFormat wav;
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate(44100.0)
        .withNumChannels(channels).withBitsPerSample(16);
    auto writer = wav.createWriterFor(stream, options);
    if (writer == nullptr) return {};
    juce::AudioBuffer<float> buffer(channels, frames);
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < frames; ++i)
            buffer.setSample(c, i, 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f
                                                    * (float) i / 44100.0f));
    writer->writeFromAudioSampleBuffer(buffer, 0, frames);
    return file;
}
} // namespace

TEST_CASE("An ordinary WAV becomes editable and dirty after a delete", "[ui][edit]")
{
    TestDirectory t;
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"), t.dir.getChildFile("exports"));
    REQUIRE(preview.loadFile(t.wave("a.wav"), false));
    REQUIRE(preview.sourceIsEditable());
    preview.selectRange(0.1, 0.2);
    preview.applyEditCommand(PreviewPanel::EditCommand::deleteRegion);
    REQUIRE(preview.hasUnsavedEdits());
    REQUIRE(preview.waveformLength() == Approx(0.9).margin(0.01));
}

TEST_CASE("Undo clears the dirty state", "[ui][edit]")
{
    TestDirectory t;
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"), t.dir.getChildFile("exports"));
    REQUIRE(preview.loadFile(t.wave("a.wav"), false));
    preview.selectRange(0.1, 0.2);
    preview.applyEditCommand(PreviewPanel::EditCommand::deleteRegion);
    REQUIRE(preview.hasUnsavedEdits());
    preview.applyEditCommand(PreviewPanel::EditCommand::undo);
    REQUIRE_FALSE(preview.hasUnsavedEdits());
    REQUIRE(preview.waveformLength() == Approx(1.0).margin(0.01));
}

TEST_CASE("A source with more than two channels is not editable", "[ui][edit]")
{
    TestDirectory t;
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"), t.dir.getChildFile("exports"));
    preview.loadFile(writeWav(t.dir.getChildFile("three.wav"), 3, 44100), false);
    REQUIRE_FALSE(preview.sourceIsEditable());
    REQUIRE_FALSE(preview.hasUnsavedEdits());
}

TEST_CASE("An empty panel is not editable", "[ui][edit]")
{
    TestDirectory t;
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"), t.dir.getChildFile("exports"));
    preview.clearSource();
    REQUIRE_FALSE(preview.sourceIsEditable());
}

TEST_CASE("Saving a copy writes a new file beside the original and reports it", "[ui][edit]")
{
    TestDirectory t;
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"), t.dir.getChildFile("exports"));
    REQUIRE(preview.loadFile(t.wave("a.wav"), false));
    preview.selectRange(0.2, 0.6);
    preview.applyEditCommand(PreviewPanel::EditCommand::crop);
    REQUIRE(preview.hasUnsavedEdits());
    juce::File saved;
    int64_t reportedId = -1;
    preview.editingSampleId = 42;
    preview.onCopySaved = [&](const juce::File& file, int64_t id) { saved = file; reportedId = id; };
    preview.saveEditsAsCopyForTest();
    REQUIRE(saved.existsAsFile());
    REQUIRE(saved.getFileName().startsWith("a edit"));
    REQUIRE(reportedId == 42);
    REQUIRE_FALSE(preview.hasUnsavedEdits());
    REQUIRE(preview.waveformLength() == Approx(0.4).margin(0.01));
}

TEST_CASE("Overwriting refreshes from the file and clears dirty", "[ui][edit]")
{
    TestDirectory t;
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"), t.dir.getChildFile("exports"));
    const auto file = t.wave("b.wav");
    REQUIRE(preview.loadFile(file, false));
    preview.selectRange(0.0, 0.5);
    preview.applyEditCommand(PreviewPanel::EditCommand::crop);
    juce::File reported;
    preview.onFileSavedInPlace = [&](const juce::File& target, int64_t) { reported = target; };
    preview.overwriteEditsForTest();
    REQUIRE(reported == file);
    REQUIRE_FALSE(preview.hasUnsavedEdits());
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    REQUIRE(reader != nullptr);
    REQUIRE(reader->lengthInSamples == 22050);
}

TEST_CASE("Loading another source while dirty is refused", "[ui][edit]")
{
    TestDirectory t;
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"), t.dir.getChildFile("exports"));
    REQUIRE(preview.loadFile(t.wave("a.wav"), false));
    preview.selectRange(0.1, 0.2);
    preview.applyEditCommand(PreviewPanel::EditCommand::deleteRegion);
    REQUIRE(preview.hasUnsavedEdits());
    REQUIRE_FALSE(preview.loadFile(t.wave("b.wav"), false));
    REQUIRE(preview.hasUnsavedEdits());
}

TEST_CASE("Discarding restores the source length", "[ui][edit]")
{
    TestDirectory t;
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"), t.dir.getChildFile("exports"));
    REQUIRE(preview.loadFile(t.wave("a.wav"), false));
    preview.selectRange(0.1, 0.2);
    preview.applyEditCommand(PreviewPanel::EditCommand::deleteRegion);
    REQUIRE(preview.hasUnsavedEdits());
    preview.discardEditsForTest();
    REQUIRE_FALSE(preview.hasUnsavedEdits());
    REQUIRE(preview.waveformLength() == Approx(1.0).margin(0.01));
}

TEST_CASE("The leave hook proceeds immediately when nothing is unsaved", "[ui][edit]")
{
    TestDirectory t;
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"), t.dir.getChildFile("exports"));
    bool ran = false;
    bool proceeded = false;
    preview.withUnsavedEditsConfirmed([&](bool ok) { ran = true; proceeded = ok; });
    REQUIRE(ran);
    REQUIRE(proceeded);
}

namespace
{
juce::int64 fileFrames(const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    return reader == nullptr ? -1 : reader->lengthInSamples;
}
} // namespace

TEST_CASE("A clean edit session does not survive a source change", "[ui][edit]")
{
    TestDirectory t;
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"), t.dir.getChildFile("exports"));
    const auto a = t.wave("a.wav");
    REQUIRE(preview.loadFile(a, false));
    // Convert to mono is a no-op on a mono file: it may open a buffer but must leave it clean.
    preview.applyEditCommand(PreviewPanel::EditCommand::toMono);
    REQUIRE_FALSE(preview.hasUnsavedEdits());
    const auto b = t.wave("b.wav");
    REQUIRE(preview.loadFile(b, false));
    preview.selectRange(0.0, 0.5);
    preview.applyEditCommand(PreviewPanel::EditCommand::deleteRegion);
    REQUIRE(preview.hasUnsavedEdits());
    preview.overwriteEditsForTest();
    REQUIRE(fileFrames(a) == 44100);
    REQUIRE(fileFrames(b) == 22050);
}

TEST_CASE("Discard restores the original audio before the next edit", "[ui][edit]")
{
    TestDirectory t;
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"), t.dir.getChildFile("exports"));
    REQUIRE(preview.loadFile(t.wave("a.wav"), false));
    preview.selectRange(0.1, 0.2);
    preview.applyEditCommand(PreviewPanel::EditCommand::deleteRegion);
    REQUIRE(preview.waveformLength() == Approx(0.9).margin(0.01));
    preview.discardEditsForTest();
    preview.selectRange(0.0, 0.5);
    preview.applyEditCommand(PreviewPanel::EditCommand::deleteRegion);
    // 0.5 s only if the discarded 0.9 s state is gone; otherwise it would be 0.45 s.
    REQUIRE(preview.waveformLength() == Approx(0.5).margin(0.01));
}

TEST_CASE("Delete clears the selection and a mono file cannot be converted", "[ui][edit]")
{
    TestDirectory t;
    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"), t.dir.getChildFile("exports"));
    REQUIRE(preview.loadFile(t.wave("a.wav"), false));
    preview.selectRange(0.1, 0.2);
    preview.applyEditCommand(PreviewPanel::EditCommand::deleteRegion);
    REQUIRE_FALSE(preview.selectedRange().valid());
    preview.applyEditCommand(PreviewPanel::EditCommand::toMono);
    REQUIRE(preview.waveformLength() == Approx(0.9).margin(0.01));
}
