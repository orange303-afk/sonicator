#include "SplitDialog.h"
#include "ui/shell/Theme.h"
namespace sonicator {
SplitDialog::SplitDialog(std::vector<juce::File> inputSources, const AppSettings& settings,
                         std::function<void(SplitOptions)> onAccept)
    : sources(std::move(inputSources)), acceptCallback(std::move(onAccept))
{
    setSize(540, 340);
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
    folder.setText(sources.empty() ? juce::String()
                                   : sources.front().getParentDirectory().getFullPathName());
    folder.setMultiLine(false);
    error.setColour(juce::Label::textColourId, juce::Colour(0xffff8b8b));

    const auto updateFolderState = [this]
    {
        const bool enabled = ! sameFolder.getToggleState();
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
        if (acceptCallback) acceptCallback(next);
        closeDialog();
    };
    cancel.onClick = [this] { closeDialog(); };

    addAndMakeVisible(title);
    addAndMakeVisible(thresholdLabel); addAndMakeVisible(threshold);
    addAndMakeVisible(minSilenceLabel); addAndMakeVisible(minSilence);
    addAndMakeVisible(minPartLabel); addAndMakeVisible(minPart);
    addAndMakeVisible(sameFolder);
    addAndMakeVisible(folder); addAndMakeVisible(browse);
    addAndMakeVisible(error);
    addAndMakeVisible(accept); addAndMakeVisible(cancel);
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
    area.removeFromTop(8);
    auto folderRow = area.removeFromTop(26);
    browse.setBounds(folderRow.removeFromRight(96));
    folderRow.removeFromRight(8);
    folder.setBounds(folderRow);
    area.removeFromTop(8);
    error.setBounds(area.removeFromTop(20));
    auto buttons = area.removeFromBottom(32);
    accept.setBounds(buttons.removeFromRight(110));
    buttons.removeFromRight(8);
    cancel.setBounds(buttons.removeFromRight(90));
}
SplitOptions SplitDialog::currentOptions() const
{
    SplitOptions options;
    options.silence.thresholdDb = threshold.getValue();
    options.silence.minSilenceMs = minSilence.getValue();
    options.silence.minPartMs = minPart.getValue();
    options.silence.edgePaddingMs = 5.0;
    options.sameFolder = sameFolder.getToggleState();
    options.outputFolder = options.sameFolder ? juce::File()
                                              : juce::File(folder.getText().trim());
    return options;
}
void SplitDialog::closeDialog()
{
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState(0);
}
} // namespace sonicator
