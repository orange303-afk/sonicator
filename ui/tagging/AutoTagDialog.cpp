#include "AutoTagDialog.h"
#include "ui/library/RoundTagMenu.h"
#include "ui/shell/AppFonts.h"
#include "ui/shell/Theme.h"
#include <algorithm>
#include <limits>

namespace sonicator
{
namespace
{
constexpr int pillHeight = 18;
constexpr int pillGap = 4;
constexpr int pillPadding = 7;
constexpr int plusWidth = 20;
constexpr size_t plusMark = std::numeric_limits<size_t>::max();
}

class AutoTagDialog::ProposalModel final : public juce::TableListBoxModel
{
public:
    explicit ProposalModel(AutoTagDialog& o) : owner(o) {}
    int getNumRows() override { return (int) owner.rows.size(); }
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
        const auto& group = owner.rows[(size_t) row];
        g.setFont(fonts::font(12.0f));
        g.setColour(theme::text);
        auto area = juce::Rectangle<int>(0, 0, width, height).reduced(6, 0);
        bool anyNew = false, anyIncludedNew = false;
        for (const auto index : group.proposals)
        {
            const auto& proposal = owner.result.proposals[index];
            if (proposal.alreadyAssigned) continue;
            anyNew = true;
            if (proposal.included) anyIncludedNew = true;
        }
        if (column == 1)
        {
            g.setColour(anyIncludedNew ? theme::accent : theme::muted);
            g.drawText(anyNew ? (anyIncludedNew ? "x" : " ") : "-", area,
                       juce::Justification::centredLeft);
            g.setColour(theme::line);
            g.drawRect(area.withSizeKeepingCentre(14, 14), 1);
        }
        else if (column == 2) g.drawText(group.fileName, area, juce::Justification::centredLeft, true);
        else if (column == 3)
        {
            g.setColour(theme::muted);
            g.drawText(group.folder, area, juce::Justification::centredLeft, true);
        }
        else if (column == 4)
        {
            std::vector<std::pair<juce::Rectangle<int>, size_t>> pills;
            owner.layoutPills(group, { 0, 0, width, height }, pills);
            g.setFont(fonts::font(11.0f));
            for (const auto& entry : pills)
            {
                const auto r = entry.first;
                const bool plus = entry.second == plusMark;
                const auto& proposal = plus ? owner.result.proposals.front()
                                            : owner.result.proposals[entry.second];
                const auto colour = plus ? theme::muted : owner.tagColour(proposal.tagId);
                const bool included = plus || proposal.included;
                g.setColour((plus ? theme::raised : colour).withAlpha(included ? 0.22f : 0.07f));
                g.fillRoundedRectangle(r.toFloat(), 9.0f);
                g.setColour((plus ? theme::line : colour).withAlpha(included ? 0.65f : 0.22f));
                g.drawRoundedRectangle(r.toFloat(), 9.0f, 1.0f);
                g.setColour(included ? theme::text : theme::muted.withAlpha(0.65f));
                g.drawText(plus ? "+" : proposal.canonicalType, r.reduced(pillPadding, 0),
                           juce::Justification::centred, false);
            }
        }
        else if (column == 5)
        {
            const AutoTagProposal* source = nullptr;
            for (const auto index : group.proposals)
                if (owner.result.proposals[index].included)
                {
                    source = &owner.result.proposals[index];
                    break;
                }
            if (source == nullptr && ! group.proposals.empty())
                source = &owner.result.proposals[group.proposals.front()];
            g.setColour(theme::muted);
            g.drawText(source != nullptr ? source->reason : juce::String(), area,
                       juce::Justification::centredLeft, true);
        }
        else if (column == 6)
        {
            g.setColour(anyNew ? theme::text : theme::muted);
            g.drawText(anyNew ? "New" : "Already assigned", area,
                       juce::Justification::centredLeft, true);
        }
    }
    void cellClicked(int row, int columnId, const juce::MouseEvent& event) override
    {
        if (columnId == 1) owner.toggleRow(row);
        else if (columnId == 4) owner.openTagMenuFromCell(row, event);
    }
    void selectedRowsChanged(int lastRowSelected) override
    {
        owner.previewProposal(lastRowSelected);
    }
    void sortOrderChanged(int newSortColumnId, bool isForwards) override
    {
        AutoTagDialog::sortProposals(owner.result.proposals, newSortColumnId, isForwards);
        owner.rebuildRows();
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
        g.setFont(fonts::font(12.0f));
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
                             std::function<void()> onApplied,
                             std::function<void(const juce::File&)> onPreview,
                             std::function<void()> onStopPreview,
                             std::vector<Tag> ranked, std::vector<Tag> all)
    : result(std::move(prepared)), service(s), appliedCallback(std::move(onApplied)),
      rankedTags(std::move(ranked)), allTags(std::move(all)),
      previewCallback(std::move(onPreview)), stopPreviewCallback(std::move(onStopPreview))
{
    for (const auto& tag : allTags) tagColours[tag.id] = tag.colour;
    addAndMakeVisible(summary);
    addAndMakeVisible(selectedLabel);
    addAndMakeVisible(progress);
    addAndMakeVisible(tabs);
    addAndMakeVisible(apply);
    addAndMakeVisible(cancel);
    summary.setFont(fonts::font(12.5f));
    summary.setColour(juce::Label::textColourId, theme::text);
    selectedLabel.setFont(fonts::font(12.0f));
    selectedLabel.setColour(juce::Label::textColourId, theme::muted);
    selectedLabel.setComponentID("autoTagSelectedLabel");
    progress.setFont(fonts::font(12.0f));
    progress.setColour(juce::Label::textColourId, theme::muted);

    roundMenu = std::make_unique<RoundTagMenu>();
    roundMenu->setComponentID("autoTagRoundMenu");
    addChildComponent(*roundMenu);
    roundMenu->setAlwaysOnTop(true);

    proposalModel = std::make_unique<ProposalModel>(*this);
    skipModel = std::make_unique<SkipModel>(*this);
    proposals.setModel(proposalModel.get());
    skips.setModel(skipModel.get());
    proposals.setComponentID("autoTagProposals");
    skips.setComponentID("autoTagSkips");
    apply.setComponentID("autoTagApply");
    cancel.setComponentID("autoTagCancel");
    proposals.setRowHeight(26);
    skips.setRowHeight(22);
    proposals.getHeader().addColumn("Use", 1, 44, 36, 60);
    proposals.getHeader().addColumn("File", 2, 170, 110, 400);
    proposals.getHeader().addColumn("Folder", 3, 170, 100, 700);
    proposals.getHeader().addColumn("Tags", 4, 260, 120, 700);
    proposals.getHeader().addColumn("Reason", 5, 190, 110, 900);
    proposals.getHeader().addColumn("State", 6, 96, 80, 180);
    skips.getHeader().addColumn("File", 1, 230, 120, 400);
    skips.getHeader().addColumn("Reason code", 2, 150, 90, 240);
    skips.getHeader().addColumn("Explanation", 3, 430, 150, 900);

    rebuildRows();
    tabs.addTab("Files (" + juce::String(rows.size()) + ")", theme::panel, &proposals, false);
    tabs.addTab("Skipped (" + juce::String(result.skippedCount) + ")", theme::panel, &skips, false);

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
    ++previewGeneration;
    if (stopPreviewCallback) stopPreviewCallback();
    if (applying) service.cancel();
}
void AutoTagDialog::paint(juce::Graphics& g)
{
    surface.paint(g, getLocalBounds());
}
void AutoTagDialog::resized()
{
    using dialogs::Surface;
    auto area = Surface::contentArea(getLocalBounds())
                    .withTrimmedBottom(studio::iconButtonSize + studio::spacingTight);
    summary.setBounds(area.removeFromTop(22));
    selectedLabel.setBounds(area.removeFromTop(20));
    tabs.setBounds(area);

    auto buttons = Surface::footerArea(getLocalBounds());
    cancel.setBounds(buttons.removeFromRight(110));
    buttons.removeFromRight(studio::spacingTight);
    apply.setBounds(buttons.removeFromRight(150));
    progress.setBounds(buttons);
    if (roundMenu != nullptr)
    {
        roundMenu->setBounds(getLocalBounds());
        roundMenu->close();
    }
}

juce::Colour AutoTagDialog::tagColour(int64_t tagId) const
{
    const auto found = tagColours.find(tagId);
    return found != tagColours.end() ? juce::Colour(found->second) : theme::muted;
}

bool AutoTagDialog::rowHasIncludedTag(int64_t sampleId, int64_t tagId) const
{
    const auto row = std::find_if(rows.begin(), rows.end(),
                                  [sampleId](const RowGroup& group) { return group.sampleId == sampleId; });
    if (row == rows.end()) return false;
    for (const auto index : row->proposals)
    {
        const auto& proposal = result.proposals[index];
        if (proposal.tagId == tagId && proposal.included) return true;
    }
    return false;
}

void AutoTagDialog::layoutPills(const RowGroup& row, juce::Rectangle<int> area,
                                std::vector<std::pair<juce::Rectangle<int>, size_t>>& out) const
{
    out.clear();
    const auto font = fonts::font(11.0f);
    const int y = area.getCentreY() - pillHeight / 2;
    int x = area.getX() + 2;
    for (const auto index : row.proposals)
    {
        const auto& proposal = result.proposals[index];
        const int width = juce::GlyphArrangement::getStringWidthInt(font, proposal.canonicalType)
                        + pillPadding * 2;
        if (x + width > area.getRight() - 2) break;
        out.push_back({ juce::Rectangle<int>(x, y, width, pillHeight), index });
        x += width + pillGap;
    }
    // Keep the + reachable: drop the last pills until it fits, rather than hiding the only way
    // to add a tag from this row.
    while (! out.empty() && x + plusWidth > area.getRight() - 2)
    {
        x -= out.back().first.getWidth() + pillGap;
        out.pop_back();
    }
    if (x + plusWidth <= area.getRight() - 2)
        out.push_back({ juce::Rectangle<int>(x, y, plusWidth, pillHeight), plusMark });
}

void AutoTagDialog::rebuildRows()
{
    rows.clear();
    for (size_t i = 0; i < result.proposals.size(); ++i)
    {
        const auto& proposal = result.proposals[i];
        auto row = std::find_if(rows.begin(), rows.end(), [&proposal](const RowGroup& group)
        {
            return group.sampleId == proposal.sampleId;
        });
        if (row == rows.end())
        {
            RowGroup group;
            group.sampleId = proposal.sampleId;
            group.fileName = proposal.fileName;
            group.filePath = proposal.filePath;
            group.folder = juce::File(proposal.filePath).getParentDirectory().getFullPathName();
            rows.push_back(std::move(group));
            row = rows.end() - 1;
        }
        row->proposals.push_back(i);
    }
    proposals.updateContent();
    updateSummary();
}

void AutoTagDialog::updateSummary()
{
    int included = 0, includedNew = 0, includedAlready = 0;
    for (const auto& proposal : result.proposals)
    {
        if (! proposal.included) continue;
        ++included;
        if (proposal.alreadyAssigned) ++includedAlready;
        else ++includedNew;
    }
    int ambiguous = 0;
    for (const auto& skipped : result.skipped)
        if (skipped.code == "ambiguous-types" || skipped.code == "conflicting-types") ++ambiguous;
    summary.setText((result.scope.isNotEmpty() ? "Scope: " + result.scope + "   |   " : juce::String())
        + "Checked " + juce::String(result.checked)
        + "   |   files " + juce::String(rows.size())
        + "   |   tags " + juce::String(included)
        + " (new " + juce::String(includedNew)
        + ", already assigned " + juce::String(includedAlready) + ")"
        + "   |   skipped " + juce::String(result.skippedCount)
        + " (ambiguous/conflict " + juce::String(ambiguous) + ")"
        + "   |   no evidence " + juce::String(result.noEvidence)
        + "   |   errors " + juce::String(result.errors),
        juce::dontSendNotification);
    if (result.truncated) summary.setTooltip("The proposal limit was reached; narrow the folder.");
}

void AutoTagDialog::toggleRow(int row)
{
    if (applying || row < 0 || row >= (int) rows.size()) return;
    const auto& group = rows[(size_t) row];
    bool anyIncluded = false, canInclude = false;
    for (const auto index : group.proposals)
    {
        const auto& proposal = result.proposals[index];
        if (proposal.alreadyAssigned) continue;
        canInclude = true;
        if (proposal.included) anyIncluded = true;
    }
    if (! canInclude) return;
    for (const auto index : group.proposals)
    {
        auto& proposal = result.proposals[index];
        if (! proposal.alreadyAssigned) proposal.included = ! anyIncluded;
    }
    proposals.updateContent();
    updateSelectionLabel();
    updateSummary();
    proposals.repaint();
}

void AutoTagDialog::toggleTagForSample(int64_t sampleId, int64_t tagId)
{
    if (applying || tagId <= 0) return;
    auto row = std::find_if(rows.begin(), rows.end(),
                            [sampleId](const RowGroup& group) { return group.sampleId == sampleId; });
    if (row == rows.end()) return;
    for (const auto index : row->proposals)
    {
        auto& proposal = result.proposals[index];
        if (proposal.tagId != tagId) continue;
        proposal.included = ! proposal.included;
        proposals.updateContent();
        updateSelectionLabel();
        updateSummary();
        proposals.repaint();
        return;
    }
    const auto tag = std::find_if(allTags.begin(), allTags.end(),
                                  [tagId](const Tag& candidate) { return candidate.id == tagId; });
    if (tag == allTags.end()) return;
    const juce::File file(row->filePath);
    if (! file.existsAsFile()) return;
    AutoTagProposal added;
    added.sampleId = sampleId;
    added.filePath = row->filePath;
    added.fileName = row->fileName;
    added.canonicalType = tag->name;
    added.tagId = tag->id;
    added.rule = AutoTagRule::Review;
    added.reason = "added in the review";
    added.source = "review";
    added.included = true;
    added.fileSize = file.getSize();
    added.fileModified = file.getLastModificationTime().toMilliseconds();
    result.proposals.push_back(std::move(added));
    rebuildRows();
    updateSelectionLabel();
    proposals.repaint();
}

void AutoTagDialog::openTagMenuForRow(int row, juce::Point<int> anchor)
{
    if (row < 0 || row >= (int) rows.size() || roundMenu == nullptr) return;
    const auto sampleId = rows[(size_t) row].sampleId;
    const auto fileName = rows[(size_t) row].fileName;
    std::vector<RoundTagItem> inner, outer;
    for (size_t i = 0; i < rankedTags.size(); ++i)
    {
        const auto& tag = rankedTags[i];
        RoundTagItem item;
        item.tagId = tag.id;
        item.label = tag.name;
        item.colour = juce::Colour(tag.colour);
        item.assigned = rowHasIncludedTag(sampleId, tag.id);
        if (i < 8) inner.push_back(item);
        else if (outer.size() < 7) outer.push_back(item);
        else break;
    }
    RoundTagItem all;
    all.action = true;
    all.label = "All tags" + juce::String::charToString((juce::juce_wchar) 0x2026);
    all.colour = theme::muted;
    outer.push_back(all);
    lastMenuAnchorScreen = getScreenPosition() + anchor;
    roundMenu->onTagPicked = [this, sampleId](int64_t tagId)
    {
        toggleTagForSample(sampleId, tagId);
    };
    roundMenu->onAllTagsPicked = [this, sampleId]
    {
        showAllTagsForSample(sampleId, lastMenuAnchorScreen);
    };
    auto geometry = layoutRoundTagMenu(anchor, getLocalBounds(), inner, outer,
                                       fonts::font(10.5f, fonts::Weight::medium));
    roundMenu->open(std::move(geometry), fileName, "TAGS");
}

void AutoTagDialog::openTagMenuFromCell(int row, const juce::MouseEvent& event)
{
    if (row < 0 || row >= (int) rows.size()) return;
    const auto& header = proposals.getHeader();
    int columnIndex = 0;
    for (int i = 0; i < header.getNumColumns(true); ++i)
        if (header.getColumnIdOfIndex(i, true) == 4) { columnIndex = i; break; }
    const auto columnX = header.getColumnPosition(columnIndex).getX();
    const auto columnWidth = header.getColumnWidth(4);
    // cellClicked delivers coordinates relative to the row component, so the cell starts at y=0.
    const juce::Rectangle<int> cell(columnX, 0, columnWidth, proposals.getRowHeight());
    std::vector<std::pair<juce::Rectangle<int>, size_t>> pills;
    layoutPills(rows[(size_t) row], cell, pills);
    for (const auto& entry : pills)
    {
        if (! entry.first.contains(event.getPosition())) continue;
        if (entry.second != plusMark && event.mods.isAltDown())
        {
            auto& proposal = result.proposals[entry.second];
            proposal.included = ! proposal.included;
            proposals.updateContent();
            updateSelectionLabel();
            updateSummary();
            proposals.repaint();
            return;
        }
        openTagMenuForRow(row, getLocalPoint(nullptr, event.getScreenPosition()));
        return;
    }
}

void AutoTagDialog::showAllTagsForSample(int64_t sampleId, juce::Point<int> screenPosition)
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&getLookAndFeel());
    std::vector<int64_t> ids;
    for (const auto& tag : allTags)
    {
        menu.addItem((int) ids.size() + 1, tag.name, true, rowHasIncludedTag(sampleId, tag.id));
        ids.push_back(tag.id);
    }
    if (allTags.empty()) menu.addSectionHeader("No tags yet");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                           { screenPosition.x, screenPosition.y, 1, 1 })
                           .withMaximumNumColumns(1),
        [safe = juce::Component::SafePointer<AutoTagDialog>(this), sampleId, ids](int result)
        {
            if (safe == nullptr || result <= 0 || (size_t) (result - 1) >= ids.size()) return;
            safe->toggleTagForSample(sampleId, ids[(size_t) (result - 1)]);
        });
}

void AutoTagDialog::updateSelectionLabel()
{
    int proposed = 0, selected = 0;
    for (const auto& proposal : result.proposals)
    {
        if (proposal.alreadyAssigned) continue;
        ++proposed;
        if (proposal.included) ++selected;
    }
    selectedLabel.setText("Selected for apply: " + juce::String(selected)
        + " of " + juce::String(proposed) + " proposed new assignment(s)",
        juce::dontSendNotification);
    apply.setEnabled(! applying && selected > 0);
}

void AutoTagDialog::previewProposal(int row)
{
    if (row < 0 || row >= (int) rows.size() || ! previewCallback) return;
    const auto file = juce::File(rows[(size_t) row].filePath);
    const int generation = ++previewGeneration;
    juce::Component::SafePointer<AutoTagDialog> safe(this);
    juce::Timer::callAfterDelay(180, [safe, generation, file]
    {
        if (safe == nullptr || safe->previewGeneration != generation) return;
        if (file.existsAsFile() && safe->previewCallback) safe->previewCallback(file);
    });
}

void AutoTagDialog::beginApply()
{
    if (applying) return;
    ++previewGeneration;
    if (stopPreviewCallback) stopPreviewCallback();
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
    if (roundMenu != nullptr) roundMenu->close();
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
    if (roundMenu != nullptr) roundMenu->close();
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState(0);
}
void AutoTagDialog::sortProposals(std::vector<AutoTagProposal>& proposals, int columnId,
                                  bool forwards)
{
    const auto less = [columnId](const AutoTagProposal& a, const AutoTagProposal& b)
    {
        switch (columnId)
        {
            case 2: return a.fileName.compareIgnoreCase(b.fileName) < 0;
            case 3: return juce::File(a.filePath).getParentDirectory().getFullPathName()
                               .compareIgnoreCase(
                                   juce::File(b.filePath).getParentDirectory().getFullPathName()) < 0;
            case 4: return a.canonicalType.compareIgnoreCase(b.canonicalType) < 0;
            case 6: return (int) a.alreadyAssigned < (int) b.alreadyAssigned;
            case 1: return (int) a.included < (int) b.included;
            default: return a.sampleId < b.sampleId;
        }
    };
    std::stable_sort(proposals.begin(), proposals.end(),
                     [&less, forwards](const AutoTagProposal& a, const AutoTagProposal& b)
                     {
                         return forwards ? less(a, b) : less(b, a);
                     });
}
} // namespace sonicator
