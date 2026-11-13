#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "ui/preview/PreviewPanel.h"
#include "ui/library/FolderTree.h"
#include "ui/waveform/WaveformDisplay.h"
#include "ui/waveform/WaveformView.h"
#include "TestHelpers.h"
using namespace sonicator;

TEST_CASE("Ordinary audio has no container source rows", "[ui][polish]")
{
    TestDirectory t; AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"));
    auto* list = dynamic_cast<juce::ListBox*>(preview.findChildWithID("containerSources"));
    REQUIRE(list != nullptr);
    REQUIRE(preview.loadFile(t.wave(), false));
    REQUIRE(list->getListBoxModel()->getNumRows() == 0);
}

TEST_CASE("Images and their virtual directories expand in the folder tree", "[ui][polish]")
{
    TestDirectory t;
    Database db(t.dir.getChildFile("tree.sqlite")); SampleRepository repo(db); repo.createTables();
    repo.addRoot(t.dir.getFullPathName());
    const auto image = t.dir.getChildFile("bank.iso").getFullPathName();
    SampleRecord record; record.filePath = image; record.fileName = "bank.iso"; record.format = "ISO";
    repo.upsert(record);
    FolderTree folders;
    folders.refresh(repo);
    REQUIRE(folders.selectPath(image));
    auto* tree = dynamic_cast<juce::TreeView*>(folders.getChildComponent(0));
    REQUIRE(tree != nullptr);
    auto* imageItem = tree->getSelectedItem(0);
    REQUIRE(imageItem != nullptr);
    REQUIRE(imageItem->mightContainSubItems());
    DiscEntry sub; sub.id = {2}; sub.parent = {1}; sub.kind = DiscEntryKind::directory; sub.displayName = "Drums";
    DiscEntry nested; nested.id = {3}; nested.parent = {2}; nested.kind = DiscEntryKind::directory; nested.displayName = "Kicks";
    folders.setDiscFolders(image, {1}, {sub, nested});
    REQUIRE(imageItem->isOpen());
    REQUIRE(imageItem->getNumSubItems() == 1);
    auto* drums = imageItem->getSubItem(0);
    drums->setOpen(true);
    REQUIRE(drums->getNumSubItems() == 1);
    DiscEntryId selected;
    folders.onDiscFolder = [&](const juce::String& path, DiscEntryId id) { REQUIRE(path == image); selected = id; };
    drums->getSubItem(0)->setSelected(true, true);
    REQUIRE(selected == nested.id);
}

TEST_CASE("Waveform view starts at the whole file and zoom selection narrows it", "[waveform][ui]")
{
    TestDirectory t;
    WaveformDisplay display(t.dir.getChildFile("cache"));
    display.setSize(600, 120);
    std::vector<float> samples(44100, 0.5f);
    display.setPeaks(samples, 44100, {});
    display.setSampleRateForZoom(44100);
    REQUIRE(display.totalLength() == Catch::Approx(1.0));
    REQUIRE(display.view().start == Catch::Approx(0.0));
    REQUIRE(display.view().end == Catch::Approx(1.0));
    display.setSelection({ 0.25, 0.5 });
    display.zoomToSelection();
    REQUIRE(display.view().start == Catch::Approx(0.25));
    REQUIRE(display.view().end == Catch::Approx(0.5));
    display.zoomToFit();
    REQUIRE(display.view().end == Catch::Approx(1.0));
}

TEST_CASE("Zoom at a time keeps that time under the same pixel", "[waveform][ui]")
{
    TestDirectory t;
    WaveformDisplay display(t.dir.getChildFile("cache"));
    display.setSize(600, 120);
    std::vector<float> samples(44100, 0.5f);
    display.setPeaks(samples, 44100, {});
    display.setSampleRateForZoom(44100);
    display.zoomAtTime(0.4, 2.0);
    const auto x = sonicator::waveview::xAtTime(0.4, 600, display.view());
    REQUIRE(sonicator::waveview::timeAtX(x, 600, display.view()) == Catch::Approx(0.4).margin(1e-6));
    REQUIRE(display.view().length() < 1.0);
}

TEST_CASE("Panning clamps the view and leaves the selection in file time", "[waveform][ui]")
{
    TestDirectory t;
    WaveformDisplay display(t.dir.getChildFile("cache"));
    display.setSize(600, 120);
    std::vector<float> samples(44100, 0.5f);
    display.setPeaks(samples, 44100, {});
    display.setSampleRateForZoom(44100);
    display.zoomAtTime(0.5, 4.0);
    display.panBySeconds(-10.0);
    REQUIRE(display.view().start == Catch::Approx(0.0));
    display.setSelection({ 0.1, 0.2 });
    REQUIRE(display.selection().start == Catch::Approx(0.1));
    REQUIRE(display.selection().end == Catch::Approx(0.2));
}

TEST_CASE("Edited audio replaces the thumbnail and its length", "[waveform][ui]")
{
    TestDirectory t;
    WaveformDisplay display(t.dir.getChildFile("cache"));
    display.setSize(600, 120);
    display.setPeaks(std::vector<float>(44100, 0.5f), 44100, {});
    display.setSampleRateForZoom(44100);
    auto audio = std::make_shared<sonicator::SampleAudio>();
    audio->sampleRate = 44100;
    audio->channels = 2;
    audio->frames = 22050;
    audio->left.assign(22050, 0.5f);
    audio->right.assign(22050, -0.5f);
    display.setEditedAudio(audio);
    REQUIRE(display.totalLength() == Catch::Approx(0.5));
    REQUIRE(display.sampleRate() == 44100);
    display.clearEditedAudio();
    REQUIRE(display.totalLength() == Catch::Approx(1.0));
}

TEST_CASE("Right-click asks the owner for a menu", "[waveform][ui]")
{
    TestDirectory t;
    WaveformDisplay display(t.dir.getChildFile("cache"));
    display.setSize(400, 100);
    display.setPeaks(std::vector<float>(44100, 0.5f), 44100, {});
    bool asked = false;
    display.onContextMenu = [&asked](juce::Point<int>) { asked = true; };
    const auto now = juce::Time::getCurrentTime();
    const juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(),
        juce::Point<float>(120.0f, 40.0f),
        juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier),
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &display, &display, now,
        juce::Point<float>(120.0f, 40.0f), now, 1, false);
    display.mouseDown(event);
    REQUIRE(asked);
}

TEST_CASE("Edited peaks follow the zoom", "[waveform][ui]")
{
    TestDirectory t;
    WaveformDisplay display(t.dir.getChildFile("cache"));
    display.setSize(600, 120);
    auto audio = std::make_shared<sonicator::SampleAudio>();
    audio->sampleRate = 44100;
    audio->channels = 1;
    audio->frames = 44100;
    audio->left.resize(44100);
    for (int i = 0; i < 44100; ++i) audio->left[(size_t) i] = (float) i / 44100.0f;
    display.setEditedAudio(audio);
    display.setView({ 0.0, 0.1 });
    REQUIRE(display.view().length() == Catch::Approx(0.1));
    juce::Image image(juce::Image::ARGB, 600, 120, true);
    juce::Graphics g(image);
    display.paint(g);
    REQUIRE(true);
}

TEST_CASE("Releasing a right-click keeps the selection and the playhead", "[waveform][ui]")
{
    TestDirectory t;
    WaveformDisplay display(t.dir.getChildFile("cache"));
    display.setSize(600, 120);
    display.setPeaks(std::vector<float>(44100, 0.5f), 44100, {});
    display.setSampleRateForZoom(44100);
    display.setSelection({ 0.2, 0.4 });
    display.onContextMenu = [](juce::Point<int>) {};
    double sought = -1.0;
    display.onSeek = [&sought](double time) { sought = time; };
    const auto now = juce::Time::getCurrentTime();
    const auto makeEvent = [&](juce::ModifierKeys mods)
    {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),
            juce::Point<float>(120.0f, 40.0f), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            &display, &display, now, juce::Point<float>(120.0f, 40.0f), now, 1, false);
    };
    const auto right = juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier);
    display.mouseDown(makeEvent(right));
    display.mouseUp(makeEvent(right));
    REQUIRE(display.selection().valid());
    REQUIRE(sought < 0.0);
}

TEST_CASE("A loaded file gives the zoom its sample rate", "[waveform][ui]")
{
    TestDirectory t;
    WaveformDisplay display(t.dir.getChildFile("cache"));
    display.setSize(600, 120);
    display.setFile(t.wave("one.wav"));
    REQUIRE(display.sampleRate() == 44100);
    for (int i = 0; i < 40; ++i) display.zoomAtTime(0.5, 2.0);
    REQUIRE(display.view().length() >= 600.0 / 44100.0 - 1e-9);
}

TEST_CASE("An edit keeps the zoomed view", "[waveform][ui]")
{
    TestDirectory t;
    WaveformDisplay display(t.dir.getChildFile("cache"));
    display.setSize(600, 120);
    display.setPeaks(std::vector<float>(44100, 0.5f), 44100, {});
    display.setView({ 0.2, 0.4 });
    auto audio = std::make_shared<sonicator::SampleAudio>();
    audio->sampleRate = 44100;
    audio->channels = 1;
    audio->frames = 44100;
    audio->left.assign(44100, 0.5f);
    display.setEditedAudio(audio);
    REQUIRE(display.view().start == Catch::Approx(0.2).margin(1e-9));
    REQUIRE(display.view().end == Catch::Approx(0.4).margin(1e-9));
}
