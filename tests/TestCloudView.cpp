#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "ui/MainComponent.h"
#include "ui/cloud/CloudView.h"
#include "core/database/SampleRepository.h"
#include "core/audio/AudioEngine.h"
#include "TestHelpers.h"

using namespace sonicator;
using namespace sonicator::cloud;

namespace
{
CloudLayout gridLayout()
{
    CloudLayout layout;
    layout.fingerprint = 42;
    CloudIsland island;
    island.tagId = 1;
    island.name = "Kick";
    island.colour = 0xffe05a5a;
    island.x = 500;
    island.y = 500;
    island.radius = 300;
    island.count = 9;
    layout.islands.push_back(island);
    for (int i = 0; i < 9; ++i)
    {
        CloudPoint point;
        point.sampleId = 100 + i;
        point.x = 300.0f + (float) (i % 3) * 100.0f;
        point.y = 300.0f + (float) (i / 3) * 100.0f;
        point.colour = 0xffe05a5a;
        point.islandIndex = 0;
        point.flags = cloudHasBasic | cloudHasV2;
        layout.points.push_back(point);
    }
    layout.bounds = { 0, 0, 1000, 1000 };
    return layout;
}
} // namespace

TEST_CASE("Cloud view fits the whole map and keeps the aspect", "[cloud][view]")
{
    CloudView view;
    view.setSize(800, 600);
    view.setLayout(gridLayout());
    // fitAll animates; drive the camera directly through a wheel with factor 1 on the centre.
    view.fitAll();
    for (int i = 0; i < 120; ++i) view.onFrameForTest();
    const auto topLeft = view.worldToScreen(0, 0);
    const auto bottomRight = view.worldToScreen(1000, 1000);
    REQUIRE(topLeft.x >= 0.0f);
    REQUIRE(topLeft.y >= 0.0f);
    REQUIRE(bottomRight.x <= 800.0f);
    REQUIRE(bottomRight.y <= 600.0f);
    // 1000x1000 fitted into 800x600 is height-limited: the map fills the height and is centred.
    REQUIRE(bottomRight.y > 540.0f);
    REQUIRE(bottomRight.x > 600.0f);
}

TEST_CASE("Zoom keeps the world point under the cursor", "[cloud][view]")
{
    CloudView view;
    view.setSize(800, 600);
    view.setLayout(gridLayout());
    for (int i = 0; i < 120; ++i) view.onFrameForTest();
    const juce::Point<float> cursor { 500.0f, 300.0f };
    const auto before = view.screenToWorld(cursor);
    view.zoomBy(2.0f, cursor);
    for (int i = 0; i < 120; ++i) view.onFrameForTest();
    const auto after = view.screenToWorld(cursor);
    REQUIRE(after.x == Catch::Approx(before.x).margin(1.0f));
    REQUIRE(after.y == Catch::Approx(before.y).margin(1.0f));
}

TEST_CASE("Cloud view hit test respects the current filter", "[cloud][view]")
{
    CloudView view;
    view.setSize(800, 600);
    view.setLayout(gridLayout());
    for (int i = 0; i < 120; ++i) view.onFrameForTest();
    const auto screen = view.worldToScreen(400, 400);
    const auto index = view.pointAtScreen({ (int) screen.x, (int) screen.y });
    REQUIRE(index != CloudIndex::none);
    REQUIRE(view.layout().points[index].sampleId == 104);

    std::vector<bool> matches(view.layout().points.size(), false);
    matches[0] = true;
    view.setFilter(matches);
    REQUIRE_FALSE(view.isPointVisible(index));

    int64_t activated = 0;
    view.onActivate = [&](int64_t id) { activated = id; };
    view.activatePointForTest(index); // dimmed points are not clickable
    REQUIRE(activated == 0);
    view.activatePointForTest(0);
    REQUIRE(activated == 100);
}

TEST_CASE("Hover reports once per point and ends on leaving", "[cloud][view]")
{
    CloudView view;
    view.setSize(800, 600);
    view.setLayout(gridLayout());
    for (int i = 0; i < 120; ++i) view.onFrameForTest();
    int begins = 0, ends = 0;
    int64_t hovered = 0;
    view.onHover = [&](int64_t id) { ++begins; hovered = id; };
    view.onHoverEnd = [&] { ++ends; };
    const auto screen = view.worldToScreen(300, 300);
    view.hoverForTest({ (int) screen.x, (int) screen.y });
    view.hoverForTest({ (int) screen.x + 1, (int) screen.y + 1 });
    REQUIRE(begins == 1);
    REQUIRE(hovered == 100);
    view.hoverForTest({ 5, 5 });
    REQUIRE(ends == 1);
}

TEST_CASE("Back returns to the previous camera", "[cloud][view]")
{
    CloudView view;
    view.setSize(800, 600);
    view.setLayout(gridLayout());
    for (int i = 0; i < 120; ++i) view.onFrameForTest();
    const auto original = view.scale();
    view.zoomBy(3.0f, { 400.0f, 300.0f });
    for (int i = 0; i < 120; ++i) view.onFrameForTest();
    REQUIRE(view.scale() > original);
    REQUIRE(view.canGoBack());
    view.cameraBack();
    for (int i = 0; i < 120; ++i) view.onFrameForTest();
    REQUIRE(view.scale() == Catch::Approx(original).margin(0.01));
}

TEST_CASE("A filter change never reflows the map", "[cloud][view]")
{
    CloudView view;
    view.setSize(800, 600);
    view.setLayout(gridLayout());
    for (int i = 0; i < 120; ++i) view.onFrameForTest();
    const auto before = view.worldToScreen(300, 300);
    std::vector<bool> matches(view.layout().points.size(), false);
    matches[3] = true;
    view.setFilter(matches);
    const auto after = view.worldToScreen(300, 300);
    REQUIRE(after.x == Catch::Approx(before.x).margin(0.001f));
    REQUIRE(after.y == Catch::Approx(before.y).margin(0.001f));
}

TEST_CASE("Painting 35k points stays inside a frame budget", "[cloud][view][perf]")
{
    CloudLayout layout;
    layout.fingerprint = 7;
    for (int t = 0; t < 46; ++t)
    {
        CloudIsland island;
        island.tagId = t + 1;
        island.name = "Tag" + juce::String(t);
        island.colour = (uint32_t) (0xff000000 | (uint32_t) (t * 0x010203));
        island.x = 200.0f + (float) (t % 7) * 400.0f;
        island.y = 200.0f + (float) (t / 7) * 400.0f;
        island.radius = 180.0f;
        island.count = 700;
        layout.islands.push_back(island);
    }
    for (int i = 0; i < 35636; ++i)
    {
        CloudPoint point;
        point.sampleId = i + 1;
        point.x = 200.0f + (float) (i % 7) * 400.0f + (float) (i % 97) * 2.0f;
        point.y = 200.0f + (float) ((i / 7) % 9) * 400.0f + (float) (i % 89) * 2.0f;
        point.colour = (uint32_t) (0xff000000 | (uint32_t) ((i % 46) * 0x010203));
        point.islandIndex = (int) (i % 46);
        point.flags = cloudHasBasic;
        layout.points.push_back(point);
    }
    layout.bounds = { 0, 0, 3200, 2400 };

    CloudView view;
    view.setSize(1200, 800);
    view.setLayout(layout);
    for (int i = 0; i < 120; ++i) view.onFrameForTest();
    juce::Image image(juce::Image::RGB, 1200, 800, true);
    juce::Graphics g(image);
    const auto wallStart = juce::Time::getMillisecondCounterHiRes();
    const auto cpuStart = std::clock();
    view.paint(g);
    const auto elapsed = juce::Time::getMillisecondCounterHiRes() - wallStart;
    // CPU time, not wall time: a parallel build or CI neighbour must not turn a 17 ms frame into
    // a red test, while a real regression (one fill per point again) still shows up.
    const auto cpuMs = 1000.0 * (double) (std::clock() - cpuStart) / (double) CLOCKS_PER_SEC;
    std::printf("35k paint: %.2f ms wall, %.2f ms cpu\n", elapsed, cpuMs);
    // Debug build, software renderer, one blit of each layer plus the labels. Release is far
    // below a 60 Hz frame; the number is printed above for the acceptance record.
    REQUIRE(cpuMs < 30.0);
}

TEST_CASE("The map builds from a real catalog inside the window", "[ui][cloud]")
{
    TestDirectory t;
    const auto dbPath = t.dir.getChildFile("cloud-ui.sqlite");
    {
        Database db(dbPath);
        SampleRepository repo(db);
        repo.createTables();
        const auto kick = repo.createTag("Kick", 0xffe05a5a);
        const auto pad = repo.createTag("Pad", 0xffa96ae0);
        for (int i = 0; i < 6; ++i)
        {
            SampleRecord r;
            r.filePath = t.dir.getChildFile("s" + juce::String(i) + ".wav").getFullPathName();
            r.fileName = "s" + juce::String(i) + ".wav";
            r.fileHash = "h" + juce::String(i);
            r.format = "WAV";
            r.version = 1;
            r.duration = 0.2 + 0.1 * i;
            r.sampleRate = 44100;
            r.numChannels = 1;
            r.rmsLevel = 0.1 + 0.01 * i;
            r.peakLevel = 0.5;
            r.spectralCentroid = 900 + 100 * i;
            r.spectralRolloff = 3000;
            r.spectralFlatness = 0.1;
            r.dateAnalyzed = 1000;
            const auto id = repo.upsert(r);
            repo.setSampleTag(id, i % 2 == 0 ? kick : pad, true);
        }
    }
    AudioEngine engine(false);
    MainComponent app(dbPath, false, {}, t.dir.getChildFile("settings.json"), &engine, true);
    app.setSize(1180, 720);
    app.resized();
    app.showCloudView(true);
    auto* panel = dynamic_cast<cloud::CloudView*>(app.findChildWithID("cloudView"));
    REQUIRE(panel != nullptr);
    REQUIRE(panel->isVisible());
    REQUIRE(pumpUntil([panel] { return panel->hasLayout(); }, 15000));
    REQUIRE(panel->layout().points.size() == 6);
    REQUIRE(panel->layout().islands.size() == 2);
    app.showCloudView(false);
    REQUIRE_FALSE(panel->isVisible());
}
