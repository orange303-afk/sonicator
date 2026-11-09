# Extract Embedded Samples Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an "Extract samples…" library action that writes every embedded, non-empty, non-silent sample of tracker modules (MOD/XM/S3M) and SoundFont banks (SF2) as WAV files.

**Architecture:** A `core/extract` module lists and decodes embedded samples behind the existing `TrackerDocument` and `Sf2Bank` readers, a `core/export` writer emits one WAV per sample with optional peak normalisation, and a background `ExtractService` mirrors `SplitService`. A `ui/extract/ExtractDialog` owns the setup, progress and report states, launched from the sample context menu in `MainComponent`.

**Tech Stack:** C++20, JUCE 9, Catch2, CMake.

**Spec:** This plan is the approved design from the session (2026-11-09): embedded-only sources, task-per-sample WAV export, optional normalize to −0.1 dBFS, same-folder or chosen folder, `<name> samples` subfolders, progress in the dialog, short report with details.

## Global Constraints

- Only formats whose audio is packed inside the source file: MOD, XM, S3M, SF2. EXS/NKI and external dependencies are excluded.
- Extract all non-empty samples, including SF2 samples unused by presets. Skip empty slots and samples whose PCM is entirely zero (checked before normalisation). Keep quiet but non-zero samples.
- Stereo pairs count as one source and are written as one stereo WAV. Trackers are mono.
- Normalize is off by default; when on, peak-normalise each sample independently to −0.1 dBFS with one gain for both channels. Zero audio stays unchanged (it is skipped anyway).
- Output: WAV, no effects and no transposition. Names `001 Name.wav`, fallback `001 Sample.wav`. Never overwrite; allocate a numeric folder suffix.
- One background job at a time; the dialog stays open, reports progress, and ends with OK. Cancel keeps finished WAVs and removes the partial file.
- Tests must be green before any commit: `ctest --test-dir build --output-on-failure`.

## Review Focus

- A source file that cannot be opened: the batch continues and the report names it.
- An entry that fails to decode: count it as an error, keep extracting siblings.
- A destination path that cannot be created or written: report per file, never crash.
- Cancelling mid-file: no `.part` file remains and finished WAVs survive.
- A sample name with path separators, wildcards or non-ASCII characters: the produced filename stays inside the destination folder.

---

### Task 1: Embedded sample listing and decoding

**Files:**
- Create: `core/extract/EmbeddedSamples.h`, `core/extract/EmbeddedSamples.cpp`
- Create: `tests/TestEmbeddedSamples.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `struct EmbeddedSampleEntry { juce::String name; int index = -1; };`
- Produces: `bool isEmbeddedSampleSource(const juce::File&);`
- Produces: `class EmbeddedSampleSet { bool open(const juce::File&, juce::String&); const std::vector<EmbeddedSampleEntry>& entries() const; SampleAudioPtr read(int index, juce::String&) const; };`
- Tracker entries skip `empty` and `fmInstrument` slots. SF2 entries skip right-channel headers (`sampleType == 2`).

- [ ] **Step 1: Write failing tests for listing** — a MOD with one sample lists `sine`; an SF2 with a used and an unused sample lists both; a stereo pair lists one entry; an empty tracker slot is not listed; a non-embedded file is rejected.
- [ ] **Step 2: Run `ctest` and watch them fail to compile/fail.**
- [ ] **Step 3: Implement `EmbeddedSampleSet`** dispatching on `.mod/.xm/.s3m` (via `TrackerDocument::open`) and `.sf2` (via `Sf2Bank::load`).
- [ ] **Step 4: Verify tests pass and the full suite is green.**
- [ ] **Step 5: Commit.**

### Task 2: WAV writer with silence detection and normalisation

**Files:**
- Modify: `core/export/AudioExporter.h`, `core/export/AudioExporter.cpp`
- Modify: `tests/TestExport.cpp`

**Interfaces:**
- Produces: `bool isEntirelySilent(const SampleAudio&);` (core/export or core/extract)
- Produces: `ExportResult AudioExporter::exportSampleFile(const SampleAudio&, const juce::File& destination, bool normalize, CancelCheck) const;`
- Preserves the existing bit-depth policy: 16-bit source → 16-bit, otherwise 24-bit; float → 32-bit float.
- Destination must not exist; writes `<destination>.part`, validates with `validateWavOutput`, then moves into place. Deletes the part on failure or cancel.

- [ ] **Step 1: Write failing tests** — all-zero mono/stereo is silent; a tiny non-zero sample is not; normalised peak reads back at −0.1 dBFS within 16-bit tolerance; stereo channels keep their ratio; non-normalised output preserves amplitude; an existing destination is refused.
- [ ] **Step 2: Run and watch fail.**
- [ ] **Step 3: Implement writer and peak normalisation** using `AudioFormatWriter` chunked writes like `exportSampleSelection`.
- [ ] **Step 4: Verify tests pass and suite is green.**
- [ ] **Step 5: Commit.**

### Task 3: Background extract service

**Files:**
- Create: `core/extract/ExtractService.h`, `core/extract/ExtractService.cpp`
- Create: `tests/TestExtractService.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `struct ExtractOptions { juce::File outputFolder; bool sameFolder = true; bool normalize = false; };`
- Produces: `enum class ExtractFileState { extracted, failed, cancelled };`
- Produces: `struct ExtractFileResult { juce::File source, folder; int written, empty, silent, failed; ExtractFileState state; juce::String error; juce::StringArray problems; };`
- Produces: `struct ExtractProgress { bool running, cancelled; int totalFiles, processedFiles, totalSamples, processedSamples, written, skippedEmpty, skippedSilent, failed; double fraction; juce::String current; };`
- Produces: `class ExtractService final : private juce::Thread { bool start(const std::vector<juce::File>&, const ExtractOptions&); void cancel(); ExtractProgress progress() const; std::vector<ExtractFileResult> results() const; bool waitUntilFinished(int); };`
- Folder per source: `<stem> samples`, then `<stem> samples 2`, `3`, … when taken.

- [ ] **Step 1: Write failing tests** — extracts a MOD and an SF2 into separate subfolders; same-folder mode writes next to the source; a second run creates a suffixed folder; empty and silent entries increment their counters without files; cancel stops the batch and leaves the first WAV; mixing an unsupported file records a failure without stopping.
- [ ] **Step 2: Run and watch fail.**
- [ ] **Step 3: Implement the service** following `SplitService`'s mutex/atomic/thread shape.
- [ ] **Step 4: Verify tests pass and suite is green.**
- [ ] **Step 5: Commit.**

### Task 4: Dialog, settings and context menu

**Files:**
- Create: `ui/extract/ExtractDialog.h`, `ui/extract/ExtractDialog.cpp`
- Modify: `core/settings/AppSettings.h`, `core/settings/AppSettings.cpp`
- Modify: `ui/MainComponent.h`, `ui/MainComponent.cpp`
- Modify: `tests/TestUI.cpp`, `CMakeLists.txt`

**Interfaces:**
- `ExtractDialog(std::vector<juce::File> sources, const AppSettings&, std::function<bool(ExtractOptions)>)`; `currentOptions()`, `enterRunningState()`, `setProgress(const ExtractProgress&, double)`, `showFinished(const juce::String& summary, const juce::String& details, double fraction)`, `isRunning()`, `isFinished()`, `onCancel`, `onOk`.
- Component IDs: `extractFolder`, `extractBrowse`, `extractSameFolder`, `extractNormalize`, `extractAccept`, `extractCancel`, `extractOk`, `extractProgress`, `extractDetails`.
- Settings: `bool extractSameFolder = true; bool extractNormalize = false; juce::String extractOutputFolder;`
- Context menu item id `9010`, label `Extract samples…` (or `Extract samples from N files…`), enabled only when at least one selected file is an embedded source.
- MainComponent members mirror split: `extractor`, `extractDialog`, `extractContent`, `extractStartMs`, `extractWasRunning`, `startExtract`, `finishExtract`, `closeExtractDialog`, `requestCloseExtract`, `isExtractRunning`.

- [ ] **Step 1: Write failing UI tests** — dialog defaults and IDs; Extract/Cancel become OK after completion; details toggle reveals the problems editor.
- [ ] **Step 2: Run and watch fail.**
- [ ] **Step 3: Implement the dialog and wire the menu/timer/destructor**, using `SplitDialogWindow` as the model.
- [ ] **Step 4: Verify tests pass and suite is green.**
- [ ] **Step 5: Commit.**

### Task 5: Manual verification and graph refresh

- [ ] Build `Sonicator` and `SonicatorTests`, run the full suite.
- [ ] Extract a real SF2 and a real module through the UI; verify folder naming, normalize on/off, cancel and report.
- [ ] Run `graphify update .`.
- [ ] Merge the branch into `main`, verify the suite on the merged result, push, delete the branch.
