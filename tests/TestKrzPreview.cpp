#include <catch2/catch_test_macros.hpp>
#include "core/audio/AudioEngine.h"
#include "ui/preview/PreviewPanel.h"
#include "TestHelpers.h"
#include "KrzTestFile.h"
using namespace sonicator;
using namespace krztest;

namespace {
EntrySpec keyEntry(int sampleId)
{
    EntrySpec e; e.sampleId = sampleId; return e;
}
KeymapSpec simpleKeymap(int id, int sampleId)
{
    KeymapSpec k; k.id = id;
    for (auto& level : k.levels)
    {
        level.assign(128, EntrySpec {});
        for (int key = 12; key < 128; ++key) level[(size_t) (key - 12)] = keyEntry(sampleId);
    }
    return k;
}
juce::File twoProgramBank(const juce::File& dir)
{
    BankSpec spec;
    SampleSpec sample; sample.id = 200; sample.name = "Tone";
    HeaderSpec header; header.rootKey = 60; header.startWord = 0; header.endWord = 99;
    sample.headers = { header };
    spec.samples = { sample };
    spec.pcm.assign(200, 0);
    for (int i = 0; i < 100; ++i) spec.pcm[(size_t) i] = (int16_t) (i % 2 == 0 ? 10000 : -10000);
    spec.keymaps = { simpleKeymap(200, 200) };
    ProgramSpec first; first.id = 200; first.name = "Test Prog"; first.layers = { LayerSpec { 200, 0 } };
    ProgramSpec second; second.id = 201; second.name = "Second"; second.layers = { LayerSpec { 200, 0 } };
    spec.programs = { first, second };
    return writeBank(dir, "bank.krz", spec);
}
} // namespace

TEST_CASE("KRZ bank opens in the preview with its programs", "[krz][preview]")
{
    TestDirectory dir;
    auto file = twoProgramBank(dir.dir);
    AudioEngine engine(false);
    REQUIRE(engine.loadFile(file));
    CHECK(engine.isExternalInstrumentMode());
    CHECK(engine.isSamplerInstrumentMode());
    CHECK_FALSE(engine.isSoundFontMode());
    CHECK(engine.isMidiPreview());
    CHECK(engine.instrumentProgramCount() == 2);
    CHECK(engine.instrumentProgramIndex() == 0);
    CHECK(engine.instrumentProgramLabel(0) == "200  Test Prog");
    CHECK(engine.instrumentProgramLabel(1) == "201  Second");
    const auto generation = engine.instrumentGeneration();
    REQUIRE(engine.selectInstrumentProgram(1));
    CHECK(engine.instrumentProgramIndex() == 1);
    CHECK(engine.instrumentGeneration() > generation);
    CHECK(engine.instrumentName() == "Second");
    CHECK_FALSE(engine.selectInstrumentProgram(9));
    CHECK(engine.error().isNotEmpty());
}

TEST_CASE("KRZ bank only of factory ROM objects reports the reason", "[krz][preview]")
{
    TestDirectory dir;
    BankSpec spec;
    spec.keymaps = { simpleKeymap(200, 0) };
    ProgramSpec program; program.id = 200; program.name = "Rom"; program.layers = { LayerSpec { 200, 0 } };
    spec.programs = { program };
    auto file = writeBank(dir.dir, "rom.krz", spec);
    AudioEngine engine(false);
    CHECK_FALSE(engine.loadFile(file));
    CHECK(engine.error() == "This bank uses the K2000 factory ROM samples; the file contains no samples of its own");
}

TEST_CASE("Preview shows the program switcher for a Kurzweil bank only", "[krz][preview][ui]")
{
    TestDirectory dir;
    auto file = twoProgramBank(dir.dir);
    AudioEngine engine(false);
    PreviewPanel preview(engine, dir.dir.getChildFile("cache"));
    REQUIRE(preview.loadFile(file, false));
    auto* box = dynamic_cast<juce::ComboBox*>(preview.findChildWithID("soundFontPreset"));
    if (box == nullptr)
        for (int i = 0; i < preview.getNumChildComponents(); ++i)
            if (auto* combo = dynamic_cast<juce::ComboBox*>(preview.getChildComponent(i)))
                if (combo->getTooltip().containsIgnoreCase("preset")) box = combo;
    REQUIRE(box != nullptr);
    CHECK(box->isVisible());
    CHECK(box->getNumItems() == 2);
    box->setSelectedId(2, juce::sendNotificationSync);
    CHECK(engine.instrumentProgramIndex() == 1);
}
