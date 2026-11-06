#include <catch2/catch_test_macros.hpp>
#include "ui/MainComponent.h"
#include "ui/library/FileListPanel.h"
#include "core/database/SampleRepository.h"
#include "core/dsp/BasicAnalyzer.h"
#include "TestHelpers.h"
#include <iostream>
using namespace sonicator;
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
    REQUIRE(app.createComponentSnapshot(app.getLocalBounds()).isValid());
    app.setBounds(0,0,1440,900);
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
