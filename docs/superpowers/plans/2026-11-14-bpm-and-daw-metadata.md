# Manual BPM and drag metadata implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (chosen method) to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a person set or tap a BPM per sample, store it as a manual override that survives analysis and reaches contribution, and give dragged files `acid`/`smpl` metadata so DAWs see the tempo and root.

**Architecture:** A pure `TapTempo` counter and a `rootMidiFromKey` helper in `core/smart`; contribution SQL prefers the BPM override; a `core/export/DragMetadata` module byte-copies WAVs and splices RIFF `acid`/`smpl` chunks (encoding non-WAV sources to WAV first); `FileListPanel` supplies per-path metadata; `TapTempoDialog` owns the tap UI with Space/Return/Esc; `MainComponent` adds the BPM menu and writes overrides.

**Tech Stack:** C++20, JUCE 9.0.5, SQLite catalog, Catch2, CMake presets.

**Spec:** `docs/superpowers/specs/2026-11-14-bpm-and-daw-metadata-design.md`

## Global Constraints

- No new dependencies; JUCE and existing modules only.
- Every test writes only under `juce::File::getSpecialLocation(juce::File::tempDirectory)` via `TestDirectory`. Never the live catalog or `settings.json`.
- Build gate: `cmake --build --preset release --target Sonicator SonicatorTests` (both targets), then tests with `~/Library/Caches/sonicator-build/release/tests/SonicatorTests "<filter>"`.
- 4-space indent, `namespace sonicator`, no `using namespace` in headers.
- New core sources go into the `SonicatorCore` source list in `CMakeLists.txt`; new tests into `tests/CMakeLists.txt` with `target_sources(SonicatorTests PRIVATE ...)`.
- Before each commit: `git ls-files | grep -E ' [0-9]+\.'` must print nothing.

## Review Focus

- ACID tempo written for a 128 BPM 2-second loop: our reader and JUCE see the same float, and the chunk appears once.
- Tapping at 120 BPM with one wildly late tap: the value stays near 120, not pulled by the outlier.
- A manual BPM on a tagged sample: contribution returns it, `hasBpm` is true, and clearing reverts to the machine value.
- Dragging an MP3 or AIFF with a known BPM: the cache holds a decodable WAV with `acid`, and the original is untouched.
- Space in the tap dialog taps instead of starting playback; Esc applies nothing.

---

### Task 1: TapTempo counter and key root helper

**Files:**
- Create: `core/smart/TapTempo.h`, `core/smart/TapTempo.cpp`
- Create: `tests/TestTapTempo.cpp`
- Modify: `core/smart/KeyNames.h`, `core/smart/KeyNames.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces:
  ```cpp
  namespace sonicator {
  class TapTempo
  {
  public:
      static constexpr int maxIntervals = 8;
      static constexpr int64_t resetAfterMs = 3000;
      void tap(int64_t timeMs);
      void reset();
      bool hasValue() const;      // at least two valid intervals
      double bpm() const;         // 0 when !hasValue
      int tapCount() const;       // taps since the last reset
  };
  // C4 = 60; accepts "C", "C#", "F#", "Bb", "A minor"; -1 when unknown.
  int rootMidiFromKey(const juce::String& key);
  }
  ```

- [ ] **Step 1: Write failing tests** in `tests/TestTapTempo.cpp`, tag `[bpm]`:
  - `"Taps at 120 BPM give 120"`: taps at 0, 500, 1000, 1500 → `bpm()` approx 120 (margin 0.5), `hasValue()`.
  - `"A late tap is dropped as an outlier"`: 0, 500, 1000, 2100, 2600 → bpm within 120 ± 15.
  - `"Two taps are not enough"`: two taps → `!hasValue()`, `bpm() == 0`.
  - `"A three second pause resets the taps"`: 0, 500, then 4000 → `tapCount() == 1`, `!hasValue()`.
  - `"Jitter stays close"`: intervals 495/505/497/503 → 120 ± 2.
  - `"Key names map to root notes"`: `"C"` → 60, `"C#"` → 61, `"F#"` → 66, `"Bb"` → 70, `"A minor"` → 69, `""` → -1, `"?"` → -1.

- [ ] **Step 2: Run tests to verify failure** — compile error, symbols missing.

- [ ] **Step 3: Implement**

  TapTempo keeps a `std::vector<int64_t> taps` capped at `maxIntervals + 1`; `tap` resets first when the gap exceeds `resetAfterMs`, then pushes. `bpm()` builds consecutive intervals, takes the median (sort a copy), keeps intervals within ±25 % of it, requires at least two, returns `60000.0 / mean`. `rootMidiFromKey` takes the text before the first space, maps `C, C#, Db, D, ... B` to 0..11, returns `60 + index` or -1.

- [ ] **Step 4: Run tests to verify pass** — `SonicatorTests "[bpm]"`.

- [ ] **Step 5: Commit** — `feat(bpm): tap tempo counter and key root mapping`.

---

### Task 2: Manual BPM reaches contribution

**Files:**
- Modify: `core/database/SampleRepository.cpp` (`contributionRecords` SQL)
- Create: `tests/TestBpmOverride.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `setSmartOverride`, `clearSmartOverride`, `recordFeedback`, `touchFeedback`, `contributionRecords`.
- Produces: no new API; `ContributionRecord.bpm`/`hasBpm` prefer the `bpm` override.

- [ ] **Step 1: Write failing tests** in `tests/TestBpmOverride.cpp`, tag `[bpm][catalog]`:
  - `"A manual BPM overrides the analysed tempo in contribution"`: upsert a WAV row, `recordFeedback(id, "loop", {"Loop"}, {…14 floats…}, user)`, `setSmartOverride(id, SmartField::Bpm, { .numericValue = 128.0 })`; `contributionRecords(0, 10)` has one record with `bpm == 128`, `hasBpm`.
  - `"Clearing the manual BPM restores the machine tempo"`: after the first case, `clearSmartOverride(id, SmartField::Bpm)`, `touchFeedback({id})`; the record has `hasBpm == false` (no smart results row).
  - `"A manual BPM does not create feedback for an unlabelled sample"`: no `recordFeedback`; set override; `contributionRecords(0, 10)` is empty.

- [ ] **Step 2: Run to verify failure** — the first test fails with `bpm == 0`.

- [ ] **Step 3: Implement**

  In the `contributionRecords` SELECT replace `r.bpm, r.has_bpm` with:
  ```sql
  COALESCE((SELECT o.numeric_value FROM smart_overrides o
            WHERE o.sample_id=f.sample_id AND o.field='bpm'
              AND o.cleared=0 AND o.not_applicable=0 AND o.numeric_value>0), r.bpm),
  (EXISTS(SELECT 1 FROM smart_overrides o WHERE o.sample_id=f.sample_id AND o.field='bpm'
            AND o.cleared=0 AND o.not_applicable=0 AND o.numeric_value>0) OR r.has_bpm=1)
  ```
  Column indexes stay unchanged.

- [ ] **Step 4: Run to verify pass** — `SonicatorTests "[bpm][catalog]"`.

- [ ] **Step 5: Commit** — `feat(bpm): contribution prefers the manual tempo`.

---

### Task 3: RIFF chunk utilities and chunk builders

**Files:**
- Create: `core/export/RiffChunks.h`, `core/export/RiffChunks.cpp`
- Create: `tests/TestRiffChunks.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `juce::File`, `juce::MemoryBlock`.
- Produces:
  ```cpp
  namespace sonicator::riff {
  // Rewrites the file in place: removes every existing chunk with `id`, then appends the new one.
  // All other chunks and their bytes are preserved; RIFF sizes are fixed up.
  bool insertOrReplaceChunk(const juce::File& wav, const char* id, const juce::MemoryBlock& payload);
  bool removeChunk(const juce::File& wav, const char* id);
  // 24-byte ACID payload. rootMidi < 0 writes flags without the root bit.
  juce::MemoryBlock buildAcid(double bpm, int beats, int rootMidi);
  // 36-byte smpl payload with the unity note and no loops, or the existing payload with the note
  // replaced when `existing` is not empty.
  juce::MemoryBlock buildSmplRoot(int rootMidi, const juce::MemoryBlock& existing = {});
  }
  ```

- [ ] **Step 1: Write failing tests** in `tests/TestRiffChunks.cpp`, tag `[bpm][riff]`:
  - `"An acid chunk round-trips through our reader"`: write a WAV with JUCE, `buildAcid(128.0, 4, 60)`, insert; `extractWavMetadata` returns a `SmartField::Bpm` candidate with `numericValue == 128` and a `SmartField::RootNote` candidate with 60.
  - `"Replacing acid leaves a single chunk"`: insert twice with 128 then 90; scan the chunk list (test helper reading ids) and REQUIRE one `acid`; reader says 90.
  - `"Inserting a chunk preserves the audio bytes"`: read `data` chunk bytes before/after; identical.
  - `"buildSmplRoot keeps an existing loop"`: take an existing 36+24-byte smpl payload with one loop, `buildSmplRoot(48, existing)` → payload length unchanged, byte 12 (unity) is 48, the loop bytes are intact.
  - `"removeChunk drops only the requested chunk"`: remove `acid`, other ids unchanged.

- [ ] **Step 2: Run to verify failure** — compile error.

- [ ] **Step 3: Implement**

  Read the file into a `MemoryBlock`, walk `RIFF` > chunks `(id, size, payload, pad)`, copy all chunks except the target id, then append `(id, size, payload, pad)`; write `"RIFF"` + total size + `"WAVE"` + chunks via a temporary file and atomic move. ACID payload: little-endian, flags = 0x02 when `rootMidi >= 0`, root at 4 (uint16), beats at 12 (uint32), denominator 4 at 16, numerator 4 at 18, tempo float at 20, trailing uint32 0. smpl payload: 36 bytes, unity note at 12, `numSampleLoops` at 28 = 0; with `existing`, copy it and patch bytes 12..15.

- [ ] **Step 4: Run to verify pass** — `SonicatorTests "[bpm][riff]"`.

- [ ] **Step 5: Commit** — `feat(drag): RIFF chunk helpers for acid and smpl`.

---

### Task 4: Materialising drag copies with metadata

**Files:**
- Create: `core/export/DragMetadata.h`, `core/export/DragMetadata.cpp`
- Create: `tests/TestDragMetadata.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `riff::*` (Task 3), `registerSonicatorFormats`, `AudioExporter::validateWavOutput`, `CancelCheck`.
- Produces:
  ```cpp
  namespace sonicator {
  struct DragMetadata { bool hasBpm = false; double bpm = 0; bool hasRoot = false; int rootMidi = -1; };
  bool hasDragMetadata(const DragMetadata&);
  // Source path when there is nothing to write; otherwise `<cacheDir>/<hash>/<stem>.wav`.
  juce::File dragCopyPath(const juce::File& source, const DragMetadata&, const juce::File& cacheDir);
  // Returns the source unchanged when there is nothing to write. WAV is byte-copied and spliced;
  // other readable formats are decoded to WAV first. `error` is set on failure and the result is
  // an invalid File.
  juce::File materialiseWithMetadata(const juce::File& source, const DragMetadata&,
                                     const juce::File& cacheDir, juce::String& error);
  }
  ```

- [ ] **Step 1: Write failing tests** in `tests/TestDragMetadata.cpp`, tag `[bpm][drag]`:
  - `"A WAV with BPM gets an acid chunk and keeps its audio"`: 2 s 16-bit WAV; materialise with BPM 128, no root; result path ≠ source, inside cache; `extractWavMetadata` sees 128 BPM; source bytes unchanged; PCM bytes equal.
  - `"BPM only writes no smpl chunk"`: chunk-id scan of the copy has no `smpl`.
  - `"Root only writes smpl and no acid"`: `hasRoot = true, rootMidi = 60`; ids contain `smpl`, not `acid`.
  - `"An AIFF with BPM becomes a decodable WAV"`: AIFF fixture; materialise; result ends `.wav`, opens with `juce::WavAudioFormat`, `extractWavMetadata` sees the BPM.
  - `"No metadata returns the source path"`: `materialiseWithMetadata(source, {}, cache, error) == source`.
  - `"A repeated call reuses the cache file"`: two calls; same path, mtime unchanged.
  - `"An unreadable file reports an error"`: text file with `.wav` extension → invalid File, `error` non-empty.

- [ ] **Step 2: Run to verify failure** — compile error.

- [ ] **Step 3: Implement**

  `dragCopyPath` hashes `path | mtime | size | bpm | root <n>` with SHA-256, uses `<hash>/<stem>.wav` (stem sanitized with `AudioExporter::sanitizeStem`). `materialiseWithMetadata` returns source when `!hasDragMetadata`. Reuse an existing destination whose mtime is not older than the source. WAV/WAVE: `copyFileTo`; AIFF/MP3/OGG/others: `AudioFormatManager` reader, write WAV (32-bit float when the source is float, else 24-bit) with no metadata. Then, if `hasBpm`, `insertOrReplaceChunk(acid, buildAcid(bpm, round(duration*bpm/60), rootMidi))`; if `hasRoot`, replace/insert `smpl` via `buildSmplRoot` (read the existing payload first). Delete a partially written copy on failure and return an invalid file.

- [ ] **Step 4: Run to verify pass** — `SonicatorTests "[bpm][drag]"`.

- [ ] **Step 5: Commit** — `feat(drag): WAV copies with tempo and root metadata`.

---

### Task 5: Tap tempo dialog

**Files:**
- Create: `ui/library/TapTempoDialog.h`, `ui/library/TapTempoDialog.cpp`
- Create: `tests/TestTapTempoDialog.cpp`
- Modify: `CMakeLists.txt` (`SonicatorUI`), `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `TapTempo` (Task 1).
- Produces:
  ```cpp
  namespace sonicator {
  class TapTempoDialog final : public juce::Component
  {
  public:
      explicit TapTempoDialog(juce::String titleText);
      std::function<void()> onStartLoop, onStopLoop, onCancel;
      std::function<void(double bpm)> onApply;
      void tap();
      bool keyPressed(const juce::KeyPress&) override;
      double currentBpm() const;
      // For the owner: opens the dialog window and wires the callbacks.
      static void launch(juce::Component* owner, juce::String titleText,
                         std::function<void()> startLoop, std::function<void()> stopLoop,
                         std::function<void(double)> apply);
  };
  }
  ```
  Keyboard: Space taps, Return applies, Esc cancels. Buttons do not take keyboard focus so Space never clicks them.

- [ ] **Step 1: Write failing tests** in `tests/TestTapTempoDialog.cpp`, tag `[bpm][ui]`:
  - `"Space taps and not playback"`: create the component, call `keyPressed(space)` three times with simulated `Time` gaps → `currentBpm() > 0`; no loop callbacks fired.
  - `"Return applies the current value"`: after taps, `keyPressed(returnKey)` → `onApply` got the value.
  - `"Escape cancels without applying"`: `onCancel` fired, `onApply` not.
  - `"Apply does nothing before three taps"`: `keyPressed(returnKey)` → `onApply` not called.

- [ ] **Step 2: Run to verify failure** — compile error.

- [ ] **Step 3: Implement**

  Layout: title label, BPM label (large), `Tap` button (calls `tap()`), `Start loop` toggle button (calls `onStartLoop`/`onStopLoop`), `Apply`, `Cancel`. `tap()` uses `juce::Time::getMillisecondCounter()` and updates the label; Apply is enabled only when `TapTempo::hasValue()`. Space/Return/Esc in `keyPressed`. `setWantsKeyboardFocus(true)`; buttons `setWantsKeyboardFocus(false)`. `launch` uses `juce::DialogWindow::LaunchOptions` with a content component, sets `onCancel` to close, `onApply` to close after applying, and `startLoop` optional (owner passes an empty function for a batch).

- [ ] **Step 4: Run to verify pass** — `SonicatorTests "[bpm][ui]"`.

- [ ] **Step 5: Commit** — `feat(bpm): tap tempo dialog with space, return and escape`.

---

### Task 6: BPM menu, override writes and loop for tapping

**Files:**
- Modify: `ui/MainComponent.h`, `ui/MainComponent.cpp`
- Modify: `ui/preview/PreviewPanel.h`, `ui/preview/PreviewPanel.cpp`
- Modify: `tests/TestBpmOverride.cpp` (integration of the write path)

**Interfaces:**
- Consumes: `TapTempoDialog` (Task 5), `setSmartOverride`, `clearSmartOverride`, `touchFeedback`.
- Produces on `PreviewPanel`:
  ```cpp
  void startTapLoop();   // loops the waveform selection, else the whole source
  void stopTapLoop();
  bool canLoopForTap() const;   // false while the panel shows a batch
  ```
  On `MainComponent`: `void setBpmForRows(const std::vector<int64_t>&, double bpm)`,
  `void clearBpmForRows(const std::vector<int64_t>&)`.

- [ ] **Step 1: Write failing tests** in `tests/TestBpmOverride.cpp`, tag `[bpm][catalog]`:
  - `"Setting a BPM writes an override that the list can read"`: `setBpmForRows` is on MainComponent, which needs a full UI; test the same effect at the repository level: `setSmartOverride` then `smartDisplays({id})` shows `bpmOverride == OverrideState::Value` and `bpm == 128`. (This pins the display contract the menu relies on.)

- [ ] **Step 2: Run to verify failure** — `smartDisplays` returns no override.

- [ ] **Step 3: Implement**

  - Context menu: add a `BPM` submenu near the top in `showSampleContextMenu`:
    `Set BPM...` (id 25001), `Tap BPM...` (25002), `Clear manual BPM` (25003, enabled when any selected row has a BPM override). Handler: Set opens `dialogs::showPrompt` (title "Set BPM", label "Tempo (20 to 300)", initial current value or "120"); invalid input writes an error to `status`; valid calls `setBpmForRows`.
  - `setBpmForRows`: `SmartOverride o; o.numericValue = bpm;` for each id `writeRepo->setSmartOverride(id, SmartField::Bpm, o)`; `writeRepo->touchFeedback(ids)`; `reloadLibrary()`; status "BPM 128 set for N sample(s)"; keep the selection via existing reload behavior.
  - `clearBpmForRows`: `clearSmartOverride(id, SmartField::Bpm)` for each; `touchFeedback(ids)`; reload; status.
  - Tap: `TapTempoDialog::launch(this, title, canLoop ? [this]{ preview.startTapLoop(); } : std::function<void()>{}, same for stop, [ids](double bpm){ setBpmForRows(ids, bpm); })`. Title: `sample.fileName` for one row, `N samples` for a batch. Loop callbacks only for a single selected row (`library.getSelectedRowCount() == 1`).
  - `PreviewPanel::startTapLoop`: force `loop` on (`setToggleState(true, sendNotification)`), `applyTransportLooping()`, and if a selection is valid set the engine selection loop (already done through `updateSelectionPlayback`) then `engine.play()`; otherwise `engine.play()` on the whole source. `stopTapLoop`: `engine.stop()`. `canLoopForTap`: `loadedFile.existsAsFile() || engine.length() > 0`.
  - `preview.stopTapLoop()` when the dialog closes.

- [ ] **Step 4: Run to verify pass** — `SonicatorTests "[bpm]"`.

- [ ] **Step 5: Commit** — `feat(bpm): set, tap and clear BPM from the sample menu`.

---

### Task 7: Drag pipeline carries per-file metadata

**Files:**
- Modify: `ui/library/FileListPanel.h`, `ui/library/FileListPanel.cpp`
- Modify: `tests/TestDragMetadata.cpp` (path→metadata mapping)

**Interfaces:**
- Consumes: `DragMetadata`, `materialiseWithMetadata`, `SmartDisplay`, `smartDisplays`.
- Produces on `core/export/DragMetadata.h`:
  ```cpp
  // Manual or resolved BPM and key root from one row; conflicting/ambiguous/stale values are
  // dropped. A visible row's hidden stereo partner receives the same DragMetadata.
  DragMetadata dragMetadataForDisplay(const SmartDisplay&);
  ```
  `FileListPanel` uses it while building the drag path map.

- [ ] **Step 1: Write failing tests** (in `tests/TestDragMetadata.cpp`, tag `[bpm][drag]`):
  - `"A stereo partner inherits the visible row metadata"`: build `FileListPanel`, `setStereoPairs({{1, "/x/R.wav"}})`, `setSmartDisplays({{1, display}})` with resolved BPM/key, `smartDisplay` fixture; call a pure helper
    `DragMetadata dragMetadataForDisplay(const SmartDisplay&)` (expose it in `core/export/DragMetadata.h` so it is testable) → same values; and for `bpmState == Conflicting` → `hasBpm == false`.
  - `"Only resolved or manual values are written"`: `OverrideState::Value` counts; `bpmState == SmartState::Resolved` counts; `Conflicting`, `Ambiguous`, `Stale`, `Unknown` do not.

- [ ] **Step 2: Run to verify failure** — helper missing.

- [ ] **Step 3: Implement**

  Add `DragMetadata dragMetadataForDisplay(const SmartDisplay&)` in `DragMetadata.cpp`: BPM when `bpmOverride == OverrideState::Value` or `bpmState == SmartState::Resolved` and `hasBpm`; root from `rootMidiFromKey(key)` when `keyOverride == Value` or `keyState == Resolved`.
  In `FileListPanel::handleDrag`, build `std::map<juce::String, DragMetadata>` from the selected records (`recordAt`, `smartDisplays`), give the stereo partner the same entry, then for every non-EBL path call `materialiseWithMetadata` (errors to `problems`) and pass the results to `materialiseForDrag` for EBL handling. Keep the existing message flow.

- [ ] **Step 4: Run to verify pass** — `SonicatorTests "[bpm][drag]"`.

- [ ] **Step 5: Commit** — `feat(drag): attach tempo and root to library drags`.

---

### Task 8: Documentation, graph and full verification

**Files:**
- Modify: `docs/ARCHITECTURE.md`
- Modify: `graphify-out/` (generated, if the tool completes)

- [ ] **Step 1: Document** — add a section on manual BPM (override storage, tap rules, contribution boundary) and drag metadata (RIFF splice, cache layout, which states are written).

- [ ] **Step 2: Full gate** — `cmake --build --preset release --target Sonicator SonicatorTests`, then run the whole `SonicatorTests`; confirm the suite is green. Check `git ls-files | grep -E ' [0-9]+\.'` prints nothing.

- [ ] **Step 3: Graph** — `graphify update .` with a bounded timeout; if it hangs (known iCloud issue), record the deferral in the report.

- [ ] **Step 4: Commit and report** — commit docs; report the manual DAW acceptance step to the owner: drag a known-BPM loop into the DAW and check the tempo/root; DAWs that ignore ACID are a known limitation of the format, not a bug.
