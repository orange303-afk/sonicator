#include <catch2/catch_test_macros.hpp>
#include "core/export/ExternalFileDrag.h"
#include "ui/library/FileListPanel.h"
#include "TestHelpers.h"
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
