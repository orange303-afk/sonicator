#pragma once
#include "core/database/SampleRepository.h"
#include "core/files/DuplicateCleanup.h"
#include "ui/shell/DialogSurface.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>

namespace sonicator
{
// Review window for byte-identical duplicates. The kept original is chosen per group, redundant
// copies are checked explicitly, and the selected action applies to every checked copy.
class DuplicateCleanupDialog final : public juce::Component, private juce::Timer
{
public:
    using Group = SampleRepository::DuplicateGroup;
    explicit DuplicateCleanupDialog(std::vector<Group> groups, juce::String scope = {});
    ~DuplicateCleanupDialog() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    std::function<void()> onCancel;
    // Called when a cleanup really starts, so the owner can stop preview playback first.
    std::function<void()> onRun;
    // Clicking a copy asks the owner to audition it; closing the dialog or changing the group
    // asks the owner to stop and restore what was playing before.
    std::function<void(const juce::File&)> onAudition;
    std::function<void()> onStopAudition;
    // Called once when the dialog has something to report: a running job ended or nothing was
    // selected. The owner updates the catalog and the status line.
    std::function<void(const juce::StringArray& removedPaths,
                       const juce::StringArray& linkedPaths,
                       const juce::String& summary)> onFinished;

    bool isRunning() const { return running; }
    bool isFinished() const { return finished; }
    // The window close button and Escape stop a running job first; closing again leaves.
    void requestStop() { if (running) service.cancel(); }
    // Starts the job the dialog built. Returns false when nothing was selected.
    bool beginCleanup();
    void setJournalForTest(juce::File file) { journalOverride = std::move(file); }
    // Test hooks: the review state that the buttons would submit, and the audition path a click
    // would take.
    int selectedCopyCount() const;
    int64_t selectedCopyBytes() const;
    files::DuplicateAction selectedAction() const;
    void selectRowForTest(int row) { audition(row); }
    int auditionedRow() const { return auditionRow; }

private:
    struct Member
    {
        int64_t id = 0;
        juce::String path;
        int64_t size = 0;
        bool missing = false, alias = false, remove = false;
    };
    struct Row
    {
        juce::String hash;
        int64_t size = 0;
        std::vector<Member> members;
        int keep = 0;
    };

    class MemberRow;

    class GroupModel final : public juce::ListBoxModel
    {
    public:
        explicit GroupModel(DuplicateCleanupDialog& o) : owner(o) {}
        int getNumRows() override;
        void paintListBoxItem(int row, juce::Graphics&, int width, int height, bool) override;
        void selectedRowsChanged(int row) override;
    private:
        DuplicateCleanupDialog& owner;
    };
    class MemberModel final : public juce::ListBoxModel
    {
    public:
        explicit MemberModel(DuplicateCleanupDialog& o) : owner(o) {}
        int getNumRows() override;
        void paintListBoxItem(int, juce::Graphics&, int, int, bool) override {}
        juce::Component* refreshComponentForRow(int row, bool selected, juce::Component*) override;
        void selectedRowsChanged(int) override {}
    private:
        DuplicateCleanupDialog& owner;
    };

    const std::vector<Member>& currentMembers() const;

    void timerCallback() override;
    void selectGroup(int index);
    void setKeep(int memberIndex);
    void setRemove(int memberIndex, bool remove);
    void updateSummary();
    void chooseAndRun();
    void audition(int row);
    juce::String summaryText() const;

    dialogs::Surface surface;
    std::vector<Row> rows;
    int current = 0;
    int auditionRow = -1;
    bool running = false, finished = false;
    juce::String lastSummary;
    files::DuplicateCleanupService service;
    juce::File journalOverride;

    juce::Label title, intro, groupHeading, memberHeading, actionLabel, summary, progressLabel, error;
    juce::ComboBox actionBox;
    juce::TextButton clean { "Clean..." }, cancel { "Cancel" }, close { "Close" };
    double progressValue = 0.0;
    juce::ProgressBar progressBar { progressValue };

    GroupModel groupModel { *this };
    MemberModel memberModel { *this };
    juce::ListBox groupList { "Duplicate groups", &groupModel };
    juce::ListBox memberList { "Duplicate members", &memberModel };
};
} // namespace sonicator
