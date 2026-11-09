#include "SplitDialog.h"
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
SplitDialog::SplitDialog(std::vector<juce::File> inputSources, const AppSettings& settings,
                         std::function<bool(SplitOptions)> onAccept)
    : sources(std::move(inputSources)), acceptCallback(std::move(onAccept))
{
    setSize(560, 440);
    title.setText("Split " + juce::String((int) sources.size()) + " file(s) by silence",
                  juce::dontSendNotification);
    title.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    thresholdLabel.setText("Silence threshold", juce::dontSendNotification);
    minSilenceLabel.setText("Minimum silence", juce::dontSendNotification);
    minPartLabel.setText("Minimum part", juce::dontSendNotification);
    for (auto* label : { &thresholdLabel, &minSilenceLabel, &minPartLabel })
        label->setColour(juce::Label::textColourId, theme::muted);

    threshold.setComponentID("splitThreshold");
    threshold.setRange(-80.0, -20.0, 1.0);
    threshold.setValue(settings.splitThresholdDb);
    threshold.setTextValueSuffix(" dBFS");
    threshold.setSliderStyle(juce::Slider::LinearHorizontal);
    threshold.setTextBoxStyle(juce::Slider::TextBoxRight, false, 90, 22);

    minSilence.setComponentID("splitMinSilence");
    minSilence.setRange(50.0, 2000.0, 10.0);
    minSilence.setValue(settings.splitMinSilenceMs);
    minSilence.setTextValueSuffix(" ms");
    minSilence.setSliderStyle(juce::Slider::LinearHorizontal);
    minSilence.setTextBoxStyle(juce::Slider::TextBoxRight, false, 90, 22);

    minPart.setComponentID("splitMinPart");
    minPart.setRange(20.0, 2000.0, 10.0);
    minPart.setValue(settings.splitMinPartMs);
    minPart.setTextValueSuffix(" ms");
    minPart.setSliderStyle(juce::Slider::LinearHorizontal);
    minPart.setTextBoxStyle(juce::Slider::TextBoxRight, false, 90, 22);

    sameFolder.setComponentID("splitSameFolder");
    sameFolder.setToggleState(settings.splitSameFolder, juce::dontSendNotification);
    moveToTrash.setComponentID("splitMoveToTrash");
    moveToTrash.setToggleState(settings.splitMoveToTrash, juce::dontSendNotification);
    folder.setText(sources.empty() ? juce::String()
                                   : sources.front().getParentDirectory().getFullPathName());
    folder.setMultiLine(false);
    error.setColour(juce::Label::textColourId, juce::Colour(0xffff8b8b));
    resultLabel.setColour(juce::Label::textColourId, theme::text);
    resultLabel.setJustificationType(juce::Justification::topLeft);
    progressLabel.setColour(juce::Label::textColourId, theme::muted);
    progressBar.setPercentageDisplay(false);
    progressBar.setColour(juce::ProgressBar::backgroundColourId, theme::table);
    progressBar.setColour(juce::ProgressBar::foregroundColourId, theme::accent);
    progressBar.setVisible(false);
    progressLabel.setVisible(false);
    resultLabel.setVisible(false);
    ok.setVisible(false);
    ok.setComponentID("splitOk");
    accept.setComponentID("splitAccept");
    cancel.setComponentID("splitCancel");
    progressBar.setComponentID("splitProgress");

    const auto updateFolderState = [this]
    {
        const bool enabled = ! running && ! sameFolder.getToggleState();
        folder.setEnabled(enabled);
        browse.setEnabled(enabled);
    };
    sameFolder.onClick = updateFolderState;
    updateFolderState();

    browse.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser>("Choose output folder",
            juce::File(folder.getText()), juce::String());
        chooser->launchAsync(juce::FileBrowserComponent::openMode
                                 | juce::FileBrowserComponent::canSelectDirectories,
            [safe = juce::Component::SafePointer<SplitDialog>(this)](const juce::FileChooser& fc)
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
            error.setText("Split could not start.", juce::dontSendNotification);
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
    addAndMakeVisible(thresholdLabel); addAndMakeVisible(threshold);
    addAndMakeVisible(minSilenceLabel); addAndMakeVisible(minSilence);
    addAndMakeVisible(minPartLabel); addAndMakeVisible(minPart);
    addAndMakeVisible(sameFolder);
    addAndMakeVisible(moveToTrash);
    addAndMakeVisible(folder); addAndMakeVisible(browse);
    addAndMakeVisible(error);
    addAndMakeVisible(progressBar); addAndMakeVisible(progressLabel);
    addAndMakeVisible(resultLabel);
    addAndMakeVisible(accept); addAndMakeVisible(cancel); addAndMakeVisible(ok);
    ok.setVisible(false);
    progressBar.setVisible(false);
    progressLabel.setVisible(false);
    resultLabel.setVisible(false);
}
void SplitDialog::paint(juce::Graphics& g)
{
    g.fillAll(theme::panel);
}
void SplitDialog::resized()
{
    auto area = getLocalBounds().reduced(18);
    title.setBounds(area.removeFromTop(28));
    area.removeFromTop(4);
    const auto row = [&area](juce::Label& label, juce::Component& control)
    {
        auto line = area.removeFromTop(26);
        label.setBounds(line.removeFromLeft(150));
        control.setBounds(line);
        area.removeFromTop(8);
    };
    row(thresholdLabel, threshold);
    row(minSilenceLabel, minSilence);
    row(minPartLabel, minPart);
    sameFolder.setBounds(area.removeFromTop(24));
    area.removeFromTop(4);
    moveToTrash.setBounds(area.removeFromTop(24));
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
SplitOptions SplitDialog::currentOptions() const
{
    SplitOptions options;
    options.silence.thresholdDb = threshold.getValue();
    options.silence.minSilenceMs = minSilence.getValue();
    options.silence.minPartMs = minPart.getValue();
    options.silence.edgePaddingMs = 5.0;
    options.sameFolder = sameFolder.getToggleState();
    options.moveToTrash = moveToTrash.getToggleState();
    options.outputFolder = options.sameFolder ? juce::File()
                                              : juce::File(folder.getText().trim());
    return options;
}
void SplitDialog::setInputsEnabled(bool enabled)
{
    threshold.setEnabled(enabled);
    minSilence.setEnabled(enabled);
    minPart.setEnabled(enabled);
    sameFolder.setEnabled(enabled);
    moveToTrash.setEnabled(enabled);
    folder.setEnabled(enabled && ! sameFolder.getToggleState());
    browse.setEnabled(enabled && ! sameFolder.getToggleState());
}
void SplitDialog::updateButtons()
{
    accept.setVisible(! running && ! finished);
    cancel.setVisible(! finished);
    ok.setVisible(finished);
    progressBar.setVisible(running || finished);
    progressLabel.setVisible(running || finished);
    resultLabel.setVisible(finished);
    resized();
}
void SplitDialog::enterRunningState()
{
    running = true;
    finished = false;
    setInputsEnabled(false);
    error.setText({}, juce::dontSendNotification);
    progressValue = 0.0;
    progressLabel.setText("Splitting 0/" + juce::String((int) sources.size())
        + "  |  0%  |  estimating...", juce::dontSendNotification);
    updateButtons();
}
void SplitDialog::setProgress(const SplitProgress& progress, double elapsedSeconds)
{
    if (! running) return;
    const double fraction = progress.total > 0
        ? juce::jlimit(0.0, 1.0, (double) progress.processed / (double) progress.total) : 0.0;
    progressValue = fraction;
    const auto percent = (int) std::floor(fraction * 100.0);
    juce::String eta;
    if (progress.processed > 0 && progress.total > progress.processed)
        eta = "~" + formatEta(elapsedSeconds * (double) (progress.total - progress.processed)
                              / (double) progress.processed) + " left";
    else if (progress.total > 0 && progress.processed >= progress.total)
        eta = "finishing";
    else
        eta = "estimating...";
    progressLabel.setText("Splitting " + juce::String(progress.processed) + "/"
        + juce::String(progress.total) + "  |  " + juce::String(percent) + "%  |  " + eta
        + (progress.current.isNotEmpty() ? "  |  " + progress.current : juce::String()),
        juce::dontSendNotification);
}
void SplitDialog::showFinished(const juce::String& summary, double fraction)
{
    running = false;
    finished = true;
    progressValue = juce::jlimit(0.0, 1.0, fraction);
    progressLabel.setText("Finished", juce::dontSendNotification);
    resultLabel.setText(summary, juce::dontSendNotification);
    updateButtons();
}
void SplitDialog::closeDialog()
{
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState(0);
}
} // namespace sonicator
