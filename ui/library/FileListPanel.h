#pragma once
#include "core/database/LibraryQuery.h"
#include "core/database/SampleRecord.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <vector>
namespace sonicator {
class FileListPanel final : public juce::Component, public juce::TableListBoxModel
{
public:
    FileListPanel();
    ~FileListPanel() override { table.setModel(nullptr); }
    void setSamples(std::vector<SampleRecord>);
    void setRowTags(std::map<int64_t, std::vector<Tag>>);
    void setDuplicateCounts(std::map<int64_t, int>);
    void setSearchText(const juce::String&);
    void selectRow(int row) { table.selectRow(row); }
    void selectRows(const std::vector<int>& rows);
    std::vector<SampleRecord> getSelectedSamples() const;
    bool isRowSelected(int row) const { return table.isRowSelected(row); }
    int getSelectedRow() const { return table.getSelectedRow(); }
    void focusForNavigation() { table.grabKeyboardFocus(); }
    int getNumRows() override { return (int) visible.size(); }
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
private:
    juce::TableListBox table { "Sample library", this };
    std::vector<SampleRecord> all, visible;
    std::map<int64_t, std::vector<Tag>> rowTags;
    std::map<int64_t, int> duplicateCounts;
    juce::String search;
    int sortColumn = 1;
    bool ascending = true;
    bool restoringSelection = false;
    void rebuild();
    void sortVisible(int column, bool forwards);
};
} // namespace sonicator
