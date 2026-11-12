#pragma once
#include "core/tagging/AutoTagService.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
namespace sonicator {
// Reviewable AutoTag proposals. Nothing is written until Apply selected; Cancel closes without
// writing. Rows stay virtualized, so a large folder does not create one component per file.
class AutoTagDialog final : public juce::Component, private juce::Timer
{
public:
    AutoTagDialog(AutoTagResult prepared, AutoTagService& service,
                  std::function<void()> onApplied);
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
    AutoTagResult result;
    AutoTagService& service;
    std::function<void()> appliedCallback;
    juce::Label summary, selectedLabel, progress;
    juce::TableListBox proposals { "AutoTag proposals", nullptr };
    juce::TableListBox skips { "Skipped files", nullptr };
    std::unique_ptr<ProposalModel> proposalModel;
    std::unique_ptr<SkipModel> skipModel;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    juce::TextButton apply { "Apply selected" }, cancel { "Cancel" };
    bool applying = false, finishReported = false;
    void toggleRow(int row);
    void beginApply();
    void finishApply();
    void updateSelectionLabel();
    void closeDialog();
    void timerCallback() override;
};
} // namespace sonicator
