#pragma once
#include "core/database/LibraryQuery.h"
#include "core/database/SampleRecord.h"
#include "core/export/ExternalFileDrag.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <vector>
namespace sonicator {
class FileListPanel final : public juce::Component, public juce::TableListBoxModel
{
public:
    FileListPanel();
    ~FileListPanel() override
    {
        table.removeMouseListener(&rowDragListener);
        table.setModel(nullptr);
    }
    struct LoadedRows
    {
        std::vector<SampleRecord> records;
        std::map<int64_t, std::vector<Tag>> tags;
        std::map<int64_t, int> duplicates;
    };
    using RowLoader = std::function<LoadedRows(const std::vector<int64_t>&)>;
    void setSamples(std::vector<SampleRecord>);
    // Virtual mode: the row count is known, records are loaded in blocks on demand.
    void setVirtualSamples(std::vector<int64_t> orderedIds, RowLoader);
    void setRowTags(std::map<int64_t, std::vector<Tag>>);
    void setDuplicateCounts(std::map<int64_t, int>);
    void setSearchText(const juce::String&);
    void selectRow(int row) { table.selectRow(row); }
    void selectRows(const std::vector<int>& rows);
    std::vector<SampleRecord> getSelectedSamples();
    juce::StringArray dragPaths();
    int prepareRowDrag(int row, bool addToSelection = false);
    void setExternalDragHandler(ExternalFileDrag::DragFunction handler) { externalDrag = std::move(handler); }
    bool isRowSelected(int row) const { return table.isRowSelected(row); }
    int getSelectedRow() const { return table.getSelectedRow(); }
    void focusForNavigation() { table.grabKeyboardFocus(); }
    int getNumRows() override { return virtualMode ? (int) virtualIds.size() : (int) visible.size(); }
    void paintRowBackground(juce::Graphics&,int,int,int,bool) override;
    void paintCell(juce::Graphics&,int,int,int,int,bool) override;
    void selectedRowsChanged(int) override;
    void sortOrderChanged(int,bool) override;
    void cellClicked(int row,int column,const juce::MouseEvent&) override;
    void resized() override { table.setBounds(getLocalBounds()); }
    void lookAndFeelChanged() override;
    void refreshTheme();
    std::function<void(const SampleRecord&)> onSelected;
    // Header clicks also change the whole-catalog order; the owner re-runs the SQL query.
    std::function<void(int,bool)> onSortChanged;
    std::function<void(const std::vector<SampleRecord>&, const SampleRecord&, juce::Point<int>)> onContextMenu;
    std::function<void(juce::String)> onDragMessage;
private:
    // Rows are child components, so a plain TableListBox override never sees their mouse events.
    // A recursive listener receives the row gesture and maps it into table coordinates.
    struct RowDragListener final : public juce::MouseListener
    {
        explicit RowDragListener(FileListPanel& o) : owner(o) {}
        void mouseDown(const juce::MouseEvent& e) override { owner.handlePress(e); }
        void mouseDrag(const juce::MouseEvent& e) override { owner.handleDrag(e); }
        void mouseUp(const juce::MouseEvent&) override { owner.handleRelease(); }
        FileListPanel& owner;
    };
    friend struct FileListPanelTestAccess;
    juce::TableListBox table { "Sample library", this };
    RowDragListener rowDragListener { *this };
    std::vector<SampleRecord> all, visible;
    bool virtualMode = false;
    std::vector<int64_t> virtualIds;
    std::map<int, SampleRecord> virtualRows;
    RowLoader rowLoader;
    std::map<int64_t, std::vector<Tag>> rowTags;
    std::map<int64_t, int> duplicateCounts;
    juce::String search;
    int sortColumn = 1;
    bool ascending = true;
    bool restoringSelection = false;
    juce::Point<int> dragOrigin;
    int pressedRow = -1;
    bool dragArmed = false, dragStarted = false;
    ExternalFileDrag::DragFunction externalDrag;
    void rebuild();
    void sortVisible(int column, bool forwards);
    int64_t idAt(int row) const;
    const SampleRecord* recordAt(int row);
    void loadVirtualBlock(int row);
    void evictVirtualRows(int centreRow);
    void handlePress(const juce::MouseEvent&);
    void handleDrag(const juce::MouseEvent&);
    void handleRelease();
};
} // namespace sonicator
