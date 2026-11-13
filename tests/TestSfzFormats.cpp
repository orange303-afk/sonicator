#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/audio/AudioEngine.h"
#include "core/sampler/ExternalKitSampler.h"
#include "core/sampler/FileInstrument.h"
#include "core/scanner/FileScanner.h"
#include "core/smart/AssetAnalyzer.h"
#include "ui/preview/PreviewPanel.h"
#include "TestHelpers.h"
#include <algorithm>
#include <iostream>
#include <vector>
using namespace sonicator;

namespace {
juce::File writeSfz(TestDirectory& t, const juce::String& relative, const juce::String& text)
{
    auto file = t.dir.getChildFile(relative);
    file.getParentDirectory().createDirectory();
    REQUIRE(file.replaceWithText(text));
    return file;
}
const char* const twoRegionSfz =
    "<control> default_path=Samples\\\n"
    "<master> volume=-3 pan=-20\n"
    "<group> lovel=40 hivel=100\n"
    "<region> sample=Kick.wav lokey=36 hikey=40 pitch_keycenter=36 "
    "end=999 loop_start=100 loop_end=500 loopmode=loop_continuous\n"
    "<region> sample=Snare.wav lokey=41 hikey=41 pitch_keycenter=43 lovel=0 hivel=39 "
    "tune=25 transpose=2\n";
} // namespace

TEST_CASE("SFZ regions map keys, velocity, loops and inherited values", "[sfz][sampler]")
{
    TestDirectory t;
    t.wave("Kit/Samples/Kick.wav", 220, 1, 1000);
    t.wave("Kit/Samples/Snare.wav", 660, 1, 1000);
    const auto file = writeSfz(t, "Kit/Kit.sfz", twoRegionSfz);

    FileInstrument instrument;
    juce::String error;
    REQUIRE(loadFileInstrument(file, FileInstrumentFormat::sfz, instrument, error));
    REQUIRE(error.isEmpty());
    REQUIRE(instrument.name == "Kit");
    REQUIRE(instrument.sources.size() == 2);
    REQUIRE(instrument.sources[0].name == "Kick.wav");
    REQUIRE(instrument.sources[1].name == "Snare.wav");
    REQUIRE(instrument.sources[0].frameCount == 1000);
    REQUIRE(instrument.zones.size() == 2);

    const auto& kick = instrument.zones[0];
    CHECK(kick.keyLow == 36);
    CHECK(kick.keyHigh == 40);
    CHECK(kick.velocityLow == 40);
    CHECK(kick.velocityHigh == 100);
    CHECK(kick.rootKey == 36);
    CHECK(kick.sampleIndex == 0);
    CHECK(kick.start == 0);
    CHECK(kick.end == 1000); // SFZ `end` is the last frame played, inclusive
    CHECK(kick.looping);
    CHECK(kick.loopStart == 100);
    CHECK(kick.loopEnd == 501);
    CHECK(kick.attenuationDb == Catch::Approx(3.0));
    CHECK(kick.pan == Catch::Approx(-0.2));

    const auto& snare = instrument.zones[1];
    CHECK(snare.keyLow == 41);
    CHECK(snare.keyHigh == 41);
    CHECK(snare.velocityLow == 0);
    CHECK(snare.velocityHigh == 39);
    CHECK(snare.rootKey == 43);
    CHECK(snare.end == 1000);
    CHECK_FALSE(snare.looping);
    CHECK(snare.tuningCents == Catch::Approx(225.0)); // tune + transpose * 100
    CHECK(snare.attenuationDb == Catch::Approx(3.0));

    ExternalKitSampler sampler;
    REQUIRE(sampler.load(instrument, error));
    sampler.prepare(44100);
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.7f), 0);
    sampler.render(buffer, 0, 512, midi);
    CHECK(sampler.activeVoices() == 1);
    CHECK(buffer.getMagnitude(0, 512) > 0.001f);
    ExternalKitSampler outside;
    REQUIRE(outside.load(instrument, error));
    outside.prepare(44100);
    juce::MidiBuffer outOfRange;
    outOfRange.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    outside.render(buffer, 0, 512, outOfRange);
    CHECK(outside.activeVoices() == 0); // note outside every region is silent
}

TEST_CASE("SFZ group and master inheritance reach nested default paths", "[sfz][sampler]")
{
    TestDirectory t;
    t.wave("Packs/Drums/Samples/Sub/Hit.wav", 220, 1, 500);
    const auto text =
        "<master> default_path=Packs\\Drums\\Samples\\ volume=-6\n"
        "<group> lovel=10 hivel=90 pan=50\n"
        "<region> sample=Sub\\Hit.wav lokey=60 hikey=64 key=62\n";
    const auto file = writeSfz(t, "Packs/Drums/Kit.sfz", text);

    FileInstrument instrument;
    juce::String error;
    REQUIRE(loadFileInstrument(file, FileInstrumentFormat::sfz, instrument, error));
    REQUIRE(instrument.sources.size() == 1);
    REQUIRE(instrument.zones.size() == 1);
    const auto& zone = instrument.zones[0];
    CHECK(zone.keyLow == 60);
    CHECK(zone.keyHigh == 64);
    CHECK(zone.rootKey == 62);   // `key` supplies the key center too
    CHECK(zone.velocityLow == 10);
    CHECK(zone.velocityHigh == 90);
    CHECK(zone.pan == Catch::Approx(0.5));
    CHECK(zone.attenuationDb == Catch::Approx(6.0));
    CHECK(zone.end == 500);
}

TEST_CASE("SFZ loop modes choose a one-shot or a sustained loop", "[sfz][sampler]")
{
    TestDirectory t;
    t.wave("Kit/Kick.wav", 220, 1, 1000);
    const auto file = writeSfz(t, "Kit/one.sfz",
        "<region> sample=Kick.wav lokey=36 pitch_keycenter=36 loop_start=100 loop_end=400 "
        "loopmode=one_shot\n"
        "<region> sample=Kick.wav lokey=40 pitch_keycenter=40 loop_start=100 loop_end=400\n");
    FileInstrument instrument;
    juce::String error;
    REQUIRE(loadFileInstrument(file, FileInstrumentFormat::sfz, instrument, error));
    REQUIRE(instrument.zones.size() == 2);
    CHECK(instrument.zones[0].oneShot);
    CHECK_FALSE(instrument.zones[0].looping);
    CHECK(instrument.zones[0].loopStart == 0);
    CHECK_FALSE(instrument.zones[1].oneShot); // without loopmode the loop implies sustain
    CHECK(instrument.zones[1].looping);
    CHECK(instrument.zones[1].loopStart == 100);
    CHECK(instrument.zones[1].loopEnd == 401);
}

TEST_CASE("SFZ missing samples are named, and a fully missing kit fails", "[sfz][sampler]")
{
    TestDirectory t;
    t.wave("Kit/Kick.wav", 220, 1, 500);
    const auto partial = writeSfz(t, "Kit/partial.sfz",
        "<region> sample=Kick.wav lokey=36 pitch_keycenter=36\n"
        "<region> sample=Gone.wav lokey=40 pitch_keycenter=40\n");
    FileInstrument instrument;
    juce::String error;
    REQUIRE(loadFileInstrument(partial, FileInstrumentFormat::sfz, instrument, error));
    REQUIRE(instrument.zones.size() == 1);
    CHECK(instrument.partial);
    CHECK(instrument.warnings.joinIntoString(" ").contains("Gone.wav"));

    const auto missing = writeSfz(t, "Kit/missing.sfz",
        "<region> sample=Nope.wav lokey=36 pitch_keycenter=36\n");
    FileInstrument failed;
    error.clear();
    REQUIRE_FALSE(loadFileInstrument(missing, FileInstrumentFormat::sfz, failed, error));
    CHECK(error.contains("Nope.wav"));

    const auto empty = writeSfz(t, "Kit/empty.sfz", "<control> default_path=Samples\\\n");
    error.clear();
    REQUIRE_FALSE(loadFileInstrument(empty, FileInstrumentFormat::sfz, failed, error));
    CHECK(error.containsIgnoreCase("region"));
}

TEST_CASE("SFZ files open in the preview and index as metadata rows", "[sfz][preview][catalog]")
{
    TestDirectory t;
    t.wave("Kit/Samples/Kick.wav", 220, 1, 1000);
    t.wave("Kit/Samples/Snare.wav", 660, 1, 1000);
    const auto file = writeSfz(t, "Kit/Kit.sfz", twoRegionSfz);

    AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"));
    REQUIRE(preview.loadFile(file, false));
    CHECK(engine.isExternalInstrumentMode());
    CHECK(engine.isSamplerInstrumentMode());
    CHECK(engine.isMidiPreview());
    CHECK(engine.instrumentName() == "Kit");
    CHECK(preview.instrumentSampleCount() == 2);

    REQUIRE(FileScanner::isSupportedFormat(file));
    CHECK(FileScanner::isMetadataOnlyFormat(file));
    const auto record = analyzeMetadataAsset(file, {});
    CHECK(record.error.isEmpty());
    CHECK(record.format == "SFZ");
    CHECK(record.compatibility == AssetCompatibility::supported);

    const auto broken = writeSfz(t, "Kit/broken.sfz", "this is not an sfz instrument\n");
    const auto brokenRecord = analyzeMetadataAsset(broken, {});
    CHECK(brokenRecord.compatibility == AssetCompatibility::invalid);
    CHECK(brokenRecord.error.containsIgnoreCase("sfz"));
}

// Opt-in corpus check: SONICATOR_SFZ_DIR points at the read-only library leaf. The run prints
// the load rate and the reasons for the rejected programs, which are recorded in docs/SFZ.md.
TEST_CASE("Real SFZ corpus opens read-only", "[.external][sfz-corpus]")
{
    const auto directory = juce::SystemStats::getEnvironmentVariable("SONICATOR_SFZ_DIR", {});
    REQUIRE(directory.isNotEmpty());
    const juce::File root(directory);
    REQUIRE(root.isDirectory());
    std::vector<juce::File> files;
    for (const auto& entry : juce::RangedDirectoryIterator(root, true, "*.sfz;*.SFZ",
                                                           juce::File::findFiles))
        files.push_back(entry.getFile());
    std::sort(files.begin(), files.end(), [] (const auto& a, const auto& b)
              { return a.getFullPathName() < b.getFullPathName(); });
    int loaded = 0, rejected = 0, zones = 0;
    juce::StringArray reasons;
    for (const auto& file : files)
    {
        FileInstrument instrument;
        juce::String error;
        if (loadFileInstrument(file, FileInstrumentFormat::sfz, instrument, error))
        {
            ++loaded;
            zones += (int) instrument.zones.size();
        }
        else
        {
            ++rejected;
            reasons.add(file.getFileName() + ": " + error.substring(0, 160));
        }
    }
    std::cout << "SFZ corpus: files=" << files.size() << " loaded=" << loaded
              << " rejected=" << rejected << " zones=" << zones << "\n";
    for (int i = 0; i < juce::jmin(30, reasons.size()); ++i)
        std::cout << "  " << reasons[i] << "\n";
    INFO("files=" << files.size() << " loaded=" << loaded << " rejected=" << rejected
         << " zones=" << zones);
    INFO(reasons.joinIntoString("\n"));
    REQUIRE_FALSE(files.empty());
    CHECK(loaded > 0);
    CHECK(zones > 0);
}
