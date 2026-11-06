#pragma once
#include "core/database/SampleRepository.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstddef>
#include <vector>

namespace sonicator {
struct RelocateRootInfo
{
    juce::String path;
    int fileCount = 0;
};
struct RelocationProbe
{
    int sampled = 0;
    int found = 0;
};
// Modal content for pointing catalog roots at their new locations. It touches no files and no
// database state itself; MainComponent supplies probing and the transactional apply callback.
class RelocateFoldersPanel final : public juce::Component
{
public:
    explicit RelocateFoldersPanel(std::vector<RelocateRootInfo>);
    ~RelocateFoldersPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;
    void setError(const juce::String&);
    bool setTarget(size_t index, const juce::File& folder);
    bool canApply() const;
    bool needsConfirmation() const;
    std::vector<RootRelocation> relocations() const;
    std::function<RelocationProbe(const juce::String& oldRoot, const juce::String& newRoot)> onProbe;
    std::function<juce::String(const std::vector<RootRelocation>&)> onApply;

private:
    struct Row;
    std::vector<std::unique_ptr<Row>> rows;
    juce::Viewport viewport;
    juce::Component content;
    juce::Label title, explanation, errorLabel;
    juce::TextButton apply { "Relocate" }, cancel { "Not now" };
    std::unique_ptr<juce::FileChooser> chooser;
    void chooseTarget(size_t index);
    void requestApply();
    void performApply();
    void updateApplyState();
};
} // namespace sonicator
