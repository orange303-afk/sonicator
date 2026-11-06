#include "FileListPanel.h"
#include "ui/shell/Theme.h"
namespace sonicator {
namespace {
void drawStar(juce::Graphics& g, juce::Rectangle<float> r, bool filled)
{
    juce::Path path;
    const auto centre = r.getCentre();
    const float outer = juce::jmin(r.getWidth(), r.getHeight()) / 2.0f;
    const float inner = outer * 0.45f;
    for (int i = 0; i < 10; ++i)
    {
        const float angle = -juce::MathConstants<float>::halfPi
            + i * juce::MathConstants<float>::pi / 5.0f;
        const float radius = (i % 2 == 0) ? outer : inner;
        const juce::Point<float> point(centre.x + std::cos(angle) * radius,
                                       centre.y + std::sin(angle) * radius);
        if (i == 0) path.startNewSubPath(point); else path.lineTo(point);
    }
    path.closeSubPath();
    if (filled) g.fillPath(path);
    else g.strokePath(path, juce::PathStrokeType(1.0f));
}
}
FileListPanel::FileListPanel()
{
    addAndMakeVisible(table); table.setRowHeight(34);
    refreshTheme();
    table.setMultipleSelectionEnabled(true);
    auto& h = table.getHeader();
    h.addColumn("",8,30,24,40);
    h.addColumn("SAMPLE",1,265,120,1200);
    h.addColumn("FORMAT",2,70,55,120);
    h.addColumn("LENGTH",3,85,60,140);
    h.addColumn("RATE",4,85,60,140);
    h.addColumn("CH",5,45,35,70);
    h.addColumn("TAGS",7,145,80,350);
    h.addColumn("STATUS",6,120,80,350);
    h.setSortColumnId(1,true);
}
void FileListPanel::lookAndFeelChanged() { refreshTheme(); }
void FileListPanel::refreshTheme()
{
    table.setColour(juce::ListBox::backgroundColourId,theme::background);
    table.setColour(juce::ListBox::outlineColourId,theme::line);
    table.setColour(juce::ListBox::textColourId,theme::text);
    table.repaint();
}
void FileListPanel::selectRows(const std::vector<int>& rows)
{
    table.deselectAllRows();
    for (auto row : rows) if (row >= 0 && row < (int) visible.size()) table.selectRow(row, true, false);
}
std::vector<SampleRecord> FileListPanel::getSelectedSamples() const
{
    std::vector<SampleRecord> out;
    const auto rows = table.getSelectedRows();
    for (int i = 0; i < rows.size(); ++i)
    {
        const int row = rows[i];
        if (row >= 0 && row < (int) visible.size()) out.push_back(visible[(size_t) row]);
    }
    return out;
}
void FileListPanel::setSamples(std::vector<SampleRecord> records)
{
    std::vector<int64_t> selectedIds;
    int64_t leadId = 0;
    const auto selectedRows = table.getSelectedRows();
    for (int i = 0; i < selectedRows.size(); ++i)
    {
        const int row = selectedRows[i];
        if (row >= 0 && row < getNumRows()) selectedIds.push_back(visible[(size_t) row].id);
    }
    const int leadRow = table.getLastRowSelected();
    if (leadRow >= 0 && leadRow < getNumRows()) leadId = visible[(size_t) leadRow].id;
    const juce::ScopedValueSetter<bool> restoring(restoringSelection, true);
    all = std::move(records); rebuild();
    if (selectedIds.empty()) return;
    int restoredLead = -1;
    for (int row = 0; row < getNumRows(); ++row)
    {
        const auto id = visible[(size_t) row].id;
        if (std::find(selectedIds.begin(), selectedIds.end(), id) == selectedIds.end()) continue;
        table.selectRow(row, true, false);
        if (id == leadId) restoredLead = row;
    }
    if (restoredLead >= 0) table.selectRow(restoredLead, true, false);
}
void FileListPanel::setRowTags(std::map<int64_t, std::vector<Tag>> tags)
{
    rowTags = std::move(tags);
    table.repaint();
}
void FileListPanel::setSearchText(const juce::String& text) { search = text.trim(); rebuild(); }
void FileListPanel::rebuild()
{
    visible.clear();
    for (auto& r : all) if (search.isEmpty() || r.fileName.containsIgnoreCase(search)) visible.push_back(r);
    sortVisible(sortColumn,ascending);
    table.deselectAllRows(); table.updateContent(); repaint();
}
void FileListPanel::sortOrderChanged(int column, bool forwards)
{
    if (column == 7 || column == 8)
    {
        // Tags are shown for context but are not a sort key; keep the header arrow on the real one.
        const juce::ScopedValueSetter<bool> restoring(restoringSelection, true);
        table.getHeader().setSortColumnId(sortColumn, ascending);
        return;
    }
    sortColumn = column; ascending = forwards;
    sortVisible(column, forwards);
    table.updateContent(); table.repaint();
    // Rebuilding a page re-applies its order locally; only a real header click reaches the owner.
    if (! restoringSelection && onSortChanged != nullptr) onSortChanged(column, forwards);
}
void FileListPanel::sortVisible(int column, bool forwards)
{
    std::stable_sort(visible.begin(),visible.end(),[column,forwards](const auto& a,const auto& b)
    {
        int order = 0;
        if (column == 3) order = (a.duration > b.duration) - (a.duration < b.duration);
        else if (column == 4) order = (a.sampleRate > b.sampleRate) - (a.sampleRate < b.sampleRate);
        else if (column == 5) order = a.numChannels - b.numChannels;
        else if (column == 2) order = a.format.compareIgnoreCase(b.format);
        else if (column == 6) order = a.error.compareIgnoreCase(b.error);
        else order = a.fileName.compareIgnoreCase(b.fileName);
        if (order == 0) return a.id < b.id;
        return forwards ? order < 0 : order > 0;
    });
}
void FileListPanel::paintRowBackground(juce::Graphics& g,int row,int width,int height,bool selected)
{
    g.fillAll(selected ? theme::selection : (row % 2 ? theme::panel : theme::background));
    if (selected) { g.setColour(theme::accent); g.fillRect(0,0,3,height); }
    g.setColour(theme::line.withAlpha(0.25f)); g.drawHorizontalLine(height-1,0,(float) width);
}
void FileListPanel::paintCell(juce::Graphics& g,int row,int col,int w,int h,bool)
{
    if (row < 0 || row >= getNumRows()) return;
    const auto& r = visible[(size_t) row];
    if (col == 8)
    {
        const auto area = juce::Rectangle<float>(0, 0, (float) w, (float) h).withSizeKeepingCentre(14.0f, 14.0f);
        g.setColour(r.starred ? juce::Colour(0xffe0b84a) : theme::muted.withAlpha(0.55f));
        drawStar(g, area, r.starred);
        return;
    }
    if (col == 7)
    {
        const auto found = rowTags.find(r.id);
        if (found == rowTags.end() || found->second.empty())
        {
            g.setColour(theme::muted.withAlpha(0.5f)); g.setFont(juce::FontOptions(12));
            g.drawText("--",10,0,w-16,h,juce::Justification::centredLeft,true);
            return;
        }
        g.setFont(juce::FontOptions(11));
        int x = 8;
        for (const auto& tag : found->second)
        {
            if (x >= w - 12) break;
            const auto colour = juce::Colour(tag.colour);
            g.setColour(colour); g.fillEllipse((float) x, h / 2.0f - 3.0f, 6.0f, 6.0f);
            x += 10;
            const int textWidth = juce::GlyphArrangement::getStringWidthInt(juce::FontOptions(11), tag.name);
            g.setColour(theme::tagText(colour));
            g.drawText(tag.name, x, 0, juce::jmax(0, w - x - 6), h, juce::Justification::centredLeft, true);
            x += juce::jmin(textWidth + 10, juce::jmax(0, w - x));
        }
        return;
    }
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
void FileListPanel::cellClicked(int row, int, const juce::MouseEvent& event)
{
    if (! event.mods.isPopupMenu() || row < 0 || row >= getNumRows() || ! onContextMenu) return;
    if (! table.isRowSelected(row)) table.selectRow(row, true);
    const auto selection = getSelectedSamples();
    if (! selection.empty()) onContextMenu(selection, visible[(size_t) row], event.getScreenPosition());
}
void FileListPanel::selectedRowsChanged(int row)
{
    if (! restoringSelection && row >= 0 && row < getNumRows() && onSelected) onSelected(visible[(size_t) row]);
}
} // namespace sonicator
