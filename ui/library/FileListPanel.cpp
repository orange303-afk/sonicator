#include "FileListPanel.h"
#include "ui/shell/Theme.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
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
    table.addMouseListener(&rowDragListener, true);
    auto& h = table.getHeader();
    h.addColumn("",8,30,24,40);
    h.addColumn("SAMPLE",1,250,120,1200);
    h.addColumn("FORMAT",2,70,55,120);
    h.addColumn("LENGTH",3,85,60,140);
    h.addColumn("RATE",4,85,60,140);
    h.addColumn("CH",5,45,35,70);
    h.addColumn("TAGS",7,145,80,350);
    h.addColumn("DUPLICATES",9,88,70,130);
    h.addColumn("STATUS",6,120,80,350);
    h.setSortColumnId(1,true);
}
void FileListPanel::lookAndFeelChanged() { refreshTheme(); }
void FileListPanel::refreshTheme()
{
    table.setColour(juce::ListBox::backgroundColourId,theme::table);
    table.setColour(juce::ListBox::outlineColourId,juce::Colours::transparentBlack);
    table.setColour(juce::ListBox::textColourId,theme::text);
    table.repaint();
}
void FileListPanel::selectRows(const std::vector<int>& rows)
{
    table.deselectAllRows();
    for (auto row : rows) if (row >= 0 && row < getNumRows()) table.selectRow(row, true, false);
}
int64_t FileListPanel::idAt(int row) const
{
    const int total = virtualMode ? (int) virtualIds.size() : (int) visible.size();
    if (row < 0 || row >= total) return 0;
    return virtualMode ? virtualIds[(size_t) row] : visible[(size_t) row].id;
}
const SampleRecord* FileListPanel::recordAt(int row)
{
    if (row < 0 || row >= getNumRows()) return nullptr;
    if (! virtualMode) return &visible[(size_t) row];
    const auto found = virtualRows.find(row);
    if (found != virtualRows.end()) return &found->second;
    loadVirtualBlock(row);
    const auto loaded = virtualRows.find(row);
    return loaded != virtualRows.end() ? &loaded->second : nullptr;
}
std::vector<SampleRecord> FileListPanel::getSelectedSamples()
{
    std::vector<SampleRecord> out;
    const auto rows = table.getSelectedRows();
    for (int i = 0; i < rows.size(); ++i)
        if (const auto* record = recordAt(rows[i])) out.push_back(*record);
    return out;
}
juce::StringArray FileListPanel::dragPaths()
{
    juce::StringArray candidates;
    for (const auto& record : getSelectedSamples()) candidates.add(record.filePath);
    return ExternalFileDrag::existingPaths(candidates);
}
int FileListPanel::prepareRowDrag(int row, bool addToSelection)
{
    if (row < 0 || row >= getNumRows()) return row;
    if (! table.isRowSelected(row)) table.selectRow(row, true, ! addToSelection);
    return row;
}
void FileListPanel::handlePress(const juce::MouseEvent& e)
{
    const auto pos = e.getEventRelativeTo(&table).getPosition();
    dragOrigin = pos;
    dragStarted = false;
    pressedRow = table.getRowContainingPosition(pos.x, pos.y);
    dragArmed = pressedRow >= 0;
    if (dragArmed) prepareRowDrag(pressedRow, e.mods.isCommandDown() || e.mods.isShiftDown());
}
void FileListPanel::handleDrag(const juce::MouseEvent& e)
{
    if (! dragArmed || dragStarted) return;
    const auto pos = e.getEventRelativeTo(&table).getPosition();
    if (! ExternalFileDrag::exceedsThreshold(pos.x - dragOrigin.x, pos.y - dragOrigin.y)) return;
    dragStarted = true;
    dragArmed = false;
    const auto paths = dragPaths();
    if (paths.isEmpty())
    {
        if (onDragMessage) onDragMessage("Selected files are missing or unavailable");
        return;
    }
    ExternalFileDrag::start(paths, &table, externalDrag);
}
void FileListPanel::handleRelease()
{
    dragArmed = false;
    dragStarted = false;
    pressedRow = -1;
}
void FileListPanel::loadVirtualBlock(int row)
{
    if (! rowLoader || virtualIds.empty()) return;
    const int total = (int) virtualIds.size();
    int start = juce::jmax(0, row - 100);
    int end = juce::jmin(total, start + 400);
    if (end - start < 400) start = juce::jmax(0, end - 400);
    std::vector<int64_t> ids(virtualIds.begin() + start, virtualIds.begin() + end);
    LoadedRows loaded;
    try { loaded = rowLoader(ids); }
    catch (...) { return; }
    std::map<int64_t, SampleRecord> byId;
    for (auto& record : loaded.records) byId[record.id] = std::move(record);
    for (int i = start; i < end; ++i)
    {
        const auto found = byId.find(virtualIds[(size_t) i]);
        if (found != byId.end()) virtualRows[i] = found->second;
    }
    for (auto& [id, tags] : loaded.tags) rowTags[id] = std::move(tags);
    for (auto& [id, count] : loaded.duplicates) duplicateCounts[id] = count;
    evictVirtualRows(row);
}
void FileListPanel::evictVirtualRows(int centreRow)
{
    if (virtualRows.size() <= 4000) return;
    for (auto it = virtualRows.begin(); it != virtualRows.end();)
    {
        if (std::abs(it->first - centreRow) > 2000)
        {
            rowTags.erase(it->second.id);
            duplicateCounts.erase(it->second.id);
            it = virtualRows.erase(it);
        }
        else ++it;
    }
}
void FileListPanel::setVirtualSamples(std::vector<int64_t> orderedIds, RowLoader loader)
{
    std::vector<int64_t> selectedIds;
    int64_t leadId = 0;
    const auto selectedRows = table.getSelectedRows();
    for (int i = 0; i < selectedRows.size(); ++i) selectedIds.push_back(idAt(selectedRows[i]));
    const int leadRow = table.getLastRowSelected();
    if (leadRow >= 0 && leadRow < getNumRows()) leadId = idAt(leadRow);
    const juce::ScopedValueSetter<bool> restoring(restoringSelection, true);
    virtualMode = true;
    virtualIds = std::move(orderedIds);
    virtualRows.clear();
    all.clear(); visible.clear(); search.clear();
    rowTags.clear(); duplicateCounts.clear();
    rowLoader = std::move(loader);
    table.deselectAllRows();
    table.updateContent();
    int restoredLead = -1;
    for (int row = 0; row < (int) virtualIds.size(); ++row)
    {
        const auto id = virtualIds[(size_t) row];
        if (std::find(selectedIds.begin(), selectedIds.end(), id) == selectedIds.end()) continue;
        table.selectRow(row, true, false);
        if (id == leadId) restoredLead = row;
    }
    if (restoredLead >= 0)
    {
        table.selectRow(restoredLead, true, false);
        table.scrollToEnsureRowIsOnscreen(restoredLead);
    }
    else table.scrollToEnsureRowIsOnscreen(0);
    repaint();
}
void FileListPanel::setSamples(std::vector<SampleRecord> records)
{
    std::vector<int64_t> selectedIds;
    int64_t leadId = 0;
    const auto selectedRows = table.getSelectedRows();
    for (int i = 0; i < selectedRows.size(); ++i) selectedIds.push_back(idAt(selectedRows[i]));
    const int leadRow = table.getLastRowSelected();
    if (leadRow >= 0 && leadRow < getNumRows()) leadId = idAt(leadRow);
    const juce::ScopedValueSetter<bool> restoring(restoringSelection, true);
    virtualMode = false; virtualIds.clear(); virtualRows.clear(); rowLoader = nullptr;
    rowTags.clear(); duplicateCounts.clear();
    all = std::move(records); rebuild();
    if (selectedIds.empty()) return;
    int restoredLead = -1;
    for (int row = 0; row < getNumRows(); ++row)
    {
        const auto id = idAt(row);
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
void FileListPanel::setDuplicateCounts(std::map<int64_t, int> counts)
{
    duplicateCounts = std::move(counts);
    table.repaint();
}
void FileListPanel::setSearchText(const juce::String& text)
{
    if (virtualMode) return; // whole-catalog search is executed in SQL by the owner
    search = text.trim(); rebuild();
}
void FileListPanel::rebuild()
{
    visible.clear();
    for (auto& r : all) if (search.isEmpty() || r.fileName.containsIgnoreCase(search)) visible.push_back(r);
    sortVisible(sortColumn,ascending);
    table.deselectAllRows(); table.updateContent(); repaint();
}
void FileListPanel::sortOrderChanged(int column, bool forwards)
{
    if (column == 7 || column == 8 || column == 9)
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
    g.fillAll(selected ? theme::selection : (row % 2 ? theme::rowAlternate : theme::table));
    g.setColour(theme::line.withAlpha(0.25f)); g.drawHorizontalLine(height-1,0,(float) width);
}
void FileListPanel::paintCell(juce::Graphics& g,int row,int col,int w,int h,bool)
{
    const auto* record = recordAt(row);
    if (record == nullptr) return;
    const auto& r = *record;
    if (col == 8)
    {
        const auto area = juce::Rectangle<float>(0, 0, (float) w, (float) h).withSizeKeepingCentre(14.0f, 14.0f);
        g.setColour(r.starred ? theme::text : theme::muted.withAlpha(0.55f));
        drawStar(g, area, r.starred);
        return;
    }
    if (col == 9)
    {
        const auto found = duplicateCounts.find(r.id);
        if (found == duplicateCounts.end() || found->second < 2)
        {
            g.setColour(theme::muted.withAlpha(0.4f)); g.setFont(juce::FontOptions(12));
            g.drawText("--",8,0,w-12,h,juce::Justification::centredLeft,true);
        }
        else
        {
            g.setColour(theme::muted); g.setFont(juce::FontOptions(12));
            g.drawText(juce::String(juce::CharPointer_UTF8("\xc3\x97")) + juce::String(found->second),
                       8,0,w-12,h,juce::Justification::centredLeft,true);
        }
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
    const auto* lead = recordAt(row);
    if (lead != nullptr && ! selection.empty()) onContextMenu(selection, *lead, event.getScreenPosition());
}
void FileListPanel::selectedRowsChanged(int row)
{
    if (restoringSelection || row < 0 || row >= getNumRows() || ! onSelected) return;
    if (const auto* record = recordAt(row)) onSelected(*record);
}
} // namespace sonicator
