#include <catch2/catch_test_macros.hpp>
#include "core/batch/BatchPresetStore.h"
#include "TestHelpers.h"

using namespace sonicator;
using namespace sonicator::batch;

namespace
{
BatchPreset customPreset()
{
    BatchPreset preset;
    preset.name = "Sample prep";
    preset.conflicts = ConflictPolicy::skip;
    preset.recipe.steps = {
        { true, Gain { 3.0 } },
        { false, TrimSilence { -50.0, 12.0 } },
        { true, RemoveDc {} },
        { true, FadeIn { 25.0, true } },
        { true, Gain { -2.5 } },
    };
    OutputPreset mp3 { Format::mp3, Encoding::int16, 5 };
    mp3.mp3Mode = Mp3Mode::vbr;
    mp3.mp3BitrateKbps = 320;
    mp3.mp3VbrQuality = 1;
    OutputPreset ogg { Format::ogg, Encoding::int16, 5 };
    ogg.oggQuality = 9;
    preset.recipe.outputs = { OutputPreset { Format::wav, Encoding::int24, 5 },
                              OutputPreset { Format::flac, Encoding::int16, 8 }, mp3, ogg };
    preset.recipe.sampleRate = 48000;
    preset.recipe.channels = ChannelMode::mono;
    preset.recipe.dither = DitherMode::tpdf;
    preset.recipe.clipping = ClippingPolicy::clipInteger;
    preset.recipe.allowIntegerClipping = true;
    return preset;
}
} // namespace

TEST_CASE("Presets round-trip every recipe field and stay independent of sources",
          "[batch][batch-presets]")
{
    TestDirectory t;
    const auto file = t.dir.getChildFile("settings/batch-presets.json");
    BatchPresetStore store(file);
    const auto preset = customPreset();
    REQUIRE(store.save(preset).ok);

    const auto loaded = store.load();
    REQUIRE(loaded.ok);
    REQUIRE(loaded.presets.size() == 3); // two built-ins and one custom
    const BatchPreset* found = nullptr;
    for (const auto& candidate : loaded.presets)
        if (candidate.name == "Sample prep") found = &candidate;
    REQUIRE(found != nullptr);
    REQUIRE(found->recipe == preset.recipe);
    REQUIRE(found->conflicts == ConflictPolicy::skip);
    REQUIRE_FALSE(found->builtIn);

    const auto json = file.loadFileAsString();
    REQUIRE(json.contains("schemaVersion"));
    REQUIRE_FALSE(json.contains("source"));
    REQUIRE_FALSE(json.contains("destination"));

    REQUIRE(loaded.presets[0].id == "neutral");
    REQUIRE(loaded.presets[1].id == "sample-cleanup");
    REQUIRE(loaded.presets[1].recipe.steps.size() == 4);
    REQUIRE(loaded.presets[1].builtIn);
}

TEST_CASE("Built-ins are immutable and duplicate names need an explicit replace",
          "[batch][batch-presets]")
{
    TestDirectory t;
    const auto file = t.dir.getChildFile("settings/batch-presets.json");
    BatchPresetStore store(file);
    auto builtIn = BatchPresetStore::builtIns().front();
    builtIn.recipe.steps.push_back({ true, Gain { 1.0 } });
    REQUIRE_FALSE(store.save(builtIn).ok);
    REQUIRE_FALSE(store.remove("sample-cleanup").ok);

    auto preset = customPreset();
    REQUIRE(store.save(preset).ok);
    preset.recipe.steps = { { true, Gain { -6.0 } } };
    auto duplicate = store.save(preset);
    REQUIRE_FALSE(duplicate.ok);
    REQUIRE(duplicate.needsReplace);

    const auto replaced = store.save(preset, true);
    REQUIRE(replaced.ok);
    const auto loaded = store.load();
    int matches = 0;
    for (const auto& candidate : loaded.presets)
        if (candidate.name == preset.name)
        {
            ++matches;
            REQUIRE(candidate.recipe.steps.size() == 1);
        }
    REQUIRE(matches == 1);

    REQUIRE(store.remove(loaded.presets.back().id).ok);
    REQUIRE(store.load().presets.size() == 2);
}

TEST_CASE("A corrupt or future preset file is reported and never overwritten",
          "[batch][batch-presets]")
{
    TestDirectory t;
    const auto folder = t.dir.getChildFile("settings");
    folder.createDirectory();
    const auto file = folder.getChildFile("batch-presets.json");
    file.replaceWithText("{ this is not json");
    BatchPresetStore store(file);
    auto loaded = store.load();
    REQUIRE_FALSE(loaded.ok);
    REQUIRE(loaded.presets.size() == 2);
    REQUIRE(file.loadFileAsString() == "{ this is not json");
    REQUIRE_FALSE(store.save(customPreset()).ok);
    REQUIRE(file.loadFileAsString() == "{ this is not json");

    file.replaceWithText("{\"schemaVersion\":99,\"presets\":[]}");
    loaded = store.load();
    REQUIRE_FALSE(loaded.ok);
    REQUIRE(loaded.readOnly);
    REQUIRE_FALSE(store.save(customPreset()).ok);
    REQUIRE(file.loadFileAsString().contains("99"));
}

TEST_CASE("A save failure keeps the previous preset file", "[batch][batch-presets]")
{
    TestDirectory t;
    const auto folder = t.dir.getChildFile("settings");
    folder.createDirectory();
    const auto file = folder.getChildFile("batch-presets.json");
    BatchPresetStore store(file);
    REQUIRE(store.save(customPreset()).ok);
    BatchPreset healthy = customPreset();
    healthy.name = "Keep me";
    healthy.recipe.steps = { { true, Normalize { -1.0 } } };
    REQUIRE(store.save(healthy).ok);
    const auto before = file.loadFileAsString();
    // An unknown step type in the stored document must be reported, not silently dropped.
    file.replaceWithText(before.replace("\"gain\"", "\"futureStep\""));
    const auto loaded = store.load();
    // One unusable preset is reported, the healthy presets stay readable and the file is intact.
    REQUIRE(loaded.ok);
    REQUIRE(loaded.error.contains("futureStep"));
    REQUIRE(loaded.presets.size() == 3);
    REQUIRE(file.loadFileAsString().contains("futureStep"));
}

TEST_CASE("An invalid recipe is refused before it can corrupt the preset file",
          "[batch][batch-presets]")
{
    TestDirectory t;
    const auto file = t.dir.getChildFile("settings/batch-presets.json");
    BatchPresetStore store(file);
    REQUIRE(store.save(customPreset()).ok);
    const auto before = file.loadFileAsString();
    BatchPreset invalid = customPreset();
    invalid.name = "Broken";
    invalid.recipe.outputs.clear();
    invalid.recipe.steps = { { true, Gain { 100.0 } } };
    const auto saved = store.save(invalid);
    REQUIRE_FALSE(saved.ok);
    REQUIRE(file.loadFileAsString() == before);
    const auto loaded = store.load();
    REQUIRE(loaded.ok);
    REQUIRE(loaded.presets.size() == 3);
}
