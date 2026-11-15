#pragma once
#include "core/batch/BatchPresetStore.h"
#include "core/batch/BatchPreviewService.h"
#include "core/batch/BatchTypes.h"
#include "ui/shell/DialogSurface.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>

namespace sonicator::batch
{
class StepRow;

// Configuration, progress and report for one batch job. The dialog never writes audio, never
// touches the catalog and never creates an audio device: the owner runs BatchService and the
// preview, and pushes progress/results back through the setters.
class BatchProcessingDialog final : public juce::Component, private juce::Timer
{
public:
    BatchProcessingDialog(PreparedBatch, juce::File presetFile);
    ~BatchProcessingDialog() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    Recipe currentRecipe() const;
    ConflictPolicy conflictPolicy() const;
    Destination destination() const;
    int64_t expectedOutputs() const;
    juce::String validationMessage() const;
    bool canStart() const;
    void setRunning(bool);
    bool isRunning() const { return running; }
    void setProgress(const Progress&);
    void showReport(const Report&);
    const Report& report() const { return latestReport; }
    void setPreviewReady(const PreviewResult&, bool stale);
    void setPreviewStatus(const juce::String&);
    const std::vector<SourceSnapshot>& sources() const { return batch.sources; }
    const PreparedBatch& prepared() const { return batch; }
    const SourceSelection& selection() const { return batch.request.sources; }
    std::function<void()> onStart, onCancelJob, onClose;
    std::function<void(SourceSnapshot, Recipe, int)> onPreparePreview;
    std::function<void(juce::File, bool processed)> onPlayPreview;
    std::function<void()> onStopPreview;
    std::function<void()> onOpenOutputFolder, onExportReport, onScanNewFiles;
    static juce::String formatSummary(const PreparedBatch&);
private:
    void timerCallback() override;
    void rebuildSteps();
    void rebuildPresets();
    void refreshFooter();
    void updateOutputControls();
    void loadPreset(const juce::String& id);
    void savePreset();
    void deletePreset();
    void addStep(int type);
    void moveStep(int index, int delta);
    void duplicateStep(int index);
    void removeStep(int index);
    void startPreview();
    void closeDialog();
    dialogs::Surface surface;
    PreparedBatch batch;
    BatchPresetStore presetStore;
    std::vector<ProcessStep> steps;
    std::vector<juce::String> presetIds;
    std::vector<std::unique_ptr<StepRow>> rows;
    juce::Label title, summary, phase, reportLabel, previewLabel, destinationLabel;
    juce::Viewport stepsViewport;
    juce::Component stepsContent;
    juce::TextButton addStepButton { "Add step" }, presetBoxButton { "Preset..." };
    juce::ComboBox presetBox;
    juce::TextButton savePresetButton { "Save preset" }, deletePresetButton { "Delete preset" };
    juce::ToggleButton wavToggle { "WAV" }, aiffToggle { "AIFF" }, flacToggle { "FLAC" };
    juce::ComboBox wavDepth, aiffDepth, flacDepth, flacLevel, rateBox, channelBox, ditherBox, conflictBox;
    juce::ToggleButton allowClipping { "Allow integer clipping (reported)" };
    juce::ToggleButton nextToSources { "Save next to originals" };
    juce::TextEditor outputFolder;
    juce::TextButton browseFolder { "Browse..." };
    juce::ComboBox previewSource;
    juce::TextButton preparePreview { "Prepare preview" }, playDry { "Original" },
        playWet { "Processed" }, stopPreview { "Stop" };
    juce::Label previewStatus;
    juce::ProgressBar progressBar;
    double progressValue = 0.0;
    juce::TextEditor reportText;
    juce::TextButton exportReport { "Export report..." }, openFolder { "Open output folder" },
        scanNewFiles { "Scan new files" }, startButton { "Start" }, cancelButton { "Cancel" };
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<Issue> previewWarnings;
    juce::File dryFile, wetFile;
    Report latestReport;
    int previewGeneration = 0;
    bool running = false, finished = false, reportShown = false, controlRefresh = false;
    bool showingPreview = false, previewStale = false;
    juce::String previewFingerprint;
};
} // namespace sonicator::batch
