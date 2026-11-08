#include "DiscBrowserPanel.h"
#include "ui/shell/Theme.h"
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
    image = file;
    crumbs.clear();
    entries.clear();
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
    service.close();
    entries.clear();
    trackChoices.clear();
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
    g.setColour(entry.capability.isEmpty() ? theme::text : theme::muted);
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
    if (busy || row < 0 || row >= (int) entries.size()) return;
    selectEntry(row);
}
void DiscBrowserPanel::selectEntry(int row)
{
    if (row < 0 || row >= (int) entries.size()) return;
    const auto& entry = entries[(size_t) row];
    if (entry.kind != DiscEntryKind::file || entry.capability.isNotEmpty())
    {
        if (entry.capability.isNotEmpty()) statusLabel.setText(entry.capability, juce::dontSendNotification);
        return;
    }
    if (entry.id == lastSelectedEntry && lastSelectedAudio != nullptr) return; // already loaded
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
    if (entry.capability.isNotEmpty())
    {
        showError(entry.capability);
        return;
    }
    if (entry.kind == DiscEntryKind::file)
    {
        if (entry.id == lastSelectedEntry && lastSelectedAudio != nullptr)
        {
            if (onSampleSelected) onSampleSelected(image, entry.displayName, lastSelectedAudio);
            return;
        }
        pendingEntry = entry.id;
        pendingDecodeName = entry.displayName;
        statusLabel.setText("Decoding " + entry.displayName + "...", juce::dontSendNotification);
        service.requestDecode(entry.id, entry.displayName, entry.partner);
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
    crumbs.pop_back();
    current = crumbs.back().id;
    juce::StringArray parts;
    for (const auto& crumb : crumbs) parts.add(crumb.name);
    pathLabel.setText(parts.joinIntoString(" / "), juce::dontSendNotification);
    busy = true;
    statusLabel.setText("Loading...", juce::dontSendNotification);
    service.requestList(current);
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
        service.requestList(completion.root);
        return;
    }
    if (completion.kind == DiscBrowserService::Kind::list)
    {
        entries = completion.listing.entries;
        lastSelectedEntry = {};
        lastSelectedAudio.reset();
        list.updateContent();
        list.repaint();
        list.grabKeyboardFocus();
        busy = false;
        statusLabel.setText(completion.listing.partial
            ? (completion.listing.warning.isNotEmpty() ? completion.listing.warning : "Partial listing")
            : juce::String(entries.size()) + " entries", juce::dontSendNotification);
        if (completion.status != DiscStatus::ready)
            statusLabel.setText(completion.error, juce::dontSendNotification);
        return;
    }
    if (completion.status != DiscStatus::ready)
    {
        showError(completion.error.isNotEmpty() ? completion.error : "Cannot decode this source");
        return;
    }
    if (completion.audio == nullptr)
    {
        showError("The decoded source is empty");
        return;
    }
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
