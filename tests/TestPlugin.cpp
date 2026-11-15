#include <catch2/catch_test_macros.hpp>
#include "plugin/PluginState.h"

using namespace sonicator;

TEST_CASE("Plugin state survives serialisation and clamps the window", "[plugin][state]")
{
    plugin::PluginState state;
    CHECK(state.version == plugin::PluginState::currentVersion);
    state.lastFolder = "/Music/Drums";
    state.lastSamplePath = "/Music/Drums/kick.wav";
    state.windowWidth = 1400;
    state.windowHeight = 900;

    const auto block = state.toMemoryBlock();
    REQUIRE(block.getSize() > 0);
    const auto restored = plugin::PluginState::fromMemoryBlock(block.getData(), block.getSize());
    CHECK(restored.version == plugin::PluginState::currentVersion);
    CHECK(restored.lastFolder == state.lastFolder);
    CHECK(restored.lastSamplePath == state.lastSamplePath);
    CHECK(restored.windowWidth == 1400);
    CHECK(restored.windowHeight == 900);

    state.windowWidth = 10;
    state.windowHeight = 99999;
    const auto clampedBlock = state.toMemoryBlock();
    const auto clamped = plugin::PluginState::fromMemoryBlock(clampedBlock.getData(),
                                                             clampedBlock.getSize());
    CHECK(clamped.windowWidth == plugin::PluginState::minimumWidth);
    CHECK(clamped.windowHeight == plugin::PluginState::maximumHeight);

    const char junk[] = "<not-sonicator/>";
    const auto fallback = plugin::PluginState::fromMemoryBlock(junk, sizeof(junk) - 1);
    CHECK(fallback.version == plugin::PluginState::currentVersion);
    CHECK(fallback.lastFolder.isEmpty());
    CHECK(fallback.lastSamplePath.isEmpty());
    CHECK(fallback.windowWidth == plugin::PluginState::defaultWidth);
    CHECK(fallback.windowHeight == plugin::PluginState::defaultHeight);

    const auto empty = plugin::PluginState::fromMemoryBlock(nullptr, 0);
    CHECK(empty.windowWidth == plugin::PluginState::defaultWidth);

    // A future writer may append fields: the known ones still restore and its version is kept.
    const auto future = juce::String("<SONICATOR_PLUGIN version=\"7\" lastFolder=\"/x\""
                                     " lastSamplePath=\"/x/a.wav\" width=\"1024\" height=\"768\"/>");
    const auto futureState = plugin::PluginState::fromMemoryBlock(future.toRawUTF8(),
                                                                 future.getNumBytesAsUTF8());
    CHECK(futureState.version == 7);
    CHECK(futureState.lastFolder == "/x");
    CHECK(futureState.windowWidth == 1024);
}

#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"
#include "ui/MainComponent.h"
#include "core/database/Database.h"
#include "core/database/SampleRepository.h"
#include "core/dsp/BasicAnalyzer.h"
#include "core/settings/AppSettings.h"
#include "TestHelpers.h"
#include <memory>
#include <sqlite3.h>

using namespace sonicator;

TEST_CASE("Plugin processor stores project state and keeps the device closed", "[plugin][state][audio]")
{
    TestDirectory t;
    const auto dbPath = t.dir.getChildFile("plugin.sqlite");
    {
        Database db(dbPath);
        SampleRepository repo(db);
        repo.createTables();
    }
    plugin::PluginProcessor processor(dbPath, t.dir.getChildFile("settings.json"));

    // The host owns the device: nothing is opened until prepareToPlay, and only in host mode.
    CHECK_FALSE(processor.engine().isHostAudio());
    processor.prepareToPlay(48000.0, 256);
    CHECK(processor.engine().isHostAudio());
    juce::AudioBuffer<float> block(2, 256);
    juce::MidiBuffer midi;
    processor.processBlock(block, midi);
    CHECK(block.getMagnitude(0, 256) == 0.0f);
    processor.releaseResources();
    CHECK_FALSE(processor.engine().isHostAudio());

    plugin::PluginState state;
    state.lastFolder = "/lib/Drums";
    state.lastSamplePath = "/lib/Drums/kick.wav";
    state.windowWidth = 1300;
    state.windowHeight = 800;
    processor.setPluginState(state);

    juce::MemoryBlock stored;
    processor.getStateInformation(stored);
    const auto restored = plugin::PluginState::fromMemoryBlock(stored.getData(), stored.getSize());
    CHECK(restored.lastFolder == state.lastFolder);
    CHECK(restored.lastSamplePath == state.lastSamplePath);
    CHECK(restored.windowWidth == 1300);
    CHECK(restored.windowHeight == 800);

    processor.setPluginState({});
    CHECK(processor.pluginState().lastFolder.isEmpty());
    processor.setStateInformation(stored.getData(), (int) stored.getSize());
    CHECK(processor.pluginState().lastSamplePath == state.lastSamplePath);

    // Garbage from a foreign or older host must not throw.
    const char junk[] = "garbage";
    processor.setStateInformation(junk, 7);
    CHECK(processor.pluginState().lastFolder.isEmpty());
    CHECK(processor.pluginState().windowWidth == plugin::PluginState::defaultWidth);
}

TEST_CASE("Two plugin instances keep independent project state", "[plugin][state]")
{
    TestDirectory t;
    plugin::PluginProcessor first(t.dir.getChildFile("first.sqlite"),
                                 t.dir.getChildFile("first.json"));
    plugin::PluginProcessor second(t.dir.getChildFile("second.sqlite"),
                                  t.dir.getChildFile("second.json"));
    first.updateSession("/a", "/a/kick.wav");
    second.updateSession("/b", "/b/snare.wav");
    CHECK(first.pluginState().lastSamplePath == "/a/kick.wav");
    CHECK(second.pluginState().lastSamplePath == "/b/snare.wav");
    CHECK_FALSE(first.engine().isHostAudio());
    CHECK_FALSE(second.engine().isHostAudio());
}

TEST_CASE("Plugin editor reports a missing catalog and survives repeated opening",
          "[plugin][ui]")
{
    TestDirectory t;
    plugin::PluginProcessor processor(t.dir.getChildFile("absent.sqlite"),
                                     t.dir.getChildFile("settings.json"));
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        REQUIRE(editor != nullptr);
        auto* missing = editor->findChildWithID("pluginMissingCatalog");
        REQUIRE(missing != nullptr);
        CHECK(editor->getWidth() == plugin::PluginState::defaultWidth);
        CHECK(editor->getHeight() == plugin::PluginState::defaultHeight);
    }
}

TEST_CASE("Plugin editor restores the project selection without autoplay", "[plugin][ui][session]")
{
    TestDirectory t;
    const auto kick = t.wave("lib/Drums/kick.wav");
    const auto snare = t.wave("lib/Drums/snare.wav");
    const auto bass = t.wave("lib/Bass/bass.wav");
    const auto lib = t.dir.getChildFile("lib");
    const auto dbPath = t.dir.getChildFile("plugin.sqlite");
    {
        Database db(dbPath);
        SampleRepository repo(db);
        repo.createTables();
        for (const auto& file : { kick, snare, bass })
        {
            auto record = BasicAnalyzer().analyzeAndHash(file);
            REQUIRE(record.error.isEmpty());
            repo.upsert(record);
        }
        repo.addRoot(lib.getFullPathName());
    }

    const auto settingsFile = t.dir.getChildFile("settings.json");
    AppSettings application;
    application.lastFolder = lib.getChildFile("Bass").getFullPathName();
    application.lastSamplePath = bass.getFullPathName();
    SettingsStore(settingsFile).save(application);

    plugin::PluginProcessor processor(dbPath, settingsFile);
    plugin::PluginState state;
    state.lastFolder = lib.getChildFile("Drums").getFullPathName();
    state.lastSamplePath = snare.getFullPathName();
    state.windowWidth = 1320;
    state.windowHeight = 820;
    processor.setPluginState(state);

    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        REQUIRE(editor != nullptr);
        auto* main = dynamic_cast<MainComponent*>(editor->findChildWithID("pluginMain"));
        REQUIRE(main != nullptr);
        CHECK(editor->getWidth() == 1320);
        CHECK(editor->getHeight() == 820);
        CHECK(main->selectedSamplePath() == snare.getFullPathName());
        CHECK(main->selectedFolderPath() == lib.getChildFile("Drums").getFullPathName());
        CHECK_FALSE(processor.engine().isPlaying());

        // The plugin reads the shared settings but keeps the application's session untouched.
        const auto settings = SettingsStore(settingsFile).load();
        CHECK(settings.lastFolder == application.lastFolder);
        CHECK(settings.lastSamplePath == application.lastSamplePath);
    }
    CHECK_FALSE(processor.engine().isPlaying());
    CHECK(processor.pluginState().lastSamplePath == snare.getFullPathName());
    CHECK(processor.pluginState().lastFolder == lib.getChildFile("Drums").getFullPathName());
    CHECK(processor.pluginState().windowWidth == 1320);
}

TEST_CASE("Embedded window restores project state and never writes the application session",
          "[plugin][session]")
{
    TestDirectory t;
    const auto kick = t.wave("lib/Drums/kick.wav");
    const auto bass = t.wave("lib/Bass/bass.wav");
    const auto lib = t.dir.getChildFile("lib");
    const auto dbPath = t.dir.getChildFile("plugin.sqlite");
    {
        Database db(dbPath);
        SampleRepository repo(db);
        repo.createTables();
        for (const auto& file : { kick, bass })
        {
            auto record = BasicAnalyzer().analyzeAndHash(file);
            REQUIRE(record.error.isEmpty());
            repo.upsert(record);
        }
        repo.addRoot(lib.getFullPathName());
    }
    const auto settingsFile = t.dir.getChildFile("settings.json");
    AppSettings application;
    application.lastFolder = lib.getChildFile("Bass").getFullPathName();
    application.lastSamplePath = bass.getFullPathName();
    SettingsStore(settingsFile).save(application);
    const auto initial = SettingsStore(settingsFile).load();

    AudioEngine engine(false);
    {
        EmbeddedSession session;
        session.lastFolder = lib.getChildFile("Drums").getFullPathName();
        session.lastSamplePath = kick.getFullPathName();
        MainComponent app(dbPath, false, initial, settingsFile, &engine, true, std::move(session));
        auto* library = dynamic_cast<FileListPanel*>(app.findChildWithID("library"));
        REQUIRE(library != nullptr);
        CHECK(app.selectedSamplePath() == kick.getFullPathName());
        CHECK(app.settingsFile() == settingsFile);
        library->selectRow(0);
        app.flushSessionState();
    }
    const auto unchanged = SettingsStore(settingsFile).load();
    CHECK(unchanged.lastFolder == application.lastFolder);
    CHECK(unchanged.lastSamplePath == application.lastSamplePath);
}

TEST_CASE("Embedded window disables scanning and file processing with an explanation",
          "[plugin][ui][gates]")
{
    TestDirectory t;
    const auto dbPath = t.dir.getChildFile("gate.sqlite");
    {
        Database db(dbPath);
        SampleRepository repo(db);
        repo.createTables();
    }
    AudioEngine engine(false);
    MainComponent app(dbPath, false, {}, t.dir.getChildFile("settings.json"), &engine, true);
    const juce::StringArray blocked { "+ Add folder", "Rescan", "Reanalyze all",
                                      "Rescan and analyze new files", "Smart metadata" };
    juce::StringArray found;
    for (int i = 0; i < app.getNumChildComponents(); ++i)
        if (auto* button = dynamic_cast<juce::TextButton*>(app.getChildComponent(i)))
            if (blocked.contains(button->getButtonText()))
            {
                found.add(button->getButtonText());
                CHECK_FALSE(button->isEnabled());
                CHECK(button->getTooltip().contains("SONICATOR app"));
            }
    INFO("found: " + found.joinIntoString(", "));
    for (const auto& name : blocked)
        CHECK(found.contains(name));
    CHECK(found.size() == blocked.size());

    const auto batch = app.commandRegistry().state("library.batchProcess");
    CHECK_FALSE(batch.canRun());
    CHECK(batch.reason.contains("SONICATOR app"));
    const auto folderBatch = app.commandRegistry().state("folders.batchProcess");
    CHECK_FALSE(folderBatch.canRun());
    CHECK(folderBatch.reason.contains("SONICATOR app"));
}

TEST_CASE("Host state restored while the editor is open survives closing the window",
          "[plugin][ui][state]")
{
    TestDirectory t;
    const auto kick = t.wave("lib/Drums/kick.wav");
    const auto bass = t.wave("lib/Bass/bass.wav");
    const auto lib = t.dir.getChildFile("lib");
    const auto dbPath = t.dir.getChildFile("restore.sqlite");
    {
        Database db(dbPath);
        SampleRepository repo(db);
        repo.createTables();
        for (const auto& file : { kick, bass })
        {
            auto record = BasicAnalyzer().analyzeAndHash(file);
            REQUIRE(record.error.isEmpty());
            repo.upsert(record);
        }
        repo.addRoot(lib.getFullPathName());
    }
    plugin::PluginProcessor processor(dbPath, t.dir.getChildFile("settings.json"));
    plugin::PluginState opened;
    opened.lastFolder = lib.getChildFile("Drums").getFullPathName();
    opened.lastSamplePath = kick.getFullPathName();
    opened.windowWidth = 1000;
    opened.windowHeight = 700;
    processor.setPluginState(opened);

    auto editor = std::unique_ptr<juce::AudioProcessorEditor>(processor.createEditor());
    REQUIRE(editor != nullptr);
    auto* main = dynamic_cast<MainComponent*>(editor->findChildWithID("pluginMain"));
    REQUIRE(main != nullptr);
    REQUIRE(main->selectedSamplePath() == kick.getFullPathName());

    // The host loads another project state while this window stays open.
    plugin::PluginState restored;
    restored.lastFolder = lib.getChildFile("Bass").getFullPathName();
    restored.lastSamplePath = bass.getFullPathName();
    restored.windowWidth = 1200;
    restored.windowHeight = 760;
    juce::MemoryBlock block;
    {
        const auto temporary = restored.toMemoryBlock();
        block = temporary;
    }
    processor.setStateInformation(block.getData(), (int) block.getSize());
    editor.reset();

    const auto finalState = processor.pluginState();
    CHECK(finalState.lastFolder == restored.lastFolder);
    CHECK(finalState.lastSamplePath == restored.lastSamplePath);
    CHECK(finalState.windowWidth == 1200);
    CHECK(finalState.windowHeight == 760);
}

TEST_CASE("Embedded window never clears the application session when it hides an ignored folder",
          "[plugin][session][gates]")
{
    TestDirectory t;
    const auto kick = t.wave("lib/Drums/kick.wav");
    const auto bass = t.wave("lib/Bass/bass.wav");
    const auto lib = t.dir.getChildFile("lib");
    const auto drums = lib.getChildFile("Drums");
    const auto dbPath = t.dir.getChildFile("ignored.sqlite");
    {
        Database db(dbPath);
        SampleRepository repo(db);
        repo.createTables();
        for (const auto& file : { kick, bass })
        {
            auto record = BasicAnalyzer().analyzeAndHash(file);
            REQUIRE(record.error.isEmpty());
            repo.upsert(record);
        }
        repo.addRoot(lib.getFullPathName());
        repo.ignorePath(drums.getFullPathName(), true);
    }
    const auto settingsFile = t.dir.getChildFile("settings.json");
    AppSettings application;
    application.lastFolder = lib.getChildFile("Bass").getFullPathName();
    application.lastSamplePath = bass.getFullPathName();
    SettingsStore(settingsFile).save(application);
    const auto initial = SettingsStore(settingsFile).load();

    AudioEngine engine(false);
    {
        EmbeddedSession session;
        session.lastFolder = drums.getFullPathName();
        MainComponent app(dbPath, false, initial, settingsFile, &engine, true, std::move(session));
        CHECK(app.selectedFolderPath().isEmpty());
    }
    const auto unchanged = SettingsStore(settingsFile).load();
    CHECK(unchanged.lastFolder == application.lastFolder);
    CHECK(unchanged.lastSamplePath == application.lastSamplePath);
}

TEST_CASE("Embedded window does not run the feature backfill", "[plugin][session][gates]")
{
    TestDirectory t;
    const auto file = t.wave("lib/Drums/kick.wav");
    const auto lib = t.dir.getChildFile("lib");
    const auto dbPath = t.dir.getChildFile("backfill.sqlite");
    int64_t sampleId = 0;
    {
        Database db(dbPath);
        SampleRepository repo(db);
        repo.createTables();
        auto record = BasicAnalyzer().analyzeAndHash(file);
        REQUIRE(record.error.isEmpty());
        sampleId = repo.upsert(record);
        repo.addRoot(lib.getFullPathName());
    }
    {
        sqlite3* raw = nullptr;
        REQUIRE(sqlite3_open(dbPath.getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
        const auto sql = "INSERT INTO ml_feedback_samples"
                         " (sample_id, file_hash, target_type, tags, features, updated_at, origin)"
                         " VALUES (" + juce::String(sampleId) + ", 'hash', 'Kick', '', X'00', 0, 'user')";
        REQUIRE(sqlite3_exec(raw, sql.toRawUTF8(), nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(raw);
    }
    AudioEngine engine(false);
    {
        MainComponent app(dbPath, false, {}, t.dir.getChildFile("settings.json"), &engine, true);
        app.setSize(1180, 720);
    }
    Database db(dbPath);
    SampleRepository repo(db);
    const auto rows = repo.allFeedback();
    REQUIRE(rows.size() == 1);
    // The legacy row stays legacy: the application owns the catalog-wide backfill.
    CHECK(rows.front().featureSchema < 2);

    // Control: an ordinary application window does run the same backfill on the same catalog.
    {
        MainComponent application(dbPath, false, {}, t.dir.getChildFile("app-settings.json"),
                                  &engine, false);
        application.setSize(1180, 720);
    }
    Database appDb(dbPath);
    SampleRepository appRepo(appDb);
    const auto backfilled = appRepo.allFeedback();
    REQUIRE(backfilled.size() == 1);
    CHECK(backfilled.front().featureSchema == 2);
}
