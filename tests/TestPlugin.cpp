#include <catch2/catch_test_macros.hpp>
#include "plugin/PluginState.h"
#include <catch2/catch_approx.hpp>
#include "plugin/PluginProcessor.h"
#include "ui/MainComponent.h"
#include "ui/settings/SettingsPanel.h"
#include "core/database/SampleRepository.h"
#include "core/settings/AppSettings.h"
#include "TestHelpers.h"

using namespace sonicator;

TEST_CASE("Plugin interface scale includes text without scaling the host", "[plugin][acceptance-fixes]")
{
    TestDirectory t;
    const auto catalog = t.dir.getChildFile("scale.sqlite");
    { Database db(catalog); SampleRepository repo(db); repo.createTables(); }
    const auto settingsFile = t.dir.getChildFile("settings.json");
    AppSettings settings;
    settings.scalePercent = 125;
    SettingsStore(settingsFile).save(settings);
    const auto hostScale = juce::Desktop::getInstance().getGlobalScaleFactor();
    plugin::PluginProcessor processor(catalog, settingsFile);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    auto* main = editor->findChildWithID("pluginMain");
    REQUIRE(main != nullptr);
    CHECK(main->getTransform().getScaleFactor() == Catch::Approx(1.25f));
    CHECK(editor->getWidth() == juce::roundToInt(plugin::PluginState::defaultWidth * 1.25f));
    CHECK(editor->getHeight() == juce::roundToInt(plugin::PluginState::defaultHeight * 1.25f));
    CHECK(main->getWidth() == juce::roundToInt(editor->getWidth() / 1.25f));
    CHECK(juce::Desktop::getInstance().getGlobalScaleFactor() == hostScale);
    auto* browser = dynamic_cast<MainComponent*>(main);
    REQUIRE(browser != nullptr);
    juce::TextButton* settingsButton = nullptr;
    for (auto* child : browser->getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*>(child))
            if (button->getButtonText() == "Settings") settingsButton = button;
    REQUIRE(settingsButton != nullptr);
    settingsButton->onClick();
    auto* scale = dynamic_cast<juce::ComboBox*>(findDescendantWithID(*main, "settingsScale"));
    REQUIRE(scale != nullptr);
    scale->setSelectedId(150, juce::sendNotificationSync);
    CHECK(main->getTransform().getScaleFactor() == Catch::Approx(1.5f));
    CHECK(editor->getWidth() == juce::roundToInt(plugin::PluginState::defaultWidth * 1.5f));
    CHECK(processor.pluginState().windowWidth == plugin::PluginState::defaultWidth);
    CHECK(juce::Desktop::getInstance().getGlobalScaleFactor() == hostScale);
    const auto captureDir = juce::SystemStats::getEnvironmentVariable("SONICATOR_PLUGIN_SCREENSHOT_DIR", {});
    if (captureDir.isNotEmpty())
    {
        const juce::File folder(captureDir);
        REQUIRE(folder.createDirectory());
        auto* panel = dynamic_cast<SettingsPanel*>(findDescendantWithID(*main, "settingsPanel"));
        REQUIRE(panel != nullptr);
        for (int percent : { 100, 125 })
        {
            scale->setSelectedId(percent, juce::sendNotificationSync);
            for (auto page : { SettingsSection::general, SettingsSection::about })
            {
                panel->selectSection(page);
                const auto image = editor->createComponentSnapshot(editor->getLocalBounds());
                const auto file = folder.getChildFile("plugin-settings-" + settingsSectionId(page)
                    + "-" + juce::String(percent) + ".png");
                if (file.existsAsFile()) REQUIRE(file.deleteFile());
                auto stream = file.createOutputStream();
                REQUIRE(stream != nullptr);
                REQUIRE(juce::PNGImageFormat().writeImageToStream(image, *stream));
            }
        }
    }
    scale->setSelectedId(125, juce::sendNotificationSync);
    editor.reset();
    CHECK(processor.pluginState().windowWidth == plugin::PluginState::defaultWidth);
    editor.reset(processor.createEditor());
    CHECK(editor->getWidth() == juce::roundToInt(plugin::PluginState::defaultWidth * 1.25f));
    CHECK(juce::Desktop::getInstance().getGlobalScaleFactor() == hostScale);
}

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
#include "ui/library/TapTempoDialog.h"
#include "core/database/CatalogCompatibility.h"
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
        // The backfill owns a background thread that the window cancels when it goes away. Waiting
        // inside the window keeps the control from racing the destructor on a slow runner.
        Database probe(dbPath);
        SampleRepository probeRepo(probe);
        REQUIRE(pumpUntil([&]
        {
            const auto current = probeRepo.allFeedback();
            return current.size() == 1 && current.front().featureSchema == 2;
        }));
    }
    Database appDb(dbPath);
    SampleRepository appRepo(appDb);
    const auto backfilled = appRepo.allFeedback();
    REQUIRE(backfilled.size() == 1);
    CHECK(backfilled.front().featureSchema == 2);
}

namespace
{
// Writes a schema version into a catalog the way another SONICATOR build would have left it.
void setCatalogVersion(const juce::File& path, int version)
{
    sqlite3* raw = nullptr;
    REQUIRE(sqlite3_open(path.getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
    const auto sql = "PRAGMA user_version=" + juce::String(version) + ";";
    REQUIRE(sqlite3_exec(raw, sql.toRawUTF8(), nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(raw);
}
int catalogVersion(const juce::File& path)
{
    sqlite3* raw = nullptr;
    REQUIRE(sqlite3_open_v2(path.getFullPathName().toRawUTF8(), &raw, SQLITE_OPEN_READONLY,
                            nullptr) == SQLITE_OK);
    sqlite3_stmt* statement = nullptr;
    int version = -1;
    if (sqlite3_prepare_v2(raw, "PRAGMA user_version", -1, &statement, nullptr) == SQLITE_OK
        && sqlite3_step(statement) == SQLITE_ROW)
        version = sqlite3_column_int(statement, 0);
    if (statement != nullptr) sqlite3_finalize(statement);
    sqlite3_close(raw);
    return version;
}
// Builds a small catalog with a folder and one sample in it.
juce::File makeCatalog(TestDirectory& t, const juce::String& name, const juce::File& sample)
{
    const auto dbPath = t.dir.getChildFile(name);
    Database db(dbPath);
    SampleRepository repo(db);
    repo.createTables();
    if (sample != juce::File())
    {
        auto record = BasicAnalyzer().analyzeAndHash(sample);
        REQUIRE(record.error.isEmpty());
        repo.upsert(record);
        repo.addRoot(t.dir.getChildFile("lib").getFullPathName());
    }
    return dbPath;
}
} // namespace

TEST_CASE("Catalog compatibility is read-only and names what to update", "[plugin][catalog]")
{
    TestDirectory t;
    const auto file = t.wave("lib/Drums/kick.wav");
    const auto dbPath = makeCatalog(t, "catalog.sqlite", file);

    const auto ready = inspectCatalog(dbPath);
    CHECK(ready.state == CatalogCompatibility::State::ready);
    CHECK(ready.readable());
    CHECK(ready.version == catalogSchemaVersion);
    CHECK(ready.message.isEmpty());

    // Another build wrote this catalog: the check says which one and does not touch the file.
    setCatalogVersion(dbPath, catalogSchemaVersion - 1);
    const auto older = inspectCatalog(dbPath);
    CHECK(older.state == CatalogCompatibility::State::older);
    CHECK_FALSE(older.readable());
    CHECK(older.message.contains("old"));

    setCatalogVersion(dbPath, catalogSchemaVersion + 1);
    const auto newer = inspectCatalog(dbPath);
    CHECK(newer.state == CatalogCompatibility::State::newer);
    CHECK(newer.message.contains("newer"));
    CHECK(catalogVersion(dbPath) == catalogSchemaVersion + 1);

    CHECK(inspectCatalog(t.dir.getChildFile("absent.sqlite")).state
          == CatalogCompatibility::State::missing);
    const auto text = t.dir.getChildFile("notes.txt");
    text.replaceWithText("this is not a database");
    CHECK(inspectCatalog(text).state == CatalogCompatibility::State::notCatalog);
}

TEST_CASE("Plugin never migrates a catalog from another build", "[plugin][catalog][ui]")
{
    TestDirectory t;
    const auto kick = t.wave("lib/Drums/kick.wav");
    const auto bass = t.wave("lib/Bass/bass.wav");
    const auto dbPath = makeCatalog(t, "shared.sqlite", kick);
    {
        Database db(dbPath);
        SampleRepository repo(db);
        auto record = BasicAnalyzer().analyzeAndHash(bass);
        REQUIRE(record.error.isEmpty());
        repo.upsert(record);
    }
    // The application is still an older build that writes this catalog.
    setCatalogVersion(dbPath, catalogSchemaVersion - 1);

    plugin::PluginProcessor processor(dbPath, t.dir.getChildFile("settings.json"));
    plugin::PluginState state;
    state.lastFolder = t.dir.getChildFile("lib/Drums").getFullPathName();
    state.lastSamplePath = kick.getFullPathName();
    state.windowWidth = 1040;
    state.windowHeight = 660;
    processor.setPluginState(state);

    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        REQUIRE(editor != nullptr);
        // No browser: the catalog is another build's and this one must not upgrade it.
        CHECK(editor->findChildWithID("pluginMain") == nullptr);
        auto* blocked = editor->findChildWithID("pluginCatalogBlocked");
        auto* label = dynamic_cast<juce::Label*>(blocked);
        REQUIRE(label != nullptr);
        CHECK(label->getText().contains("older SONICATOR"));
        CHECK(label->getText().contains(dbPath.getFullPathName()));
        // The window size still comes from the project, so reopening looks the same.
        CHECK(editor->getWidth() == 1040);
        CHECK(editor->getHeight() == 660);
    }

    // Nothing was migrated and the project's selection survived a window that could not open.
    CHECK(catalogVersion(dbPath) == catalogSchemaVersion - 1);
    CHECK(processor.pluginState().lastSamplePath == kick.getFullPathName());
    CHECK(processor.pluginState().lastFolder == state.lastFolder);

    // No upgrade means no backup either: opening a plug-in leaves no files behind.
    for (const auto& entry : t.dir.findChildFiles(juce::File::findFiles, false))
        CHECK_FALSE(entry.getFileName().contains(".backup-"));

    // The same catalog after the application updated it opens normally, without a backup.
    setCatalogVersion(dbPath, catalogSchemaVersion);
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        REQUIRE(editor != nullptr);
        CHECK(editor->findChildWithID("pluginMain") != nullptr);
    }
    for (const auto& entry : t.dir.findChildFiles(juce::File::findFiles, false))
        CHECK_FALSE(entry.getFileName().contains(".backup-"));
}

TEST_CASE("Plugin says so when the catalog was written by a newer build", "[plugin][catalog][ui]")
{
    TestDirectory t;
    const auto dbPath = makeCatalog(t, "future.sqlite", t.wave("lib/kick.wav"));
    setCatalogVersion(dbPath, catalogSchemaVersion + 1);

    plugin::PluginProcessor processor(dbPath, t.dir.getChildFile("settings.json"));
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    REQUIRE(editor != nullptr);
    auto* blocked = dynamic_cast<juce::Label*>(editor->findChildWithID("pluginCatalogBlocked"));
    REQUIRE(blocked != nullptr);
    CHECK(blocked->getText().contains("Update SONICATOR"));
    CHECK(catalogVersion(dbPath) == catalogSchemaVersion + 1);
}

TEST_CASE("Oversized state blocks are ignored instead of parsed", "[plugin][state]")
{
    juce::MemoryBlock huge;
    huge.ensureSize(plugin::PluginState::maximumStateBytes + 1);
    huge.fillWith(0x20);
    const auto ignored = plugin::PluginState::fromMemoryBlock(huge.getData(), huge.getSize());
    CHECK(ignored.windowWidth == plugin::PluginState::defaultWidth);
    CHECK(ignored.lastSamplePath.isEmpty());

    // A real block stays well under the limit.
    plugin::PluginState state;
    state.lastSamplePath = "/lib/a.wav";
    const auto block = state.toMemoryBlock();
    CHECK(block.getSize() <= plugin::PluginState::maximumStateBytes);
    CHECK(plugin::PluginState::fromMemoryBlock(block.getData(), block.getSize())
              .lastSamplePath == state.lastSamplePath);
}

TEST_CASE("Host state restored while the editor is open updates the window", "[plugin][ui][state]")
{
    TestDirectory t;
    const auto kick = t.wave("lib/Drums/kick.wav");
    const auto bass = t.wave("lib/Bass/bass.wav");
    const auto dbPath = makeCatalog(t, "restore-open.sqlite", kick);
    {
        Database db(dbPath);
        SampleRepository repo(db);
        auto record = BasicAnalyzer().analyzeAndHash(bass);
        REQUIRE(record.error.isEmpty());
        repo.upsert(record);
    }
    plugin::PluginProcessor processor(dbPath, t.dir.getChildFile("settings.json"));
    plugin::PluginState opened;
    opened.lastFolder = t.dir.getChildFile("lib/Drums").getFullPathName();
    opened.lastSamplePath = kick.getFullPathName();
    opened.windowWidth = 980;
    opened.windowHeight = 640;
    processor.setPluginState(opened);

    auto editor = std::unique_ptr<juce::AudioProcessorEditor>(processor.createEditor());
    REQUIRE(editor != nullptr);
    auto* main = dynamic_cast<MainComponent*>(editor->findChildWithID("pluginMain"));
    REQUIRE(main != nullptr);
    REQUIRE(main->selectedSamplePath() == kick.getFullPathName());

    // The host loads another project's state into this running instance.
    plugin::PluginState restored;
    restored.lastFolder = t.dir.getChildFile("lib/Bass").getFullPathName();
    restored.lastSamplePath = bass.getFullPathName();
    restored.windowWidth = 1240;
    restored.windowHeight = 780;
    const auto block = restored.toMemoryBlock();
    processor.setStateInformation(block.getData(), (int) block.getSize());

    // The editor polls the processor on the message thread; the window must follow the state.
    REQUIRE(pumpUntil([&] { return main->selectedSamplePath() == bass.getFullPathName(); }));
    CHECK(main->selectedFolderPath() == t.dir.getChildFile("lib/Bass").getFullPathName());
    CHECK(editor->getWidth() == 1240);
    CHECK(editor->getHeight() == 780);
    CHECK_FALSE(processor.engine().isPlaying());

    editor.reset();
    CHECK(processor.pluginState().lastSamplePath == bass.getFullPathName());
    CHECK(processor.pluginState().windowWidth == 1240);
}

TEST_CASE("Reopening a real catalog restores the project selection every time", "[plugin][ui]")
{
    TestDirectory t;
    const auto kick = t.wave("lib/Drums/kick.wav");
    const auto snare = t.wave("lib/Drums/snare.wav");
    const auto dbPath = makeCatalog(t, "reopen.sqlite", kick);
    {
        Database db(dbPath);
        SampleRepository repo(db);
        auto record = BasicAnalyzer().analyzeAndHash(snare);
        REQUIRE(record.error.isEmpty());
        repo.upsert(record);
    }
    plugin::PluginProcessor processor(dbPath, t.dir.getChildFile("settings.json"));
    plugin::PluginState state;
    state.lastFolder = t.dir.getChildFile("lib/Drums").getFullPathName();
    state.lastSamplePath = snare.getFullPathName();
    processor.setPluginState(state);

    for (int attempt = 0; attempt < 3; ++attempt)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        REQUIRE(editor != nullptr);
        auto* main = dynamic_cast<MainComponent*>(editor->findChildWithID("pluginMain"));
        REQUIRE(main != nullptr);
        CHECK(main->selectedSamplePath() == snare.getFullPathName());
        CHECK(processor.pluginState().lastSamplePath == snare.getFullPathName());
    }
}

TEST_CASE("Two editors open at once keep their own project state", "[plugin][ui][state]")
{
    TestDirectory t;
    const auto kick = t.wave("lib/Drums/kick.wav");
    const auto bass = t.wave("lib/Bass/bass.wav");
    const auto dbPath = makeCatalog(t, "twice.sqlite", kick);
    {
        Database db(dbPath);
        SampleRepository repo(db);
        auto record = BasicAnalyzer().analyzeAndHash(bass);
        REQUIRE(record.error.isEmpty());
        repo.upsert(record);
    }
    const auto settingsFile = t.dir.getChildFile("settings.json");

    plugin::PluginProcessor first(dbPath, settingsFile);
    plugin::PluginProcessor second(dbPath, settingsFile);
    plugin::PluginState firstState, secondState;
    firstState.lastSamplePath = kick.getFullPathName();
    firstState.lastFolder = t.dir.getChildFile("lib/Drums").getFullPathName();
    secondState.lastSamplePath = bass.getFullPathName();
    secondState.lastFolder = t.dir.getChildFile("lib/Bass").getFullPathName();
    first.setPluginState(firstState);
    second.setPluginState(secondState);

    auto firstEditor = std::unique_ptr<juce::AudioProcessorEditor>(first.createEditor());
    auto secondEditor = std::unique_ptr<juce::AudioProcessorEditor>(second.createEditor());
    REQUIRE(firstEditor != nullptr);
    REQUIRE(secondEditor != nullptr);
    auto* firstMain = dynamic_cast<MainComponent*>(firstEditor->findChildWithID("pluginMain"));
    auto* secondMain = dynamic_cast<MainComponent*>(secondEditor->findChildWithID("pluginMain"));
    REQUIRE(firstMain != nullptr);
    REQUIRE(secondMain != nullptr);
    CHECK(firstMain->selectedSamplePath() == kick.getFullPathName());
    CHECK(secondMain->selectedSamplePath() == bass.getFullPathName());

    // Closing one window leaves the other project alone.
    firstEditor.reset();
    CHECK(first.pluginState().lastSamplePath == kick.getFullPathName());
    CHECK(second.pluginState().lastSamplePath == bass.getFullPathName());
    secondEditor.reset();
    CHECK(second.pluginState().lastSamplePath == bass.getFullPathName());
}

TEST_CASE("Embedded prompts and tap tempo are drawn inside the editor", "[plugin][ui][dialogs]")
{
    TestDirectory t;
    const auto kick = t.wave("lib/Drums/kick.wav");
    const auto dbPath = makeCatalog(t, "dialogs.sqlite", kick);
    AudioEngine engine(false);
    MainComponent app(dbPath, false, {}, t.dir.getChildFile("settings.json"), &engine, true);
    app.setSize(1180, 720);

    // BPM, tag and genre prompts share this path: no top-level window, a panel inside the editor.
    juce::String accepted;
    dialogs::showPrompt(app, "Set BPM", "Tempo (20 to 300)", "120",
                        [&accepted](const juce::String& text) { accepted = text; });
    auto* overlay = findDescendantWithID(app, "embeddedDialog");
    REQUIRE(overlay != nullptr);
    auto* field = dynamic_cast<juce::TextEditor*>(findDescendantWithID(app, "embeddedDialogField"));
    REQUIRE(field != nullptr);
    CHECK(field->getText() == "120");
    field->setText("128", juce::dontSendNotification);
    auto* ok = dynamic_cast<juce::TextButton*>(findDescendantWithID(app, "embeddedDialogButton0"));
    REQUIRE(ok != nullptr);
    CHECK(ok->getButtonText() == "OK");
    ok->triggerClick();
    REQUIRE(pumpUntil([&] { return findDescendantWithID(app, "embeddedDialog") == nullptr; }));
    CHECK(accepted == "128");

    // Cancel reports nothing, and Escape on the overlay works too.
    bool acceptedAgain = false;
    dialogs::showPrompt(app, "Custom genre", "Genre name", "Deep House",
                        [&acceptedAgain](const juce::String&) { acceptedAgain = true; });
    auto* cancel = dynamic_cast<juce::TextButton*>(findDescendantWithID(app, "embeddedDialogButton1"));
    REQUIRE(cancel != nullptr);
    CHECK(cancel->getButtonText() == "Cancel");
    cancel->triggerClick();
    REQUIRE(pumpUntil([&] { return findDescendantWithID(app, "embeddedDialog") == nullptr; }));
    CHECK_FALSE(acceptedAgain);

    // Tap tempo uses the same host instead of a DialogWindow the host could lose.
    double tapped = 0.0;
    TapTempoDialog::launch(&app, "kick", {}, {}, [&tapped](double bpm) { tapped = bpm; });
    auto* tempoOverlay = findDescendantWithID(app, "embeddedDialog");
    REQUIRE(tempoOverlay != nullptr);
    auto* tempo = findDescendantWithID(app, "tapTempoDialog");
    REQUIRE(tempo != nullptr);
    CHECK(tempo->getParentComponent() == tempoOverlay);
    juce::ignoreUnused(tapped);
}

TEST_CASE("Application and plug-in keep one catalog open at the same time", "[plugin][catalog][ui]")
{
    TestDirectory t;
    const auto kick = t.wave("Drums/kick.wav");
    const auto bass = t.wave("Bass/bass.wav");
    const auto catalog = t.dir.getChildFile("shared.sqlite");
    {
        Database db(catalog);
        SampleRepository repo(db);
        repo.createTables();
        for (const auto& file : { kick, bass })
        {
            auto record = BasicAnalyzer().analyzeAndHash(file);
            REQUIRE(record.error.isEmpty());
            repo.upsert(record);
        }
        repo.addRoot(t.dir.getFullPathName());
    }

    // The application window and the plug-in editor run against the same file, which is the
    // ordinary case when a DAW project is open beside the application.
    AudioEngine engine(false);
    MainComponent application(catalog, false, {}, t.dir.getChildFile("app-settings.json"),
                              &engine, false);
    plugin::PluginProcessor processor(catalog, t.dir.getChildFile("plugin-settings.json"));
    plugin::PluginState state;
    state.lastSamplePath = kick.getFullPathName();
    processor.setPluginState(state);
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        REQUIRE(editor != nullptr);
        auto* main = dynamic_cast<MainComponent*>(editor->findChildWithID("pluginMain"));
        REQUIRE(main != nullptr);
        CHECK(main->selectedSamplePath() == kick.getFullPathName());
        CHECK(application.selectedSamplePath() != kick.getFullPathName());

        // Opening the plug-in neither migrated the catalog nor left a backup behind.
        CHECK(catalogVersion(catalog) == catalogSchemaVersion);
        for (const auto& entry : t.dir.findChildFiles(juce::File::findFiles, false))
            CHECK_FALSE(entry.getFileName().contains(".backup-"));
    }
    // A write from another connection is visible to both windows' repositories.
    {
        Database db(catalog);
        SampleRepository repo(db);
        const auto record = repo.findByPath(kick.getFullPathName());
        REQUIRE(record.has_value());
        const auto tag = repo.createTag("Parallel", 0xff4fb8c8);
        repo.setSampleTag(record->id, tag, true);
    }
    {
        Database db(catalog);
        SampleRepository repo(db);
        const auto record = repo.findByPath(kick.getFullPathName());
        REQUIRE(record.has_value());
        const auto tags = repo.tagsForSample(record->id);
        REQUIRE(tags.size() == 1);
        CHECK(tags.front().name == "Parallel");
    }
    CHECK(processor.pluginState().lastSamplePath == kick.getFullPathName());
}
