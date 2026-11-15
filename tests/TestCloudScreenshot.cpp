#include <catch2/catch_test_macros.hpp>
#include "core/cloud/CloudLayoutBuilder.h"
#include "core/cloud/CloudSource.h"
#include "core/database/Database.h"
#include "core/database/SampleRepository.h"
#include "ui/MainComponent.h"
#include "ui/cloud/CloudView.h"
#include "TestHelpers.h"
#include <cstdlib>

using namespace sonicator;

// Renders the map offscreen so the owner can look at it without launching the app. Hidden from
// the default run. SONICATOR_SHOT_DB points at a copy of the catalog; nothing is ever written to
// the live one.
TEST_CASE("Render the live catalog map to a PNG", "[.][cloud][screenshot]")
{
    const auto databasePath = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                                  .getChildFile("Library/Sonicator/sonicator.sqlite");
    if (! databasePath.existsAsFile()) return;
    Database database(databasePath);
    SampleRepository repository(database);
    auto input = cloud::loadCloudInput(repository);
    auto layout = cloud::buildCloudLayout(input);
    REQUIRE(layout.valid());

    cloud::CloudView view;
    view.setSize(1400, 900);
    view.setLayout(std::move(layout));
    view.fitAll();
    for (int i = 0; i < 200; ++i) view.onFrameForTest();
    auto image = view.createComponentSnapshot(view.getLocalBounds(), false, 1.0f);
    const auto out = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cloud-view.png");
    out.deleteFile();
    auto stream = out.createOutputStream();
    juce::PNGImageFormat().writeImageToStream(image, *stream);
    stream->flush();
    std::printf("wrote %s\n", out.getFullPathName().toRawUTF8());
}

TEST_CASE("Screenshot the whole window in cloud mode", "[.][cloud][screenshot][window]")
{
    const auto shotDb = juce::SystemStats::getEnvironmentVariable("SONICATOR_SHOT_DB", {});
    if (shotDb.isEmpty()) return;
    TestDirectory t;
    AudioEngine engine(false);
    MainComponent app(juce::File(shotDb), false, {}, t.dir.getChildFile("settings.json"), &engine, true);
    AppSettings settings;
    settings.theme = ThemeMode::Light;
    MainComponent light(juce::File(shotDb), false, settings, t.dir.getChildFile("light.json"), &engine, true);
    light.setSize(1400, 900);
    light.resized();
    light.showCloudView(true);
    auto* lightPanel = dynamic_cast<cloud::CloudView*>(light.findChildWithID("cloudView"));
    REQUIRE(lightPanel != nullptr);
    REQUIRE(pumpUntil([lightPanel] { return lightPanel->hasLayout(); }, 60000));
    for (int i = 0; i < 240; ++i) lightPanel->onFrameForTest();
    {
        auto lightImage = light.createComponentSnapshot(light.getLocalBounds(), false, 1.25f);
        const auto lightOut = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cloud-window-light-125.png");
        lightOut.deleteFile();
        auto lightStream = lightOut.createOutputStream();
        juce::PNGImageFormat().writeImageToStream(lightImage, *lightStream);
        lightStream->flush();
        std::printf("wrote %s\n", lightOut.getFullPathName().toRawUTF8());
    }
    app.setSize(1400, 900);
    app.resized();
    app.showCloudView(true);
    auto* panel = dynamic_cast<cloud::CloudView*>(app.findChildWithID("cloudView"));
    REQUIRE(panel != nullptr);
    REQUIRE(pumpUntil([panel] { return panel->hasLayout(); }, 60000));
    for (int i = 0; i < 240; ++i) panel->onFrameForTest();
    auto image = app.createComponentSnapshot(app.getLocalBounds(), false, 1.0f);
    const auto out = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cloud-window.png");
    out.deleteFile();
    auto stream = out.createOutputStream();
    juce::PNGImageFormat().writeImageToStream(image, *stream);
    stream->flush();
    std::printf("wrote %s\n", out.getFullPathName().toRawUTF8());
}
