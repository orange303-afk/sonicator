#include <catch2/catch_test_macros.hpp>
#include "core/batch/BatchSources.h"
#include "core/database/SampleRepository.h"
#include "ui/batch/BatchProcessingDialog.h"
#include "ui/MainComponent.h"
#include "core/settings/AppSettings.h"
#include "TestHelpers.h"

using namespace sonicator;
using namespace sonicator::batch;

namespace
{
SampleRecord recordFor(const juce::File& file)
{
    SampleRecord record;
    record.filePath = file.getFullPathName();
    record.fileName = file.getFileName();
    record.format = "WAV";
    record.sampleRate = 44100;
    record.numChannels = 1;
    record.bitsPerSample = 16;
    record.duration = 1.0;
    record.version = basicFeaturesVersion;
    return record;
}

juce::File floatWav(const juce::File& file, const std::vector<float>& samples)
{
    file.deleteFile();
    file.getParentDirectory().createDirectory();
    juce::WavAudioFormat format;
    auto stream = std::unique_ptr<juce::OutputStream>(file.createOutputStream());
    REQUIRE(stream != nullptr);
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate(44100.0).withNumChannels(1)
        .withBitsPerSample(32)
        .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = format.createWriterFor(stream, options);
    REQUIRE(writer != nullptr);
    const float* channels[1] { samples.data() };
    REQUIRE(writer->writeFromFloatArrays(channels, 1, (int) samples.size()));
    writer.reset();
    stream.reset();
    return file;
}

template <typename T>
T* child(juce::Component& root, const juce::String& id)
{
    return dynamic_cast<T*>(findDescendantWithID(root, id));
}
} // namespace

TEST_CASE("Batch dialog turns its controls into the exact request recipe", "[batch][batch-dialog]")
{
    TestDirectory t;
    const auto lib = t.dir.getChildFile("Library");
    lib.createDirectory();
    const auto a = t.wave("Library/a.wav");
    const auto b = t.wave("Library/b.wav", 220.0);
    Database db(t.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    repo.addRoot(lib.getFullPathName());
    const auto idA = repo.upsert(recordFor(a));
    const auto idB = repo.upsert(recordFor(b));
    Request request;
    request.sources.rows = { idA, idB };
    request.recipe.outputs = { OutputPreset { Format::wav, Encoding::int24, 5 } };
    const auto prepared = prepareBatch(repo, request, {});
    REQUIRE(prepared.ok);

    BatchProcessingDialog dialog(prepared.batch, t.dir.getChildFile("settings/batch-presets.json"));
    REQUIRE(dialog.expectedOutputs() == 2);
    REQUIRE(dialog.canStart());
    REQUIRE(dialog.currentRecipe().outputs.size() == 1);
    REQUIRE(dialog.currentRecipe().outputs[0].format == Format::wav);
    REQUIRE(dialog.currentRecipe().outputs[0].encoding == Encoding::int24);
    REQUIRE(dialog.validationMessage().isEmpty());

    auto* wav = child<juce::ToggleButton>(dialog, "batchFormatWav");
    auto* flac = child<juce::ToggleButton>(dialog, "batchFormatFlac");
    auto* flacDepth = child<juce::ComboBox>(dialog, "batchDepthFlac");
    REQUIRE(wav != nullptr);
    REQUIRE(flac != nullptr);
    REQUIRE(flacDepth != nullptr);
    wav->setToggleState(false, juce::dontSendNotification);
    wav->onClick();
    flac->setToggleState(true, juce::dontSendNotification);
    flac->onClick();
    REQUIRE(dialog.expectedOutputs() == 2);
    const auto recipe = dialog.currentRecipe();
    REQUIRE(recipe.outputs.size() == 1);
    REQUIRE(recipe.outputs[0].format == Format::flac);
    REQUIRE(recipe.outputs[0].encoding == Encoding::int16);

    auto* conflict = child<juce::ComboBox>(dialog, "batchConflict");
    REQUIRE(conflict != nullptr);
    conflict->setSelectedId(2);
    conflict->onChange();
    REQUIRE(dialog.conflictPolicy() == ConflictPolicy::skip);

    auto* mp3 = child<juce::ToggleButton>(dialog, "batchFormatMp3");
    auto* bitrate = child<juce::ComboBox>(dialog, "batchMp3Bitrate");
    REQUIRE(mp3 != nullptr);
    REQUIRE(bitrate != nullptr);
    mp3->setToggleState(true, juce::dontSendNotification);
    mp3->onClick();
    auto* mode = child<juce::ComboBox>(dialog, "batchMp3Mode");
    auto* quality = child<juce::ComboBox>(dialog, "batchMp3VbrQuality");
    auto* ogg = child<juce::ToggleButton>(dialog, "batchFormatOgg");
    auto* oggQuality = child<juce::ComboBox>(dialog, "batchOggQuality");
    REQUIRE(mode != nullptr);
    REQUIRE(quality != nullptr);
    REQUIRE(ogg != nullptr);
    REQUIRE(oggQuality != nullptr);
    mode->setSelectedId(2);
    mode->onChange();
    quality->setSelectedId(6);
    quality->onChange();
    ogg->setToggleState(true, juce::dontSendNotification);
    ogg->onClick();
    oggQuality->setSelectedId(10);
    oggQuality->onChange();
    const auto withMp3 = dialog.currentRecipe();
    bool mp3Found = false;
    for (const auto& output : withMp3.outputs)
        if (output.format == Format::mp3)
        {
            mp3Found = true;
            REQUIRE(output.mp3Mode == Mp3Mode::vbr);
            REQUIRE(output.mp3VbrQuality == 5);
            REQUIRE(output.encoding == Encoding::int16);
        }
    REQUIRE(mp3Found);
    bool oggFound = false;
    for (const auto& output : withMp3.outputs)
        if (output.format == Format::ogg)
        {
            oggFound = true;
            REQUIRE(output.oggQuality == 9);
        }
    REQUIRE(oggFound);

    // Loading a built-in preset replaces the step list and is serialized back in order.
    auto* preset = child<juce::ComboBox>(dialog, "batchPreset");
    REQUIRE(preset != nullptr);
    preset->setSelectedId(2);
    preset->onChange();
    const auto cleanup = dialog.currentRecipe();
    REQUIRE(cleanup.steps.size() == 4);
    REQUIRE(std::holds_alternative<RemoveDc>(cleanup.steps[0].parameters));
    REQUIRE(std::holds_alternative<TrimSilence>(cleanup.steps[1].parameters));
    REQUIRE(std::holds_alternative<Normalize>(cleanup.steps[2].parameters));
    REQUIRE(std::holds_alternative<FadeOut>(cleanup.steps[3].parameters));
    REQUIRE_FALSE(cleanup.allowIntegerClipping);
}

TEST_CASE("Batch dialog disables Start for an unusable source/format pair", "[batch][batch-dialog]")
{
    TestDirectory t;
    const auto lib = t.dir.getChildFile("Library");
    lib.createDirectory();
    const auto source = floatWav(lib.getChildFile("float.wav"), { 0.25f, -0.25f });
    Database db(t.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    repo.addRoot(lib.getFullPathName());
    const auto id = repo.upsert(recordFor(source));
    Request request;
    request.sources.rows = { id };
    request.recipe.outputs = { OutputPreset { Format::wav, Encoding::sameAsSource, 5 } };
    const auto prepared = prepareBatch(repo, request, {});
    REQUIRE(prepared.ok);
    BatchProcessingDialog dialog(prepared.batch, t.dir.getChildFile("settings/batch-presets.json"));

    auto* wav = child<juce::ToggleButton>(dialog, "batchFormatWav");
    auto* aiff = child<juce::ToggleButton>(dialog, "batchFormatAiff");
    auto* depth = child<juce::ComboBox>(dialog, "batchDepthAiff");
    REQUIRE(wav != nullptr);
    REQUIRE(aiff != nullptr);
    REQUIRE(depth != nullptr);
    wav->setToggleState(false, juce::dontSendNotification);
    wav->onClick();
    aiff->setToggleState(true, juce::dontSendNotification);
    aiff->onClick();
    depth->setSelectedId(1); // Same as source is float, and AIFF cannot store float.
    depth->onChange();
    REQUIRE_FALSE(dialog.canStart());
    REQUIRE(dialog.validationMessage().contains("float"));
}

TEST_CASE("Batch commands are registered without default hotkeys", "[batch][batch-integration]")
{
    TestDirectory t;
    AppSettings::setTestSettingsDirectory(t.dir.getChildFile("settings"));
    MainComponent component(t.dir.getChildFile("catalog.sqlite"), false, {}, t.dir.getChildFile("settings/settings.json"));
    const auto& registry = component.commandRegistry();
    REQUIRE(registry.contains("library.batchProcess"));
    REQUIRE(registry.contains("folders.batchProcess"));
    const auto* libraryCommand = registry.find("library.batchProcess");
    const auto* folderCommand = registry.find("folders.batchProcess");
    REQUIRE(libraryCommand != nullptr);
    REQUIRE(folderCommand != nullptr);
    REQUIRE(libraryCommand->defaults.empty());
    REQUIRE(folderCommand->defaults.empty());
    REQUIRE_FALSE(registry.state("library.batchProcess").canRun());
    REQUIRE_FALSE(registry.state("folders.batchProcess").canRun());
    AppSettings::setTestSettingsDirectory({});
}

TEST_CASE("A running batch freezes configuration and preview controls", "[batch][batch-dialog]")
{
    TestDirectory t;
    const auto lib = t.dir.getChildFile("Library");
    lib.createDirectory();
    const auto source = t.wave("Library/run.wav");
    Database db(t.dir.getChildFile("catalog.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    repo.addRoot(lib.getFullPathName());
    const auto id = repo.upsert(recordFor(source));
    Request request;
    request.sources.rows = { id };
    const auto prepared = prepareBatch(repo, request, {});
    REQUIRE(prepared.ok);
    BatchProcessingDialog dialog(prepared.batch, t.dir.getChildFile("settings/batch-presets.json"));
    dialog.setRunning(true);
    REQUIRE_FALSE(child<juce::TextButton>(dialog, "batchStart")->isEnabled());
    REQUIRE_FALSE(child<juce::TextButton>(dialog, "batchPreparePreview")->isEnabled());
    REQUIRE_FALSE(child<juce::ToggleButton>(dialog, "batchFormatFlac")->isEnabled());
    REQUIRE(child<juce::TextButton>(dialog, "batchCancel")->isEnabled());
    dialog.setRunning(false);
    REQUIRE(child<juce::TextButton>(dialog, "batchStart")->isEnabled());
}
