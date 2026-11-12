# Disc Extract Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract the samples of a disc image (ISO/CUE/BIN) from the library: right-click the image row,
right-click its sample rows, or drop the file on the window; the WAVs are written, added to the
library, scanned and smart-analysed.

**Architecture:** A new disc branch inside the existing `ExtractService` (same job model, progress,
naming and write path as embedded sources), driven by a synchronous owner of a private
`DiscBrowserService`. The library menu feeds it a small, testable candidate collector; the window
becomes a `FileDragAndDropTarget`; the existing indexer and smart-analysis services run afterwards
through a follow-up queue modelled on `pendingSplitScan`.

**Tech Stack:** C++20, JUCE, Catch2 v3, CMake preset `release` (build in `~/Library/Caches/sonicator-build/release`).

**Spec:** `docs/superpowers/specs/2026-11-12-disc-extract-design.md`.

## Global Constraints

- Images are opened read-only; the original file is never modified or deleted.
- Existing WAVs are never overwritten: `uniqueSampleFile`/`uniqueSampleFolder` allocate free names.
- Output layout is unchanged: `<image stem> samples/001 Name.wav`, one stereo WAV per merged L/R pair.
- Empty and entirely silent samples are skipped and counted; programs/unknown records are not extracted.
- All extraction and post-processing runs off the message thread; Cancel keeps finished WAVs.
- Tests write to temporary catalogs only; the owner's `~/Library/Sonicator/sonicator.sqlite` is never touched.
- The repo is in iCloud: after creating files check `git ls-files | grep -E ' [0-9]+\.'` is empty before committing.
- Build: `cmake --build --preset release`; tests: `~/Library/Caches/sonicator-build/release/tests/SonicatorTests "<tag>"`.

## Review Focus

1. **A stereo pair must yield one stereo WAV**, not two mono files, from both the whole-image and the
   selected-entry paths (Task 1/2 tests).
2. **A failed image must not stop the batch** and must be named in the dialog's problems (Task 2).
3. **Cancel mid-image** must keep finished WAVs, leave no `.part`, and stop before more writes (Task 2).
4. **Post-extract follow-up** must not register an existing folder as a new root and must not scan the
   whole library again (Task 4 tests on a temp catalog).
5. **Drop target** must not swallow ordinary audio files or folders (Task 3 tests).
6. **The owner's catalog** is never a test target (Global Constraints).

---

### Task 0: Branch, spec and plan

- [ ] `git switch -c feature/disc-extract`
- [ ] Commit the spec and this plan: `docs: disc extract spec and plan`

### Task 1: Disc extraction in `ExtractService`

**Files:**
- Create: `core/extract/DiscExtract.h`
- Modify: `core/extract/ExtractService.h`, `core/extract/ExtractService.cpp`
- Test: `tests/TestExtractService.cpp` (new cases), `tests/TestExtractDisc.cpp` (new file)

**Interfaces:**
```cpp
// core/extract/DiscExtract.h
struct DiscExtractEntry { DiscEntryId id, partner; juce::String name; };
struct DiscExtractJob { juce::File image; std::vector<DiscExtractEntry> entries; }; // entries empty = whole image
bool isDiscExtractSource(const juce::File&); // .iso/.img/.bin/.cue that exists; .bin with a sibling .cue is accepted
```
```cpp
// ExtractService: same progress/result types, a second entry point.
bool startDisc(const std::vector<DiscExtractJob>& jobs, const ExtractOptions&);
```
- `run()` dispatches: `discJobs.empty() ? runEmbedded() : runDisc()`; the existing body moves to
  `runEmbedded()` unchanged.
- `runDisc()` owns a local `DiscBrowserService` and a `Completion c`; `waitFor(service, Kind::open|list|decode, c)`
  polls every 10 ms, checks `cancelled.load()`, and fails after 120 s.
- Per job: `requestOpen(image)`; if several data tracks (`status == ambiguous`) fail the job with the
  error text. Whole image → `requestList(root, recursive)`; then entries = `kind == file`,
  `capability` empty. Selected → `job.entries`.
- Per entry, ordinal from 1: `requestDecode(entry.id, entry.name, entry.partner)`; skip when
  `AudioExporter::isEntirelySilent`; destination `uniqueSampleFile(folder, ordinal, name)`;
  `exporter.exportSampleFile(*audio, destination, options.normalize, cancel)`.
- Folder: `uniqueSampleFolder(parent, folderStem(image.getFileNameWithoutExtension()))`,
  `parent = options.sameFolder ? image.getParentDirectory() : options.outputFolder`.
- Cancellation: after each entry and each job; `service.close()` on cancel; the remaining jobs are
  reported as cancelled (same shape as the embedded path).

- [ ] **Step 1: tests.** In `tests/TestExtractDisc.cpp` add a synthetic Akai image builder (copy the
  block/volume/FAT helpers from `tests/TestAkaiFilesystem.cpp`): volume with `BASS` (audible, 150-byte
  header), `TINK` (all zeros), a `PAIR .L/.R` stereo pair, and a program `PROG`.
  Case `[extract][disc] Whole image writes every audible sample`:
  `startDisc({{image, {}}}, sameFolder)` → `waitUntilFinished(60000)`; results size 1, `written == 2`
  (BASS + stereo PAIR), `silent == 1`, folder `<stem> samples`, files `001 BASS.wav` and
  `002 PAIR.wav`; `juce::WavAudioFormat` reads PAIR as 2 channels with the same frames as
  `AkaiFilesystem::decodeSample`; the program is not written.
  Case `[extract][disc] Selected entries extract only those` with `{BASS}` → one file.
  Case `[extract][disc] Cancel keeps finished files` → cancel from a thread after the first write
  (`while (extractor.progress().written == 0 && ...) sleep`) → `written >= 1`, no `*.part`.
  Case `[extract][disc] A broken image is reported and the batch continues` → image file of garbage +
  the good image → 2 results, first failed with a non-empty error.
- [ ] **Step 2:** build, run `[extract][disc]` → every new case fails (no `startDisc`).
- [ ] **Step 3:** implement `DiscExtract.h`, the dispatch split and `runDisc()`.
- [ ] **Step 4:** `[extract][disc]` and `[extract]` pass.
- [ ] **Step 5:** commit `feat(extract): extract the samples of a disc image`.

### Task 2: Candidate collector and menu wiring

**Files:**
- Create: `core/extract/ExtractCandidates.h`, `core/extract/ExtractCandidates.cpp`
- Modify: `ui/MainComponent.h`, `ui/MainComponent.cpp`
- Test: `tests/TestExtractCandidates.cpp`

**Interfaces:**
```cpp
struct ExtractCandidates
{
    std::vector<juce::File> embedded;      // SF2/MOD/XM/S3M
    std::vector<juce::File> images;        // whole-image jobs
    std::vector<DiscExtractJob> entries;   // selected sample rows, grouped by image path
    int sourceCount() const;               // menu count: embedded + images + entry groups
    bool empty() const { return sourceCount() == 0; }
};
ExtractCandidates collectExtractCandidates(const std::vector<SampleRecord>& selection,
                                           const std::function<bool(const juce::String&)>& ignored);
void MainComponent::startExtract(const std::vector<juce::File>& files,
                                 const std::vector<DiscExtractJob>& discJobs = {});
```
- Disc-entry rows (`record.isDiscEntry()`): parse `discEntry`/`discPartner` with `getLargeIntValue`,
  group by `containerPath`, name from `record.fileName`; a record whose `discEntry` is empty or zero is skipped.
- Image rows: `record.filePath` exists and `isDiscExtractSource(file)` → `images`.
- Embedded: existing `embeddedSources(...)` on the selection's existing files.
- `showSampleContextMenu`: use the collector instead of `embeddedSources(existingFiles)`; item 9010 label
  `"Extract samples…"` or `"Extract samples from N item(s)"`; handler calls
  `startExtract(candidates.embedded + candidates.images, candidates.entries)`.
- `startExtract` keeps its guards; the dialog's accept callback calls `startDisc(jobs, options)` when
  `files` is empty, otherwise the existing `start(files, options)`; the dialog receives the image paths
  as `sources` for its title and default folder.

- [ ] **Step 1: tests.** `[extract][candidates]`: a selection with an image row (exists), a disc-entry
  row, a disc-entry row of another image, an ordinary WAV and an ignored row → counts and grouping are
  exact; `isDiscExtractSource` is true for `.cue/.iso/.bin`, false for `.wav` and for a directory.
- [ ] **Step 2:** build → fails to compile (no collector).
- [ ] **Step 3:** implement the collector and wire the menu.
- [ ] **Step 4:** `[extract][candidates]` passes; full suite compiles.
- [ ] **Step 5:** commit `feat(ui): extract from an image or its sample rows`.

### Task 3: Drop an image on the window

**Files:**
- Modify: `ui/MainComponent.h` (inherit `public juce::FileDragAndDropTarget`), `ui/MainComponent.cpp`
- Test: `tests/TestExtractCandidates.cpp` (predicate) and a UI case in `tests/TestDiscBrowserService.cpp`
  or a new `[extract][drop]` case that constructs `MainComponent` with a temp catalog.

**Interfaces:**
- `bool isInterestedInFileDrag(const juce::StringArray& files) const override` → true when any path is a
  `isDiscExtractSource` file; false for folders, `.wav`, empty.
- `void filesDropped(const juce::StringArray&, int, int) override` → `startExtract(images, {})`, deduplicated;
  status message "Extracting samples from N image(s)".

- [ ] **Step 1: test** `[extract][drop]`: `MainComponent` with a temp catalog reports interest for a
  `.cue` path and none for a folder or `.wav`; `filesDropped` with a garbage `.cue` opens no crash and
  leaves the app idle (the Extract dialog is modal, so assert via `isExtractRunning()` false and a status
  message). Skip the modal by calling the predicate and a small `static bool isDiscDropTarget(const juce::StringArray&)` helper.
- [ ] **Step 2:** build → fails.
- [ ] **Step 3:** implement the target and the helper; `filesDropped` reuses `startExtract`.
- [ ] **Step 4:** `[extract][drop]` and `[disc-service]` pass.
- [ ] **Step 5:** commit `feat(ui): drop a disc image on the window to extract it`.

### Task 4: Library, scan and smart analysis after a batch

**Files:**
- Create: `core/extract/ExtractFollowUp.h`, `.cpp`
- Modify: `ui/MainComponent.cpp` (`finishExtract`, timer follow-up), `ui/MainComponent.h`
- Test: `tests/TestExtractFollowUp.cpp`

**Interfaces:**
```cpp
struct ExtractFollowUp { juce::StringArray newRoots; juce::StringArray scanFolders; };
ExtractFollowUp planExtractFollowUp(const juce::StringArray& outputFolders,
                                    const juce::StringArray& existingRoots);
```
- `newRoots`: output folders not inside any existing root (and not equal to one).
- `scanFolders`: every output folder (deduplicated, parents first).
- `finishExtract` collects `result.folder` of every `extracted`/`cancelled` result with `written > 0`
  into `pendingExtractScan`.
- Timer: when extract is idle, indexer idle, smart idle, tagging/autotag/split idle and
  `pendingExtractScan` is not empty → plan, `writeRepo->addRoot` for `newRoots`, refresh the folder tree,
  `indexer->startFoldersNewOnly(scanFolders)`; on success remember `pendingExtractSmart = scanFolders`,
  else keep the queue.
- When the indexer finishes and smart is idle with `pendingExtractSmart` → pop one folder,
  `smart->startFolder(folder)`; empty queue when done. Status lines "Indexing extracted samples…",
  "Analysing extracted samples…".

- [ ] **Step 1: tests** `[extract][follow-up]`: planner — a folder inside a root is only scanned, an
  outside folder is a new root and scanned, duplicates collapse.
- [ ] **Step 2:** build → fails.
- [ ] **Step 3:** implement the planner and the timer chain.
- [ ] **Step 4:** `[extract][follow-up]` passes; the full suite passes.
- [ ] **Step 5:** commit `feat(extract): add extracted samples to the library, scan and analyse them`.

### Task 5: Real image, user path and finish

**Files:**
- Test: `tests/TestExtractDisc.cpp` (opt-in case)
- Modify: `docs/DISC_IMAGES.md` (Extract section), `docs/ARCHITECTURE.md` if it lists the extract flows
- Test helper: reuse `openImageBytes` logic or extract it into `core/disc/` if the duplication is ugly

- [ ] **Step 1:** opt-in `[.extract-corpus]` with `SONICATOR_DISC_IMAGE`: `startDisc` with `{whole image}`
  into a temp folder, cancel after 5 files; read the 5 WAVs back and compare int16 with
  `readAkaiPcm16` from the same entries; image size/mtime/SHA-256 unchanged.
- [ ] **Step 2:** run it on `TZRMRM1AK.cue` → PASS; record counts for the report.
- [ ] **Step 3:** docs: how Extract works for images, naming, what is skipped, what happens after.
- [ ] **Step 4:** `graphify update .`, commit `docs(disc): image extract`.
- [ ] **Step 5:** finish: full `ctest`, merge to `main` (ff), push, delete the branch, report.
