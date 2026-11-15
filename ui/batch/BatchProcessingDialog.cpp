#include "BatchProcessingDialog.h"
#include "core/batch/BatchService.h"
#include "core/settings/AppSettings.h"
#include "ui/shell/Theme.h"
#include "ui/shell/AppFonts.h"
#include <algorithm>

namespace sonicator::batch
{
namespace
{
juce::String defaultParameters(const StepParameters& parameters)
{
    if (auto* trim = std::get_if<TrimSilence>(&parameters))
        return juce::String(trim->thresholdDb, 1) + ", " + juce::String(trim->paddingMs, 1);
    if (auto* normalize = std::get_if<Normalize>(&parameters))
        return juce::String(normalize->peakDb, 2);
    if (auto* gain = std::get_if<Gain>(&parameters))
        return juce::String(gain->gainDb, 2);
    if (auto* fade = std::get_if<FadeIn>(&parameters))
        return juce::String(fade->ms, 1) + ", " + (fade->cosine ? "1" : "0");
    if (auto* fade = std::get_if<FadeOut>(&parameters))
        return juce::String(fade->ms, 1) + ", " + (fade->cosine ? "1" : "0");
    return {};
}

StepParameters parseParameters(int type, const juce::String& text)
{
    const auto tokens = juce::StringArray::fromTokens(text, ",", "");
    auto at = [&tokens](int index, double fallback)
    {
        return index < tokens.size() ? tokens[index].trim().getDoubleValue() : fallback;
    };
    switch (type)
    {
        case 1: return RemoveDc {};
        case 2: return TrimSilence { at(0, -60.0), at(1, 5.0) };
        case 3: return Normalize { at(0, -0.1) };
        case 4: return Gain { at(0, 0.0) };
        case 5: return FadeIn { at(0, 5.0), at(1, 0.0) != 0.0 };
        case 6: return FadeOut { at(0, 5.0), at(1, 0.0) != 0.0 };
        default: return RemoveDc {};
    }
}

int typeOf(const StepParameters& parameters)
{
    if (std::holds_alternative<RemoveDc>(parameters)) return 1;
    if (std::holds_alternative<TrimSilence>(parameters)) return 2;
    if (std::holds_alternative<Normalize>(parameters)) return 3;
    if (std::holds_alternative<Gain>(parameters)) return 4;
    if (std::holds_alternative<FadeIn>(parameters)) return 5;
    return 6;
}

const char* typeName(int type)
{
    switch (type)
    {
        case 1: return "Remove DC offset";
        case 2: return "Trim silence";
        case 3: return "Normalize";
        case 4: return "Gain";
        case 5: return "Fade in";
        case 6: return "Fade out";
        default: return "Step";
    }
}
} // namespace

class StepRow final : public juce::Component
{
public:
    StepRow(int rowIndex, const ProcessStep& source, std::function<void()> changedCallback,
            std::function<void(int, int)> moveCallback, std::function<void(int)> duplicateCallback,
            std::function<void(int)> removeCallback)
        : index(rowIndex), changed(std::move(changedCallback)), move(std::move(moveCallback)),
          duplicate(std::move(duplicateCallback)), remove(std::move(removeCallback))
    {
        enabled.setToggleState(source.enabled, juce::dontSendNotification);
        enabled.onClick = [this] { if (changed) changed(); };
        addAndMakeVisible(enabled);
        enabled.setComponentID("batchStepEnabled");
        for (int type = 1; type <= 6; ++type) typeBox.addItem(typeName(type), type);
        typeBox.setSelectedId(typeOf(source.parameters), juce::dontSendNotification);
        typeBox.onChange = [this]
        {
            parameters.setText(defaultParameters(parseParameters(typeBox.getSelectedId(), {})),
                               juce::dontSendNotification);
            if (changed) changed();
        };
        addAndMakeVisible(typeBox);
        typeBox.setComponentID("batchStepType");
        parameters.setText(defaultParameters(source.parameters), juce::dontSendNotification);
        parameters.onTextChange = [this] { if (changed) changed(); };
        parameters.setSelectAllWhenFocused(true);
        parameters.setInputRestrictions(40, "0123456789.,- ");
        addAndMakeVisible(parameters);
        parameters.setComponentID("batchStepParameters");
        up.setButtonText("Up"); down.setButtonText("Down");
        duplicateButton.setButtonText("Duplicate"); removeButton.setButtonText("Remove");
        for (auto* button : { &up, &down, &duplicateButton, &removeButton }) addAndMakeVisible(*button);
        up.onClick = [this] { if (move) move(index, -1); };
        down.onClick = [this] { if (move) move(index, 1); };
        duplicateButton.onClick = [this] { if (duplicate) duplicate(index); };
        removeButton.onClick = [this] { if (remove) remove(index); };
    }
    ProcessStep current() const
    {
        ProcessStep step;
        step.enabled = enabled.getToggleState();
        step.parameters = parseParameters(typeBox.getSelectedId(), parameters.getText());
        return step;
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced(2);
        enabled.setBounds(area.removeFromLeft(46));
        typeBox.setBounds(area.removeFromLeft(150));
        parameters.setBounds(area.removeFromLeft(140));
        area.removeFromLeft(4);
        auto buttons = area.removeFromRight(230);
        removeButton.setBounds(buttons.removeFromRight(58));
        duplicateButton.setBounds(buttons.removeFromRight(78));
        down.setBounds(buttons.removeFromRight(44));
        up.setBounds(buttons.removeFromRight(44));
    }
private:
    int index;
    std::function<void()> changed;
    std::function<void(int, int)> move;
    std::function<void(int)> duplicate, remove;
    juce::ToggleButton enabled { "On" };
    juce::ComboBox typeBox;
    juce::TextEditor parameters;
    juce::TextButton up, down, duplicateButton, removeButton;
};

BatchProcessingDialog::BatchProcessingDialog(PreparedBatch prepared, juce::File presetFile)
    : batch(std::move(prepared)), presetStore(std::move(presetFile)),
      progressBar(progressValue)
{
    steps = batch.request.recipe.steps;
    title.setText("Batch Processing", juce::dontSendNotification);
    title.setFont(fonts::font(18.0f, fonts::Weight::bold));
    addAndMakeVisible(title);
    summary.setText(formatSummary(batch), juce::dontSendNotification);
    summary.setColour(juce::Label::textColourId, theme::muted);
    addAndMakeVisible(summary);

    addAndMakeVisible(presetBox);
    presetBox.setComponentID("batchPreset");
    presetBox.onChange = [this]
    {
        const int selected = presetBox.getSelectedId();
        if (selected > 0 && selected <= (int) presetIds.size())
            loadPreset(presetIds[(size_t) selected - 1]);
    };
    addAndMakeVisible(addStepButton);
    addAndMakeVisible(savePresetButton);
    addAndMakeVisible(deletePresetButton);
    addStepButton.onClick = [this]
    {
        juce::PopupMenu menu;
        for (int type = 1; type <= 6; ++type) menu.addItem(type, typeName(type));
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addStepButton),
                           [safe = juce::Component::SafePointer<BatchProcessingDialog>(this)](int result)
                           {
                               if (safe != nullptr && result > 0) safe->addStep(result);
                           });
    };
    savePresetButton.onClick = [this] { savePreset(); };
    deletePresetButton.onClick = [this] { deletePreset(); };
    addAndMakeVisible(stepsViewport);
    stepsViewport.setViewedComponent(&stepsContent, false);
    stepsViewport.setScrollBarsShown(true, false);

    const std::array<std::pair<juce::ToggleButton*, juce::ComboBox*>, 3> formats { {
        { &wavToggle, &wavDepth }, { &aiffToggle, &aiffDepth }, { &flacToggle, &flacDepth } } };
    for (auto& pair : formats)
    {
        addAndMakeVisible(*pair.first);
        addAndMakeVisible(*pair.second);
        pair.first->onClick = [this] { updateOutputControls(); refreshFooter(); };
    }
    wavToggle.setComponentID("batchFormatWav");
    aiffToggle.setComponentID("batchFormatAiff");
    flacToggle.setComponentID("batchFormatFlac");
    wavToggle.setToggleState(true, juce::dontSendNotification);
    for (auto* depth : { &wavDepth, &aiffDepth, &flacDepth })
    {
        depth->addItem("Same as source", 1);
        depth->addItem("16-bit PCM", 2);
        depth->addItem("24-bit PCM", 3);
    }
    wavDepth.addItem("32-bit float", 4);
    wavDepth.setSelectedId(3, juce::dontSendNotification);
    aiffDepth.setSelectedId(3, juce::dontSendNotification);
    flacDepth.setSelectedId(2, juce::dontSendNotification);
    wavDepth.setComponentID("batchDepthWav");
    aiffDepth.setComponentID("batchDepthAiff");
    flacDepth.setComponentID("batchDepthFlac");
    for (auto* depth : { &wavDepth, &aiffDepth, &flacDepth })
        depth->onChange = [this] { refreshFooter(); };
    addAndMakeVisible(flacLevel);
    for (int level = 1; level <= 8; ++level) flacLevel.addItem(juce::String(level), level);
    flacLevel.setSelectedId(5, juce::dontSendNotification);
    flacLevel.onChange = [this] { refreshFooter(); };

    addAndMakeVisible(rateBox);
    rateBox.addItem("Same as source", 1);
    const std::array<int, 5> rates { 44100, 48000, 88200, 96000, 192000 };
    for (size_t i = 0; i < rates.size(); ++i)
        rateBox.addItem(juce::String(rates[i]) + " Hz", (int) i + 2);
    rateBox.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(channelBox);
    channelBox.addItem("Same as source", 1); channelBox.addItem("Mono", 2); channelBox.addItem("Stereo", 3);
    channelBox.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(ditherBox);
    ditherBox.addItem("Dither: Auto", 1); ditherBox.addItem("Dither: TPDF", 2);
    ditherBox.addItem("Dither: Off", 3);
    ditherBox.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(conflictBox);
    conflictBox.addItem("Existing outputs: Rename", 1);
    conflictBox.addItem("Existing outputs: Skip", 2);
    conflictBox.addItem("Existing outputs: Replace (confirmed)", 3);
    conflictBox.setSelectedId(1, juce::dontSendNotification);
    conflictBox.onChange = [this] { refreshFooter(); };
    for (auto* combo : { &rateBox, &channelBox, &ditherBox, &conflictBox })
    {
        combo->onChange = [this] { refreshFooter(); };
    }
    rateBox.setComponentID("batchRate");
    channelBox.setComponentID("batchChannels");
    ditherBox.setComponentID("batchDither");
    conflictBox.setComponentID("batchConflict");
    addAndMakeVisible(allowClipping);
    allowClipping.onClick = [this] { refreshFooter(); };

    addAndMakeVisible(nextToSources);
    nextToSources.setToggleState(true, juce::dontSendNotification);
    nextToSources.onClick = [this] { updateOutputControls(); refreshFooter(); };
    addAndMakeVisible(outputFolder);
    outputFolder.setReadOnly(true);
    outputFolder.setComponentID("batchDestination");
    addAndMakeVisible(browseFolder);
    browseFolder.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser>("Choose an output folder",
            juce::File(outputFolder.getText()), "*", true, false, nullptr);
        chooser->launchAsync(juce::FileBrowserComponent::openMode
                                 | juce::FileBrowserComponent::canSelectDirectories,
            [this](const juce::FileChooser& chosen)
            {
                const auto folder = chosen.getResult();
                if (folder != juce::File())
                {
                    outputFolder.setText(folder.getFullPathName());
                    nextToSources.setToggleState(false, juce::dontSendNotification);
                    updateOutputControls();
                    refreshFooter();
                }
            });
    };
    addAndMakeVisible(destinationLabel);

    addAndMakeVisible(previewSource);
    for (size_t i = 0; i < batch.sources.size(); ++i)
        previewSource.addItem(batch.sources[i].path.getFileName(), (int) i + 1);
    if (! batch.sources.empty()) previewSource.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(preparePreview);
    preparePreview.onClick = [this] { startPreview(); };
    addAndMakeVisible(playDry); addAndMakeVisible(playWet); addAndMakeVisible(stopPreview);
    playDry.onClick = [this] { if (onPlayPreview && dryFile.existsAsFile()) onPlayPreview(dryFile, false); };
    playWet.onClick = [this] { if (onPlayPreview && wetFile.existsAsFile()) onPlayPreview(wetFile, true); };
    stopPreview.onClick = [this] { if (onStopPreview) onStopPreview(); };
    playDry.setEnabled(false); playWet.setEnabled(false);
    addAndMakeVisible(previewLabel);
    previewLabel.setColour(juce::Label::textColourId, theme::muted);
    addAndMakeVisible(previewStatus);
    previewStatus.setColour(juce::Label::textColourId, theme::muted);
    previewStatus.setFont(fonts::font(12.0f));

    addAndMakeVisible(progressBar);
    progressBar.setPercentageDisplay(false);
    addAndMakeVisible(phase);
    phase.setColour(juce::Label::textColourId, theme::muted);

    addChildComponent(reportLabel);
    addChildComponent(reportText);
    reportText.setMultiLine(true);
    reportText.setReadOnly(true);
    reportText.setScrollbarsShown(true);
    reportText.setFont(fonts::font(12.0f));
    reportText.setComponentID("batchReport");
    addChildComponent(exportReport); addChildComponent(openFolder); addChildComponent(scanNewFiles);
    exportReport.onClick = [this] { if (onExportReport) onExportReport(); };
    openFolder.onClick = [this] { if (onOpenOutputFolder) onOpenOutputFolder(); };
    scanNewFiles.onClick = [this] { if (onScanNewFiles) onScanNewFiles(); };

    addAndMakeVisible(startButton);
    addAndMakeVisible(cancelButton);
    startButton.setComponentID("batchStart");
    cancelButton.setComponentID("batchCancel");
    preparePreview.setComponentID("batchPreparePreview");
    startButton.onClick = [this] { if (onStart && canStart()) onStart(); };
    cancelButton.onClick = [this]
    {
        if (running && onCancelJob) { onCancelJob(); return; }
        closeDialog();
    };
    startButton.setEnabled(false);
    rebuildPresets();
    rebuildSteps();
    updateOutputControls();
    refreshFooter();
    setSize(820, 760);
    startTimerHz(4);
}

BatchProcessingDialog::~BatchProcessingDialog()
{
    stopTimer();
    chooser.reset();
}

juce::String BatchProcessingDialog::formatSummary(const PreparedBatch& prepared)
{
    const auto eligible = (int) prepared.sources.size();
    juce::String text = juce::String(eligible) + " eligible file"
        + (eligible == 1 ? "" : "s");
    if (! prepared.skipped.empty()) text += ", " + juce::String((int) prepared.skipped.size()) + " skipped";
    const auto outputs = prepared.outputs.size();
    if (outputs > 0) text += " | " + juce::String((int) outputs) + " planned output"
        + (outputs == 1 ? "" : "s");
    const bool recursive = prepared.request.sources.kind == SourceSelection::Kind::folder;
    text += recursive ? " | folder and subfolders" : " | current selection";
    return text;
}

void BatchProcessingDialog::paint(juce::Graphics& g)
{
    g.fillAll(theme::background);
    surface.paint(g, getLocalBounds());
}

void BatchProcessingDialog::resized()
{
    auto area = dialogs::Surface::contentArea(getLocalBounds());
    dialogs::Surface::titleArea(getLocalBounds());
    title.setBounds(area.removeFromTop(26));
    summary.setBounds(area.removeFromTop(22));
    area.removeFromTop(4);

    auto presetRow = area.removeFromTop(30);
    presetBox.setBounds(presetRow.removeFromLeft(190));
    presetRow.removeFromLeft(6);
    addStepButton.setBounds(presetRow.removeFromLeft(96));
    presetRow.removeFromLeft(6);
    savePresetButton.setBounds(presetRow.removeFromLeft(100));
    presetRow.removeFromLeft(6);
    deletePresetButton.setBounds(presetRow.removeFromLeft(104));
    stepsViewport.setBounds(area.removeFromTop(150));
    for (int i = 0; i < (int) rows.size(); ++i)
        rows[(size_t) i]->setBounds(0, i * 36, stepsViewport.getMaximumVisibleWidth() - 8, 34);
    stepsContent.setSize(stepsViewport.getMaximumVisibleWidth() - 8,
                         std::max(1, (int) rows.size() * 36));
    area.removeFromTop(6);

    auto output = area.removeFromTop(30);
    wavToggle.setBounds(output.removeFromLeft(58));
    wavDepth.setBounds(output.removeFromLeft(120));
    output.removeFromLeft(8);
    aiffToggle.setBounds(output.removeFromLeft(58));
    aiffDepth.setBounds(output.removeFromLeft(120));
    output.removeFromLeft(8);
    flacToggle.setBounds(output.removeFromLeft(58));
    flacDepth.setBounds(output.removeFromLeft(108));
    output.removeFromLeft(8);
    flacLevel.setBounds(output.removeFromLeft(70));
    output.removeFromLeft(8);
    rateBox.setBounds(output.removeFromLeft(140));
    output.removeFromLeft(6);
    channelBox.setBounds(output.removeFromLeft(130));
    area.removeFromTop(6);
    auto row2 = area.removeFromTop(30);
    ditherBox.setBounds(row2.removeFromLeft(160));
    row2.removeFromLeft(8);
    conflictBox.setBounds(row2.removeFromLeft(250));
    row2.removeFromLeft(8);
    allowClipping.setBounds(row2.removeFromLeft(240));
    area.removeFromTop(4);
    auto row3 = area.removeFromTop(30);
    nextToSources.setBounds(row3.removeFromLeft(200));
    row3.removeFromLeft(6);
    outputFolder.setBounds(row3.removeFromLeft(std::max(120, row3.getWidth() - 90)));
    browseFolder.setBounds(row3.removeFromRight(84));

    area.removeFromTop(6);
    previewLabel.setText("Preview", juce::dontSendNotification);
    previewLabel.setBounds(area.removeFromTop(20));
    auto previewRow = area.removeFromTop(30);
    previewSource.setBounds(previewRow.removeFromLeft(180));
    previewRow.removeFromLeft(6);
    preparePreview.setBounds(previewRow.removeFromLeft(120));
    previewRow.removeFromLeft(6);
    playDry.setBounds(previewRow.removeFromLeft(90));
    playWet.setBounds(previewRow.removeFromLeft(100));
    stopPreview.setBounds(previewRow.removeFromLeft(70));
    area.removeFromTop(2);
    previewStatus.setBounds(area.removeFromTop(34));

    auto footer = dialogs::Surface::footerArea(getLocalBounds());
    cancelButton.setBounds(footer.removeFromRight(110));
    footer.removeFromRight(8);
    startButton.setBounds(footer.removeFromRight(100));
    footer.removeFromRight(12);
    progressBar.setBounds(footer.removeFromLeft(150));
    footer.removeFromLeft(8);
    phase.setBounds(footer);

    if (reportShown)
    {
        auto reportArea = getLocalBounds().withTrimmedTop(150).withTrimmedBottom(60).reduced(24);
        reportLabel.setBounds(reportArea.removeFromTop(20));
        auto buttons = reportArea.removeFromBottom(30);
        exportReport.setBounds(buttons.removeFromLeft(130));
        buttons.removeFromLeft(8);
        openFolder.setBounds(buttons.removeFromLeft(150));
        buttons.removeFromLeft(8);
        scanNewFiles.setBounds(buttons.removeFromLeft(130));
        reportArea.removeFromTop(4);
        reportText.setBounds(reportArea);
    }
}

void BatchProcessingDialog::rebuildSteps()
{
    rows.clear();
    stepsContent.removeAllChildren();
    for (size_t i = 0; i < steps.size(); ++i)
    {
        auto row = std::make_unique<StepRow>((int) i, steps[i],
            [this] { refreshFooter(); },
            [this](int index, int delta) { moveStep(index, delta); },
            [this](int index) { duplicateStep(index); },
            [this](int index) { removeStep(index); });
        stepsContent.addAndMakeVisible(*row);
        rows.push_back(std::move(row));
    }
    resized();
    refreshFooter();
}

void BatchProcessingDialog::rebuildPresets()
{
    presetBox.clear(juce::dontSendNotification);
    presetIds.clear();
    const auto loaded = presetStore.load();
    for (const auto& preset : loaded.presets)
    {
        presetBox.addItem(preset.name + (preset.builtIn ? " (built-in)" : ""),
                          presetBox.getNumItems() + 1);
        presetIds.push_back(preset.id);
    }
}

void BatchProcessingDialog::loadPreset(const juce::String& id)
{
    const auto loaded = presetStore.load();
    for (const auto& preset : loaded.presets)
        if (preset.id == id)
        {
            steps = preset.recipe.steps;
            wavToggle.setToggleState(false, juce::dontSendNotification);
            aiffToggle.setToggleState(false, juce::dontSendNotification);
            flacToggle.setToggleState(false, juce::dontSendNotification);
            for (const auto& output : preset.recipe.outputs)
            {
                auto* toggle = output.format == Format::wav ? &wavToggle
                    : output.format == Format::aiff ? &aiffToggle : &flacToggle;
                auto* depth = output.format == Format::wav ? &wavDepth
                    : output.format == Format::aiff ? &aiffDepth : &flacDepth;
                toggle->setToggleState(true, juce::dontSendNotification);
                depth->setSelectedId((int) output.encoding + 1, juce::dontSendNotification);
                if (output.format == Format::flac)
                    flacLevel.setSelectedId(output.flacCompressionLevel, juce::dontSendNotification);
            }
            const std::array<int, 5> rates { 44100, 48000, 88200, 96000, 192000 };
            int rateId = 1;
            for (size_t i = 0; i < rates.size(); ++i)
                if (rates[i] == preset.recipe.sampleRate) rateId = (int) i + 2;
            rateBox.setSelectedId(rateId, juce::dontSendNotification);
            channelBox.setSelectedId((int) preset.recipe.channels + 1, juce::dontSendNotification);
            ditherBox.setSelectedId((int) preset.recipe.dither + 1, juce::dontSendNotification);
            conflictBox.setSelectedId((int) preset.conflicts + 1, juce::dontSendNotification);
            allowClipping.setToggleState(preset.recipe.allowIntegerClipping,
                                         juce::dontSendNotification);
            rebuildSteps();
            return;
        }
}

void BatchProcessingDialog::savePreset()
{
    auto* window = new juce::AlertWindow("Save preset", "Preset name",
                                         juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("name", "Preset " + juce::String(presetBox.getNumItems() + 1));
    window->addButton("Save", 1);
    window->addButton("Cancel", 0);
    window->enterModalState(true, juce::ModalCallbackFunction::create(
        [safe = juce::Component::SafePointer<BatchProcessingDialog>(this), window](int choice)
        {
            if (safe == nullptr || choice != 1) return;
            const auto name = window->getTextEditorContents("name").trim();
            if (name.isEmpty()) return;
            BatchPreset preset;
            preset.name = name;
            preset.recipe = safe->currentRecipe();
            preset.conflicts = safe->conflictPolicy();
            auto result = safe->presetStore.save(preset);
            if (! result.ok && result.needsReplace)
            {
                const auto overwrite = juce::AlertWindow::showOkCancelBox(
                    juce::MessageBoxIconType::QuestionIcon, "Replace preset",
                    "A preset with this name exists. Replace it?", "Replace", "Cancel",
                    safe.getComponent(), juce::ModalCallbackFunction::create(
                        [safe, preset](int confirmed)
                        {
                            if (safe == nullptr || confirmed == 0) return;
                            const auto replaced = safe->presetStore.save(preset, true);
                            if (! replaced.ok)
                                juce::AlertWindow::showMessageBoxAsync(
                                    juce::MessageBoxIconType::WarningIcon,
                                    "Preset not saved", replaced.error);
                            safe->rebuildPresets();
                        }));
                juce::ignoreUnused(overwrite);
                return;
            }
            if (! result.ok)
            {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                       "Preset not saved", result.error);
                return;
            }
            safe->rebuildPresets();
        }), true);
}

void BatchProcessingDialog::deletePreset()
{
    const int selected = presetBox.getSelectedItemIndex();
    if (selected < 0 || selected >= (int) presetIds.size()) return;
    const auto id = presetIds[(size_t) selected];
    {
            const auto result = presetStore.remove(id);
            if (! result.ok)
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                       "Preset not removed", result.error);
            rebuildPresets();
            return;
    }
}

void BatchProcessingDialog::addStep(int type)
{
    if ((int) steps.size() >= maxSteps) return;
    steps.push_back({ true, parseParameters(type, {}) });
    rebuildSteps();
}

void BatchProcessingDialog::moveStep(int index, int delta)
{
    const int target = index + delta;
    if (index < 0 || index >= (int) steps.size() || target < 0 || target >= (int) steps.size()) return;
    steps[(size_t) index] = rows[(size_t) index]->current();
    std::swap(steps[(size_t) index], steps[(size_t) target]);
    rebuildSteps();
}

void BatchProcessingDialog::duplicateStep(int index)
{
    if (index < 0 || index >= (int) steps.size() || (int) steps.size() >= maxSteps) return;
    steps[(size_t) index] = rows[(size_t) index]->current();
    steps.insert(steps.begin() + index + 1, steps[(size_t) index]);
    rebuildSteps();
}

void BatchProcessingDialog::removeStep(int index)
{
    if (index < 0 || index >= (int) steps.size()) return;
    steps.erase(steps.begin() + index);
    rebuildSteps();
}

Recipe BatchProcessingDialog::currentRecipe() const
{
    Recipe recipe;
    recipe.outputs.clear();
    recipe.steps = steps;
    for (size_t i = 0; i < rows.size(); ++i)
        if (i < steps.size()) recipe.steps[i] = rows[i]->current();
    auto depthToEncoding = [](const juce::ComboBox& box)
    {
        switch (box.getSelectedId())
        {
            case 1: return Encoding::sameAsSource;
            case 2: return Encoding::int16;
            case 4: return Encoding::float32;
            default: return Encoding::int24;
        }
    };
    if (wavToggle.getToggleState())
        recipe.outputs.push_back({ Format::wav, depthToEncoding(wavDepth), 5 });
    if (aiffToggle.getToggleState())
        recipe.outputs.push_back({ Format::aiff, depthToEncoding(aiffDepth), 5 });
    if (flacToggle.getToggleState())
        recipe.outputs.push_back({ Format::flac, depthToEncoding(flacDepth),
                                   flacLevel.getSelectedId() });
    const std::array<int, 5> rates { 44100, 48000, 88200, 96000, 192000 };
    const int rateIndex = rateBox.getSelectedId() - 2;
    recipe.sampleRate = rateIndex >= 0 && rateIndex < 5 ? rates[(size_t) rateIndex] : 0;
    recipe.channels = (ChannelMode) juce::jlimit(0, 2, channelBox.getSelectedId() - 1);
    recipe.dither = (DitherMode) juce::jlimit(0, 2, ditherBox.getSelectedId() - 1);
    recipe.clipping = allowClipping.getToggleState() ? ClippingPolicy::clipInteger
                                                     : ClippingPolicy::rejectInteger;
    recipe.allowIntegerClipping = allowClipping.getToggleState();
    return recipe;
}

ConflictPolicy BatchProcessingDialog::conflictPolicy() const
{
    return (ConflictPolicy) juce::jlimit(0, 2, conflictBox.getSelectedId() - 1);
}

Destination BatchProcessingDialog::destination() const
{
    Destination result;
    result.nextToSources = nextToSources.getToggleState();
    result.folder = juce::File(outputFolder.getText());
    return result;
}

int64_t BatchProcessingDialog::expectedOutputs() const
{
    int formats = 0;
    if (wavToggle.getToggleState()) ++formats;
    if (aiffToggle.getToggleState()) ++formats;
    if (flacToggle.getToggleState()) ++formats;
    return (int64_t) formats * (int64_t) batch.sources.size();
}

juce::String BatchProcessingDialog::validationMessage() const
{
    if (batch.sources.empty()) return "No eligible source file";
    const auto recipe = currentRecipe();
    const auto issues = validateRecipe(recipe);
    for (const auto& issue : issues)
        if (! issue.warning) return issue.message;
    if (recipe.outputs.empty()) return "Select at least one output format";
    for (const auto& source : batch.sources)
        for (const auto& output : recipe.outputs)
        {
            Encoding encoding;
            int bits = 0;
            bool floating = false;
            juce::String reason;
            if (! resolveEncoding(output, source, encoding, bits, floating, reason))
                return source.path.getFileName() + ": " + reason;
        }
    if (! nextToSources.getToggleState() && outputFolder.getText().isEmpty())
        return "Choose a destination folder";
    return {};
}

bool BatchProcessingDialog::canStart() const
{
    return ! running && ! finished && validationMessage().isEmpty();
}

void BatchProcessingDialog::setRunning(bool value)
{
    running = value;
    finished = false;
    reportShown = false;
    const bool enabled = ! value;
    for (auto& row : rows) row->setEnabled(enabled);
    juce::Component* controls[] { &addStepButton, &presetBox, &savePresetButton,
        &deletePresetButton, &wavToggle, &aiffToggle, &flacToggle, &wavDepth, &aiffDepth,
        &flacDepth, &flacLevel, &rateBox, &channelBox, &ditherBox, &conflictBox,
        &allowClipping, &nextToSources, &browseFolder, &previewSource, &preparePreview };
    for (auto* control : controls) control->setEnabled(enabled);
    playDry.setEnabled(enabled && dryFile.existsAsFile() && ! previewStale);
    playWet.setEnabled(enabled && wetFile.existsAsFile() && ! previewStale);
    outputFolder.setEnabled(enabled && ! nextToSources.getToggleState());
    startButton.setEnabled(! value && canStart());
    cancelButton.setButtonText(value ? "Cancel job" : "Cancel");
    progressBar.setVisible(value);
    repaint();
}

void BatchProcessingDialog::setProgress(const Progress& progress)
{
    progressValue = progress.fraction;
    progressBar.repaint();
    phase.setText(progress.phase + (progress.current.isNotEmpty() ? "  |  " + progress.current
                                                                  : juce::String()),
                  juce::dontSendNotification);
}

void BatchProcessingDialog::showReport(const Report& result)
{
    latestReport = result;
    finished = true;
    running = false;
    reportShown = true;
    reportLabel.setText("Report", juce::dontSendNotification);
    reportLabel.setVisible(true);
    reportText.setText(BatchService::reportToText(result), false);
    reportText.setVisible(true);
    exportReport.setVisible(true);
    openFolder.setVisible(true);
    scanNewFiles.setVisible(result.completed > 0);
    cancelButton.setButtonText("Close");
    startButton.setEnabled(false);
    phase.setText("Finished: " + juce::String(result.completed) + " completed, "
                      + juce::String(result.failed) + " failed, "
                      + juce::String(result.skipped) + " skipped",
                  juce::dontSendNotification);
    resized();
    repaint();
}

void BatchProcessingDialog::setPreviewReady(const PreviewResult& result, bool stale)
{
    showingPreview = false;
    stale = stale || result.generation != previewGeneration
        || (previewFingerprint.isNotEmpty()
            && recipeFingerprint(currentRecipe()) != previewFingerprint);
    previewStale = stale;
    previewGeneration = result.generation;
    if (! result.ok)
    {
        previewStatus.setText(result.cancelled ? "Preview cancelled"
                                               : "Preview failed: " + result.error,
                              juce::dontSendNotification);
        playDry.setEnabled(false);
        playWet.setEnabled(false);
        return;
    }
    dryFile = result.dry;
    wetFile = result.wet;
    playDry.setEnabled(! stale && dryFile.existsAsFile());
    playWet.setEnabled(! stale && wetFile.existsAsFile());
    previewStatus.setText("Dry " + juce::String(result.dryInfo.frames) + " frames, peak "
        + juce::String(result.dryInfo.peak, 3) + ", DC " + juce::String(result.dryInfo.dc, 5)
        + " | wet " + juce::String(result.wetInfo.frames) + " frames, peak "
        + juce::String(result.wetInfo.peak, 3) + ", DC " + juce::String(result.wetInfo.dc, 5)
        + " | output quantization is not auditioned" + (stale ? "  (settings changed)" : ""),
        juce::dontSendNotification);
}

void BatchProcessingDialog::setPreviewStatus(const juce::String& text)
{
    previewStatus.setText(text, juce::dontSendNotification);
}

void BatchProcessingDialog::startPreview()
{
    if (showingPreview || batch.sources.empty()) return;
    const int index = previewSource.getSelectedId() - 1;
    if (index < 0 || index >= (int) batch.sources.size()) return;
    const auto recipe = currentRecipe();
    if (recipe.outputs.empty())
    {
        previewStatus.setText("Select an output format first", juce::dontSendNotification);
        return;
    }
    const auto message = validationMessage();
    if (message.isNotEmpty())
    {
        previewStatus.setText(message, juce::dontSendNotification);
        return;
    }
    dryFile = juce::File();
    wetFile = juce::File();
    playDry.setEnabled(false);
    playWet.setEnabled(false);
    previewStale = false;
    previewFingerprint = recipeFingerprint(recipe);
    showingPreview = true;
    ++previewGeneration;
    previewStatus.setText("Analyzing the whole source...", juce::dontSendNotification);
    if (onPreparePreview) onPreparePreview(batch.sources[(size_t) index], recipe, previewGeneration);
}

void BatchProcessingDialog::refreshFooter()
{
    if (controlRefresh) return;
    controlRefresh = true;
    if (dryFile.existsAsFile() && previewFingerprint.isNotEmpty()
        && recipeFingerprint(currentRecipe()) != previewFingerprint)
    {
        previewStale = true;
        playDry.setEnabled(false);
        playWet.setEnabled(false);
        previewStatus.setText("Preview settings changed; prepare it again", juce::dontSendNotification);
    }
    const auto message = validationMessage();
    juce::String text = juce::String((int) batch.sources.size()) + " source(s), "
        + juce::String((int) expectedOutputs()) + " expected output(s)";
    if (message.isNotEmpty()) text += "  |  " + message;
    else
    {
        const auto recipe = currentRecipe();
        int64_t estimate = 0;
        for (const auto& source : batch.sources)
            for (const auto& output : recipe.outputs)
            {
                const int rate = recipe.sampleRate > 0 ? recipe.sampleRate : source.sampleRate;
                const int channels = recipe.channels == ChannelMode::mono ? 1
                    : recipe.channels == ChannelMode::stereo ? 2 : source.channels;
                const auto frames = outputFrameEstimate(source.frames, source.sampleRate, rate);
                int bits = 24;
                if (output.encoding == Encoding::int16) bits = 16;
                else if (output.encoding == Encoding::float32) bits = 32;
                else if (output.encoding == Encoding::sameAsSource) bits = source.bits;
                estimate += outputByteEstimate(frames, channels, bits);
            }
        const auto bytes = (double) estimate / (1024.0 * 1024.0);
        text += "  |  about " + juce::String(bytes, 1) + " MiB";
    }
    summary.setText(text, juce::dontSendNotification);
    startButton.setEnabled(canStart());
    updateOutputControls();
    controlRefresh = false;
}

void BatchProcessingDialog::updateOutputControls()
{
    wavDepth.setEnabled(wavToggle.getToggleState());
    aiffDepth.setEnabled(aiffToggle.getToggleState());
    flacDepth.setEnabled(flacToggle.getToggleState());
    flacLevel.setEnabled(flacToggle.getToggleState());
    outputFolder.setEnabled(! nextToSources.getToggleState());
    browseFolder.setEnabled(! nextToSources.getToggleState());
    if (aiffDepth.getSelectedId() == 4) aiffDepth.setSelectedId(3, juce::dontSendNotification);
    if (flacDepth.getSelectedId() == 4) flacDepth.setSelectedId(2, juce::dontSendNotification);
}

void BatchProcessingDialog::timerCallback()
{
    if (! running) refreshFooter();
}

void BatchProcessingDialog::closeDialog()
{
    if (running)
    {
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon,
            "Batch Processing", "A batch job is running. Cancel it and close?",
            "Cancel job and close", "Stay", this,
            juce::ModalCallbackFunction::create(
                [safe = juce::Component::SafePointer<BatchProcessingDialog>(this)](int result)
                {
                    if (safe == nullptr || result == 0) return;
                    if (safe->onCancelJob) safe->onCancelJob();
                    if (safe->onClose) safe->onClose();
                }));
        return;
    }
    if (onClose) onClose();
}
} // namespace sonicator::batch
