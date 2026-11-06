#include <catch2/catch_test_macros.hpp>
#include "ui/library/FileListPanel.h"
#include "ui/preview/PreviewPanel.h"
#include "ui/library/FolderTree.h"
#include "core/indexing/IndexingService.h"
#include "TestHelpers.h"
using namespace sonicator;
TEST_CASE("Auto Preview is independently switchable while MIDI remains enabled", "[ui][auto-preview]")
{
    TestDirectory t; AudioEngine engine(false); PreviewPanel preview(engine,t.dir.getChildFile("cache"));
    auto* autoPreview = dynamic_cast<juce::ToggleButton*>(preview.findChildWithID("autoPreview"));
    REQUIRE(autoPreview != nullptr); REQUIRE(autoPreview->getToggleState());
    REQUIRE(engine.setMidiPreview(true));
    REQUIRE(preview.loadFile(t.wave("a.wav"))); REQUIRE(engine.isPlaying()); REQUIRE(engine.isMidiPreview());
    autoPreview->setToggleState(false,juce::sendNotificationSync);
    REQUIRE(preview.loadFile(t.wave("b.wav"))); REQUIRE_FALSE(engine.isPlaying()); REQUIRE(engine.isMidiPreview());
}
TEST_CASE("Preview controls leave keyboard navigation with the library", "[ui][focus]")
{
    TestDirectory t; AudioEngine engine(false); PreviewPanel preview(engine,t.dir.getChildFile("cache"));
    int buttons = 0;
    for (auto* child : preview.getChildren())
        if (auto* button = dynamic_cast<juce::Button*>(child))
        { ++buttons; REQUIRE_FALSE(button->getWantsKeyboardFocus()); REQUIRE_FALSE(button->getMouseClickGrabsKeyboardFocus()); }
    REQUIRE(buttons == 6);
}
TEST_CASE("Added folder tree includes deep descendants and responds to arrow keys", "[ui][folders]")
{
    TestDirectory t; Database db(t.dir.getChildFile("tree.sqlite")); SampleRepository repo(db); repo.createTables();
    auto rootPath = t.dir.getChildFile("audio").getFullPathName(); repo.addRoot(rootPath);
    SampleRecord r; r.filePath = rootPath + "/deep/more/sample.wav"; r.fileName = "sample.wav"; repo.upsert(r);
    FolderTree folders; folders.refresh(repo);
    auto* tree = dynamic_cast<juce::TreeView*>(folders.getChildComponent(0)); REQUIRE(tree != nullptr);
    auto* root = tree->getRootItem(); REQUIRE(root != nullptr); REQUIRE(root->getNumSubItems() == 1);
    auto* audio = root->getSubItem(0); audio->setSelected(true,true);
    juce::String selection; folders.onFolder = [&](juce::String p) { selection = p; };
    REQUIRE(tree->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    REQUIRE(audio->isOpen()); REQUIRE(audio->getNumSubItems() == 1);
    auto* deep = audio->getSubItem(0); deep->setSelected(true,true);
    REQUIRE(selection == rootPath+"/deep");
    REQUIRE(tree->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    REQUIRE(deep->isOpen()); REQUIRE(deep->getNumSubItems() == 1);
    REQUIRE(tree->keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    REQUIRE_FALSE(deep->isOpen());
    folders.refresh(repo);
    REQUIRE(tree->getSelectedItem(0) != nullptr);
    REQUIRE(tree->getSelectedItem(0)->getUniqueName() == rootPath+"/deep");
}
TEST_CASE("Nested added roots share the same descendant hierarchy", "[ui][folders]")
{
    TestDirectory t; Database db(t.dir.getChildFile("nested.sqlite")); SampleRepository repo(db); repo.createTables();
    auto path = t.dir.getChildFile("audio").getFullPathName(); repo.addRoot(path); repo.addRoot(path+"/Drums");
    SampleRecord r; r.filePath = path+"/Drums/deep/kick.wav"; r.fileName = "kick.wav"; repo.upsert(r);
    FolderTree folders; folders.refresh(repo);
    auto* tree = dynamic_cast<juce::TreeView*>(folders.getChildComponent(0)); REQUIRE(tree != nullptr);
    REQUIRE(tree->getRootItem()->getNumSubItems() == 2);
    auto* nested = tree->getRootItem()->getSubItem(1); nested->setOpen(true);
    REQUIRE(nested->getNumSubItems() == 1);
    REQUIRE(nested->getSubItem(0)->getUniqueName() == path+"/Drums/deep");
}
TEST_CASE("Refreshing a selected row does not trigger preview again", "[ui][regression]")
{
    FileListPanel list;
    SampleRecord a; a.id = 1; a.fileName = "a.wav";
    int callbacks = 0;
    list.onSelected = [&](const auto&) { ++callbacks; };
    list.setSamples({a}); list.selectRow(0);
    REQUIRE(callbacks == 1);
    list.setSamples({a});
    REQUIRE(list.getSelectedRow() == 0);
    REQUIRE(callbacks == 1);
}
TEST_CASE("Reloading the same preview does not reset transport", "[ui][regression]")
{
    TestDirectory t; auto file = t.wave(); AudioEngine engine(false);
    PreviewPanel preview(engine,t.dir.getChildFile("cache"));
    REQUIRE(preview.loadFile(file)); engine.seek(0.4);
    REQUIRE(preview.loadFile(file));
    REQUIRE(engine.position() > 0.39);
}
TEST_CASE("Library filters names and keeps selection identity after sorting", "[ui]")
{
    FileListPanel list;
    SampleRecord a, b; a.id = 1; a.fileName = "Kick.wav"; a.duration = 2;
    b.id = 2; b.fileName = "Bass.wav"; b.duration = 1;
    list.setSamples({a,b}); REQUIRE(list.getNumRows() == 2);
    int64_t selected = 0;
    list.onSelected = [&](const auto& r) { selected = r.id; };
    list.sortOrderChanged(3,true); list.selectedRowsChanged(0); REQUIRE(selected == 2);
    list.setSearchText("KICK"); REQUIRE(list.getNumRows() == 1);
    list.selectedRowsChanged(0); REQUIRE(selected == 1);
    list.setSearchText("no-match"); REQUIRE(list.getNumRows() == 0);
    list.selectedRowsChanged(-1); REQUIRE(selected == 1);
}
TEST_CASE("Unicode filenames survive filesystem indexing, database reopen and preview", "[ui][unicode]")
{
    TestDirectory t; auto file = t.wave(juce::String::fromUTF8("папка/音_🎵.wav"));
    auto database = t.dir.getChildFile("catalog.sqlite");
    {
        Database db(database); SampleRepository repo(db); repo.createTables(); IndexingService index(repo);
        REQUIRE(index.start({file.getParentDirectory().getFullPathName()})); REQUIRE(index.waitUntilFinished(10000));
        REQUIRE(index.progress().analyzed == 1);
    }
    Database db(database); SampleRepository repo(db); repo.createTables();
    auto records = repo.page(); REQUIRE(records.size() == 1);
    REQUIRE(records[0].fileName.toStdString() == "音_🎵.wav");
    AudioEngine engine(false); PreviewPanel preview(engine,t.dir.getChildFile("cache"));
    REQUIRE(preview.loadFile(juce::File(records[0].filePath)));
    REQUIRE(engine.length() > 0.99);
}
TEST_CASE("Preview loads real audio and keyboard toggles playback", "[ui]")
{
    TestDirectory t; AudioEngine engine(false);
    PreviewPanel preview(engine,t.dir.getChildFile("cache"));
    REQUIRE(preview.loadFile(t.wave())); REQUIRE(engine.isPlaying());
    REQUIRE(preview.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    REQUIRE_FALSE(engine.isPlaying());
    REQUIRE_FALSE(preview.loadFile(t.dir.getChildFile("absent.wav")));
    preview.setBounds(0,0,900,156);
    auto image = preview.createComponentSnapshot(preview.getLocalBounds());
    REQUIRE(image.isValid());
}
