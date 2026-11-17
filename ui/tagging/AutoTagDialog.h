#pragma once
#include "core/database/LibraryQuery.h"
#include "core/tagging/AutoTagService.h"
#include "ui/shell/DialogSurface.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace sonicator
{
class RoundTagMenu;
// Reviewable AutoTag proposals. One table row is one file; the Tags cell holds every proposed tag
// as a pill. Alt-click a pill to drop it from this apply, click a pill (or the +) to open the round
// tag menu and add or drop proposals for that file. Nothing is written until Apply selected.
class AutoTagDialog final : public juce::Component, private juce::Timer
{
public:
    AutoTagDialog(AutoTagResult prepared, AutoTagService& service,
                  std::function<void()> onApplied,
                  std::function<void(const juce::File&)> onPreview = {},
                  std::function<void()> onStopPreview = {},
                  std::vector<Tag> rankedTags = {},
                  std::vector<Tag> allTags = {});
    ~AutoTagDialog() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    // Sorts the review table by a header column; exposed so the UI test covers every column.
    static void sortProposals(std::vector<AutoTagProposal>& proposals, int columnId, bool forwards);
private:
    class ProposalModel;
    class SkipModel;
    friend class ProposalModel;
    friend class SkipModel;

    struct RowGroup
    {
        int64_t sampleId = 0;
        juce::String fileName, folder, filePath;
        std::vector<size_t> proposals; // indices into result.proposals, in display order
    };

    AutoTagResult result;
    AutoTagService& service;
    std::function<void()> appliedCallback;
    std::vector<Tag> rankedTags, allTags;
    std::map<int64_t, uint32_t> tagColours;
    std::vector<RowGroup> rows;
    std::unique_ptr<RoundTagMenu> roundMenu;
    juce::Point<int> lastMenuAnchorScreen;

    dialogs::Surface surface;
    juce::Label summary, selectedLabel, progress;
    juce::TableListBox proposals { "AutoTag proposals", nullptr };
    juce::TableListBox skips { "Skipped files", nullptr };
    std::unique_ptr<ProposalModel> proposalModel;
    std::unique_ptr<SkipModel> skipModel;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    juce::TextButton apply { "Apply selected" }, cancel { "Cancel" };
    bool applying = false, finishReported = false;
    std::function<void(const juce::File&)> previewCallback;
    std::function<void()> stopPreviewCallback;
    int previewGeneration = 0;

    void rebuildRows();
    void updateSummary();
    void toggleRow(int row);
    void toggleTagForSample(int64_t sampleId, int64_t tagId);
    void openTagMenuForRow(int row);
    void openTagMenuForRow(int row, juce::Point<int> anchor);
    void openTagMenuFromCell(int row, const juce::MouseEvent&);
    void showAllTagsForSample(int64_t sampleId, juce::Point<int> screenPosition);
    void layoutPills(const RowGroup& row, juce::Rectangle<int> area,
                     std::vector<std::pair<juce::Rectangle<int>, size_t>>& out) const;
    juce::Colour tagColour(int64_t tagId) const;
    bool rowHasIncludedTag(int64_t sampleId, int64_t tagId) const;
    void beginApply();
    void finishApply();
    void previewProposal(int row);
    void updateSelectionLabel();
    void closeDialog();
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoTagDialog)
};
} // namespace sonicator
