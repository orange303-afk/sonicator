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
    REQUIRE(buttons == 11);
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
TEST_CASE("Audio settings dialog is destroyed before the application's audio engine", "[ui][dialog]")
{
    TestDirectory t;
    auto app = std::make_unique<MainComponent>(t.dir.getChildFile("library.sqlite"),false);
    juce::TextButton* settings = nullptr;
    for (auto* child : app->getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*>(child))
            if (button->getButtonText() == "Audio settings") settings = button;
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
