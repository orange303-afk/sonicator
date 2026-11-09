#include "DiscBrowserPanel.h"
#include "ui/shell/Theme.h"
#include <set>
namespace sonicator {
DiscBrowserPanel::DiscBrowserPanel()
{
    setComponentID("discBrowser");
    setOpaque(true);
    addAndMakeVisible(backButton);
    addAndMakeVisible(exitButton);
    addAndMakeVisible(list);
    addAndMakeVisible(pathLabel);
    addAndMakeVisible(statusLabel);
    backButton.onClick = [this] { back(); };
    exitButton.onClick = [this] { if (onExit) onExit(); };
    pathLabel.setFont(juce::FontOptions(14, juce::Font::bold));
    statusLabel.setComponentID("discStatus");
    statusLabel.setColour(juce::Label::textColourId, theme::muted);
    statusLabel.setFont(juce::FontOptions(12));
    list.setRowHeight(24);
    list.setWantsKeyboardFocus(true);
    startTimerHz(30);
}
DiscBrowserPanel::~DiscBrowserPanel()
{
    stopTimer();
    service.close();
}
void DiscBrowserPanel::open(const juce::File& file)
{
    if (onSelectionBegin) onSelectionBegin();
    image = file;
    imageSize = file.getSize(); imageModified = file.getLastModificationTime().toMilliseconds();
    imageRoot = {}; requestedDirectory = {};
    imageReady = false;
    crumbs.clear();
    entries.clear();
    imageEntries.clear();
    current = {};
    pendingDecodeName.clear();
    busy = true;
    statusLabel.setText("Opening " + file.getFileName() + "...", juce::dontSendNotification);
    pathLabel.setText(file.getFileName(), juce::dontSendNotification);
    list.updateContent();
    list.repaint();
    service.requestOpen(file);
}
void DiscBrowserPanel::closeImage()
{
    if (onSelectionBegin) onSelectionBegin();
    service.close();
    entries.clear();
    trackChoices.clear();
    imageEntries.clear(); imageRoot = {}; requestedDirectory = {};
    imageReady = false;
    pendingDecodeName.clear(); pendingEntry = {}; lastSelectedEntry = {}; lastSelectedAudio.reset();
    crumbs.clear();
    image = juce::File();
    busy = false;
    list.updateContent();
    statusLabel.setText({}, juce::dontSendNotification);
}
void DiscBrowserPanel::resized()
{
    auto area = getLocalBounds().reduced(10);
    auto top = area.removeFromTop(30);
    backButton.setBounds(top.removeFromLeft(70)); top.removeFromLeft(6);
    exitButton.setBounds(top.removeFromRight(70));
    pathLabel.setBounds(top.reduced(8, 0));
    area.removeFromTop(6);
    statusLabel.setBounds(area.removeFromBottom(34));
    list.setBounds(area);
}
bool DiscBrowserPanel::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::backspaceKey) { back(); return true; }
    if (key == juce::KeyPress(juce::KeyPress::upKey) || key == juce::KeyPress(juce::KeyPress::downKey))
        return list.keyPressed(key);
    return false;
}
int DiscBrowserPanel::getNumRows() { return (int) entries.size(); }
void DiscBrowserPanel::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= (int) entries.size()) return;
    const auto& entry = entries[(size_t) row];
    if (selected) g.fillAll(theme::selection);
    auto area = juce::Rectangle<int>(0, 0, width, height).reduced(8, 0);
    auto name = entry.displayName;
    if (entry.kind == DiscEntryKind::directory) name += "/";
    g.setColour(entry.capability.isEmpty() ? theme::text
        : (theme::isDark ? juce::Colour(0xffff8b8b) : juce::Colour(0xffb3261e)));
    g.setFont(juce::FontOptions(13));
    g.drawText(name, area.removeFromLeft(juce::jmax(120, width - 200)),
               juce::Justification::centredLeft, true);
    if (entry.declaredBytes > 0)
        g.drawText(juce::String(entry.declaredBytes) + " B", area, juce::Justification::centredRight, true);
    else if (entry.capability.isNotEmpty())
        g.drawText(entry.capability, area, juce::Justification::centredRight, true);
}
void DiscBrowserPanel::listBoxItemDoubleClicked(int row, const juce::MouseEvent&) { activate(row); }
void DiscBrowserPanel::returnKeyPressed(int row) { activate(row); }
void DiscBrowserPanel::selectedRowsChanged(int row)
{
    if (busy || updatingSelection || ! trackChoices.empty() || row < 0 || row >= (int) entries.size()) return;
    selectEntry(row);
}
void DiscBrowserPanel::selectEntry(int row)
{
    if (row < 0 || row >= (int) entries.size()) return;
    const auto& entry = entries[(size_t) row];
    service.cancelPending();
    pendingDecodeName.clear();
    if (onSelectionBegin) onSelectionBegin();
    if (entry.kind != DiscEntryKind::file || entry.capability.isNotEmpty())
    {
        if (entry.capability.isNotEmpty()) statusLabel.setText(entry.capability, juce::dontSendNotification);
        return;
    }
    if (entry.id == lastSelectedEntry && lastSelectedAudio != nullptr)
    {
        if (onSampleSelected) onSampleSelected(image, entry.displayName, lastSelectedAudio);
        return;
    }
    pendingEntry = entry.id;
    pendingDecodeName = entry.displayName;
    statusLabel.setText("Decoding " + entry.displayName + "...", juce::dontSendNotification);
    service.requestDecode(entry.id, entry.displayName, entry.partner);
}
void DiscBrowserPanel::activate(int row)
{
    if (busy || row < 0 || row >= (int) entries.size()) return;
    if (! trackChoices.empty() && row < (int) trackChoices.size())
    {
        const auto number = trackChoices[(size_t) row].number;
        trackChoices.clear();
        entries.clear();
        list.updateContent();
        busy = true;
        statusLabel.setText("Opening track " + juce::String(number) + "...", juce::dontSendNotification);
        service.requestOpen(image, number);
        return;
    }
    const auto entry = entries[(size_t) row];
    if (entry.kind == DiscEntryKind::file || entry.capability.isNotEmpty())
    {
        selectEntry(row);
        return;
    }
    if (entry.kind == DiscEntryKind::directory || entry.capability.isEmpty())
    {
        busy = true;
        crumbs.push_back({ entry.id, entry.displayName });
        current = entry.id;
        pathLabel.setText([this] { juce::StringArray parts; for (const auto& crumb : crumbs) parts.add(crumb.name); return parts.joinIntoString(" / "); }(),
                          juce::dontSendNotification);
        statusLabel.setText("Loading...", juce::dontSendNotification);
        entries.clear();
        list.updateContent();
        service.requestList(entry.id);
        return;
    }
    showError(entry.capability.isNotEmpty() ? entry.capability : "This entry cannot be opened");
}
void DiscBrowserPanel::showError(const juce::String& message)
{
    busy = false;
    revealPending = false;
    if (! imageReady && onImageError) onImageError(image, message);
    if (onError) onError(message);
    statusLabel.setText(message, juce::dontSendNotification);
}
void DiscBrowserPanel::back()
{
    if (busy) return;
    if (crumbs.size() <= 1)
    {
        if (onExit) onExit();
        return;
    }
    showDirectory(crumbs[crumbs.size() - 2].id);
}
void DiscBrowserPanel::showDirectory(DiscEntryId directory)
{
    requestedDirectory = directory;
    if (onSelectionBegin) onSelectionBegin();
    if (! imageReady) return;
    service.cancelPending();
    if (directory.value == 0) directory = imageRoot;
    current = directory;
    pendingDecodeName.clear();
    lastSelectedEntry = {};
    lastSelectedAudio.reset();
    std::map<DiscEntryId, const DiscEntry*> byId;
    for (const auto& entry : imageEntries) byId[entry.id] = &entry;
    crumbs.clear();
    std::set<DiscEntryId> seen;
    auto id = directory;
    while (id != imageRoot && seen.insert(id).second)
    {
        auto found = byId.find(id);
        if (found == byId.end()) break;
        crumbs.insert(crumbs.begin(), { id, found->second->displayName });
        id = found->second->parent;
    }
    crumbs.insert(crumbs.begin(), { imageRoot, image.getFileName() });
    juce::StringArray parts;
    for (const auto& crumb : crumbs) parts.add(crumb.name);
    pathLabel.setText(parts.joinIntoString(" / "), juce::dontSendNotification);
    refreshEntries();
    busy = false;
}
void DiscBrowserPanel::previewEntry(const juce::File& file, DiscEntryId parent, DiscEntryId entry,
                                    const juce::String& name)
{
    if (! file.existsAsFile()) { showError("The disc image is not available"); return; }
    revealParent = parent; revealEntry = entry; revealName = name;
    revealPending = ! canReuseImage(file);
    if (! revealPending)
    {
        showDirectory(parent);
        for (int row = 0; row < (int) entries.size(); ++row)
            if (entries[(size_t) row].id == entry)
            {
                list.deselectAllRows();
                list.selectRow(row);
                return;
            }
        statusLabel.setText(revealName + " is no longer in this image", juce::dontSendNotification);
        return;
    }
    open(file);
}
void DiscBrowserPanel::setHideEmpty(bool hide)
{
    if (hideEmpty == hide) return;
    hideEmpty = hide;
    if (imageReady) refreshEntries();
}
void DiscBrowserPanel::refreshEntries()
{
    const auto directory = current;
    const int previousRow = list.getSelectedRow();
    const bool hadSelection = previousRow >= 0 && previousRow < (int) entries.size();
    const auto selected = hadSelection ? entries[(size_t) previousRow].id : DiscEntryId{};
    std::map<DiscEntryId, const DiscEntry*> byId;
    for (const auto& entry : imageEntries) byId[entry.id] = &entry;
    std::set<DiscEntryId> seen;
    entries.clear();
    for (const auto& entry : imageEntries)
    {
        if (entry.kind != DiscEntryKind::file && entry.capability.isEmpty()) continue;
        if (hideEmpty && (entry.capability.isNotEmpty() || entry.declaredBytes == 0)) continue;
        auto parent = entry.parent;
        seen.clear();
        while (parent != directory && parent != imageRoot && seen.insert(parent).second)
        {
            auto found = byId.find(parent);
            if (found == byId.end()) break;
            parent = found->second->parent;
        }
        if (parent == directory || directory == imageRoot) entries.push_back(entry);
    }
    const juce::ScopedValueSetter<bool> guard(updatingSelection, true);
    list.deselectAllRows();
    list.updateContent();
    bool restored = false;
    if (hadSelection)
        for (int row = 0; row < (int) entries.size(); ++row)
            if (entries[(size_t) row].id == selected) { list.selectRow(row); restored = true; break; }
    if (hadSelection && ! restored)
    {
        service.cancelPending(); pendingDecodeName.clear();
        if (onSelectionBegin) onSelectionBegin();
    }
    list.repaint();
    statusLabel.setText(juce::String(entries.size()) + " files (including subfolders)", juce::dontSendNotification);
}
void DiscBrowserPanel::applyCompletion(const DiscBrowserService::Completion& completion)
{
    if (completion.kind == DiscBrowserService::Kind::open)
    {
        if (completion.status == DiscStatus::ambiguous && ! completion.cueTracks.empty())
        {
            trackChoices = completion.cueTracks;
            entries.clear();
            for (const auto& track : trackChoices)
            {
                DiscEntry entry;
                entry.id = { (uint64_t) track.number };
                entry.kind = DiscEntryKind::file;
                entry.displayName = "Track " + juce::String(track.number) + "  ("
                    + (track.mode == CueTrackMode::mode1_2048 ? "MODE1/2048"
                     : track.mode == CueTrackMode::mode1_2352 ? "MODE1/2352" : "MODE2/2352") + ")";
                entry.declaredBytes = (track.endFrames - track.index01Frames)
                    * (track.mode == CueTrackMode::mode1_2048 ? 2048 : 2352);
                entries.push_back(std::move(entry));
            }
            pathLabel.setText(image.getFileName() + "  /  data track", juce::dontSendNotification);
            statusLabel.setText("Choose the data track to open", juce::dontSendNotification);
            busy = false;
            list.updateContent();
            list.repaint();
            return;
        }
        if (completion.status != DiscStatus::ready)
        {
            showError(completion.error.isNotEmpty() ? completion.error : "Cannot open this image");
            return;
        }
        trackChoices.clear();
        crumbs = { { completion.root, image.getFileName() } };
        current = completion.root;
        pathLabel.setText(image.getFileName(), juce::dontSendNotification);
        imageRoot = completion.root;
        service.requestList(completion.root, true);
        return;
    }
    if (completion.kind == DiscBrowserService::Kind::list)
    {
        imageEntries = completion.listing.entries;
        imageReady = true;
        refreshFolderTree();
        showDirectory(requestedDirectory);
        lastSelectedEntry = {};
        lastSelectedAudio.reset();
        list.updateContent();
        list.repaint();
        if (isShowing()) list.grabKeyboardFocus(); // a hidden panel only prepares a preview
        busy = false;
        statusLabel.setText(completion.listing.partial
            ? (completion.listing.warning.isNotEmpty() ? completion.listing.warning : "Partial listing")
            : juce::String(entries.size()) + " entries", juce::dontSendNotification);
        if (completion.status != DiscStatus::ready)
            statusLabel.setText(completion.error, juce::dontSendNotification);
        if (revealPending)
        {
            const auto parent = revealParent;
            const auto entry = revealEntry;
            const auto name = revealName;
            revealPending = false;
            showDirectory(parent);
            bool selected = false;
            for (int row = 0; row < (int) entries.size(); ++row)
                if (entries[(size_t) row].id == entry)
                {
                    list.deselectAllRows();
                    list.selectRow(row);
                    selected = true;
                    break;
                }
            if (! selected)
                statusLabel.setText(name + " is no longer in this image", juce::dontSendNotification);
        }
        return;
    }
    if (pendingDecodeName.isEmpty() || completion.entry != pendingEntry) return;
    if (completion.status != DiscStatus::ready || completion.audio == nullptr || completion.audio->frames <= 0)
    {
        const auto error = completion.error.isNotEmpty() ? completion.error : "Cannot decode this source";
        for (auto& entry : entries)
            if (entry.id == pendingEntry) entry.capability = error;
        for (auto& entry : imageEntries)
            if (entry.id == pendingEntry) entry.capability = error;
        refreshEntries();
        showError(error);
        return;
    }
    if (pendingDecodeName.isEmpty()) return;
    const auto name = pendingDecodeName;
    busy = false;
    lastSelectedEntry = pendingEntry;
    lastSelectedAudio = completion.audio;
    statusLabel.setText("Ready: " + name, juce::dontSendNotification);
    if (onSampleSelected) onSampleSelected(image, name, completion.audio);
}
void DiscBrowserPanel::timerCallback()
{
    DiscBrowserService::Completion completion;
    while (service.poll(completion))
    {
        if (completion.status != DiscStatus::cancelled && completion.generation != 0)
            applyCompletion(completion);
    }
}
} // namespace sonicator
