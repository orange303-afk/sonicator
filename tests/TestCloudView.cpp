#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "ui/MainComponent.h"
#include "ui/preview/PreviewPanel.h"
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

TEST_CASE("Cloud view hit test always activates a point", "[cloud][view]")
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
    // The cloud is the whole tagged library; the list filter must not hide or dim its points.
    REQUIRE(view.isPointVisible(index));

    int64_t activated = 0;
    view.onActivate = [&](int64_t id) { activated = id; };
    view.activatePointForTest(index);
    REQUIRE(activated == 104);
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
    juce::Thread::sleep(140); // labels return once the camera has settled
    juce::Image image(juce::Image::RGB, 1200, 800, true);
    juce::Graphics g(image);
    // Five paints: the first one composes the settled frame, the rest are the cached blit the map
    // shows while the user hovers. Report both, assert on the composed frame.
    std::vector<double> cpu;
    std::vector<double> wall;
    for (int pass = 0; pass < 5; ++pass)
    {
        const auto wallStart = juce::Time::getMillisecondCounterHiRes();
        const auto cpuStart = std::clock();
        view.paint(g);
        wall.push_back(juce::Time::getMillisecondCounterHiRes() - wallStart);
        cpu.push_back(1000.0 * (double) (std::clock() - cpuStart) / (double) CLOCKS_PER_SEC);
    }
    std::printf("35k paint: first %.2f ms cpu, cached %.2f ms cpu (wall %.2f / %.2f)\n",
                cpu.front(), cpu.back(), wall.front(), wall.back());
    // CPU time, not wall time: a parallel build or CI neighbour must not turn a fast frame into a
    // red test, while a real regression (one fill per point again) still shows up. The Windows
    // ceiling is wider on purpose: MSVC's Debug build draws the same geometry through Direct2D and
    // measures about twice the Apple toolchain's Debug time on the same frame.
    const double settleBudget = JUCE_WINDOWS ? 100.0 : 45.0;
    REQUIRE(cpu.front() < settleBudget);   // Debug settle frame: layers composed, labels shaped once
    REQUIRE(cpu.back() < 10.0);    // cached frame: what a hover repaint actually costs
}

TEST_CASE("The map builds from a real catalog inside the window", "[ui][cloud]")
{
    TestDirectory t;
    const auto dbPath = t.dir.getChildFile("cloud-ui.sqlite");
    std::vector<juce::File> files;
    std::vector<int64_t> ids;
    {
        Database db(dbPath);
        SampleRepository repo(db);
        repo.createTables();
        const auto kick = repo.createTag("Kick", 0xffe05a5a);
        const auto pad = repo.createTag("Pad", 0xffa96ae0);
        for (int i = 0; i < 6; ++i)
        {
            const auto file = t.wave("s" + juce::String(i) + ".wav", 220.0 + 40.0 * i);
            files.push_back(file);
            SampleRecord r;
            r.filePath = file.getFullPathName();
            r.fileName = file.getFileName();
            r.fileHash = "h" + juce::String(i);
            r.format = "WAV";
            r.version = 1;
            r.duration = 1.0;
            r.sampleRate = 44100;
            r.numChannels = 1;
            r.rmsLevel = 0.1 + 0.01 * i;
            r.peakLevel = 0.5;
            r.spectralCentroid = 900 + 100 * i;
            r.spectralRolloff = 3000;
            r.spectralFlatness = 0.1;
            r.dateAnalyzed = 1000;
            const auto id = repo.upsert(r);
            ids.push_back(id);
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

    // A click on a point must open the sample exactly like a row: the waveform panel loads that
    // file, not a placeholder and not another sample.
    auto* preview = dynamic_cast<PreviewPanel*>(app.findChildWithID("preview"));
    REQUIRE(preview != nullptr);
    size_t index = 0;
    for (size_t i = 0; i < panel->layout().points.size(); ++i)
        if (panel->layout().points[i].sampleId == ids[3]) index = i;
    panel->activatePointForTest(index);
    REQUIRE(preview->loadedFilePath() == files[3]);


    app.showCloudView(false);
    REQUIRE_FALSE(panel->isVisible());
}

TEST_CASE("The hover lens eases in and out and never moves the hit test", "[cloud][view]")
{
    CloudView view;
    view.setSize(800, 600);
    view.setLayout(gridLayout());
    for (int i = 0; i < 120; ++i) view.onFrameForTest();
    const auto screen = view.worldToScreen(400, 400);
    const auto index = view.pointAtScreen({ (int) screen.x, (int) screen.y });
    REQUIRE(index != CloudIndex::none);
    view.pointerForTest({ (int) screen.x, (int) screen.y });
    view.hoverForTest({ (int) screen.x, (int) screen.y });
    for (int i = 0; i < 40; ++i) view.onFrameForTest();
    REQUIRE(view.lensStrengthForTest() > 0.9f);
    // The lens is display only: the same point is under the cursor and the layout did not move.
    REQUIRE(view.pointAtScreen({ (int) screen.x, (int) screen.y }) == index);
    // The seam takes integer screen points, so the anchor may sit up to a pixel away.
    REQUIRE(view.lensOffsetForTest(index).getDistanceFromOrigin() < 2.0f);
    // A neighbour inside the lens is pushed away from the cursor.
    size_t neighbour = CloudIndex::none;
    for (size_t i = 0; i < view.layout().points.size(); ++i)
    {
        if (i == index) continue;
        const auto s = view.worldToScreen(view.layout().points[i].x, view.layout().points[i].y);
        if (s.getDistanceFrom(screen) < 60.0f) { neighbour = i; break; }
    }
    REQUIRE(neighbour != CloudIndex::none);
    REQUIRE(view.lensOffsetForTest(neighbour).getDistanceFromOrigin() > 0.5f);
    view.clearPointerForTest();
    for (int i = 0; i < 60; ++i) view.onFrameForTest();
    REQUIRE(view.lensStrengthForTest() < 0.02f);
}

TEST_CASE("The list filter never reflows or empties the cloud", "[cloud][view]")
{
    CloudView view;
    view.setSize(800, 600);
    view.setLayout(gridLayout());
    for (int i = 0; i < 120; ++i) view.onFrameForTest();
    const auto before = view.worldToScreen(300, 300);
    std::vector<bool> matches(view.layout().points.size(), false);
    matches[0] = true;
    view.setFilter(matches);
    REQUIRE(view.isPointVisible(1));
    REQUIRE(view.isPointVisible(0));
    const auto after = view.worldToScreen(300, 300);
    REQUIRE(after.x == Catch::Approx(before.x).margin(0.001f));
    REQUIRE(after.y == Catch::Approx(before.y).margin(0.001f));
}

TEST_CASE("A new layout fades the cloud in", "[cloud][view]")
{
    CloudView view;
    view.setSize(800, 600);
    view.setLayout(gridLayout());
    REQUIRE(view.introProgressForTest() < 0.2f);
    for (int i = 0; i < 60; ++i) view.onFrameForTest();
    REQUIRE(view.introProgressForTest() > 0.99f);
}


TEST_CASE("The view mode survives a settings round-trip", "[cloud][settings]")
{
    TestDirectory t;
    SettingsStore store(t.dir.getChildFile("settings.json"));
    auto settings = store.load();
    REQUIRE_FALSE(settings.cloudView);
    settings.cloudView = true;
    store.save(settings);
    REQUIRE(store.load().cloudView);
    store.update([](AppSettings& saved) { saved.cloudView = false; });
    REQUIRE_FALSE(store.load().cloudView);
}
