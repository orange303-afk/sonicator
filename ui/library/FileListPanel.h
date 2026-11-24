#pragma once
#include "core/database/LibraryQuery.h"
#include "core/database/SampleRecord.h"
#include "core/database/SampleRepository.h"
#include "core/smart/SmartTypes.h"
#include "core/export/ExternalFileDrag.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <vector>
namespace sonicator {
// Human-readable STATUS cell. Decode problems win over ignore, so a hidden broken file still
// says why it is broken once shown.
juce::String sampleStatusText(const SampleRecord&, bool ignored);
// LENGTH cell. Under a minute keeps the two decimals a short sample is compared by; longer files
// read as mm:ss or hh:mm:ss, because a sound designer manages minutes and hours, not 423.75 s.
juce::String formatLengthText(double seconds);
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
        std::map<int64_t, SmartDisplay> smart;
        std::map<int64_t, juce::String> stereoPairs;
    };
    using RowLoader = std::function<LoadedRows(const std::vector<int64_t>&)>;
    // `ownerOrdered` says the rows already arrive in the requested order from the catalog query.
    // A page must then never be sorted again locally: its own order is a page, not a library.
    void setSamples(std::vector<SampleRecord>, bool ownerOrdered = false);
    // Virtual mode: the row count is known, records are loaded in blocks on demand.
    void setVirtualSamples(std::vector<int64_t> orderedIds, RowLoader);
    void setRowTags(std::map<int64_t, std::vector<Tag>>);
    void setDuplicateCounts(std::map<int64_t, int>);
    void setSmartDisplays(std::map<int64_t, SmartDisplay>);
    void setStereoPairs(std::map<int64_t, juce::String> partnerPaths);
    void setIgnoredPaths(std::vector<IgnoredPath>);
    void setStarredInPlace(const std::vector<int64_t>& ids, bool starred);
    // A left click on the star column asks the owner to toggle this row's favorite state.
    std::function<void(const SampleRecord&)> onStarClicked;
    // The click path itself, exposed so a test can drive it without synthesising a mouse.
    void notifyStarClick(int row);
    void setSearchText(const juce::String&);
    void selectRow(int row) { table.selectRow(row); }
    void selectRows(const std::vector<int>& rows);
    // Selects an id without invoking the row-selection action, used when restoring the session.
    bool selectSampleById(int64_t sampleId);
    std::vector<SampleRecord> getSelectedSamples();
    int getSelectedRowCount() const { return table.getSelectedRows().size(); }
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
    void cellDoubleClicked(int row,int column,const juce::MouseEvent&) override;
    void returnKeyPressed(int row) override;
    void resized() override { table.setBounds(getLocalBounds()); }
    void lookAndFeelChanged() override;
    void refreshTheme();
    std::function<void(const SampleRecord&)> onSelected;
    // Double-click opens a container asset (disc image). Ordinary rows stay selection-only.
    std::function<void(const SampleRecord&)> onOpen;
    // Header clicks also change the whole-catalog order; the owner re-runs the SQL query.
    std::function<void(int,bool)> onSortChanged;
    std::function<void(const std::vector<SampleRecord>&, const SampleRecord&, juce::Point<int>)> onContextMenu;
    // Alt+click on a tag in the TAGS cell: remove that tag from that sample.
    std::function<void(int64_t sampleId, int64_t tagId)> onTagAltClicked;
    std::function<void(juce::String)> onDragMessage;
    // The plug-in's Match owns the drag while it is on. Returning true means this callback
    // handled the gesture (started a prepared drag or explained why it cannot start).
    std::function<bool (const std::vector<SampleRecord>&, juce::Component*)> onMatchedDrag;
private:
    struct TagHit
    {
        juce::Rectangle<int> bounds; // cell coordinates
        juce::String name;
        int64_t tagId = 0;
    };
    std::vector<TagHit> tagHits(const std::vector<Tag>& tags, int width) const;
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
    // The owner's query already applied the requested order (pagination and the virtual page both
    // do). Re-sorting here would replace a whole-catalog order with a page-local one.
    bool ownerOrdered = false;
    std::vector<int64_t> virtualIds;
    std::map<int, SampleRecord> virtualRows;
    RowLoader rowLoader;
    std::map<int64_t, std::vector<Tag>> rowTags;
    std::map<int64_t, int> duplicateCounts;
    std::map<int64_t, SmartDisplay> smartDisplays;
    std::map<int64_t, juce::String> stereoPartners;
    std::vector<IgnoredPath> ignoredPaths;
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
    // True for a point in the scrollbar lanes, which are never rows: a press there must not
    // select anything or arm a file drag.
    bool isScrollBarPoint(juce::Point<int> tablePosition) const;
};
} // namespace sonicator
