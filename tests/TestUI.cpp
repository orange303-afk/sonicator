#include <catch2/catch_test_macros.hpp>
#include "ui/library/FileListPanel.h"
#include "ui/library/SearchField.h"
#include "ui/preview/PreviewPanel.h"
#include "ui/library/FolderTree.h"
#include "ui/library/TagStrip.h"
#include "ui/settings/SettingsPanel.h"
#include "ui/settings/RelocateFoldersPanel.h"
#include "ui/shell/WindowBounds.h"
#include <set>
#include "core/indexing/IndexingService.h"
#include "TestHelpers.h"
using namespace sonicator;
TEST_CASE("Search field requests history on left click, not on typing or right click", "[ui][search-history]")
{
    SearchField field;
    field.setPopupMenuEnabled(false);
    int requests = 0;
    field.onHistoryRequest = [&] { ++requests; };
    const auto now = juce::Time::getCurrentTime();
    auto click = [&](int flags)
    {
        field.mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),
            { 4.0f, 4.0f }, juce::ModifierKeys(flags), 1.0f, 0, 0, 0, 0,
            &field, &field, now, { 4.0f, 4.0f }, now, 1, false));
    };
    click(juce::ModifierKeys::leftButtonModifier);
    REQUIRE(requests == 1);
    field.setText("pad c4");
    REQUIRE(requests == 1);
    click(juce::ModifierKeys::leftButtonModifier);
    REQUIRE(requests == 2); // repeat click while the field already has focus
    click(juce::ModifierKeys::rightButtonModifier);
    REQUIRE(requests == 2);
}
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
    REQUIRE(buttons == 8);
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
TEST_CASE("Header sorting requests a whole-catalog order without looping on page reload", "[ui][search]")
{
    FileListPanel list;
    SampleRecord a, b; a.id = 1; a.fileName = "Kick.wav"; a.duration = 2;
    b.id = 2; b.fileName = "Bass.wav"; b.duration = 1;
    int calls = 0, lastColumn = 0; bool lastAscending = true;
    list.onSortChanged = [&](int column, bool ascending) { ++calls; lastColumn = column; lastAscending = ascending; };
    list.setSamples({a,b});
    REQUIRE(calls == 0);
    list.sortOrderChanged(3,false);
    REQUIRE(calls == 1); REQUIRE(lastColumn == 3); REQUIRE_FALSE(lastAscending);
    list.setSamples({b,a});
    REQUIRE(calls == 1);
}
TEST_CASE("Tag strip lays out coloured chips and reports its height", "[ui][tags]")
{
    TagStrip strip;
    REQUIRE(strip.getPreferredHeight(200) == 24);
    Tag kick; kick.id = 1; kick.name = "Kick"; kick.colour = 0xffe05a5a;
    Tag loop; loop.id = 2; loop.name = "Loop"; loop.colour = 0xff9ad06a;
    strip.setTags({kick,loop});
    REQUIRE(strip.getTags().size() == 2);
    const int height = strip.getPreferredHeight(200);
    REQUIRE(height >= 22);
    strip.setSize(200,height);
    REQUIRE(strip.createComponentSnapshot(strip.getLocalBounds()).isValid());
    strip.setTags({});
    REQUIRE(strip.getPreferredHeight(200) == 24);
}
TEST_CASE("Settings panel exposes theme, scale and the support links", "[ui][settings]")
{
    SettingsPanel panel(AppSettings{}, juce::File("/tmp/catalog.sqlite"), false);
    auto* themeBox = dynamic_cast<juce::ComboBox*>(panel.findChildWithID("settingsTheme"));
    auto* scaleBox = dynamic_cast<juce::ComboBox*>(panel.findChildWithID("settingsScale"));
    REQUIRE(themeBox != nullptr); REQUIRE(scaleBox != nullptr);
    AppSettings changed; int calls = 0;
    panel.onSettingsChanged = [&](const AppSettings& value) { changed = value; ++calls; };
    themeBox->setSelectedId(3, juce::sendNotificationSync);
    REQUIRE(calls == 1); REQUIRE(changed.theme == ThemeMode::Light);
    scaleBox->setSelectedId(125, juce::sendNotificationSync);
    REQUIRE(calls == 2); REQUIRE(changed.scalePercent == 125);

    std::set<juce::String> urls;
    for (auto* child : panel.getChildren())
        if (auto* link = dynamic_cast<juce::HyperlinkButton*>(child)) urls.insert(link->getURL().toString(true));
    REQUIRE(urls.count("https://ilyaorange.gumroad.com") == 1);
    REQUIRE(urls.count("https://ilyaorange.bandcamp.com") == 1);
    REQUIRE(urls.count("https://naukograd.bandcamp.com") == 1);
    REQUIRE(urls.count("https://ko-fi.com/ilyaorange") == 1);
    panel.setBounds(0,0,560,700);
    const auto image = panel.createComponentSnapshot(panel.getLocalBounds());
    REQUIRE(image.isValid());
    const auto shot = juce::SystemStats::getEnvironmentVariable("SONICATOR_SETTINGS_SCREENSHOT",{});
    if (shot.isNotEmpty())
    {
        auto stream = juce::File(shot).createOutputStream(); REQUIRE(stream != nullptr);
        juce::PNGImageFormat png; REQUIRE(png.writeImageToStream(image,*stream));
    }
}

TEST_CASE("Saved window geometry is clamped and off-screen bounds are recentred", "[ui][window]")
{
    const juce::Rectangle<int> area { 0, 0, 1440, 900 };
    const auto same = resolveWindowBounds({ 100, 80, 1200, 700 }, area);
    REQUIRE(same == juce::Rectangle<int>(100, 80, 1200, 700));
    const auto offscreen = resolveWindowBounds({ 3000, 2000, 1200, 700 }, area);
    REQUIRE(area.contains(offscreen));
    REQUIRE(offscreen.getWidth() == 1200);
    REQUIRE(offscreen.getHeight() == 700);
    const auto small = resolveWindowBounds({ 10, 10, 100, 100 }, area);
    REQUIRE(small.getWidth() == 880);
    REQUIRE(small.getHeight() == 580);
    const auto empty = resolveWindowBounds({ 0, 0, 0, 0 }, area);
    REQUIRE(empty.getWidth() == 1280);
    REQUIRE(empty.getHeight() == 800);
    REQUIRE(area.contains(empty));
    const auto large = resolveWindowBounds({ 0, 0, 3000, 2000 }, area);
    REQUIRE(large.getWidth() == 1440);
    REQUIRE(large.getHeight() == 900);
}
TEST_CASE("Multiple selected rows keep their identities across a refresh", "[ui][selection]")
{
    FileListPanel list;
    SampleRecord a, b, c;
    a.id = 1; a.fileName = "a.wav"; a.starred = true;
    b.id = 2; b.fileName = "b.wav";
    c.id = 3; c.fileName = "c.wav";
    list.setSamples({ a, b, c });
    list.selectRows({ 0, 2 });
    REQUIRE(list.getSelectedSamples().size() == 2);
    REQUIRE(list.getSelectedSamples()[0].id == 1);
    REQUIRE(list.getSelectedSamples()[1].id == 3);
    list.setSamples({ c, a, b });
    REQUIRE(list.getSelectedSamples().size() == 2);
    // The panel sorts by name: a.wav (1) then c.wav (3), regardless of insertion order.
    REQUIRE(list.getSelectedSamples()[0].id == 1);
    REQUIRE(list.getSelectedSamples()[1].id == 3);
    list.setBounds(0, 0, 900, 300);
    REQUIRE(list.createComponentSnapshot(list.getLocalBounds()).isValid());
}
TEST_CASE("Virtual sample rows load in blocks and keep multi-selection", "[ui][virtual]")
{
    FileListPanel list;
    const std::vector<int64_t> ids { 30, 10, 20 };
    int loads = 0;
    FileListPanel::RowLoader loader = [&](const std::vector<int64_t>& requested)
    {
        ++loads;
        FileListPanel::LoadedRows loaded;
        for (auto id : requested)
        {
            SampleRecord r; r.id = id; r.fileName = "s" + juce::String(id) + ".wav";
            if (id == 10) r.starred = true;
            loaded.records.push_back(r);
            if (id == 20) { Tag tag; tag.id = 1; tag.name = "Kick"; loaded.tags[id] = { tag }; }
            if (id == 30) loaded.duplicates[id] = 2;
        }
        return loaded;
    };
    list.setVirtualSamples(ids, loader);
    REQUIRE(list.getNumRows() == 3);
    list.setBounds(0, 0, 900, 260);
    REQUIRE(list.createComponentSnapshot(list.getLocalBounds()).isValid());
    REQUIRE(loads > 0);
    list.selectRows({ 0, 2 });
    REQUIRE(list.getSelectedSamples().size() == 2);
    REQUIRE(list.getSelectedSamples()[0].id == 30);
    REQUIRE(list.getSelectedSamples()[1].id == 20);
    REQUIRE(list.getSelectedSamples()[0].fileName == "s30.wav");
}
TEST_CASE("Relocation panel enables applying only after every folder is chosen", "[ui][relocation]")
{
    TestDirectory t;
    auto target = t.dir.getChildFile("target"); target.createDirectory();
    RelocateFoldersPanel panel({{ "/missing/Drums", 12 }, { "/missing/Bass", 7 }});
    panel.onProbe = [](const juce::String&, const juce::String&) { return RelocationProbe{ 10, 0 }; };
    REQUIRE_FALSE(panel.canApply());
    REQUIRE_FALSE(panel.setTarget(0, t.dir.getChildFile("absent")));
    REQUIRE(panel.setTarget(0, target));
    REQUIRE(panel.needsConfirmation());
    REQUIRE_FALSE(panel.canApply());
    auto other = t.dir.getChildFile("other"); other.createDirectory();
    panel.onProbe = [](const juce::String&, const juce::String&) { return RelocationProbe{ 10, 10 }; };
    REQUIRE(panel.setTarget(0, target));
    REQUIRE(panel.setTarget(1, other));
    REQUIRE(panel.canApply());
    REQUIRE_FALSE(panel.needsConfirmation());
    auto rows = panel.relocations();
    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].oldRoot == "/missing/Drums");
    REQUIRE(rows[0].newRoot == target.getFullPathName());

    REQUIRE_FALSE(panel.setTarget(1, target)); // duplicate target
    REQUIRE(panel.canApply()); // the previous valid target is kept
    REQUIRE(panel.relocations()[1].newRoot == other.getFullPathName());
}
TEST_CASE("Type, BPM and Key columns render every display state", "[ui][columns]")
{
    FileListPanel list;
    SampleRecord a, b, c;
    a.id = 1; a.fileName = "pad.wav"; a.format = "WAV"; a.duration = 2; a.sampleRate = 44100; a.numChannels = 2;
    b.id = 2; b.fileName = "unknown.wav"; b.format = "WAV";
    c.id = 3; c.fileName = "not-analyzed.wav"; c.format = "WAV";
    list.setSamples({a, b, c});
    SmartDisplay pad;
    pad.type = "pad"; pad.typeState = SmartState::Resolved;
    pad.bpm = 128; pad.hasBpm = true; pad.bpmState = SmartState::Conflicting;
    pad.key = "A minor"; pad.keyState = SmartState::Resolved;
    pad.pipelineState = SmartPipelineState::Complete;
    pad.bpmOverride = OverrideState::Value;
    SmartDisplay unknown;
    unknown.pipelineState = SmartPipelineState::Complete;
    unknown.typeState = SmartState::Unknown;
    unknown.bpmState = SmartState::Unknown;
    unknown.keyState = SmartState::Unknown;
    list.setSmartDisplays({ { 1, pad }, { 2, unknown } });
    list.setSize(860, 200);
    REQUIRE(list.getNumRows() == 3);
    REQUIRE(list.createComponentSnapshot(list.getLocalBounds()).isValid());
    int column = 0; bool forwards = true;
    list.onSortChanged = [&](int c, bool f) { column = c; forwards = f; };
    list.sortOrderChanged(11, false);
    REQUIRE(column == 11);
    REQUIRE_FALSE(forwards);
}
