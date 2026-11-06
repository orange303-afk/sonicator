#include "RelocateFoldersPanel.h"
#include "core/database/PathRelocation.h"
#include "ui/shell/Theme.h"
#include <array>

namespace sonicator {
namespace {
juce::Colour errorColour()
{
    return theme::isDark ? juce::Colour(0xffff8b8b) : juce::Colour(0xffb3261e);
}
} // namespace

struct RelocateFoldersPanel::Row final : juce::Component
{
    explicit Row(const RelocateRootInfo& info) : oldRoot(info.path)
    {
        addAndMakeVisible(oldPath);
        addAndMakeVisible(count);
        addAndMakeVisible(chosen);
        addAndMakeVisible(probe);
        addAndMakeVisible(choose);

        oldPath.setText(info.path, juce::dontSendNotification);
        oldPath.setTooltip(info.path);
        oldPath.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        oldPath.setMinimumHorizontalScale(0.5f);
        count.setText(juce::String(info.fileCount) + " file(s) in the catalog", juce::dontSendNotification);
        count.setFont(juce::FontOptions(11.5f));
        chosen.setFont(juce::FontOptions(12.0f));
        chosen.setMinimumHorizontalScale(0.5f);
        chosen.setText("Not chosen", juce::dontSendNotification);
        probe.setFont(juce::FontOptions(11.5f));
        probe.setJustificationType(juce::Justification::centredRight);
        probe.setMinimumHorizontalScale(0.6f);
        choose.setWantsKeyboardFocus(false);
        choose.setMouseClickGrabsKeyboardFocus(false);
        choose.onClick = [this] { if (onChoose) onChoose(); };
        lookAndFeelChanged();
    }

    void paint(juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(theme::table);
        g.fillRoundedRectangle(area, 8.0f);
        g.setColour(target.isNotEmpty() ? theme::selection : theme::line);
        g.drawRoundedRectangle(area, 8.0f, 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(12, 8);
        auto right = area.removeFromRight(104);
        choose.setBounds(right.withSizeKeepingCentre(104, 28));
        oldPath.setBounds(area.removeFromTop(20));
        count.setBounds(area.removeFromTop(16).withTrimmedTop(1));
        chosen.setBounds(area.removeFromTop(18).withTrimmedTop(2));
        probe.setBounds(area.removeFromTop(16).withTrimmedTop(1));
    }

    void lookAndFeelChanged() override
    {
        oldPath.setColour(juce::Label::textColourId, theme::text);
        count.setColour(juce::Label::textColourId, theme::muted);
        chosen.setColour(juce::Label::textColourId, target.isNotEmpty() ? theme::text : theme::muted);
        refreshProbe();
    }

    void setTarget(const juce::String& path)
    {
        target = path;
        chosen.setText(target, juce::dontSendNotification);
        chosen.setTooltip(target);
        chosen.setColour(juce::Label::textColourId, theme::text);
        repaint();
    }

    void setProbe(RelocationProbe value)
    {
        lastProbe = value;
        refreshProbe();
    }

    void refreshProbe()
    {
        if (lastProbe.sampled <= 0)
        {
            probe.setText({}, juce::dontSendNotification);
            probe.setColour(juce::Label::textColourId, theme::muted);
            return;
        }
        probe.setText(juce::String(lastProbe.found) + " of " + juce::String(lastProbe.sampled)
                          + " sampled files found",
                      juce::dontSendNotification);
        const bool weak = lastProbe.found * 10 < lastProbe.sampled * 9;
        probe.setColour(juce::Label::textColourId, weak ? errorColour() : theme::muted);
    }

    juce::String oldRoot, target;
    RelocationProbe lastProbe;
    juce::Label oldPath, count, chosen, probe;
    juce::TextButton choose { "Choose..." };
    std::function<void()> onChoose;
};

RelocateFoldersPanel::RelocateFoldersPanel(std::vector<RelocateRootInfo> roots)
{
    setSize(680, 470);
    setComponentID("relocatePanel");
    for (auto* c : std::array<juce::Component*, 7>{ &title, &explanation, &errorLabel, &viewport,
                                                    &apply, &cancel, &content })
        addAndMakeVisible(c);

    title.setText("Relocate sample folders", juce::dontSendNotification);
    title.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    explanation.setText(
        "These catalog folders were not found on this computer. Choose where each one lives now. "
        "Only stored paths are rewritten; analysis, tags, stars and hashes are kept.\n\n"
        "Each new location must keep the same folder structure inside it.",
        juce::dontSendNotification);
    explanation.setFont(juce::FontOptions(12.0f));
    explanation.setJustificationType(juce::Justification::topLeft);
    explanation.setMinimumHorizontalScale(0.8f);
    errorLabel.setFont(juce::FontOptions(12.0f));
    errorLabel.setJustificationType(juce::Justification::centredLeft);
    errorLabel.setMinimumHorizontalScale(0.7f);

    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);

    for (auto* button : { &apply, &cancel })
    {
        button->setWantsKeyboardFocus(false);
        button->setMouseClickGrabsKeyboardFocus(false);
    }
    apply.setComponentID("relocateApply");

    apply.onClick = [this] { requestApply(); };
    cancel.onClick = [this]
    {
        if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
            dialog->exitModalState(0);
    };

    size_t index = 0;
    for (const auto& info : roots)
    {
        auto row = std::make_unique<Row>(info);
        row->onChoose = [this, index] { chooseTarget(index); };
        content.addAndMakeVisible(*row);
        rows.push_back(std::move(row));
        ++index;
    }
    updateApplyState();
    lookAndFeelChanged();
}

RelocateFoldersPanel::~RelocateFoldersPanel()
{
    viewport.setViewedComponent(nullptr, false);
}

void RelocateFoldersPanel::paint(juce::Graphics& g)
{
    g.fillAll(theme::panel);
}

void RelocateFoldersPanel::resized()
{
    auto area = getLocalBounds().reduced(24, 18);
    title.setBounds(area.removeFromTop(26));
    area.removeFromTop(4);
    explanation.setBounds(area.removeFromTop(50));
    area.removeFromTop(10);

    auto footer = area.removeFromBottom(34);
    area.removeFromBottom(6);
    errorLabel.setBounds(area.removeFromBottom(22));
    area.removeFromBottom(6);
    viewport.setBounds(area);

    apply.setBounds(footer.removeFromRight(112));
    footer.removeFromRight(8);
    cancel.setBounds(footer.removeFromRight(90));

    constexpr int rowHeight = 86, gap = 8;
    content.setSize(juce::jmax(1, area.getWidth() - 12),
                    (int) rows.size() * rowHeight + juce::jmax(0, (int) rows.size() - 1) * gap);
    int y = 0;
    for (auto& row : rows)
    {
        row->setBounds(0, y, content.getWidth(), rowHeight);
        y += rowHeight + gap;
    }
}

void RelocateFoldersPanel::lookAndFeelChanged()
{
    title.setColour(juce::Label::textColourId, theme::text);
    explanation.setColour(juce::Label::textColourId, theme::muted);
    errorLabel.setColour(juce::Label::textColourId,
                         errorLabel.getText().isNotEmpty() ? errorColour() : theme::muted);
    for (auto& row : rows) row->lookAndFeelChanged();
    repaint();
}

void RelocateFoldersPanel::setError(const juce::String& text)
{
    errorLabel.setText(text, juce::dontSendNotification);
    errorLabel.setColour(juce::Label::textColourId, text.isNotEmpty() ? errorColour() : theme::muted);
}

bool RelocateFoldersPanel::setTarget(size_t index, const juce::File& folder)
{
    if (index >= rows.size() || ! folder.isDirectory()) return false;
    const auto path = folder.getFullPathName();
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (i == index || rows[i]->target.isEmpty()) continue;
        if (path == rows[i]->target
            || pathrelocation::isUnder(path, rows[i]->target)
            || pathrelocation::isUnder(rows[i]->target, path))
            return false;
    }
    rows[index]->setTarget(path);
    rows[index]->setProbe(onProbe ? onProbe(rows[index]->oldRoot, path) : RelocationProbe{});
    setError({});
    updateApplyState();
    return true;
}

bool RelocateFoldersPanel::canApply() const
{
    if (rows.empty()) return false;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i]->target.isEmpty() || ! juce::File(rows[i]->target).isDirectory()) return false;
        for (size_t j = i + 1; j < rows.size(); ++j)
        {
            const auto& a = rows[i]->target;
            const auto& b = rows[j]->target;
            if (a.isEmpty() || b.isEmpty()) return false;
            if (a == b || pathrelocation::isUnder(a, b) || pathrelocation::isUnder(b, a)) return false;
        }
    }
    return true;
}

std::vector<RootRelocation> RelocateFoldersPanel::relocations() const
{
    std::vector<RootRelocation> out;
    out.reserve(rows.size());
    for (const auto& row : rows) out.push_back({ row->oldRoot, row->target });
    return out;
}

void RelocateFoldersPanel::updateApplyState()
{
    apply.setEnabled(canApply());
}

bool RelocateFoldersPanel::needsConfirmation() const
{
    for (const auto& row : rows)
        if (row->lastProbe.sampled > 0 && row->lastProbe.found * 10 < row->lastProbe.sampled * 9)
            return true;
    return false;
}

void RelocateFoldersPanel::requestApply()
{
    if (! canApply() || ! onApply) return;
    if (! needsConfirmation()) { performApply(); return; }
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
        .withIconType(juce::MessageBoxIconType::WarningIcon)
        .withTitle("Relocate folders")
        .withMessage("Some chosen folders contain fewer than 90% of the sampled catalog files. "
                     "This usually means the wrong folder was chosen. Continue anyway?\n\n"
                     "A database backup is written before any path is changed.")
        .withButton("Relocate", 1).withButton("Cancel", 0),
        [safe = juce::Component::SafePointer<RelocateFoldersPanel>(this)](int choice)
        {
            if (safe != nullptr && choice == 1) safe->performApply();
        });
}

void RelocateFoldersPanel::performApply()
{
    if (! canApply() || ! onApply) return;
    setError({});
    apply.setEnabled(false);
    cancel.setEnabled(false);
    const auto error = onApply(relocations());
    if (error.isNotEmpty())
    {
        setError(error);
        cancel.setEnabled(true);
        updateApplyState();
        return;
    }
    if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
        dialog->exitModalState(0);
}

void RelocateFoldersPanel::chooseTarget(size_t index)
{
    if (index >= rows.size()) return;
    chooser = std::make_unique<juce::FileChooser>(
        "Choose the new location for " + rows[index]->oldRoot);
    chooser->launchAsync(juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectDirectories,
        [safe = juce::Component::SafePointer<RelocateFoldersPanel>(this), index]
        (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            const auto folder = fc.getResult();
            if (! folder.isDirectory()) return;
            if (! safe->setTarget(index, folder))
                safe->setError("A folder cannot be used twice or overlap another chosen folder.");
        });
}
} // namespace sonicator
