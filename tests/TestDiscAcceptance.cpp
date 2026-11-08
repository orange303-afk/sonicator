#include <catch2/catch_test_macros.hpp>
#include "ui/MainComponent.h"
#include "ui/library/FileListPanel.h"
#include "ui/disc/DiscBrowserPanel.h"
#include "ui/preview/PreviewPanel.h"
#include "core/smart/AssetAnalyzer.h"
#include "core/database/SampleRepository.h"
#include "TestHelpers.h"
#include <juce_events/juce_events.h>
using namespace sonicator;

TEST_CASE("Real Akai image expands in the folder tree and previews descendant samples", "[.disc-tree-ui]")
{
    const auto path = juce::SystemStats::getEnvironmentVariable("SONICATOR_DISC_IMAGE", {});
    REQUIRE(path.isNotEmpty());
    const juce::File image(path);
    REQUIRE(image.existsAsFile());
    const auto size = image.getSize(), modified = image.getLastModificationTime().toMilliseconds();
    TestDirectory t;
    const auto catalog = t.dir.getChildFile("catalog.sqlite");
    {
        Database db(catalog); SampleRepository repo(db); repo.createTables();
        SampleRecord record; record.filePath = path; record.fileName = image.getFileName();
        record.format = "AKAI"; record.version = metadataOnlyVersion;
        repo.upsert(record); repo.addRoot(image.getParentDirectory().getFullPathName());
    }
    MainComponent app(catalog, false, {}, t.dir.getChildFile("settings.json"));
    app.setSize(1440, 900);
    auto* folders = dynamic_cast<FolderTree*>(app.findChildWithID("folderTree"));
    REQUIRE(folders != nullptr);
    REQUIRE(folders->selectPath(path)); // actual tree-selection path, not a direct open callback
    auto* browser = dynamic_cast<DiscBrowserPanel*>(app.findChildWithID("discBrowser"));
    REQUIRE(browser != nullptr);
    for (int i = 0; i < 500 && browser->getNumRows() == 0; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    REQUIRE(browser->getNumRows() > 0);
    const auto allFiles = browser->getNumRows();
    auto* hide = dynamic_cast<juce::ToggleButton*>(app.findChildWithID("hideEmpty")); REQUIRE(hide != nullptr);
    REQUIRE(hide->getToggleState());
    hide->setToggleState(false, juce::dontSendNotification); hide->onClick();
    REQUIRE(browser->getNumRows() > allFiles); // includes Akai programs and unsupported records
    hide->setToggleState(true, juce::dontSendNotification); hide->onClick();
    REQUIRE(browser->getNumRows() == allFiles);
    REQUIRE(browser->canReuseImage(image));
    auto* tree = dynamic_cast<juce::TreeView*>(folders->getChildComponent(0));
    REQUIRE(tree != nullptr);
    auto* imageItem = tree->getSelectedItem(0);
    REQUIRE(imageItem != nullptr);
    REQUIRE(imageItem->getUniqueName() == path);
    REQUIRE(imageItem->isOpen());
    REQUIRE(imageItem->getNumSubItems() > 0);
    auto* partition = imageItem->getSubItem(0);
    partition->setOpen(true);
    REQUIRE(partition->getNumSubItems() > 0);
    partition->getSubItem(0)->setSelected(true, true);
    REQUIRE(browser->getNumRows() > 0);
    REQUIRE(browser->getNumRows() < allFiles);
    auto* preview = dynamic_cast<PreviewPanel*>(app.findChildWithID("preview"));
    REQUIRE(preview != nullptr);
    auto* status = dynamic_cast<juce::Label*>(browser->findChildWithID("discStatus"));
    REQUIRE(status != nullptr);
    // Akai volumes also contain program metadata, which must not be decoded as PCM.
    for (int row = 0; row < juce::jmin(12, browser->getNumRows()) && ! preview->discSampleAudio(); ++row)
    {
        browser->selectRow(row);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        for (int i = 0; i < 250 && status->getText().startsWith("Decoding"); ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    }
    INFO(status->getText());
    REQUIRE(preview->discSampleAudio() != nullptr);
    REQUIRE(preview->discSampleAudio()->frames > 0);
    const auto screenshot = juce::SystemStats::getEnvironmentVariable("SONICATOR_DISC_TREE_SCREENSHOT", {});
    if (screenshot.isNotEmpty())
    {
        auto stream = juce::File(screenshot).createOutputStream(); REQUIRE(stream != nullptr);
        REQUIRE(juce::PNGImageFormat().writeImageToStream(app.createComponentSnapshot(app.getLocalBounds()), *stream));
    }
    REQUIRE(image.getSize() == size);
    REQUIRE(image.getLastModificationTime().toMilliseconds() == modified);
}

// Hidden acceptance harness: set SONICATOR_DISC_IMAGE to a real read-only image. It builds a
// temporary catalog, opens the real MainComponent, double-clicks the image row and walks the
// browser with the same methods the keyboard uses, saving snapshots for visual inspection.
TEST_CASE("Real disc image browse acceptance", "[.disc-ui]")
{
    const auto path = juce::SystemStats::getEnvironmentVariable("SONICATOR_DISC_IMAGE", {});
    REQUIRE(path.isNotEmpty());
    juce::File image(path);
    REQUIRE(image.existsAsFile());
    TestDirectory t;
    const auto dbPath = t.dir.getChildFile("disc-ui.sqlite");
    const auto record = analyzeMetadataAsset(image);
    INFO(record.error);
    REQUIRE(record.error.isEmpty());
    REQUIRE(record.format == "AKAI");
    {
        Database db(dbPath); SampleRepository repo(db); repo.createTables();
        repo.upsert(record);
        repo.addRoot(image.getParentDirectory().getFullPathName());
    }
    const auto pump = [](int milliseconds)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(milliseconds);
    };
    MainComponent app(dbPath, false, {}, t.dir.getChildFile("settings.json"));
    app.setSize(1200, 800);
    app.resized();
    auto* library = dynamic_cast<FileListPanel*>(app.findChildWithID("library"));
    REQUIRE(library != nullptr);
    REQUIRE(library->getNumRows() == 1);
    library->selectRow(0);
    const auto selected = library->getSelectedSamples();
    REQUIRE(selected.size() == 1);
    REQUIRE(selected[0].format == "AKAI");

    REQUIRE(app.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    pump(5000);
    auto* browser = dynamic_cast<DiscBrowserPanel*>(app.findChildWithID("discBrowser"));
    REQUIRE(browser != nullptr);
    REQUIRE(browser->isVisible());
    INFO("partitions=" << browser->getNumRows());
    REQUIRE(browser->getNumRows() == 13);
    {
        auto stream = juce::File("/tmp/sonicator-disc-ui-partitions.png").createOutputStream();
        REQUIRE(stream != nullptr);
        juce::PNGImageFormat png;
        REQUIRE(png.writeImageToStream(app.createComponentSnapshot(app.getLocalBounds(), true), *stream));
    }

    browser->returnKeyPressed(0); // first partition
    pump(5000);
    INFO("volumes=" << browser->getNumRows());
    REQUIRE(browser->getNumRows() == 9);

    browser->returnKeyPressed(0); // first volume: CONGA SET
    pump(5000);
    INFO("records=" << browser->getNumRows());
    REQUIRE(browser->getNumRows() > 0);

    // Programs are metadata containers; find the first row that really decodes as a sample.
    auto* status = dynamic_cast<juce::Label*>(browser->findChildWithID("discStatus"));
    REQUIRE(status != nullptr);
    for (int row = 0; row < juce::jmin(12, browser->getNumRows()); ++row)
    {
        browser->selectRow(row); // selection alone must decode, like the library list
        pump(4000);
        if (status->getText().startsWith("Ready:")) break;
    }
    INFO("status=" << status->getText());
    REQUIRE(status->getText().startsWith("Ready:"));
    auto* preview = dynamic_cast<PreviewPanel*>(app.findChildWithID("preview"));
    REQUIRE(preview != nullptr);
    REQUIRE(preview->discSampleAudio() != nullptr);
    REQUIRE(preview->discSampleAudio()->frames > 0);
    REQUIRE(preview->discSampleAudio()->sampleRate == 44100);
    {
        auto stream = juce::File("/tmp/sonicator-disc-ui-records.png").createOutputStream();
        REQUIRE(stream != nullptr);
        juce::PNGImageFormat png;
        REQUIRE(png.writeImageToStream(app.createComponentSnapshot(app.getLocalBounds(), true), *stream));
    }
}
