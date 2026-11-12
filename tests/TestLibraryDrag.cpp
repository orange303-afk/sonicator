#include <catch2/catch_test_macros.hpp>
#include "core/export/ExternalFileDrag.h"
#include "ui/library/FileListPanel.h"
#include "TestHelpers.h"
#include "EblTestFile.h"
#include <cmath>
using namespace sonicator;
TEST_CASE("External file drag keeps only existing paths in order", "[drag][library]")
{
    TestDirectory t; auto a = t.wave("a.wav"), b = t.wave("b.wav");
    juce::StringArray candidates { b.getFullPathName(),
        t.dir.getChildFile("missing.wav").getFullPathName(), a.getFullPathName() };
    const auto files = ExternalFileDrag::existingPaths(candidates);
    REQUIRE(files.size() == 2);
    REQUIRE(files[0] == b.getFullPathName()); REQUIRE(files[1] == a.getFullPathName());
}
TEST_CASE("Selected library rows map to existing originals in order", "[drag][library]")
{
    TestDirectory t; auto a = t.wave("a.wav"), b = t.wave("b.wav");
    SampleRecord ra, rm, rb; ra.id = 1; ra.filePath = a.getFullPathName(); ra.fileName = "a.wav";
    rm.id = 2; rm.filePath = t.dir.getChildFile("gone.wav").getFullPathName(); rm.fileName = "gone.wav";
    rb.id = 3; rb.filePath = b.getFullPathName(); rb.fileName = "b.wav";
    const std::vector<SampleRecord> rows { ra, rm, rb };
    auto paths = ExternalFileDrag::selectedPaths(rows, { 0, 1, 2 });
    REQUIRE(paths.size() == 2);
    REQUIRE(paths[0] == a.getFullPathName()); REQUIRE(paths[1] == b.getFullPathName());
    paths = ExternalFileDrag::selectedPaths(rows, { 2, 0, 99 });
    REQUIRE(paths.size() == 2);
    REQUIRE(paths[0] == a.getFullPathName()); REQUIRE(paths[1] == b.getFullPathName());
}
TEST_CASE("Row drag starts only with existing files and never moves them", "[drag][library]")
{
    TestDirectory t; auto a = t.wave("a.wav");
    juce::Component sourceComponent;
    bool called = false, canMove = true; juce::Component* source = nullptr;
    auto drag = [&](const juce::StringArray& files, bool move, juce::Component* c)
    {
        called = true; canMove = move; source = c;
        return files.size() == 1 && files[0] == a.getFullPathName();
    };
    REQUIRE_FALSE(ExternalFileDrag::start({}, &sourceComponent, drag));
    REQUIRE_FALSE(called);
    REQUIRE_FALSE(ExternalFileDrag::start({ t.dir.getChildFile("gone.wav").getFullPathName() },
                                          &sourceComponent, drag));
    REQUIRE_FALSE(called);
    REQUIRE(ExternalFileDrag::start({ a.getFullPathName() }, &sourceComponent, drag));
    REQUIRE(called); REQUIRE_FALSE(canMove); REQUIRE(source == &sourceComponent);
}
TEST_CASE("Row drag threshold is five pixels", "[drag][library]")
{
    REQUIRE_FALSE(ExternalFileDrag::exceedsThreshold(5, 0));
    REQUIRE(ExternalFileDrag::exceedsThreshold(6, 0));
    REQUIRE(ExternalFileDrag::exceedsThreshold(0, -6));
}
TEST_CASE("FileListPanel prepares unselected rows and exposes multi-selected originals", "[drag][library]")
{
    TestDirectory t; auto a = t.wave("a.wav"), c = t.wave("c.wav");
    SampleRecord ra, rb, rm; ra.id = 1; ra.filePath = a.getFullPathName(); ra.fileName = "a.wav";
    rb.id = 2; rb.filePath = c.getFullPathName(); rb.fileName = "c.wav";
    rm.id = 3; rm.filePath = t.dir.getChildFile("gone.wav").getFullPathName(); rm.fileName = "b.wav";
    FileListPanel list; list.setBounds(0, 0, 800, 400); list.resized();
    list.setSamples({ ra, rm, rb });
    list.selectRows({ 0, 2 });
    const auto paths = list.dragPaths();
    REQUIRE(paths.size() == 2);
    REQUIRE(paths[0] == a.getFullPathName()); REQUIRE(paths[1] == c.getFullPathName());
    REQUIRE(list.prepareRowDrag(1) == 1);
    REQUIRE(list.getSelectedRow() == 1);
}
namespace sonicator
{
struct FileListPanelTestAccess
{
    static juce::TableListBox& table(FileListPanel& list) { return list.table; }
    static void press(FileListPanel& list, const juce::MouseEvent& e) { list.rowDragListener.mouseDown(e); }
    static void drag(FileListPanel& list, const juce::MouseEvent& e) { list.rowDragListener.mouseDrag(e); }
};
}
TEST_CASE("Row gesture reaches the listener with the whole multi-selection", "[drag][library]")
{
    TestDirectory t; auto a = t.wave("a.wav"), b = t.wave("b.wav");
    SampleRecord ra, rb; ra.id = 1; ra.filePath = a.getFullPathName(); ra.fileName = "a.wav";
    rb.id = 2; rb.filePath = b.getFullPathName(); rb.fileName = "b.wav";
    FileListPanel list; list.setBounds(0, 0, 800, 400); list.resized();
    list.setSamples({ ra, rb });
    list.createComponentSnapshot(list.getLocalBounds());
    auto* row = FileListPanelTestAccess::table(list).getComponentForRowNumber(0);
    REQUIRE(row != nullptr);
    list.selectRows({ 0, 1 });
    bool called = false, canMove = true; juce::StringArray seen;
    list.setExternalDragHandler([&](const juce::StringArray& files, bool move, juce::Component*)
    {
        called = true; seen = files; canMove = move; return true;
    });
    const auto now = juce::Time::getCurrentTime();
    const auto source = juce::Desktop::getInstance().getMainMouseSource();
    juce::MouseEvent down(source, { 5.0f, 5.0f }, juce::ModifierKeys(), 1.0f, 0, 0, 0, 0,
                          row, row, now, { 5.0f, 5.0f }, now, 1, false);
    FileListPanelTestAccess::press(list, down);
    juce::MouseEvent dragged(source, { 5.0f, 12.0f }, juce::ModifierKeys(), 1.0f, 0, 0, 0, 0,
                             row, row, now, { 5.0f, 5.0f }, now, 1, true);
    FileListPanelTestAccess::drag(list, dragged);
    REQUIRE(called); REQUIRE_FALSE(canMove);
    REQUIRE(seen.size() == 2);
    REQUIRE(seen[0] == a.getFullPathName()); REQUIRE(seen[1] == b.getFullPathName());
}

TEST_CASE("Enter opens the selected container row", "[ui][disc]")
{
    FileListPanel list;
    SampleRecord image; image.id = 1; image.fileName = "a-library.iso"; image.format = "AKAI";
    SampleRecord audio; audio.id = 2; audio.fileName = "b-kick.wav"; audio.format = "WAV";
    list.setSamples({ image, audio });
    bool opened = false; juce::String openedName;
    list.onOpen = [&](const SampleRecord& record) { opened = true; openedName = record.fileName; };
    list.selectRow(0);
    list.returnKeyPressed(0);
    REQUIRE(opened);
    REQUIRE(openedName == "a-library.iso");
}

TEST_CASE("An incompatible asset replaces the star with a prohibition badge", "[ui][asset-compat]")
{
    FileListPanel list;
    SampleRecord starred, unsupported, invalid;
    starred.id = 1; starred.fileName = "a-starred.wav"; starred.format = "WAV"; starred.starred = true;
    unsupported.id = 2; unsupported.fileName = "b-broken.rx2"; unsupported.format = "RX2";
    unsupported.compatibility = AssetCompatibility::unsupported;
    invalid.id = 3; invalid.fileName = "c-bad.wav"; invalid.format = "WAV";
    invalid.compatibility = AssetCompatibility::invalid;
    list.setSamples({ starred, unsupported, invalid });
    list.setSize(860, 200);
    auto& table = FileListPanelTestAccess::table(list);
    const auto& header = table.getHeader();
    int starIndex = -1;
    for (int i = 0; i < header.getNumColumns(true); ++i)
        if (header.getColumnIdOfIndex(i, true) == 8) starIndex = i;
    REQUIRE(starIndex >= 0);
    const auto starColumn = header.getColumnPosition(starIndex);
    const int rowHeight = table.getRowHeight();
    const int rowTop = table.getHeaderHeight();
    auto countRed = [&](int row)
    {
        const auto cell = juce::Rectangle<int>(starColumn.getX(), rowTop + row * rowHeight,
                                              starColumn.getWidth(), rowHeight);
        const auto image = list.createComponentSnapshot(list.getLocalBounds(), true);
        int red = 0;
        for (int y = cell.getY(); y < cell.getBottom(); ++y)
            for (int x = cell.getX(); x < cell.getRight(); ++x)
            {
                const auto c = image.getPixelAt(x, y);
                if (c.getRed() > 150 && c.getGreen() < 120 && c.getBlue() < 120) ++red;
            }
        return red;
    };
    REQUIRE(countRed(0) == 0);       // a starred compatible asset keeps its star
    REQUIRE(countRed(1) > 4);        // unsupported: the red badge is drawn in the same cell
    REQUIRE(countRed(2) > 4);        // invalid shares the badge
    const auto shot = juce::SystemStats::getEnvironmentVariable("SONICATOR_COMPAT_SCREENSHOT", {});
    if (shot.isNotEmpty())
    {
        auto stream = juce::File(shot).createOutputStream(); REQUIRE(stream != nullptr);
        juce::PNGImageFormat png;
        REQUIRE(png.writeImageToStream(list.createComponentSnapshot(list.getLocalBounds(), true), *stream));
    }
}

TEST_CASE("Ignored status is reported below decode problems", "[ui][library][ignore]")
{
    SampleRecord r; r.filePath = "/lib/a.wav"; r.fileName = "a.wav";
    REQUIRE(sampleStatusText(r, false) == "Analyzed");
    REQUIRE(sampleStatusText(r, true) == "Ignored");
    r.missing = true;
    REQUIRE(sampleStatusText(r, true) == "Missing");
    r.missing = false; r.error = "bad";
    REQUIRE(sampleStatusText(r, true) == "Decode error");
}

TEST_CASE("A stereo pair row drags both mono files", "[drag][library][stereo-pairs]")
{
    TestDirectory t;
    auto left = t.wave("pair-L.wav");
    auto right = t.wave("pair-R.wav");
    SampleRecord record;
    record.id = 1; record.filePath = left.getFullPathName(); record.fileName = "pair-L.wav";
    FileListPanel list; list.setBounds(0, 0, 800, 400); list.resized();
    list.setSamples({ record });
    list.setStereoPairs({ { record.id, right.getFullPathName() } });
    list.selectRows({ 0 });
    const auto paths = list.dragPaths();
    REQUIRE(paths.size() == 2);
    REQUIRE(paths.contains(left.getFullPathName()));
    REQUIRE(paths.contains(right.getFullPathName()));
}

TEST_CASE("EBL rows drag out as WAV and failed conversions are reported", "[ebl][drag]")
{
    using ebltest::monoOptions;
    TestDirectory t;
    auto looped = monoOptions();
    looped.name = "Loop Name";
    looped.loop = std::pair<uint32_t, uint32_t> { 2, 6 };
    auto ebl = t.dir.getChildFile("Looped.ebl");
    {
        const auto bytes = ebltest::makeEbl(looped);
        REQUIRE(ebl.replaceWithData(bytes.data(), bytes.size()));
    }
    auto wav = t.wave("plain.wav");
    auto broken = t.dir.getChildFile("broken.ebl");
    REQUIRE(broken.replaceWithText("not an ebl"));
    const auto cache = t.dir.getChildFile("drag");

    juce::StringArray problems;
    const auto files = ExternalFileDrag::materialiseForDrag(
        { ebl.getFullPathName(), wav.getFullPathName(), broken.getFullPathName() }, cache, problems);
    REQUIRE(files.size() == 2);
    CHECK(files[1] == wav.getFullPathName());
    CHECK(problems.size() == 1);
    CHECK(problems[0].contains("broken.ebl"));

    const juce::File cached(files[0]);
    REQUIRE(cached.existsAsFile());
    CHECK(cached.getParentDirectory() == cache);
    CHECK(cached.getFileName().startsWith("Loop_Name-"));
    CHECK(cached.hasFileExtension("wav"));

    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(cached.createInputStream().release(), true));
    REQUIRE(reader != nullptr);
    CHECK(reader->sampleRate == 47360);
    CHECK(reader->numChannels == 1);
    REQUIRE(reader->lengthInSamples == 8);
    juce::AudioBuffer<float> buffer(1, 8);
    REQUIRE(reader->read(&buffer, 0, 8, 0, true, true));
    for (int i = 0; i < 8; ++i)
        CHECK(std::abs(buffer.getSample(0, i) - (float) looped.left[(size_t) i] / 32768.0f) <= 1e-6f);
    CHECK(reader->metadataValues.getValue("NumSampleLoops", "0") == "1");
    CHECK(reader->metadataValues.getValue("Loop0Start", "") == "2");
    CHECK(reader->metadataValues.getValue("Loop0End", "") == "5"); // exclusive 6 becomes inclusive 5
    reader.reset();

    const auto firstWrite = cached.getLastModificationTime();
    juce::StringArray secondProblems;
    const auto again = ExternalFileDrag::materialiseForDrag({ ebl.getFullPathName() }, cache, secondProblems);
    REQUIRE(again.size() == 1);
    CHECK(again[0] == cached.getFullPathName());
    CHECK(secondProblems.isEmpty());
    CHECK(cached.getLastModificationTime() == firstWrite);
}
