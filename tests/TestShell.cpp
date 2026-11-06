#include <catch2/catch_test_macros.hpp>
#include "ui/MainComponent.h"
#include "ui/library/FileListPanel.h"
#include "ui/library/TagStrip.h"
#include "core/database/SampleRepository.h"
#include "core/dsp/BasicAnalyzer.h"
#include "TestHelpers.h"
#include <iostream>
using namespace sonicator;
TEST_CASE("Toolbar buttons do not steal keyboard focus from library navigation", "[ui][focus]")
{
    TestDirectory t; MainComponent app(t.dir.getChildFile("focus.sqlite"),false);
    int buttons = 0;
    for (auto* child : app.getChildren())
        if (auto* button = dynamic_cast<juce::Button*>(child))
        { ++buttons; REQUIRE_FALSE(button->getWantsKeyboardFocus()); REQUIRE_FALSE(button->getMouseClickGrabsKeyboardFocus()); }
    REQUIRE(buttons == 15);
}
TEST_CASE("Large catalog startup restores the actual page and folder tree", "[.catalog-ui]")
{
    const auto fixture = juce::SystemStats::getEnvironmentVariable("SONICATOR_CATALOG_FIXTURE",{});
    REQUIRE(fixture.isNotEmpty());
    // Explicit scratch fixture only; copy again so the test cannot modify the supplied fixture.
    TestDirectory t; auto dbPath = t.dir.getChildFile("fixture.sqlite");
    REQUIRE(juce::File(fixture).copyFileTo(dbPath));
    const auto start = juce::Time::getMillisecondCounterHiRes();
    MainComponent app(dbPath,false);
    auto* library = dynamic_cast<FileListPanel*>(app.findChildWithID("library"));
    REQUIRE(library != nullptr); REQUIRE(library->getNumRows() == 10000);
    std::cout << "Large catalog and tree startup: " << (juce::Time::getMillisecondCounterHiRes()-start)/1000 << " s\n";
    const auto screenshot = juce::SystemStats::getEnvironmentVariable("SONICATOR_CATALOG_SCREENSHOT",{});
    if (screenshot.isNotEmpty())
    {
        auto stream = juce::File(screenshot).createOutputStream(); REQUIRE(stream != nullptr);
        juce::PNGImageFormat png; REQUIRE(png.writeImageToStream(app.createComponentSnapshot(app.getLocalBounds()),*stream));
    }
}
TEST_CASE("Application shell restores the catalog and paints at minimum size", "[ui][integration]")
{
    TestDirectory t; auto audio = t.wave(); auto dbPath = t.dir.getChildFile("library.sqlite");
    {
        Database db(dbPath); SampleRepository repo(db); repo.createTables();
        auto r = BasicAnalyzer().analyze(audio); REQUIRE(r.error.isEmpty()); repo.upsert(r);
        repo.addRoot(t.dir.getFullPathName());
    }
    MainComponent app(dbPath,false);
    auto* library = dynamic_cast<FileListPanel*>(app.findChildWithID("library"));
    REQUIRE(library != nullptr); REQUIRE(library->getNumRows() == 1);
    app.setBounds(0,0,880,580);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(120);
    REQUIRE(app.createComponentSnapshot(app.getLocalBounds()).isValid());
    app.setBounds(0,0,1440,900);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(120);
    REQUIRE(app.createComponentSnapshot(app.getLocalBounds()).isValid());
    auto screenshot = juce::SystemStats::getEnvironmentVariable("SONICATOR_TEST_SCREENSHOT",{});
    if (screenshot.isNotEmpty())
    {
        library->selectedRowsChanged(0);
        auto image = app.createComponentSnapshot(app.getLocalBounds());
        auto output = juce::File(screenshot).createOutputStream();
        REQUIRE(output != nullptr);
        juce::PNGImageFormat png;
        REQUIRE(png.writeImageToStream(image,*output));
    }
}
TEST_CASE("Samples added to the catalog after startup appear in the list", "[ui][integration]")
{
    TestDirectory t; auto a = t.wave("a.wav"); auto dbPath = t.dir.getChildFile("library.sqlite");
    {
        Database db(dbPath); SampleRepository repo(db); repo.createTables();
        auto r = BasicAnalyzer().analyze(a); repo.upsert(r); repo.addRoot(t.dir.getFullPathName());
    }
    MainComponent app(dbPath, false);
    auto* library = dynamic_cast<FileListPanel*>(app.findChildWithID("library"));
    REQUIRE(library != nullptr); REQUIRE(library->getNumRows() == 1);
    library->selectRow(0);
    REQUIRE(library->getSelectedRow() == 0);
    REQUIRE_FALSE(app.refreshIfCatalogChanged());
    {
        Database db(dbPath); SampleRepository repo(db); repo.createTables();
        auto r = BasicAnalyzer().analyze(t.wave("b.wav", 660)); repo.upsert(r);
    }
    REQUIRE(app.refreshIfCatalogChanged());
    REQUIRE(library->getNumRows() == 2);
    REQUIRE(library->getSelectedRow() == 0);
}
TEST_CASE("Settings dialog is destroyed before the application", "[ui][dialog][settings]")
{
    TestDirectory t;
    auto app = std::make_unique<MainComponent>(t.dir.getChildFile("library.sqlite"),false);
    juce::TextButton* settings = nullptr;
    for (auto* child : app->getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*>(child))
            if (button->getButtonText() == "Settings") settings = button;
    REQUIRE(settings != nullptr);
    settings->onClick();
    juce::Component::SafePointer<juce::Component> dialog(juce::Component::getCurrentlyModalComponent());
    REQUIRE(dialog != nullptr);
    app.reset();
    REQUIRE(dialog == nullptr);
}
TEST_CASE("Whole-catalog search and tag filters drive the shell library", "[ui][integration][search][tags]")
{
    TestDirectory t; auto kick = t.wave("Kick.wav"); t.wave("Snare.wav"); t.wave("Bass.wav");
    auto dbPath = t.dir.getChildFile("library.sqlite");
    {
        Database db(dbPath); SampleRepository repo(db); repo.createTables();
        for (const auto& name : { "Kick.wav", "Snare.wav", "Bass.wav" })
        {
            auto r = BasicAnalyzer().analyze(t.dir.getChildFile(name)); REQUIRE(r.error.isEmpty()); repo.upsert(r);
        }
        repo.addRoot(t.dir.getFullPathName());
    }
    MainComponent app(dbPath,false);
    auto* library = dynamic_cast<FileListPanel*>(app.findChildWithID("library"));
    REQUIRE(library != nullptr); REQUIRE(library->getNumRows() == 3);
    app.searchLibrary("kick"); REQUIRE(library->getNumRows() == 1);
    app.searchLibrary("zzz-not-here"); REQUIRE(library->getNumRows() == 0);
    app.searchLibrary(""); REQUIRE(library->getNumRows() == 3);
    auto* searchEditor = dynamic_cast<juce::TextEditor*>(app.findChildWithID("search"));
    REQUIRE(searchEditor != nullptr);
    searchEditor->setText("snare", true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(500);
    REQUIRE(library->getNumRows() == 1);
    searchEditor->setText({}, true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(500);
    REQUIRE(library->getNumRows() == 3);

    int64_t kickTag = 0, snareTag = 0;
    {
        Database db(dbPath); SampleRepository repo(db);
        for (const auto& tag : repo.tags()) { if (tag.name == "Kick") kickTag = tag.id; if (tag.name == "Snare") snareTag = tag.id; }
        auto record = repo.findByPath(kick.getFullPathName()); REQUIRE(record.has_value());
        repo.setSampleTag(record->id, kickTag, true);
    }
    app.setTagFilters({kickTag}, {}, false);
    REQUIRE(library->getNumRows() == 1);
    app.setTagFilters({}, {kickTag}, false);
    REQUIRE(library->getNumRows() == 2);
    app.setTagFilters({}, {}, false);
    REQUIRE(library->getNumRows() == 3);

    library->selectRow(0);
    auto* strip = dynamic_cast<TagStrip*>(app.findChildWithID("selectedTags"));
    REQUIRE(strip != nullptr);
    int64_t selectedId = 0;
    {
        Database db(dbPath); SampleRepository repo(db);
        selectedId = repo.findByPath(kick.getFullPathName())->id;
    }
    app.searchLibrary("kick");
    library->selectRow(0);
    {
        Database db(dbPath); SampleRepository repo(db);
        REQUIRE(repo.tagsForSample(selectedId).size() == 1);
    }
    REQUIRE(strip->getTags().size() == 1);
    app.setBounds(0,0,880,580);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(120);
    REQUIRE(app.createComponentSnapshot(app.getLocalBounds()).isValid());
    app.setBounds(0,0,1440,900);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(120);
    REQUIRE(app.createComponentSnapshot(app.getLocalBounds()).isValid());
    const auto screenshot = juce::SystemStats::getEnvironmentVariable("SONICATOR_SEARCH_SCREENSHOT",{});
    if (screenshot.isNotEmpty())
    {
        app.searchLibrary("kick");
        library->selectRow(0);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(120);
        auto stream = juce::File(screenshot).createOutputStream();
        REQUIRE(stream != nullptr);
        juce::PNGImageFormat png;
        REQUIRE(png.writeImageToStream(app.createComponentSnapshot(app.getLocalBounds()),*stream));
    }
}
namespace {
juce::Button* shellButton(MainComponent& app, const juce::String& text)
{
    for (auto* child : app.getChildren())
        if (auto* button = dynamic_cast<juce::Button*>(child))
            if (button->getButtonText() == text) return button;
    return nullptr;
}
}
TEST_CASE("Rescan and Reanalyze ask before starting", "[ui][maintenance]")
{
    TestDirectory t; auto audio = t.wave("a.wav"); auto dbPath = t.dir.getChildFile("library.sqlite");
    {
        Database db(dbPath); SampleRepository repo(db); repo.createTables();
        auto r = BasicAnalyzer().analyze(audio); repo.upsert(r); repo.addRoot(t.dir.getFullPathName());
    }
    MainComponent app(dbPath,false);
    auto* rescan = shellButton(app,"Rescan");
    REQUIRE(rescan != nullptr);
    rescan->onClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    auto* dialog = dynamic_cast<juce::AlertWindow*>(juce::Component::getCurrentlyModalComponent());
    REQUIRE(dialog != nullptr);
    REQUIRE(dialog->getName() == "Rescan library");
    dialog->exitModalState(0);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    REQUIRE(juce::Component::getCurrentlyModalComponent() == nullptr);

    auto* reanalyze = shellButton(app,"Reanalyze all");
    REQUIRE(reanalyze != nullptr);
    reanalyze->onClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    auto* warning = dynamic_cast<juce::AlertWindow*>(juce::Component::getCurrentlyModalComponent());
    REQUIRE(warning != nullptr);
    REQUIRE(warning->getName() == "Reanalyze all");
    warning->exitModalState(0);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
}
TEST_CASE("Backup button writes a consistent copy beside the database", "[ui][maintenance]")
{
    TestDirectory t; auto audio = t.wave("a.wav"); auto dbPath = t.dir.getChildFile("library.sqlite");
    {
        Database db(dbPath); SampleRepository repo(db); repo.createTables();
        auto r = BasicAnalyzer().analyze(audio); repo.upsert(r); repo.addRoot(t.dir.getFullPathName());
    }
    MainComponent app(dbPath,false);
    auto* backup = shellButton(app,"Backup");
    REQUIRE(backup != nullptr);
    backup->onClick();
    auto copies = t.dir.getChildFile("backups").findChildFiles(juce::File::findFiles,false,"sonicator-*.sqlite");
    REQUIRE(copies.size() == 1);
    Database copy(copies[0]); SampleRepository copyRepo(copy); copyRepo.createTables();
    REQUIRE(copyRepo.count() == 1);
    REQUIRE_FALSE(copies[0].isDirectory());
}
TEST_CASE("Settings switch to an existing catalog or copy the current one", "[ui][settings][database]")
{
    TestDirectory t;
    auto a = t.wave("a.wav");
    auto dbA = t.dir.getChildFile("a.sqlite");
    {
        Database db(dbA); SampleRepository repo(db); repo.createTables();
        auto r = BasicAnalyzer().analyze(a); repo.upsert(r); repo.addRoot(t.dir.getFullPathName());
    }
    auto b1 = t.wave("b1.wav", 550); auto b2 = t.wave("b2.wav", 660);
    auto dbB = t.dir.getChildFile("b.sqlite");
    {
        Database db(dbB); SampleRepository repo(db); repo.createTables();
        for (const auto& file : { b1, b2 }) { auto r = BasicAnalyzer().analyze(file); repo.upsert(r); }
        repo.addRoot(t.dir.getFullPathName());
    }
    auto settingsFile = t.dir.getChildFile("settings.json");
    MainComponent app(dbA,false,{},settingsFile);
    auto* library = dynamic_cast<FileListPanel*>(app.findChildWithID("library"));
    REQUIRE(library != nullptr); REQUIRE(library->getNumRows() == 1);
    juce::String error;
    REQUIRE(app.switchCatalog(dbB,false,error));
    REQUIRE(error.isEmpty());
    REQUIRE(library->getNumRows() == 2);
    REQUIRE(SettingsStore(settingsFile).load().databasePath == dbB.getFullPathName());

    auto moved = t.dir.getChildFile("moved.sqlite");
    REQUIRE(app.switchCatalog(moved,true,error));
    REQUIRE(error.isEmpty());
    REQUIRE(moved.existsAsFile());
    REQUIRE(library->getNumRows() == 2);
    {
        Database db(moved); SampleRepository repo(db); REQUIRE(repo.count() == 2);
    }
    REQUIRE(SettingsStore(settingsFile).load().databasePath == moved.getFullPathName());
    REQUIRE_FALSE(app.switchCatalog(moved,true,error));
    REQUIRE(error.isNotEmpty());
    REQUIRE_FALSE(app.switchCatalog(t.dir.getChildFile("absent.sqlite"),false,error));
    auto foreign = t.dir.getChildFile("foreign.sqlite");
    REQUIRE(foreign.replaceWithText("this is not a catalog"));
    REQUIRE_FALSE(app.switchCatalog(foreign,false,error));
    REQUIRE(error.isNotEmpty());
    REQUIRE(foreign.loadFileAsString() == "this is not a catalog");
}
TEST_CASE("Light and dark themes apply without invalid rendering", "[ui][theme]")
{
    TestDirectory t;
    {
        MainComponent light(t.dir.getChildFile("light.sqlite"),false,AppSettings{ThemeMode::Light,100,"English"});
        REQUIRE_FALSE(theme::isDark);
        light.setBounds(0,0,1200,800);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        const auto image = light.createComponentSnapshot(light.getLocalBounds());
        REQUIRE(image.isValid());
        const auto shot = juce::SystemStats::getEnvironmentVariable("SONICATOR_LIGHT_SCREENSHOT",{});
        if (shot.isNotEmpty())
        {
            auto stream = juce::File(shot).createOutputStream(); REQUIRE(stream != nullptr);
            juce::PNGImageFormat png; REQUIRE(png.writeImageToStream(image,*stream));
        }
    }
    {
        MainComponent dark(t.dir.getChildFile("dark.sqlite"),false,AppSettings{ThemeMode::Dark,100,"English"});
        REQUIRE(theme::isDark);
        dark.setBounds(0,0,1200,800);
        REQUIRE(dark.createComponentSnapshot(dark.getLocalBounds()).isValid());
    }
    theme::apply(ThemeMode::Dark);
}
TEST_CASE("Starred filter shows only favourites", "[ui][starred]")
{
    TestDirectory t;
    auto a = t.wave("a.wav"); auto b = t.wave("b.wav");
    auto dbPath = t.dir.getChildFile("library.sqlite");
    {
        Database db(dbPath); SampleRepository repo(db); repo.createTables();
        for (const auto& file : { a, b }) { auto r = BasicAnalyzer().analyze(file); repo.upsert(r); }
        repo.addRoot(t.dir.getFullPathName());
        const auto record = repo.findByPath(a.getFullPathName());
        REQUIRE(record.has_value());
        repo.setStarred({ record->id }, true);
    }
    MainComponent app(dbPath,false);
    auto* library = dynamic_cast<FileListPanel*>(app.findChildWithID("library"));
    REQUIRE(library != nullptr); REQUIRE(library->getNumRows() == 2);
    auto* starred = shellButton(app,"Starred");
    REQUIRE(starred != nullptr);
    starred->onClick();
    REQUIRE(library->getNumRows() == 1);
    REQUIRE(library->getSelectedSamples().empty());
    const auto shot = juce::SystemStats::getEnvironmentVariable("SONICATOR_STAR_SCREENSHOT",{});
    if (shot.isNotEmpty())
    {
        app.setBounds(0,0,1440,900);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        library->selectRow(0);
        auto stream = juce::File(shot).createOutputStream(); REQUIRE(stream != nullptr);
        juce::PNGImageFormat png;
        REQUIRE(png.writeImageToStream(app.createComponentSnapshot(app.getLocalBounds()),*stream));
    }
    starred->onClick();
    REQUIRE(library->getNumRows() == 2);
}
TEST_CASE("Hide duplicates shows one byte-identical copy", "[ui][duplicates]")
{
    TestDirectory t;
    auto a = t.wave("a.wav");
    REQUIRE(a.copyFileTo(t.dir.getChildFile("b.wav")));
    auto c = t.wave("c.wav", 660);
    auto dbPath = t.dir.getChildFile("library.sqlite");
    {
        Database db(dbPath); SampleRepository repo(db); repo.createTables();
        for (const auto& file : { a, t.dir.getChildFile("b.wav"), c })
        {
            auto r = BasicAnalyzer().analyzeAndHash(file); REQUIRE(r.error.isEmpty()); repo.upsert(r);
        }
        repo.addRoot(t.dir.getFullPathName());
    }
    MainComponent app(dbPath,false);
    auto* library = dynamic_cast<FileListPanel*>(app.findChildWithID("library"));
    REQUIRE(library != nullptr); REQUIRE(library->getNumRows() == 3);
    auto* hide = shellButton(app,"Hide duplicates");
    REQUIRE(hide != nullptr);
    hide->onClick();
    REQUIRE(library->getNumRows() == 2);
    const auto shot = juce::SystemStats::getEnvironmentVariable("SONICATOR_DUPLICATES_SCREENSHOT",{});
    if (shot.isNotEmpty())
    {
        app.setBounds(0,0,1440,900);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        auto stream = juce::File(shot).createOutputStream(); REQUIRE(stream != nullptr);
        juce::PNGImageFormat png;
        REQUIRE(png.writeImageToStream(app.createComponentSnapshot(app.getLocalBounds()),*stream));
    }
    auto* clear = shellButton(app,"Clear filters");
    REQUIRE(clear != nullptr);
    clear->onClick();
    REQUIRE(library->getNumRows() == 3);
}
