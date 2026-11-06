#include "FileListPanel.h"
#include "ui/shell/Theme.h"
namespace sonicator {
FileListPanel::FileListPanel()
{
    addAndMakeVisible(table); table.setRowHeight(34);
    table.setColour(juce::ListBox::backgroundColourId,theme::background);
    table.setColour(juce::ListBox::outlineColourId,theme::line);
    auto& h = table.getHeader();
    h.addColumn("SAMPLE",1,310,120,1200);
    h.addColumn("FORMAT",2,70,55,120);
    h.addColumn("LENGTH",3,85,60,140);
    h.addColumn("RATE",4,85,60,140);
    h.addColumn("CH",5,45,35,70);
    h.addColumn("STATUS",6,130,80,350);
    h.setSortColumnId(1,true);
}
void FileListPanel::setSamples(std::vector<SampleRecord> records)
{
    const auto selectedRow = table.getSelectedRow();
    const int64_t selectedId = (selectedRow >= 0 && selectedRow < getNumRows()) ? visible[(size_t) selectedRow].id : 0;
    const juce::ScopedValueSetter<bool> restoring(restoringSelection, true);
    all = std::move(records); rebuild();
    if (selectedId == 0) return;
    for (int row = 0; row < getNumRows(); ++row)
        if (visible[(size_t) row].id == selectedId) { table.selectRow(row, true); break; }
}
void FileListPanel::setSearchText(const juce::String& text) { search = text.trim(); rebuild(); }
void FileListPanel::rebuild()
{
    visible.clear();
    for (auto& r : all) if (search.isEmpty() || r.fileName.containsIgnoreCase(search)) visible.push_back(r);
    sortOrderChanged(sortColumn,ascending);
    table.deselectAllRows(); table.updateContent(); repaint();
}
void FileListPanel::sortOrderChanged(int column, bool forwards)
{
    sortColumn = column; ascending = forwards;
    std::stable_sort(visible.begin(),visible.end(),[column,forwards](const auto& a,const auto& b)
    {
        int order = 0;
        if (column == 3) order = (a.duration > b.duration) - (a.duration < b.duration);
        else if (column == 4) order = (a.sampleRate > b.sampleRate) - (a.sampleRate < b.sampleRate);
        else if (column == 5) order = a.numChannels - b.numChannels;
        else if (column == 2) order = a.format.compareIgnoreCase(b.format);
        else if (column == 6) order = a.error.compareIgnoreCase(b.error);
        else order = a.fileName.compareIgnoreCase(b.fileName);
        if (order == 0) order = (a.id > b.id) - (a.id < b.id);
        return forwards ? order < 0 : order > 0;
    });
    table.deselectAllRows(); table.updateContent(); table.repaint();
}
void FileListPanel::paintRowBackground(juce::Graphics& g,int row,int width,int height,bool selected)
{
    g.fillAll(selected ? juce::Colour(0xff342844) : (row % 2 ? theme::panel : theme::background));
    if (selected) { g.setColour(theme::accent); g.fillRect(0,0,3,height); }
    g.setColour(theme::line.withAlpha(0.25f)); g.drawHorizontalLine(height-1,0,(float) width);
}
void FileListPanel::paintCell(juce::Graphics& g,int row,int col,int w,int h,bool)
{
    if (row < 0 || row >= getNumRows()) return;
    const auto& r = visible[(size_t) row];
    juce::String value;
    switch (col)
    {
        case 1: value = r.fileName; break;
        case 2: value = r.format; break;
        case 3: value = r.duration > 0 ? juce::String(r.duration,2) + " s" : "--"; break;
        case 4: value = r.sampleRate > 0 ? juce::String(r.sampleRate/1000,1) + " kHz" : "--"; break;
        case 5: value = r.numChannels > 0 ? juce::String(r.numChannels) : "--"; break;
        case 6: value = r.missing ? "Missing" : (r.error.isNotEmpty() ? "Decode error" : "Analyzed"); break;
    }
    g.setColour(col == 1 ? theme::text : theme::muted); g.setFont(juce::FontOptions(13));
    g.drawText(value,10,0,w-16,h,juce::Justification::centredLeft,true);
}
void FileListPanel::selectedRowsChanged(int row)
{
    if (! restoringSelection && row >= 0 && row < getNumRows() && onSelected) onSelected(visible[(size_t) row]);
}
}
