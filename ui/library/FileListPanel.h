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
    void rebuild();
};
}
