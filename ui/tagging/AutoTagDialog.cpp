#include "AutoTagDialog.h"
#include "ui/shell/Theme.h"
#include <algorithm>
namespace sonicator {
void AutoTagDialog::sortProposals(std::vector<AutoTagProposal>& proposals, int columnId, bool forwards)
{
    if (columnId < 2 || columnId > 6) return;
    const auto folder = [](const AutoTagProposal& proposal)
    {
        return juce::File(proposal.filePath).getParentDirectory().getFullPathName();
    };
    const auto compare = [&](const AutoTagProposal& a, const AutoTagProposal& b)
    {
        switch (columnId)
        {
            case 2: return a.fileName.compareIgnoreCase(b.fileName);
            case 3: return folder(a).compareIgnoreCase(folder(b));
            case 4: return a.canonicalType.compareIgnoreCase(b.canonicalType);
            case 5: return a.reason.compareIgnoreCase(b.reason);
            case 6: return (int) a.alreadyAssigned - (int) b.alreadyAssigned;
            default: return 0;
        }
    };
    std::stable_sort(proposals.begin(), proposals.end(), [&](const AutoTagProposal& a, const AutoTagProposal& b)
    {
        const auto order = compare(a, b);
        if (order != 0) return forwards ? order < 0 : order > 0;
        return forwards ? a.filePath < b.filePath : a.filePath > b.filePath;
    });
}

class AutoTagDialog::ProposalModel final : public juce::TableListBoxModel
{
public:
    explicit ProposalModel(AutoTagDialog& o) : owner(o) {}
    int getNumRows() override { return (int) owner.result.proposals.size(); }
    void paintRowBackground(juce::Graphics& g, int row, int, int height, bool selected) override
    {
        if (row < 0 || row >= getNumRows()) return;
        if (selected) g.fillAll(theme::selection);
        else g.fillAll(row % 2 ? theme::table : theme::rowAlternate);
        juce::ignoreUnused(height);
    }
    void paintCell(juce::Graphics& g, int row, int column, int width, int height, bool) override
    {
        if (row < 0 || row >= getNumRows()) return;
        const auto& proposal = owner.result.proposals[(size_t) row];
        g.setFont(juce::FontOptions(12.0f));
        g.setColour(theme::text);
        auto area = juce::Rectangle<int>(0, 0, width, height).reduced(6, 0);
        if (column == 1)
        {
            g.setColour(proposal.alreadyAssigned ? theme::muted : theme::accent);
            g.drawText(proposal.alreadyAssigned ? "-" : (proposal.included ? "x" : " "),
                       area, juce::Justification::centredLeft);
            g.setColour(theme::line);
            g.drawRect(area.withSizeKeepingCentre(14, 14), 1);
        }
        else if (column == 2) g.drawText(proposal.fileName, area, juce::Justification::centredLeft, true);
        else if (column == 3)
        {
            g.setColour(theme::muted);
            g.drawText(juce::File(proposal.filePath).getParentDirectory().getFullPathName(),
                       area, juce::Justification::centredLeft, true);
        }
        else if (column == 4)
        {
            g.setColour(theme::accent);
            g.drawText(proposal.canonicalType, area, juce::Justification::centredLeft, true);
        }
        else if (column == 5)
        {
            g.setColour(theme::muted);
            g.drawText(proposal.reason, area, juce::Justification::centredLeft, true);
        }
        else if (column == 6)
        {
            g.setColour(proposal.alreadyAssigned ? theme::muted : theme::text);
            g.drawText(proposal.alreadyAssigned ? "Already assigned" : "New", area,
                       juce::Justification::centredLeft, true);
        }
    }
    void cellClicked(int row, int columnId, const juce::MouseEvent&) override
    {
        if (columnId == 1) owner.toggleRow(row);
    }
    void sortOrderChanged(int newSortColumnId, bool isForwards) override
    {
        AutoTagDialog::sortProposals(owner.result.proposals, newSortColumnId, isForwards);
        owner.proposals.updateContent();
    }
private:
    AutoTagDialog& owner;
};
class AutoTagDialog::SkipModel final : public juce::TableListBoxModel
{
public:
    explicit SkipModel(AutoTagDialog& o) : owner(o) {}
    int getNumRows() override { return (int) owner.result.skipped.size(); }
    void paintRowBackground(juce::Graphics& g, int row, int, int, bool selected) override
    {
        g.fillAll(selected ? theme::selection : (row % 2 ? theme::table : theme::rowAlternate));
    }
    void paintCell(juce::Graphics& g, int row, int column, int width, int height, bool) override
    {
        if (row < 0 || row >= getNumRows()) return;
        const auto& skipped = owner.result.skipped[(size_t) row];
        g.setFont(juce::FontOptions(12.0f));
        g.setColour(theme::text);
        auto area = juce::Rectangle<int>(0, 0, width, height).reduced(6, 0);
        if (column == 1) g.drawText(skipped.fileName, area, juce::Justification::centredLeft, true);
        else if (column == 2)
        {
            g.setColour(theme::muted);
            g.drawText(skipped.code, area, juce::Justification::centredLeft, true);
        }
        else if (column == 3) g.drawText(skipped.reason, area, juce::Justification::centredLeft, true);
    }
private:
    AutoTagDialog& owner;
};
AutoTagDialog::AutoTagDialog(AutoTagResult prepared, AutoTagService& s,
                             std::function<void()> onApplied)
    : result(std::move(prepared)), service(s), appliedCallback(std::move(onApplied))
{
    addAndMakeVisible(summary);
    addAndMakeVisible(selectedLabel);
    addAndMakeVisible(progress);
    addAndMakeVisible(tabs);
    addAndMakeVisible(apply);
    addAndMakeVisible(cancel);
    summary.setFont(juce::FontOptions(12.5f));
    summary.setColour(juce::Label::textColourId, theme::text);
    selectedLabel.setFont(juce::FontOptions(12.0f));
    selectedLabel.setColour(juce::Label::textColourId, theme::muted);
    progress.setFont(juce::FontOptions(12.0f));
    progress.setColour(juce::Label::textColourId, theme::muted);

    proposalModel = std::make_unique<ProposalModel>(*this);
    skipModel = std::make_unique<SkipModel>(*this);
    proposals.setModel(proposalModel.get());
    skips.setModel(skipModel.get());
    proposals.setComponentID("autoTagProposals");
    skips.setComponentID("autoTagSkips");
    apply.setComponentID("autoTagApply");
    cancel.setComponentID("autoTagCancel");
    proposals.setRowHeight(22);
    skips.setRowHeight(22);
    proposals.getHeader().addColumn("Use", 1, 50, 40, 60);
    proposals.getHeader().addColumn("File", 2, 210, 120, 400);
    proposals.getHeader().addColumn("Folder", 3, 280, 120, 700);
    proposals.getHeader().addColumn("Tag", 4, 90, 70, 160);
    proposals.getHeader().addColumn("Reason", 5, 300, 150, 900);
    proposals.getHeader().addColumn("State", 6, 110, 90, 180);
    skips.getHeader().addColumn("File", 1, 230, 120, 400);
    skips.getHeader().addColumn("Reason code", 2, 150, 90, 240);
    skips.getHeader().addColumn("Explanation", 3, 430, 150, 900);
    tabs.addTab("Proposals (" + juce::String(result.proposals.size()) + ")", theme::panel,
                &proposals, false);
    tabs.addTab("Skipped (" + juce::String(result.skippedCount) + ")", theme::panel,
                &skips, false);

    int ambiguous = 0;
    for (const auto& skipped : result.skipped)
        if (skipped.code == "ambiguous-types" || skipped.code == "conflicting-types") ++ambiguous;
    summary.setText((result.scope.isNotEmpty() ? "Scope: " + result.scope + "   |   " : juce::String())
        + "Checked " + juce::String(result.checked)
        + "   |   proposals " + juce::String(result.proposals.size())
        + " (new " + juce::String(result.newAssignments)
        + ", already assigned " + juce::String(result.alreadyAssigned) + ")"
        + "   |   skipped " + juce::String(result.skippedCount)
        + " (ambiguous/conflict " + juce::String(ambiguous) + ")"
        + "   |   no evidence " + juce::String(result.noEvidence)
        + "   |   errors " + juce::String(result.errors),
        juce::dontSendNotification);
    if (result.truncated) summary.setTooltip("The proposal limit was reached; narrow the folder.");
    apply.onClick = [this] { beginApply(); };
    cancel.onClick = [this]
    {
        if (applying) service.cancel();
        else closeDialog();
    };
    updateSelectionLabel();
    setSize(980, 620);
    startTimerHz(8);
}
AutoTagDialog::~AutoTagDialog()
{
    stopTimer();
    if (applying) service.cancel();
}
void AutoTagDialog::paint(juce::Graphics& g)
{
    g.fillAll(theme::background);
}
void AutoTagDialog::resized()
{
    auto area = getLocalBounds().reduced(14);
    summary.setBounds(area.removeFromTop(22));
    selectedLabel.setBounds(area.removeFromTop(20));
    auto buttons = area.removeFromBottom(34).withTrimmedTop(8);
    cancel.setBounds(buttons.removeFromRight(110));
    buttons.removeFromRight(8);
    apply.setBounds(buttons.removeFromRight(150));
    progress.setBounds(buttons);
    area.removeFromBottom(8);
    tabs.setBounds(area);
}
void AutoTagDialog::toggleRow(int row)
{
    if (applying || row < 0 || row >= (int) result.proposals.size()) return;
    auto& proposal = result.proposals[(size_t) row];
    if (proposal.alreadyAssigned) return;
    proposal.included = ! proposal.included;
    proposals.repaintRow(row);
    updateSelectionLabel();
}
void AutoTagDialog::updateSelectionLabel()
{
    int selected = 0;
    for (const auto& proposal : result.proposals)
        if (proposal.included && ! proposal.alreadyAssigned) ++selected;
    selectedLabel.setText("Selected for apply: " + juce::String(selected)
        + " of " + juce::String(result.newAssignments) + " new assignment(s)",
        juce::dontSendNotification);
    apply.setEnabled(! applying && selected > 0);
}
void AutoTagDialog::beginApply()
{
    if (applying) return;
    std::vector<AutoTagProposal> chosen;
    for (const auto& proposal : result.proposals)
        if (proposal.included && ! proposal.alreadyAssigned) chosen.push_back(proposal);
    if (chosen.empty()) return;
    if (! service.startApply(std::move(chosen)))
    {
        progress.setText("AutoTag application could not start.", juce::dontSendNotification);
        return;
    }
    applying = true;
    finishReported = false;
    apply.setEnabled(false);
    cancel.setButtonText("Cancel apply");
    progress.setText("Applying selected proposals...", juce::dontSendNotification);
}
void AutoTagDialog::finishApply()
{
    if (finishReported) return;
    finishReported = true;
    applying = false;
    const auto state = service.progress();
    const auto outcome = service.applyResult();
    progress.setText(state.stage == AutoTagStage::Failed
        ? state.message
        : outcome.message + "   rejected " + juce::String(outcome.rejected),
        juce::dontSendNotification);
    cancel.setButtonText("Close");
    apply.setEnabled(false);
    if (appliedCallback) appliedCallback();
    repaint();
}
void AutoTagDialog::timerCallback()
{
    if (! applying) return;
    const auto state = service.progress();
    if (state.running)
    {
        progress.setText("Applying  " + juce::String(state.processed) + "/" + juce::String(state.total)
            + "   applied " + juce::String(state.applied)
            + "   rejected " + juce::String(state.errors), juce::dontSendNotification);
    }
    else finishApply();
}
void AutoTagDialog::closeDialog()
{
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState(0);
}
} // namespace sonicator
