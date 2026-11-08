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
