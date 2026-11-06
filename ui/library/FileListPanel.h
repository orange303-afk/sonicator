#pragma once
#include "core/database/SampleRecord.h"
#include <juce_gui_basics/juce_gui_basics.h>
namespace sonicator {
class FileListPanel final : public juce::Component, public juce::TableListBoxModel
{
public:
    FileListPanel();
    ~FileListPanel() override { table.setModel(nullptr); }
    void setSamples(std::vector<SampleRecord>);
    void setSearchText(const juce::String&);
    void selectRow(int row) { table.selectRow(row); }
    int getSelectedRow() const { return table.getSelectedRow(); }
    void focusForNavigation() { table.grabKeyboardFocus(); }
    int getNumRows() override { return (int) visible.size(); }
    void paintRowBackground(juce::Graphics&,int,int,int,bool) override;
    void paintCell(juce::Graphics&,int,int,int,int,bool) override;
    void selectedRowsChanged(int) override;
    void sortOrderChanged(int,bool) override;
    void resized() override { table.setBounds(getLocalBounds()); }
    std::function<void(const SampleRecord&)> onSelected;
private:
    juce::TableListBox table { "Sample library", this };
    std::vector<SampleRecord> all, visible;
    juce::String search;
    int sortColumn = 1;
    bool ascending = true;
    bool restoringSelection = false;
    void rebuild();
};
}
