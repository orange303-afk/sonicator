#include "DuplicateCleanupDialog.h"
#include "ui/shell/AppFonts.h"
#include "ui/shell/Theme.h"
#include "ui/shell/ThemedDialogs.h"
#include <algorithm>

namespace sonicator
{
namespace
{
juce::String formatBytes(juce::int64 bytes)
{
    if (bytes >= 1024ll * 1024 * 1024)
        return juce::String((double) bytes / (1024.0 * 1024.0 * 1024.0), 2) + " GB";
    if (bytes >= 1024 * 1024)
        return juce::String((double) bytes / (1024.0 * 1024.0), 1) + " MB";
    if (bytes >= 1024) return juce::String((double) bytes / 1024.0, 0) + " KB";
    return juce::String(bytes) + " B";
}
}

class DuplicateCleanupDialog::MemberRow final : public juce::Component,
                                                public juce::SettableTooltipClient
{
public:
    MemberRow()
    {
        for (auto* component : { (juce::Component*) &keep, (juce::Component*) &remove,
                                 (juce::Component*) &path })
            addAndMakeVisible(component);
        keep.setButtonText("Keep");
        remove.setButtonText("Remove");
        keep.setComponentID("duplicateKeep");
        remove.setComponentID("duplicateRemove");
        path.setFont(fonts::font(12));
        path.setColour(juce::Label::textColourId, theme::text);
        path.setMinimumHorizontalScale(1.0f);
        keep.onClick = [this] { if (onKeep) onKeep(); };
        remove.onClick = [this] { if (onRemove) onRemove(remove.getToggleState()); };
        path.setInterceptsMouseClicks(false, false);
        setTooltip("Click to audition this copy");
    }
    void set(int rowIndex, const juce::String& pathText, bool isKeep, bool isRemove, bool canEdit,
             bool playing)
    {
        row = rowIndex;
        path.setText(pathText, juce::dontSendNotification);
        keep.setToggleState(isKeep, juce::dontSendNotification);
        remove.setToggleState(isRemove, juce::dontSendNotification);
        keep.setEnabled(canEdit);
        remove.setEnabled(canEdit && ! isKeep);
        auditioning = playing;
        path.setColour(juce::Label::textColourId, playing ? theme::accent : theme::text);
        repaint();
    }
    void paint(juce::Graphics& g) override
    {
        if (! auditioning) return;
        // A small accent stripe marks the copy that is sounding; the toggles keep their own state.
        g.setColour(theme::accent);
        g.fillRect(0, 3, 3, getHeight() - 6);
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.mods.isLeftButtonDown() && onSelect) onSelect();
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced(2, 0);
        keep.setBounds(area.removeFromLeft(60));
        remove.setBounds(area.removeFromLeft(84));
        path.setBounds(area.reduced(6, 0));
    }
    int row = 0;
    bool auditioning = false;
    juce::ToggleButton keep, remove;
    juce::Label path;
    std::function<void()> onKeep;
    std::function<void(bool)> onRemove;
    std::function<void()> onSelect;
};

DuplicateCleanupDialog::DuplicateCleanupDialog(std::vector<Group> groups, juce::String scope)
{
    for (auto& group : groups)
    {
        Row row;
        row.hash = group.hash;
        row.size = group.size;
        for (const auto& member : group.members)
        {
            const juce::File file(member.path);
            row.members.push_back({ member.id, member.path, member.size,
                                    member.missing || ! file.existsAsFile(),
                                    file.isSymbolicLink(), false });
        }
        if (row.members.size() < 2) continue;
        row.keep = 0;
        for (size_t i = 0; i < row.members.size(); ++i)
            if (! row.members[i].missing && ! row.members[i].alias) { row.keep = (int) i; break; }
        for (size_t i = 0; i < row.members.size(); ++i)
            row.members[i].remove = (int) i != row.keep && ! row.members[i].missing
                                 && ! row.members[i].alias;
        rows.push_back(std::move(row));
    }

    title.setText("Duplicate files", juce::dontSendNotification);
    title.setFont(fonts::font(16, fonts::Weight::bold));
    title.setColour(juce::Label::textColourId, theme::text);
    groupHeading.setText("GROUPS", juce::dontSendNotification);
    groupHeading.setFont(fonts::font(11, fonts::Weight::semiBold));
    groupHeading.setColour(juce::Label::textColourId, theme::muted);
    memberHeading.setText("COPIES", juce::dontSendNotification);
    memberHeading.setFont(fonts::font(11, fonts::Weight::semiBold));
    memberHeading.setColour(juce::Label::textColourId, theme::muted);
    actionLabel.setText("Redundant copies:", juce::dontSendNotification);
    actionLabel.setFont(fonts::font(12));
    actionLabel.setColour(juce::Label::textColourId, theme::muted);
    intro.setFont(fonts::font(12));
    intro.setColour(juce::Label::textColourId, theme::muted);
    summary.setFont(fonts::font(12));
    summary.setColour(juce::Label::textColourId, theme::text);
    error.setFont(fonts::font(12));
    error.setColour(juce::Label::textColourId, theme::danger);
    progressLabel.setFont(fonts::font(12));
    progressLabel.setColour(juce::Label::textColourId, theme::muted);
    progressLabel.setVisible(false);
    progressBar.setVisible(false);
    intro.setText(rows.empty()
        ? "No byte-identical duplicates were found" + (scope.isEmpty() ? juce::String(" in this catalog.")
                                                                      : " in " + scope) + "."
        : juce::String((int) rows.size()) + " group(s) of byte-identical files"
          + (scope.isEmpty() ? juce::String() : " in " + scope) + ". "
          "Click a copy to audition it. Pick the copy to keep in each group, check the copies to "
          "clean up, then choose an action.",
        juce::dontSendNotification);

    actionBox.addItem("Move to Trash", 1);
    actionBox.addItem("Delete permanently", 2);
    actionBox.addItem("Replace with links", 3);
    actionBox.setSelectedId(1, juce::dontSendNotification);
    actionBox.setComponentID("duplicateAction");
    actionBox.onChange = [this] { updateSummary(); };

    clean.setComponentID("duplicateCleanButton");
    cancel.setComponentID("duplicateCancelButton");
    close.setComponentID("duplicateCloseButton");
    clean.onClick = [this] { chooseAndRun(); };
    cancel.onClick = [this]
    {
        if (running) { service.cancel(); return; }
        if (onCancel) onCancel();
    };
    close.onClick = [this]
    {
        if (running) { service.cancel(); return; }
        if (onCancel) onCancel();
    };
    close.setVisible(false);

    for (auto* component : { (juce::Component*) &title, (juce::Component*) &intro,
                             (juce::Component*) &groupHeading, (juce::Component*) &memberHeading,
                             (juce::Component*) &actionLabel, (juce::Component*) &summary,
                             (juce::Component*) &error, (juce::Component*) &progressLabel,
                             (juce::Component*) &progressBar, (juce::Component*) &groupList,
                             (juce::Component*) &memberList, (juce::Component*) &actionBox,
                             (juce::Component*) &clean, (juce::Component*) &cancel,
                             (juce::Component*) &close })
        addAndMakeVisible(component);

    groupList.setRowHeight(24);
    groupList.setComponentID("duplicateGroupList");
    memberList.setRowHeight(28);
    memberList.setComponentID("duplicateMemberList");
    if (! rows.empty()) groupList.selectRow(0);
    updateSummary();
    setSize(860, 560);
}

DuplicateCleanupDialog::~DuplicateCleanupDialog()
{
    stopTimer();
    if (auditionRow >= 0 && onStopAudition) onStopAudition();
    auditionRow = -1;
    service.cancel();
    service.waitUntilFinished(-1);
}

const std::vector<DuplicateCleanupDialog::Member>& DuplicateCleanupDialog::currentMembers() const
{
    static const std::vector<Member> empty;
    return current >= 0 && current < (int) rows.size() ? rows[(size_t) current].members : empty;
}

int DuplicateCleanupDialog::GroupModel::getNumRows() { return (int) owner.rows.size(); }

void DuplicateCleanupDialog::GroupModel::paintListBoxItem(int row, juce::Graphics& g,
                                                          int width, int height, bool selected)
{
    if (row < 0 || row >= (int) owner.rows.size()) return;
    if (selected) { g.setColour(theme::selection); g.fillRect(0, 0, width, height); }
    const auto& group = owner.rows[(size_t) row];
    const auto name = juce::File(group.members[(size_t) group.keep].path).getFileName();
    const auto text = juce::String((int) group.members.size()) + " copies  \u00b7  "
        + formatBytes(group.size) + "  \u00b7  " + name;
    g.setColour(selected ? theme::text : theme::muted);
    g.setFont(fonts::font(12));
    g.drawText(text, 8, 0, width - 12, height, juce::Justification::centredLeft, true);
}

void DuplicateCleanupDialog::GroupModel::selectedRowsChanged(int row)
{
    if (row >= 0) owner.selectGroup(row);
}

int DuplicateCleanupDialog::MemberModel::getNumRows()
{
    return (int) owner.currentMembers().size();
}

juce::Component* DuplicateCleanupDialog::MemberModel::refreshComponentForRow(
    int row, bool, juce::Component* existing)
{
    auto* component = dynamic_cast<MemberRow*>(existing);
    if (component == nullptr)
    {
        component = new MemberRow();
        component->onKeep = [this, component] { owner.setKeep(component->row); };
        component->onRemove = [this, component](bool remove)
        {
            owner.setRemove(component->row, remove);
        };
        component->onSelect = [this, component] { owner.audition(component->row); };
    }
    if (row < 0 || row >= (int) owner.currentMembers().size()) return component;
    const auto& member = owner.currentMembers()[(size_t) row];
    const auto& group = owner.rows[(size_t) owner.current];
    const bool canEdit = ! owner.running && ! member.missing && ! member.alias;
    component->set(row, member.path + (member.alias ? "  (already a link)"
                                    : member.missing ? "  (missing)" : ""),
                   row == group.keep, member.remove, canEdit, row == owner.auditionRow);
    return component;
}

void DuplicateCleanupDialog::selectGroup(int index)
{
    if (index < 0 || index >= (int) rows.size()) return;
    if (auditionRow >= 0)
    {
        auditionRow = -1;
        if (onStopAudition) onStopAudition();
    }
    current = index;
    memberList.updateContent();
    memberList.repaint();
    updateSummary();
}
void DuplicateCleanupDialog::audition(int row)
{
    const auto& members = currentMembers();
    if (row < 0 || row >= (int) members.size()) return;
    const auto& member = members[(size_t) row];
    if (member.missing) return;
    // A second click on the sounding copy stops it; another row switches the audition.
    if (row == auditionRow)
    {
        auditionRow = -1;
        memberList.updateContent();
        if (onStopAudition) onStopAudition();
        return;
    }
    auditionRow = row;
    memberList.updateContent();
    if (onAudition) onAudition(juce::File(member.path));
}

void DuplicateCleanupDialog::setKeep(int memberIndex)
{
    if (running || current < 0 || current >= (int) rows.size()) return;
    auto& row = rows[(size_t) current];
    if (memberIndex < 0 || memberIndex >= (int) row.members.size()) return;
    const auto& member = row.members[(size_t) memberIndex];
    if (member.missing || member.alias) return;
    row.keep = memberIndex;
    row.members[(size_t) memberIndex].remove = false;
    memberList.updateContent();
    updateSummary();
}

void DuplicateCleanupDialog::setRemove(int memberIndex, bool remove)
{
    if (running || current < 0 || current >= (int) rows.size()) return;
    auto& row = rows[(size_t) current];
    if (memberIndex < 0 || memberIndex >= (int) row.members.size()) return;
    auto& member = row.members[(size_t) memberIndex];
    if (member.missing || member.alias) member.remove = false;
    else member.remove = remove && memberIndex != row.keep;
    if (memberIndex == row.keep && remove) memberList.updateContent();
    updateSummary();
}

int DuplicateCleanupDialog::selectedCopyCount() const
{
    int count = 0;
    for (const auto& row : rows)
        for (size_t i = 0; i < row.members.size(); ++i)
            if (row.members[i].remove && (int) i != row.keep) ++count;
    return count;
}

int64_t DuplicateCleanupDialog::selectedCopyBytes() const
{
    int64_t bytes = 0;
    for (const auto& row : rows)
        for (size_t i = 0; i < row.members.size(); ++i)
            if (row.members[i].remove && (int) i != row.keep) bytes += row.members[i].size;
    return bytes;
}

files::DuplicateAction DuplicateCleanupDialog::selectedAction() const
{
    if (actionBox.getSelectedId() == 2) return files::DuplicateAction::DeletePermanently;
    if (actionBox.getSelectedId() == 3) return files::DuplicateAction::ReplaceWithLink;
    return files::DuplicateAction::Trash;
}

juce::String DuplicateCleanupDialog::summaryText() const
{
    if (rows.empty()) return {};
    const auto action = selectedAction();
    return juce::String(selectedCopyCount()) + " cop(ies), "
        + formatBytes(selectedCopyBytes()) + "  \u2014  "
        + (action == files::DuplicateAction::DeletePermanently ? "delete permanently"
           : action == files::DuplicateAction::ReplaceWithLink ? "replace with links"
                                                               : "move to Trash");
}

void DuplicateCleanupDialog::updateSummary()
{
    summary.setText(summaryText(), juce::dontSendNotification);
    clean.setEnabled(! rows.empty() && ! running && selectedCopyCount() > 0);
    error.setText(isFinished() ? lastSummary : juce::String(), juce::dontSendNotification);
    error.setFont(fonts::font(12));
    error.setColour(juce::Label::textColourId, theme::muted);
}

void DuplicateCleanupDialog::chooseAndRun()
{
    if (running || selectedCopyCount() == 0) return;
    const auto action = selectedAction();
    const auto count = juce::String(selectedCopyCount());
    const auto bytes = formatBytes(selectedCopyBytes());
    juce::String message;
    if (action == files::DuplicateAction::DeletePermanently)
        message = count + " redundant copy(ies), " + bytes
            + ", will be deleted permanently and cannot be recovered through Trash.";
    else if (action == files::DuplicateAction::ReplaceWithLink)
        message = count + " redundant copy(ies) will be replaced with symbolic links to the kept "
            "original. The old bytes are removed after the link is verified. Moving or deleting a "
            "kept original later breaks every link to it, and some software does not follow links.";
    else
        message = count + " redundant copy(ies), " + bytes + ", will be moved to Trash. "
            "The kept original is not touched.";
    message += "\n\nEvery copy is compared byte for byte with its kept original again before the "
               "action. Files that changed, moved or went missing are skipped.";
    dialogs::showAlert(*this, "Clean duplicate files?", message,
        { { action == files::DuplicateAction::DeletePermanently ? "Delete permanently"
               : action == files::DuplicateAction::ReplaceWithLink ? "Replace with links"
                                                                   : "Move to Trash", 1 },
          { "Cancel", 0 } },
        [safe = juce::Component::SafePointer<DuplicateCleanupDialog>(this)](int id)
        {
            if (safe != nullptr && id == 1) safe->beginCleanup();
        }, juce::MessageBoxIconType::WarningIcon);
}

bool DuplicateCleanupDialog::beginCleanup()
{
    if (running || finished) return false;
    std::vector<files::DuplicateCleanupJob> jobs;
    for (const auto& row : rows)
    {
        if (row.keep < 0 || row.keep >= (int) row.members.size()) continue;
        const auto original = juce::File(row.members[(size_t) row.keep].path);
        for (size_t i = 0; i < row.members.size(); ++i)
        {
            if (! row.members[i].remove || (int) i == row.keep) continue;
            jobs.push_back({ original, juce::File(row.members[i].path), selectedAction(), row.hash });
        }
    }
    if (jobs.empty()) return false;
    // Stop the audition before any selected copy can move to Trash under an open reader.
    if (auditionRow >= 0)
    {
        auditionRow = -1;
        memberList.updateContent();
        if (onStopAudition) onStopAudition();
    }
    if (onRun) onRun();
    if (! service.start(jobs, journalOverride))
    {
        error.setText("The cleanup could not start.", juce::dontSendNotification);
        error.setColour(juce::Label::textColourId, theme::danger);
        return false;
    }
    running = true;
    clean.setEnabled(false);
    cancel.setButtonText("Stop");
    actionBox.setEnabled(false);
    groupList.setEnabled(false);
    memberList.setEnabled(false);
    progressBar.setVisible(true);
    progressLabel.setVisible(true);
    progressLabel.setText("Comparing and cleaning...", juce::dontSendNotification);
    startTimer(200);
    resized();
    return true;
}

void DuplicateCleanupDialog::timerCallback()
{
    if (! running) return;
    const auto progress = service.progress();
    progressValue = progress.total > 0 ? (double) progress.processed / (double) progress.total : 0.0;
    progressBar.repaint();
    progressLabel.setText("Working: " + juce::String(progress.processed) + "/"
                              + juce::String(progress.total),
                          juce::dontSendNotification);
    if (progress.running) return;

    stopTimer();
    running = false;
    finished = true;
    auditionRow = -1;
    if (onStopAudition) onStopAudition();
    const auto results = service.results();
    juce::StringArray removed, linked, problems;
    juce::StringArray seenHashes;
    for (const auto& result : results)
    {
        if (result.state == files::DuplicateCleanupState::removed) removed.add(result.job.copy.getFullPathName());
        else if (result.state == files::DuplicateCleanupState::linked) linked.add(result.job.copy.getFullPathName());
        else if (result.state == files::DuplicateCleanupState::failed)
            problems.add(result.job.copy.getFileName() + ": " + result.message);
        else if (result.state == files::DuplicateCleanupState::skipped)
            problems.add(result.job.copy.getFileName() + ": " + result.message);
        if (result.job.hash.isNotEmpty()) seenHashes.addIfNotAlreadyThere(result.job.hash);
    }
    const auto progressState = service.progress();
    lastSummary = "Removed " + juce::String(progressState.removed)
        + ", linked " + juce::String(progressState.linked)
        + ", skipped " + juce::String(progressState.skipped)
        + ", failed " + juce::String(progressState.failed) + ".";
    if (progressState.freedBytes > 0)
        lastSummary += " Freed " + juce::File::descriptionOfSizeInBytes(progressState.freedBytes) + ".";
    if (progressState.trashedBytes > 0)
        lastSummary += " " + juce::File::descriptionOfSizeInBytes(progressState.trashedBytes)
            + " is in Trash; the space returns when it is emptied.";
    if (progressState.cancelled) lastSummary = "Stopped. " + lastSummary;
    error.setText(lastSummary, juce::dontSendNotification);
    error.setColour(juce::Label::textColourId, theme::text);
    progressLabel.setVisible(false);
    progressBar.setVisible(false);
    clean.setVisible(false);
    cancel.setVisible(false);
    close.setVisible(true);
    resized();
    if (onFinished) onFinished(removed, linked, lastSummary);
    if (! problems.isEmpty())
        error.setTooltip(problems.joinIntoString("\n"));
}

void DuplicateCleanupDialog::paint(juce::Graphics& g) { surface.paint(g, getLocalBounds()); }

void DuplicateCleanupDialog::resized()
{
    auto area = dialogs::Surface::contentArea(getLocalBounds());
    title.setBounds(area.removeFromTop(26));
    intro.setBounds(area.removeFromTop(38));
    area.removeFromTop(studio::spacingTight);

    auto footer = dialogs::Surface::footerArea(getLocalBounds());
    close.setBounds(footer.removeFromRight(110));
    footer.removeFromRight(8);
    cancel.setBounds(footer.removeFromRight(110));
    footer.removeFromRight(8);
    clean.setBounds(footer.removeFromRight(140));

    auto actionRow = area.removeFromBottom(studio::controlHeight);
    actionLabel.setBounds(actionRow.removeFromLeft(150));
    actionBox.setBounds(actionRow.removeFromLeft(220));
    actionRow.removeFromLeft(12);
    summary.setBounds(actionRow);
    area.removeFromBottom(studio::spacingTight);
    auto progressRow = area.removeFromBottom(24);
    progressLabel.setBounds(progressRow.removeFromLeft(260));
    progressBar.setBounds(progressRow);
    error.setBounds(area.removeFromBottom(22));

    auto lists = area;
    auto left = lists.removeFromLeft(320);
    groupHeading.setBounds(left.removeFromTop(18));
    left.removeFromTop(4);
    groupList.setBounds(left);
    lists.removeFromLeft(studio::spacingUnit);
    memberHeading.setBounds(lists.removeFromTop(18));
    lists.removeFromTop(4);
    memberList.setBounds(lists);
}
} // namespace sonicator
