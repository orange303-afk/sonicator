#include "ExtractDialog.h"
#include "ui/shell/Theme.h"
#include <cmath>
namespace sonicator {
namespace {
juce::String formatEta(double seconds)
{
    if (! std::isfinite(seconds) || seconds < 0) return "estimating...";
    const int total = (int) std::llround(seconds);
    return juce::String(total / 60).paddedLeft('0', 2) + ":"
        + juce::String(total % 60).paddedLeft('0', 2);
}
}
ExtractDialog::ExtractDialog(std::vector<juce::File> inputSources, const AppSettings& settings,
                             std::function<bool(ExtractOptions)> onAccept)
    : sources(std::move(inputSources)), acceptCallback(std::move(onAccept))
{
    setSize(560, 460);
    title.setText("Extract samples from " + juce::String((int) sources.size()) + " file(s)",
                  juce::dontSendNotification);
    title.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    sameFolder.setComponentID("extractSameFolder");
    sameFolder.setToggleState(settings.extractSameFolder, juce::dontSendNotification);
    normalize.setComponentID("extractNormalize");
    normalize.setToggleState(settings.extractNormalize, juce::dontSendNotification);
    folder.setComponentID("extractFolder");
    folder.setMultiLine(false);
    folder.setText(settings.extractOutputFolder.isNotEmpty()
        ? settings.extractOutputFolder
        : (sources.empty() ? juce::String() : sources.front().getParentDirectory().getFullPathName()));
    error.setColour(juce::Label::textColourId, juce::Colour(0xffff8b8b));
    resultLabel.setColour(juce::Label::textColourId, theme::text);
    resultLabel.setJustificationType(juce::Justification::topLeft);
    progressLabel.setColour(juce::Label::textColourId, theme::muted);
    progressLabel.setJustificationType(juce::Justification::topLeft);
    progressBar.setPercentageDisplay(false);
    progressBar.setColour(juce::ProgressBar::backgroundColourId, theme::table);
    progressBar.setColour(juce::ProgressBar::foregroundColourId, theme::selection);
    progressBar.setComponentID("extractProgress");
    progressBar.setVisible(false);
    progressLabel.setVisible(false);
    resultLabel.setVisible(false);
    details.setComponentID("extractDetails");
    details.setVisible(false);
    problems.setComponentID("extractProblems");
    problems.setReadOnly(true);
    problems.setMultiLine(true);
    problems.setScrollbarsShown(true);
    problems.setVisible(false);
    ok.setComponentID("extractOk");
    ok.setVisible(false);
    browse.setComponentID("extractBrowse");
    accept.setComponentID("extractAccept");
    cancel.setComponentID("extractCancel");

    const auto updateFolderState = [this]
    {
        const bool enabled = ! running && ! sameFolder.getToggleState();
        folder.setEnabled(enabled);
        browse.setEnabled(enabled);
    };
    sameFolder.onClick = updateFolderState;
    updateFolderState();
    details.onClick = [this]
    {
        problems.setVisible(details.getToggleState() && finished);
        resized();
    };
    browse.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser>("Choose output folder",
            juce::File(folder.getText()), juce::String());
        chooser->launchAsync(juce::FileBrowserComponent::openMode
                                 | juce::FileBrowserComponent::canSelectDirectories,
            [safe = juce::Component::SafePointer<ExtractDialog>(this)](const juce::FileChooser& fc)
            {
                if (safe == nullptr) return;
                const auto result = fc.getResult();
                if (result.isDirectory()) safe->folder.setText(result.getFullPathName());
            });
    };
    accept.onClick = [this]
    {
        const auto next = currentOptions();
        if (! next.sameFolder && ! next.outputFolder.isDirectory())
        {
            error.setText("Choose an existing output folder.", juce::dontSendNotification);
            return;
        }
        error.setText({}, juce::dontSendNotification);
        if (acceptCallback == nullptr || ! acceptCallback(next))
        {
            error.setText("Extract could not start.", juce::dontSendNotification);
            return;
        }
        enterRunningState();
    };
    cancel.onClick = [this]
    {
        if (running)
        {
            if (onCancel) onCancel();
            return;
        }
        closeDialog();
    };
    ok.onClick = [this]
    {
        if (onOk) onOk();
        closeDialog();
    };

    addAndMakeVisible(title);
    addAndMakeVisible(sameFolder);
    addAndMakeVisible(normalize);
    addAndMakeVisible(folder);
    addAndMakeVisible(browse);
    addAndMakeVisible(error);
    addAndMakeVisible(progressBar);
    addAndMakeVisible(progressLabel);
    addAndMakeVisible(resultLabel);
    addAndMakeVisible(details);
    addAndMakeVisible(problems);
    addAndMakeVisible(accept);
    addAndMakeVisible(cancel);
    addAndMakeVisible(ok);
    updateButtons();
}
void ExtractDialog::paint(juce::Graphics& g)
{
    g.fillAll(theme::panel);
}
void ExtractDialog::resized()
{
    auto area = getLocalBounds().reduced(18);
    title.setBounds(area.removeFromTop(28));
    area.removeFromTop(4);
    sameFolder.setBounds(area.removeFromTop(24));
    area.removeFromTop(4);
    normalize.setBounds(area.removeFromTop(24));
    area.removeFromTop(8);
    auto folderRow = area.removeFromTop(26);
    browse.setBounds(folderRow.removeFromRight(96));
    folderRow.removeFromRight(8);
    folder.setBounds(folderRow);
    area.removeFromTop(8);
    error.setBounds(area.removeFromTop(20));
    progressBar.setBounds(area.removeFromTop(18));
    progressLabel.setBounds(area.removeFromTop(20));
    resultLabel.setBounds(area.removeFromTop(54));
    details.setBounds(area.removeFromTop(24));
    if (problems.isVisible()) problems.setBounds(area.removeFromTop(
        juce::jmax(0, area.getHeight() - 44)));
    auto buttons = area.removeFromBottom(32);
    const int buttonWidth = 110;
    if (ok.isVisible())
    {
        ok.setBounds(buttons.removeFromRight(buttonWidth));
    }
    else
    {
        accept.setBounds(buttons.removeFromRight(buttonWidth));
        buttons.removeFromRight(8);
        cancel.setBounds(buttons.removeFromRight(90));
    }
}
ExtractOptions ExtractDialog::currentOptions() const
{
    ExtractOptions options;
    options.sameFolder = sameFolder.getToggleState();
    options.normalize = normalize.getToggleState();
    options.outputFolder = options.sameFolder ? juce::File()
                                              : juce::File(folder.getText().trim());
    return options;
}
void ExtractDialog::setInputsEnabled(bool enabled)
{
    sameFolder.setEnabled(enabled);
    normalize.setEnabled(enabled);
    folder.setEnabled(enabled && ! sameFolder.getToggleState());
    browse.setEnabled(enabled && ! sameFolder.getToggleState());
}
void ExtractDialog::updateButtons()
{
    accept.setVisible(! running && ! finished);
    cancel.setVisible(! finished);
    ok.setVisible(finished);
    progressBar.setVisible(running || finished);
    progressLabel.setVisible(running || finished);
    resultLabel.setVisible(finished);
    details.setVisible(finished);
    problems.setVisible(finished && details.getToggleState());
    resized();
}
void ExtractDialog::enterRunningState()
{
    running = true;
    finished = false;
    details.setToggleState(false, juce::dontSendNotification);
    setInputsEnabled(false);
    error.setText({}, juce::dontSendNotification);
    progressValue = 0.0;
    progressLabel.setText("Extracting 0/" + juce::String((int) sources.size())
        + "  |  0%  |  estimating...", juce::dontSendNotification);
    updateButtons();
}
void ExtractDialog::setProgress(const ExtractProgress& progress, double elapsedSeconds)
{
    if (! running) return;
    progressValue = juce::jlimit(0.0, 1.0, progress.fraction);
    const auto percent = (int) std::floor(progressValue * 100.0);
    juce::String eta;
    if (progress.processedFiles > 0 && progress.totalFiles > progress.processedFiles)
        eta = "~" + formatEta(elapsedSeconds * (double) (progress.totalFiles - progress.processedFiles)
                              / (double) progress.processedFiles) + " left";
    else if (progress.totalFiles > 0 && progress.processedFiles >= progress.totalFiles)
        eta = "finishing";
    else
        eta = "estimating...";
    progressLabel.setText("Extracting " + juce::String(progress.processedFiles) + "/"
        + juce::String(progress.totalFiles) + "  |  " + juce::String(percent) + "%  |  " + eta
        + (progress.current.isNotEmpty() ? "  |  " + progress.current : juce::String())
        + "  |  " + juce::String(progress.written) + " saved",
        juce::dontSendNotification);
}
void ExtractDialog::showFinished(const juce::String& summary, const juce::String& detailsText,
                                 double fraction)
{
    running = false;
    finished = true;
    progressValue = juce::jlimit(0.0, 1.0, fraction);
    progressLabel.setText("Finished", juce::dontSendNotification);
    resultLabel.setText(summary, juce::dontSendNotification);
    problems.setText(detailsText, false);
    details.setToggleState(false, juce::dontSendNotification);
    details.setEnabled(detailsText.isNotEmpty());
    details.setVisible(detailsText.isNotEmpty());
    problems.setVisible(false);
    updateButtons();
}
void ExtractDialog::closeDialog()
{
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState(0);
}
} // namespace sonicator
