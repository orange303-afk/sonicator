#include <catch2/catch_test_macros.hpp>
#include "ui/MainComponent.h"
#include "ui/library/FileListPanel.h"
#include "ui/library/TagStrip.h"
#include "ui/settings/RelocateFoldersPanel.h"
#include "core/database/SampleRepository.h"
#include "core/dsp/BasicAnalyzer.h"
#include "TestHelpers.h"
#include <iostream>
using namespace sonicator;
TEST_CASE("Library popup uses the application theme for hover rendering", "[ui][menu-polish]")
{
    TestDirectory t; MainComponent app(t.dir.getChildFile("menu.sqlite"), false);
    auto* library = dynamic_cast<juce::Button*>(app.findChildWithID("libraryMenu")); REQUIRE(library != nullptr);
    library->onClick();
    auto* menu = juce::Component::getCurrentlyModalComponent(); REQUIRE(menu != nullptr);
    CHECK(&menu->getLookAndFeel() == &app.getLookAndFeel());
    juce::PopupMenu::dismissAllActiveMenus();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
}
TEST_CASE("Folder context menu stays beside the click instead of jumping above it", "[ui][menu-polish]")
{
    TestDirectory t; MainComponent app(t.dir.getChildFile("context.sqlite"), false);
    auto* folders = dynamic_cast<FolderTree*>(app.findChildWithID("folderTree")); REQUIRE(folders != nullptr);
    const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay(); REQUIRE(display != nullptr);
    const auto screen = display->userBounds;
    const juce::Point<int> click(screen.getX() + screen.getWidth() / 3, screen.getY() + screen.getHeight() * 11 / 20);
    folders->onContextMenu(t.dir.getFullPathName(), false, click);
    auto* menu = juce::Component::getCurrentlyModalComponent(); REQUIRE(menu != nullptr);
    const auto bounds = menu->getScreenBounds();
    CHECK(std::abs(bounds.getY() - click.y) < 10);
    CHECK(std::abs(bounds.getX() - click.x) < 30);
    juce::PopupMenu::dismissAllActiveMenus();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
}
TEST_CASE("Library actions are compact and search has a working clear control", "[ui][polish]")
{
    TestDirectory t; MainComponent app(t.dir.getChildFile("polish.sqlite"), false);
    auto* menu = dynamic_cast<juce::Button*>(app.findChildWithID("libraryMenu"));
    REQUIRE(menu != nullptr);
    REQUIRE(menu->isVisible());
    REQUIRE(menu->getButtonText() == "LIBRARY");
    for (auto* child : app.getChildren())
        if (auto* button = dynamic_cast<juce::Button*>(child))
            if (button->getButtonText() == "+ Add folder" || button->getButtonText() == "Rescan")
                REQUIRE_FALSE(button->isVisible());
    auto* search = dynamic_cast<SearchField*>(app.findChildWithID("search"));
    REQUIRE(search != nullptr);
    auto* clear = dynamic_cast<juce::Button*>(search->findChildWithID("clearSearch"));
    REQUIRE(clear != nullptr);
    search->setText("pad", false);
    clear->onClick();
    REQUIRE(search->getText().isEmpty());
}
TEST_CASE("Toolbar buttons do not steal keyboard focus from library navigation", "[ui][focus]")
{
    TestDirectory t; MainComponent app(t.dir.getChildFile("focus.sqlite"),false);
    int buttons = 0;
    for (auto* child : app.getChildren())
        if (auto* button = dynamic_cast<juce::Button*>(child))
        { ++buttons; REQUIRE_FALSE(button->getWantsKeyboardFocus()); REQUIRE_FALSE(button->getMouseClickGrabsKeyboardFocus()); }
    REQUIRE(buttons == 16); // includes the compact library menu
}
TEST_CASE("Hide empty restores on startup and saves toggles without losing other settings", "[ui][settings][hide-empty]")
{
    TestDirectory t;
    const auto settings = t.dir.getChildFile("settings.json");
    REQUIRE(settings.replaceWithText("{\"hideEmpty\":true,\"language\":\"English\",\"searchHistory\":[\"pad\"]}"));
    SettingsStore store(settings);
    {
        MainComponent app(t.dir.getChildFile("catalog.sqlite"), false, store.load(), settings);
        auto* toggle = dynamic_cast<juce::ToggleButton*>(app.findChildWithID("hideEmpty"));
        REQUIRE(toggle != nullptr);
        REQUIRE(toggle->getToggleState());
        toggle->setToggleState(false, juce::dontSendNotification);
        toggle->onClick();
        REQUIRE_FALSE((bool) juce::JSON::parse(settings)["hideEmpty"]);
        REQUIRE(store.load().searchHistory[0] == "pad");
        toggle->setToggleState(true, juce::dontSendNotification);
        toggle->onClick();
        REQUIRE((bool) juce::JSON::parse(settings)["hideEmpty"]);
    }
    MainComponent reopened(t.dir.getChildFile("catalog.sqlite"), false, store.load(), settings);
    auto* restored = dynamic_cast<juce::ToggleButton*>(reopened.findChildWithID("hideEmpty"));
    REQUIRE(restored != nullptr);
    REQUIRE(restored->getToggleState());
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
TEST_CASE("Settings dialog closes when clicking outside", "[ui][dialog][settings]")
{
    TestDirectory t;
    MainComponent app(t.dir.getChildFile("library.sqlite"),false);
    juce::TextButton* settings = nullptr;
    for (auto* child : app.getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*>(child))
            if (button->getButtonText() == "Settings") settings = button;
    REQUIRE(settings != nullptr);
    settings->onClick();
    auto* modal = juce::Component::getCurrentlyModalComponent();
    REQUIRE(modal != nullptr);
    const auto shot = juce::SystemStats::getEnvironmentVariable("SONICATOR_SETTINGS_SCREENSHOT",{});
    if (shot.isNotEmpty())
    {
        auto stream = juce::File(shot).createOutputStream();
        REQUIRE(stream != nullptr);
        juce::PNGImageFormat png;
        REQUIRE(png.writeImageToStream(modal->createComponentSnapshot(modal->getLocalBounds(),true,2.0f),*stream));
    }
    modal->inputAttemptWhenModal();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    REQUIRE(juce::Component::getCurrentlyModalComponent() == nullptr);
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
juce::Button* dialogButton(juce::Component& parent, const juce::String& text)
{
    for (auto* child : parent.getChildren())
    {
        if (auto* button = dynamic_cast<juce::Button*>(child))
            if (button->getButtonText() == text) return button;
        if (auto* nested = dialogButton(*child, text)) return nested;
    }
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
    REQUIRE(dynamic_cast<SonicatorLookAndFeel*>(&warning->getLookAndFeel()) != nullptr);
    warning->exitModalState(0);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
}
TEST_CASE("Backup database lives in Settings and writes a consistent copy", "[ui][maintenance]")
{
    TestDirectory t; auto audio = t.wave("a.wav"); auto dbPath = t.dir.getChildFile("library.sqlite");
    {
        Database db(dbPath); SampleRepository repo(db); repo.createTables();
        auto r = BasicAnalyzer().analyze(audio); repo.upsert(r); repo.addRoot(t.dir.getFullPathName());
    }
    MainComponent app(dbPath,false);
    auto* settings = shellButton(app,"Settings");
    REQUIRE(settings != nullptr);
    settings->onClick();
    auto* modal = juce::Component::getCurrentlyModalComponent();
    REQUIRE(modal != nullptr);
    auto* window = dynamic_cast<juce::DocumentWindow*>(modal);
    REQUIRE(window != nullptr);
    REQUIRE_FALSE(window->isUsingNativeTitleBar());
    REQUIRE(dynamic_cast<SonicatorLookAndFeel*>(&modal->getLookAndFeel()) != nullptr);
    auto* backup = dialogButton(*modal,"Backup database");
    REQUIRE(backup != nullptr);
    auto* content = window->getContentComponent();
    REQUIRE(content != nullptr);
    REQUIRE(content->getLocalBounds().contains(backup->getBounds()));
    backup->onClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    auto copies = t.dir.getChildFile("backups").findChildFiles(juce::File::findFiles,false,"sonicator-*.sqlite");
    REQUIRE(copies.size() == 1);
    Database copy(copies[0]); SampleRepository copyRepo(copy); copyRepo.createTables();
    REQUIRE(copyRepo.count() == 1);
    REQUIRE_FALSE(copies[0].isDirectory());
    modal->exitModalState(0);
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
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        const auto image = dark.createComponentSnapshot(dark.getLocalBounds());
        REQUIRE(image.isValid());
        const auto shot = juce::SystemStats::getEnvironmentVariable("SONICATOR_DARK_SCREENSHOT",{});
        if (shot.isNotEmpty())
        {
            auto stream = juce::File(shot).createOutputStream(); REQUIRE(stream != nullptr);
            juce::PNGImageFormat png; REQUIRE(png.writeImageToStream(image,*stream));
        }
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
TEST_CASE("Hide empty filters fully silent files", "[ui][empty]")
{
    TestDirectory t;
    auto loud = t.wave("loud.wav");
    auto silent = t.wave("silent.wav", 0);
    auto dbPath = t.dir.getChildFile("empty-library.sqlite");
    {
        Database db(dbPath); SampleRepository repo(db); repo.createTables();
        for (const auto& file : { loud, silent })
        {
            auto r = BasicAnalyzer().analyzeAndHash(file); REQUIRE(r.error.isEmpty()); repo.upsert(r);
        }
        repo.addRoot(t.dir.getFullPathName());
    }
    MainComponent app(dbPath,false);
    auto* library = dynamic_cast<FileListPanel*>(app.findChildWithID("library"));
    REQUIRE(library != nullptr); REQUIRE(library->getNumRows() == 1);
    auto* hide = dynamic_cast<juce::ToggleButton*>(app.findChildWithID("hideEmpty"));
    REQUIRE(hide != nullptr);
    REQUIRE(hide->getToggleState());
    hide->setToggleState(false, juce::sendNotificationSync);
    REQUIRE(library->getNumRows() == 2);
    hide->setToggleState(true, juce::sendNotificationSync);
    REQUIRE(library->getNumRows() == 1);
    auto* clear = shellButton(app,"Clear filters");
    REQUIRE(clear != nullptr);
    clear->onClick();
    REQUIRE(library->getNumRows() == 2);
    REQUIRE_FALSE(hide->getToggleState());
}
TEST_CASE("Page size options page or virtualize the library", "[ui][paging]")
{
    TestDirectory t; auto dbPath = t.dir.getChildFile("paging.sqlite");
    {
        Database db(dbPath); SampleRepository repo(db); repo.createTables();
        for (int i = 0; i < 120; ++i)
        {
            SampleRecord r;
            r.fileName = "sample_" + juce::String(i).paddedLeft('0', 3) + ".wav";
            r.filePath = "/virtual/" + r.fileName;
            r.fileHash = "hash" + juce::String(i); r.format = "WAV"; r.version = basicFeaturesVersion;
            r.peakLevel = 0.5;
            repo.upsert(r);
        }
    }
    auto settingsFile = t.dir.getChildFile("settings.json");
    AppSettings settings; settings.pageSize = 100;
    MainComponent app(dbPath,false,settings,settingsFile);
    auto* library = dynamic_cast<FileListPanel*>(app.findChildWithID("library"));
    REQUIRE(library != nullptr); REQUIRE(library->getNumRows() == 100);
    auto* pageSize = dynamic_cast<juce::ComboBox*>(app.findChildWithID("pageSize"));
    REQUIRE(pageSize != nullptr);
    pageSize->setSelectedId(1000000, juce::sendNotificationSync);
    REQUIRE(library->getNumRows() == 120);
    REQUIRE(SettingsStore(settingsFile).load().pageSize == 0);
    library->selectRow(119);
    const auto selected = library->getSelectedSamples();
    REQUIRE(selected.size() == 1);
    REQUIRE(selected[0].fileName == "sample_119.wav");
    const auto shot = juce::SystemStats::getEnvironmentVariable("SONICATOR_PAGING_SCREENSHOT",{});
    if (shot.isNotEmpty())
    {
        app.setBounds(0,0,1440,900);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        auto stream = juce::File(shot).createOutputStream(); REQUIRE(stream != nullptr);
        juce::PNGImageFormat png;
        REQUIRE(png.writeImageToStream(app.createComponentSnapshot(app.getLocalBounds()),*stream));
    }
    pageSize->setSelectedId(100, juce::sendNotificationSync);
    REQUIRE(library->getNumRows() == 100);
    auto* next = shellButton(app,">");
    REQUIRE(next != nullptr);
    next->onClick();
    REQUIRE(library->getNumRows() == 20);
}
TEST_CASE("Filter bar has Hide empty and no Hide duplicates button", "[ui][filters]")
{
    TestDirectory t;
    MainComponent app(t.dir.getChildFile("filters.sqlite"),false);
    auto* hideEmpty = dynamic_cast<juce::ToggleButton*>(app.findChildWithID("hideEmpty"));
    REQUIRE(hideEmpty != nullptr);
    REQUIRE(hideEmpty->getToggleState());
    REQUIRE_FALSE(hideEmpty->getWantsKeyboardFocus());
    REQUIRE_FALSE(hideEmpty->getMouseClickGrabsKeyboardFocus());
    REQUIRE(app.findChildWithID("hideDuplicates") == nullptr);
}
TEST_CASE("A catalog with missing roots shows a relocation hint at startup", "[ui][relocation]")
{
    TestDirectory t;
    const auto gone = t.dir.getChildFile("gone/Drums").getFullPathName();
    auto dbPath = t.dir.getChildFile("missing-roots.sqlite");
    {
        Database db(dbPath); SampleRepository repo(db); repo.createTables();
        SampleRecord r; r.filePath = gone + "/kick.wav"; r.fileName = "kick.wav"; repo.upsert(r);
        repo.addRoot(gone);
    }
    MainComponent app(dbPath, false, {}, t.dir.getChildFile("settings.json"));
    auto* status = dynamic_cast<juce::Label*>(app.findChildWithID("importStatus"));
    REQUIRE(status != nullptr);
    REQUIRE(status->getText().contains("not found"));
    REQUIRE(status->getText().contains("Relocate"));
}

TEST_CASE("Relocation dialog lists every missing root", "[ui][relocation]")
{
    TestDirectory t;
    const auto gone = t.dir.getChildFile("gone/Drums").getFullPathName();
    auto dbPath = t.dir.getChildFile("dialog.sqlite");
    {
        Database db(dbPath); SampleRepository repo(db); repo.createTables();
        SampleRecord r; r.filePath = gone + "/kick.wav"; r.fileName = "kick.wav"; repo.upsert(r);
        repo.addRoot(gone);
    }
    MainComponent app(dbPath, false, {}, t.dir.getChildFile("settings.json"));
    app.showRelocateDialog();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    auto* modal = juce::Component::getCurrentlyModalComponent();
    REQUIRE(modal != nullptr);
    REQUIRE(modal->getName() == "Relocate folders");
    const auto shot = juce::SystemStats::getEnvironmentVariable("SONICATOR_RELOCATE_SCREENSHOT",{});
    if (shot.isNotEmpty())
    {
        auto stream = juce::File(shot).createOutputStream();
        REQUIRE(stream != nullptr);
        juce::PNGImageFormat png;
        REQUIRE(png.writeImageToStream(modal->createComponentSnapshot(modal->getLocalBounds(),true,2.0f),*stream));
    }
    modal->exitModalState(0);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
}

TEST_CASE("Opening a catalog with missing roots offers relocation", "[ui][relocation]")
{
    TestDirectory t;
    const auto gone = t.dir.getChildFile("gone/Drums").getFullPathName();
    auto settingsFile = t.dir.getChildFile("settings.json");
    auto dbA = t.dir.getChildFile("a.sqlite");
    {
        Database db(dbA); SampleRepository repo(db); repo.createTables();
        repo.addRoot(t.dir.getFullPathName());
    }
    auto dbB = t.dir.getChildFile("b.sqlite");
    {
        Database db(dbB); SampleRepository repo(db); repo.createTables();
        SampleRecord r; r.filePath = gone + "/kick.wav"; r.fileName = "kick.wav"; repo.upsert(r);
        repo.addRoot(gone);
    }
    MainComponent app(dbA, false, {}, settingsFile);
    juce::String error;
    REQUIRE(app.switchCatalog(dbB, false, error));
    REQUIRE(SettingsStore(settingsFile).load().databasePath == dbB.getFullPathName());
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    auto* modal = juce::Component::getCurrentlyModalComponent();
    REQUIRE(modal != nullptr);
    REQUIRE(modal->getName() == "Relocate folders");
    modal->exitModalState(0);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
}

TEST_CASE("Relocating through the dialog writes a backup and rewrites the catalog", "[ui][relocation]")
{
    TestDirectory t;
    const auto gone = t.dir.getChildFile("gone/Drums");
    const auto newRoot = t.dir.getChildFile("new-root"); newRoot.createDirectory();
    const auto dbPath = t.dir.getChildFile("apply.sqlite");
    {
        Database db(dbPath); SampleRepository repo(db); repo.createTables();
        SampleRecord r; r.filePath = gone.getChildFile("Kick/01.wav").getFullPathName();
        r.fileName = "01.wav"; r.fileHash = "h"; repo.upsert(r);
        repo.addRoot(gone.getFullPathName());
    }
    // The probe checks existence only; the fixture needs the remapped relative path.
    auto targetFile = newRoot.getChildFile("Kick/01.wav");
    REQUIRE(targetFile.getParentDirectory().createDirectory());
    REQUIRE(targetFile.replaceWithText("audio"));
    MainComponent app(dbPath, false, {}, t.dir.getChildFile("settings.json"));
    app.showRelocateDialog();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    auto* modal = dynamic_cast<juce::DialogWindow*>(juce::Component::getCurrentlyModalComponent());
    REQUIRE(modal != nullptr);
    auto* panel = dynamic_cast<RelocateFoldersPanel*>(modal->getContentComponent());
    REQUIRE(panel != nullptr);
    REQUIRE(panel->setTarget(0, newRoot));
    REQUIRE_FALSE(panel->needsConfirmation());
    auto* applyButton = dynamic_cast<juce::Button*>(panel->findChildWithID("relocateApply"));
    REQUIRE(applyButton != nullptr);
    applyButton->onClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    REQUIRE(juce::Component::getCurrentlyModalComponent() == nullptr);

    auto backups = t.dir.findChildFiles(juce::File::findFiles, false, "apply.sqlite.relocate-*.sqlite");
    REQUIRE(backups.size() == 1);
    {
        Database db(dbPath); SampleRepository repo(db); repo.createTables();
        REQUIRE_FALSE(repo.findByPath(gone.getChildFile("Kick/01.wav").getFullPathName()).has_value());
        auto moved = repo.findByPath(newRoot.getChildFile("Kick/01.wav").getFullPathName());
        REQUIRE(moved.has_value());
        REQUIRE(moved->fileHash == "h");
        REQUIRE(repo.roots().contains(newRoot.getFullPathName()));
        REQUIRE_FALSE(repo.roots().contains(gone.getFullPathName()));
    }
    auto* status = dynamic_cast<juce::Label*>(app.findChildWithID("importStatus"));
    REQUIRE(status != nullptr);
    REQUIRE(status->getText().contains("Relocated 1 file(s)"));
    REQUIRE(status->getText().contains("Backup"));
}
TEST_CASE("The bottom status bar shows the selected file path and smart values", "[ui][status]")
{
    TestDirectory t;
    const auto catalog = t.dir.getChildFile("status.sqlite");
    const auto audio = t.wave("status-pad.wav");
    {
        Database db(catalog); SampleRepository repo(db); repo.createTables();
        SampleRecord r;
        r.filePath = audio.getFullPathName(); r.fileName = audio.getFileName();
        r.fileHash = "h"; r.version = basicFeaturesVersion;
        r.format = "WAV"; r.duration = 1; r.sampleRate = 44100; r.numChannels = 1;
        r.peakLevel = 0.5;
        const auto id = repo.upsert(r);
        SmartPublication publication;
        publication.sampleId = id;
        publication.candidates = {
            { SmartField::Type, "filename", "pad", "pad", 0 },
            { SmartField::Bpm, "filename", "128bpm", {}, 128 },
            { SmartField::Key, "filename", "A minor", "A minor", 0 } };
        publication.catalogPath = repo.catalogPath();
        REQUIRE(repo.publishSmartAnalysis(publication).published);
    }
    MainComponent app(catalog, false);
    auto* library = dynamic_cast<FileListPanel*>(app.findChildWithID("library"));
    REQUIRE(library != nullptr);
    REQUIRE(library->getNumRows() == 1);
    library->selectedRowsChanged(0);
    auto* status = dynamic_cast<juce::Label*>(app.findChildWithID("selectionStatus"));
    REQUIRE(status != nullptr);
    REQUIRE(status->getText().contains(audio.getFullPathName()));
    REQUIRE(status->getText().contains("Type: pad"));
    REQUIRE(status->getText().contains("BPM: 128"));
    REQUIRE(status->getText().contains("Key: A minor"));
}
TEST_CASE("Preview toggles are restored on restart and saved when changed", "[ui][settings][preview]")
{
    TestDirectory t;
    const auto catalog = t.dir.getChildFile("preview-settings.sqlite");
    { Database db(catalog); SampleRepository repo(db); repo.createTables(); }
    const auto settings = t.dir.getChildFile("settings.json");
    REQUIRE(settings.replaceWithText(
        "{\"midiPreview\":true,\"skipSilence\":true,\"autoPreview\":false,"
        "\"loopPreview\":true,\"rexGate\":false}"));
    SettingsStore store(settings);
    MainComponent app(catalog, false, store.load(), settings);
    auto* preview = app.findChildWithID("preview");
    REQUIRE(preview != nullptr);
    auto* midi = dynamic_cast<juce::ToggleButton*>(preview->findChildWithID("midiPreview"));
    auto* skip = dynamic_cast<juce::ToggleButton*>(preview->findChildWithID("skipSilence"));
    auto* autoPreview = dynamic_cast<juce::ToggleButton*>(preview->findChildWithID("autoPreview"));
    auto* loop = dynamic_cast<juce::ToggleButton*>(preview->findChildWithID("loop"));
    auto* gate = dynamic_cast<juce::ToggleButton*>(preview->findChildWithID("rexGate"));
    REQUIRE(midi != nullptr); REQUIRE(skip != nullptr); REQUIRE(autoPreview != nullptr);
    REQUIRE(loop != nullptr); REQUIRE(gate != nullptr);
    REQUIRE(midi->getToggleState());
    REQUIRE(skip->getToggleState());
    REQUIRE_FALSE(autoPreview->getToggleState());
    REQUIRE(loop->getToggleState());
    REQUIRE_FALSE(gate->getToggleState());
    autoPreview->setToggleState(true, juce::dontSendNotification);
    autoPreview->onClick();
    REQUIRE((bool) juce::JSON::parse(settings)["autoPreview"]);
    gate->setToggleState(true, juce::dontSendNotification);
    gate->onClick();
    REQUIRE((bool) juce::JSON::parse(settings)["rexGate"]);
}
TEST_CASE("Shell shows the favorites section only when favorite folders exist", "[ui][favorites]")
{
    TestDirectory t;
    const auto catalog = t.dir.getChildFile("favorites-shell.sqlite");
    const auto settings = t.dir.getChildFile("favorites-settings.json");
    AppSettings withFavorite;
    withFavorite.favoriteFolders.add(t.dir.getChildFile("lib/Drums").getFullPathName());
    MainComponent app(catalog, false, withFavorite, settings);
    auto* label = dynamic_cast<juce::Label*>(app.findChildWithID("favoritesLabel"));
    auto* tree = dynamic_cast<FolderTree*>(app.findChildWithID("favoritesTree"));
    REQUIRE(label != nullptr);
    REQUIRE(tree != nullptr);
    REQUIRE(label->isVisible());
    REQUIRE(tree->isVisible());
    auto* folders = dynamic_cast<FolderTree*>(app.findChildWithID("folderTree"));
    REQUIRE(folders != nullptr);
    REQUIRE(tree->getBounds().getHeight() > 0);
    REQUIRE(tree->getBounds().getY() >= folders->getBounds().getBottom());
    auto* splitter = dynamic_cast<juce::Component*>(app.findChildWithID("favoritesSplitter"));
    REQUIRE(splitter != nullptr);
    REQUIRE(splitter->isVisible());
    const int oldHeight = tree->getBounds().getHeight();
    const auto now = juce::Time::getCurrentTime();
    const juce::Point<float> start(10.0f, 4.0f), end(10.0f, -36.0f);
    juce::MouseEvent down(juce::Desktop::getInstance().getMainMouseSource(), start,
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
        splitter, splitter, now, start, now, 1, false);
    splitter->mouseDown(down);
    juce::MouseEvent drag(juce::Desktop::getInstance().getMainMouseSource(), end,
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
        splitter, splitter, now, start, now, 1, true);
    splitter->mouseDrag(drag);
    splitter->mouseUp(drag);
    REQUIRE(tree->getBounds().getHeight() > oldHeight);
    SettingsStore saved(settings);
    REQUIRE(saved.load().favoritesPanelHeight > 200);

    AppSettings withoutFavorite;
    MainComponent empty(t.dir.getChildFile("favorites-empty.sqlite"), false, withoutFavorite, settings);
    auto* emptyLabel = dynamic_cast<juce::Label*>(empty.findChildWithID("favoritesLabel"));
    auto* emptyTree = dynamic_cast<FolderTree*>(empty.findChildWithID("favoritesTree"));
    REQUIRE(emptyLabel != nullptr);
    REQUIRE(emptyTree != nullptr);
    REQUIRE_FALSE(emptyLabel->isVisible());
    REQUIRE_FALSE(emptyTree->isVisible());
}
TEST_CASE("Last folder and selected file are restored on restart", "[ui][session]")
{
    TestDirectory t;
    const auto kick = t.wave("lib/Drums/kick.wav");
    const auto snare = t.wave("lib/Drums/snare.wav");
    const auto bass = t.wave("lib/Bass/bass.wav");
    const auto dbPath = t.dir.getChildFile("session.sqlite");
    const auto lib = t.dir.getChildFile("lib");
    {
        Database db(dbPath); SampleRepository repo(db); repo.createTables();
        for (const auto& file : { kick, snare, bass })
        {
            auto record = BasicAnalyzer().analyzeAndHash(file);
            REQUIRE(record.error.isEmpty());
            repo.upsert(record);
        }
        repo.addRoot(lib.getFullPathName());
    }
    const auto settingsFile = t.dir.getChildFile("session.json");
    AppSettings initial;
    initial.autoPreview = false;
    initial.lastFolder = lib.getChildFile("Drums").getFullPathName();
    initial.lastSamplePath = snare.getFullPathName();
    { SettingsStore store(settingsFile); store.save(initial); }
    {
        MainComponent app(dbPath, false, SettingsStore(settingsFile).load(), settingsFile);
        auto* library = dynamic_cast<FileListPanel*>(app.findChildWithID("library"));
        REQUIRE(library != nullptr);
        REQUIRE(library->getNumRows() == 2);
        REQUIRE(library->getSelectedRow() == 1); // snare
        auto* status = dynamic_cast<juce::Label*>(app.findChildWithID("selectionStatus"));
        REQUIRE(status != nullptr);
        REQUIRE(status->getText().contains(snare.getFullPathName()));
        auto* folders = dynamic_cast<FolderTree*>(app.findChildWithID("folderTree"));
        REQUIRE(folders != nullptr);
        auto* tree = dynamic_cast<juce::TreeView*>(folders->getChildComponent(0));
        REQUIRE(tree != nullptr);
        REQUIRE(tree->getSelectedItem(0) != nullptr);
        REQUIRE(tree->getSelectedItem(0)->getUniqueName() == lib.getChildFile("Drums").getFullPathName());
        library->selectRow(0); // kick becomes the last selection
        app.flushSessionState();
    }
    const auto saved = SettingsStore(settingsFile).load();
    REQUIRE(saved.lastFolder == lib.getChildFile("Drums").getFullPathName());
    REQUIRE(saved.lastSamplePath == kick.getFullPathName());
    MainComponent reopened(dbPath, false, saved, settingsFile);
    auto* library = dynamic_cast<FileListPanel*>(reopened.findChildWithID("library"));
    REQUIRE(library != nullptr);
    REQUIRE(library->getSelectedRow() == 0);
    auto* status = dynamic_cast<juce::Label*>(reopened.findChildWithID("selectionStatus"));
    REQUIRE(status != nullptr);
    REQUIRE(status->getText().contains(kick.getFullPathName()));
}

TEST_CASE("Hide empty and unsupported hides incompatible assets and keeps their stars", "[ui][empty][asset-compat]")
{
    TestDirectory t;
    auto loud = t.wave("compat-loud.wav");
    auto silent = t.wave("compat-silent.wav", 0);
    auto unsupportedPath = t.dir.getChildFile("compat-unsupported.rx2");
    unsupportedPath.replaceWithText("not a rex file");
    auto dbPath = t.dir.getChildFile("compat-library.sqlite");
    int64_t unsupportedId = 0;
    {
        Database db(dbPath); SampleRepository repo(db); repo.createTables();
        auto loudRecord = BasicAnalyzer().analyzeAndHash(loud); REQUIRE(loudRecord.error.isEmpty()); repo.upsert(loudRecord);
        auto silentRecord = BasicAnalyzer().analyzeAndHash(silent); REQUIRE(silentRecord.error.isEmpty()); repo.upsert(silentRecord);
        SampleRecord unsupported;
        unsupported.filePath = unsupportedPath.getFullPathName();
        unsupported.fileName = unsupportedPath.getFileName();
        unsupported.format = "RX2"; unsupported.version = metadataOnlyVersion;
        unsupported.error = "Not a supported REX2 file";
        unsupported.compatibility = AssetCompatibility::unsupported;
        unsupportedId = repo.upsert(unsupported);
        repo.setStarred({ unsupportedId }, true);
        repo.addRoot(t.dir.getFullPathName());
    }
    MainComponent app(dbPath,false,{},t.dir.getChildFile("compat-settings.json"));
    auto* library = dynamic_cast<FileListPanel*>(app.findChildWithID("library"));
    auto* hide = dynamic_cast<juce::ToggleButton*>(app.findChildWithID("hideEmpty"));
    REQUIRE(library != nullptr); REQUIRE(hide != nullptr);
    REQUIRE(hide->getButtonText() == "Hide empty and unsupported");
    REQUIRE(hide->getWidth() >= 170);
    REQUIRE(hide->getToggleState());
    REQUIRE(library->getNumRows() == 1);
    hide->setToggleState(false, juce::sendNotificationSync);
    REQUIRE(library->getNumRows() == 3);
    hide->setToggleState(true, juce::sendNotificationSync);
    REQUIRE(library->getNumRows() == 1);
    Database db(dbPath); SampleRepository repo(db); repo.createTables();
    const auto stored = repo.findByPath(unsupportedPath.getFullPathName());
    REQUIRE(stored.has_value());
    REQUIRE(stored->starred);
    REQUIRE(stored->compatibility == AssetCompatibility::unsupported);

    app.setSize(880, 580);
    const auto shot = juce::SystemStats::getEnvironmentVariable("SONICATOR_COMPAT_SCREENSHOT", {});
    if (shot.isNotEmpty())
    {
        auto stream = juce::File(shot).createOutputStream(); REQUIRE(stream != nullptr);
        juce::PNGImageFormat png;
        REQUIRE(png.writeImageToStream(app.createComponentSnapshot(app.getLocalBounds()), *stream));
    }
}
