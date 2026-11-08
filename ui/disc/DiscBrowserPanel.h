#pragma once
#include "core/disc/DiscBrowserService.h"
#include "core/sampler/SamplerTypes.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
namespace sonicator {
// Read-only browser for one disc image: partitions and volumes, or ISO folders, with a lazy list.
// It never creates SQL rows or fake file paths; a decoded source is handed to the owner as PCM.
class DiscBrowserPanel final : public juce::Component,
                               private juce::Timer,
                               private juce::ListBoxModel
{
public:
    DiscBrowserPanel();
    ~DiscBrowserPanel() override;
    void open(const juce::File&);
    void closeImage();
    juce::File openedImage() const { return image; }
    bool canReuseImage(const juce::File& file) const
    {
        return imageReady && image == file && file.existsAsFile()
            && imageSize == file.getSize() && imageModified == file.getLastModificationTime().toMilliseconds();
    }
    void showDirectory(DiscEntryId);
    void setHideEmpty(bool);
    // Called before decoding (including unsupported rows), so the old voice cannot linger.
    std::function<void()> onSelectionBegin;
    void refreshFolderTree() { if (onTreeReady && imageReady) onTreeReady(image, imageRoot, imageEntries); }
    std::function<void(const juce::File&, DiscEntryId, const std::vector<DiscEntry>&)> onTreeReady;
    std::function<void(const juce::File&, const juce::String&)> onImageError;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    void returnKeyPressed(int row) override;
    void selectedRowsChanged(int lastRowSelected) override;
    // Selection is the same manual action as a click: it decodes and follows Auto Preview.
    void selectRow(int row) { list.selectRow(row); }
    std::function<void(const juce::File&, const juce::String&, SampleAudioPtr)> onSampleSelected;
    std::function<void()> onExit;
private:
    struct Crumb { DiscEntryId id; juce::String name; };
    void timerCallback() override;
    void back();
    void activate(int row);
    void selectEntry(int row);
    void refreshEntries();
    void applyCompletion(const DiscBrowserService::Completion&);
    void showError(const juce::String&);
    DiscBrowserService service;
    juce::File image;
    int64_t imageSize = 0, imageModified = 0;
    std::vector<Crumb> crumbs;
    DiscEntryId current;
    std::vector<DiscEntry> entries;
    std::vector<DiscEntry> imageEntries;
    DiscEntryId imageRoot;
    DiscEntryId requestedDirectory;
    std::vector<CueTrack> trackChoices;
    juce::String pendingDecodeName;
    DiscEntryId pendingEntry, lastSelectedEntry;
    SampleAudioPtr lastSelectedAudio;
    bool busy = false, imageReady = false; // Akai's valid filesystem root has ID zero.
    bool hideEmpty = false, updatingSelection = false;
    juce::ListBox list { "Disc entries", this };
    juce::TextButton backButton { "Back" }, exitButton { "Close" };
    juce::Label pathLabel, statusLabel;
};
} // namespace sonicator
