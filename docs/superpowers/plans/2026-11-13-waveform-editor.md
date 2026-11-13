# Waveform Editor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the preview panel's waveform into a small destructive editor: in-memory edits with undo, a right-click context menu, zoom, and Cmd/Ctrl+S saving in place or as a copy, with the catalog kept current.

**Architecture:** Edit logic lives in `core/edit/` as a UI-free buffer (PCM + markers + undo snapshots), a writer (atomic in-place replace, unique copy names, metadata) and a catalog sync helper. The waveform widget gains a view range with zoom and draws edited PCM; `PreviewPanel` owns the edit session, menus and dialogs; `MainComponent` guards file switches and refreshes the catalog. Playback of an edited buffer reuses the existing decoded-sample path (`AudioEngine::setDiscSample`).

**Tech Stack:** C++20, JUCE 9.0.5, SQLite catalog (`SampleRepository`), Catch2 tests, CMake presets.

**Spec:** `docs/superpowers/specs/2026-11-13-waveform-editor-design.md`

## Global Constraints

- No new third-party dependencies; JUCE APIs already in the tree only.
- All new core code goes in `core/edit/` and `core/indexing/`, listed explicitly in `CMakeLists.txt` (`SonicatorCore` sources) and tests in `tests/CMakeLists.txt` (`target_sources(SonicatorTests PRIVATE ...)`).
- Tests write only to `juce::File::getSpecialLocation(juce::File::tempDirectory)` (`TestDirectory` in `tests/TestHelpers.h`). Never touch `~/Library/Sonicator/sonicator.sqlite` or `settings.json`.
- Build and test with presets: `cmake --preset release` (once), `cmake --build --preset release --target SonicatorTests`, `~/Library/Caches/sonicator-build/release/tests/SonicatorTests "<filter>"`.
- Style of the surrounding code: 4-space indent, no `using namespace` in headers, `namespace sonicator`, comments only where the reason is not obvious.
- New `.cpp`/`.h` files must not be created by iCloud-conflict copies; before each commit run `git ls-files | grep -E ' [0-9]+\.'` and expect no output.
- Catalog identifiers: `SampleId` (int32) for sampler sources, `int64_t` row id for catalog rows. Do not mix them.

## Review Focus

- A 24-bit/96 kHz or 32-bit float WAV is edited and saved back: content and format are preserved, no silent 16-bit or sample-rate conversion.
- Overwrite fails midway (disk full, read-only folder): the original stays byte-identical and the edits stay in memory.
- A long file (10+ minutes stereo) is edited: the UI stays responsive and undo memory stays within its cap.
- Deep zoom on a short sample: selection, seek and region edges land on the exact intended sample; zoom stops at ~1 sample per pixel.
- Mono sources: "Convert to mono" is a no-op that does not mark the file dirty; delete/crop keep loop and cue markers valid.

---

### Task 1: AudioEditBuffer — load, ranges, undo, delete/crop/silence/reverse

**Files:**
- Create: `core/edit/AudioEditBuffer.h`
- Create: `core/edit/AudioEditBuffer.cpp`
- Create: `tests/TestAudioEditBuffer.cpp`
- Modify: `CMakeLists.txt` (`SonicatorCore` source list)
- Modify: `tests/CMakeLists.txt` (`target_sources(SonicatorTests PRIVATE TestAudioEditBuffer.cpp)`)

**Interfaces:**
- Consumes: `SampleAudio`/`SampleAudioPtr` from `core/sampler/SamplerTypes.h`.
- Produces:
  ```cpp
  namespace sonicator::edit {
  struct FrameRange { int64_t start = 0, end = 0; bool valid() const; int64_t length() const; };
  struct FileMarkers { int rootKey = 60; bool hasLoop = false;
                       int64_t loopStart = 0, loopEndExclusive = 0;
                       std::vector<int64_t> cuePositions;
                       bool operator==(const FileMarkers&) const = default; };
  enum class EditOperation { deleteRegion, cropToSelection, silenceRegion, reverse,
                             fadeIn, fadeOut, normalize, gain, toMono, removeDcOffset, trimSilence };
  struct EditRequest { EditOperation operation; FrameRange range; float gainDb = 0.0f; };
  class AudioEditBuffer {
  public:
      static std::optional<AudioEditBuffer> load(const juce::File&, juce::String& error);
      static AudioEditBuffer fromSampleAudio(const SampleAudio&, FileMarkers markers = {});
      int channels() const; int sampleRate() const; int bitsPerSample() const; bool floatingPoint() const;
      int64_t frames() const;
      const std::vector<float>& channel(int index) const;
      const FileMarkers& markers() const;
      SampleAudioPtr snapshot() const;
      FrameRange whole() const;
      FrameRange resolve(const FrameRange& selection) const;
      bool apply(const EditRequest&);
      bool undo(); bool redo(); bool canUndo() const; bool canRedo() const;
      bool isModified() const; void clearHistory();
      size_t historyBytes() const;   // memory held by undo snapshots, for the cap test
  };
  }
  ```

- [ ] **Step 1: Write the failing tests** in `tests/TestAudioEditBuffer.cpp`, tagged `[edit]`.
  Use `TestDirectory` and `t.wave("sine.wav", 440.0, 1, 44100)`.
  - `TEST_CASE("Edit buffer loads a stereo WAV with its frame count", "[edit]")`: write a 2-channel, 16-bit, 1000-frame WAV through `juce::WavAudioFormat` (helper `writeWav` copied from `tests/TestExport.cpp` style); `AudioEditBuffer::load` returns a buffer with `channels() == 2`, `sampleRate() == 44100`, `frames() == 1000`, `frames()` of each channel vector equal to `frames()`, `isModified() == false`.
  - `TEST_CASE("Delete region removes frames and shifts the tail", "[edit]")`: sample values equal to frame index; delete frames 100..200; `frames() == 900`, `channel(0)[100] == 200.0f`, undo restores 1000 frames and value 100 at index 100.
  - `TEST_CASE("Crop keeps only the selection", "[edit]")`: crop 100..200 gives 100 frames, first value 100.
  - `TEST_CASE("Silence region zeroes frames without changing the length", "[edit]")`: silence 10..20; `frames()` unchanged; values 10..19 are 0; value 20 unchanged.
  - `TEST_CASE("Reverse mirrors the range and leaves the rest alone", "[edit]")`: reverse 0..4 on values 0,1,2,3,4,5 gives 4,3,2,1,0,5.
  - `TEST_CASE("Undo and redo walk the history", "[edit]")`: two edits, undo once, redo once, `isModified()` true; undo twice leaves `isModified()` false.
  - `TEST_CASE("Resolve uses the whole buffer when the selection is empty", "[edit]")`: `resolve({})` equals `whole()`.
  - `TEST_CASE("More than two channels is refused", "[edit]")`: 3-channel WAV; `load` returns `std::nullopt` and a non-empty error.
  - `TEST_CASE("Undo memory stays under its cap", "[edit]")`: a 2-channel buffer of 5,000,000 frames (≈40 MB per snapshot); apply 20 reverse edits; REQUIRE `historyBytes() <= 256ull * 1024 * 1024`; REQUIRE `canUndo()` and `undo()` still work for the newest edit.

- [ ] **Step 2: Run the tests to verify they fail**

  Run: `~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[edit]"` — after `cmake --preset release && cmake --build --preset release --target SonicatorTests`.
  Expected: compile error, `AudioEditBuffer` not found.

- [ ] **Step 3: Implement `AudioEditBuffer`**

  - Storage: `std::vector<float> left, right; int sampleRate = 0, bitsPerSample = 16; bool floatingPoint = false; FileMarkers markerData;` with `int64_t frames() const { return (int64_t) left.size(); }`. `channels()` is `right.empty() ? 1 : 2`.
  - `load`: `juce::AudioFormatManager` with `registerSonicatorFormats` (as `WaveformDisplay` does), create reader, refuse `numChannels > 2` or a failed read, `reader->read(&buffer, 0, (int) numFrames, 0, true, true)` into a `juce::AudioBuffer<float>`; copy into vectors; take `bitsPerSample`/`usesFloatingPointData`; read markers from `reader->metadataValues` (`MidiUnityNote`, `NumSampleLoops`, `Loop0Start`, `Loop0End` + 1, `NumCuePoints` + `Cue<i>Offset`).
  - Undo: `struct State { std::vector<float> left, right; FileMarkers markers; };` keep `std::vector<State> history` and `size_t position`, plus `size_t basePosition` semantics: `isModified()` is `position != 0`. Snapshot before each successful `apply`, cap total bytes at 256 MB (`dropOldest()`), and clear redo entries.
  - `apply`: resolve the range (`resolve`), then run the operation; return `false` without snapshotting when the operation changes nothing (invalid range, `toMono` on mono, `reverse` of a 0..1 frame range).
  - `snapshot()` builds a `SampleAudio` with the current vectors and markers-derived loop fields.

- [ ] **Step 4: Run the tests to verify they pass**

  Run: `~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[edit]"`
  Expected: all listed cases PASS.

- [ ] **Step 5: Commit**

  ```bash
  git add core/edit/AudioEditBuffer.h core/edit/AudioEditBuffer.cpp tests/TestAudioEditBuffer.cpp CMakeLists.txt tests/CMakeLists.txt
  git commit -m "feat(edit): in-memory audio edit buffer with undo"
  ```

---

### Task 2: AudioEditBuffer — fades, normalize, gain, mono, DC offset, trim silence

**Files:**
- Modify: `core/edit/AudioEditBuffer.cpp`
- Modify: `tests/TestAudioEditBuffer.cpp`

**Interfaces:**
- Consumes: Task 1 API.
- Produces: the remaining `EditOperation` values implemented; no signature changes.

- [ ] **Step 1: Write the failing tests** (append to `tests/TestAudioEditBuffer.cpp`, `[edit]`)
  - `TEST_CASE("Fade in ramps from zero to one", "[edit]")`: buffer of all 1.0, fade in over 0..100; value at 0 is 0, value at 99 ≈ 0.99, value at 100 is 1.0.
  - `TEST_CASE("Fade out ramps from one to zero", "[edit]")`: fade out 0..100; value at 0 is 1.0, last frame ≈ 0.
  - `TEST_CASE("Normalize scales the peak to -0.1 dBFS", "[edit]")`: values ±0.1 on both channels over the whole file; normalize; peak ≈ `0.8912509f` (with `Catch::Approx(1e-4)`); channel balance unchanged (ratio L/R preserved).
  - `TEST_CASE("Gain applies decibels to the range only", "[edit]")`: gain +6 dB over 0..10; those values double (approx), others unchanged.
  - `TEST_CASE("Convert to mono averages both channels", "[edit]")`: left all 0.5, right all -0.5 → mono values 0, `channel(1)` empty, `apply` on an already mono buffer returns false and leaves `isModified()` false.
  - `TEST_CASE("Remove DC offset subtracts each channel mean", "[edit]")`: left 0.25+signal, right -0.25+signal; after the op each channel mean ≈ 0.
  - `TEST_CASE("Trim silence keeps five milliseconds of margin", "[edit]")`: 44100 Hz buffer, 1000 leading zeros, 44100 of 0.5, 1000 trailing zeros; trim; `frames()` ≈ 44100 + 2 * 221 (5 ms = 220.5 frames, allow ±1), first frame non-zero, value at 0 is ≈ 0.5.
  - `TEST_CASE("Trim silence on fully silent audio leaves it unchanged", "[edit]")`: apply returns false, frames unchanged.

- [ ] **Step 2: Run to verify failure**

  Run: `~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[edit]"`
  Expected: FAIL — those operations currently do nothing.

- [ ] **Step 3: Implement the operations**

  Constants in an anonymous namespace: `kSilenceThreshold = 0.001f` (−60 dBFS), `kTrimMarginFrames(sampleRate) = (int64_t) std::lround(0.005 * sampleRate)`, `kNormalizeTarget = 0.8912509f` (−0.1 dBFS). Fades are linear in amplitude across the range length; gain multiplies by `std::pow(10.0f, gainDb / 20.0f)`; mono averages `(left+right)/2` over the whole buffer regardless of range; trim scans from both ends while every channel is below the threshold.

- [ ] **Step 4: Run to verify pass**

  Run: `~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[edit]"`
  Expected: PASS.

- [ ] **Step 5: Commit**

  ```bash
  git add core/edit/AudioEditBuffer.cpp tests/TestAudioEditBuffer.cpp
  git commit -m "feat(edit): fades, normalize, gain, mono, DC offset and silence trim"
  ```

---

### Task 3: Marker transformation across edits

**Files:**
- Create: `core/edit/AudioEditMarkers.h`
- Create: `core/edit/AudioEditMarkers.cpp`
- Create: `tests/TestAudioEditMarkers.cpp`
- Modify: `core/edit/AudioEditBuffer.cpp` (call the transform in `apply`)
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `edit::FileMarkers`, `edit::EditOperation`, `edit::FrameRange` (Task 1).
- Produces:
  ```cpp
  namespace sonicator::edit {
  // Returns the markers as they apply after `operation` changed `range` on a buffer that had
  // `framesBefore` frames. Operations that do not move frames return the input untouched.
  FileMarkers transformMarkers(const FileMarkers&, EditOperation, FrameRange range,
                               int64_t framesBefore);
  }
  ```

- [ ] **Step 1: Write the failing tests** in `tests/TestAudioEditMarkers.cpp`, `[edit][markers]`
  - `TEST_CASE("Delete drops points inside the removed range and shifts the tail", "[edit][markers]")`: markers with loop 100..400 and cues {50, 150, 350}; delete frames 100..200 → loop 100..300, cues {50, 250}; a cue exactly at 150 disappears.
  - `TEST_CASE("Crop rebases markers onto the kept range", "[edit][markers]")`: crop 100..400 → loop 0..300, cue 50 removed, cues {250, 300} for {350, 400}.
  - `TEST_CASE("Reverse mirrors loop and cue positions inside the range", "[edit][markers]")`: reverse 0..1000 with loop 100..300 → loop 700..900, cue 200 → 800.
  - `TEST_CASE("A loop that becomes invalid is removed", "[edit][markers]")`: loop 0..100, delete 0..100 → `hasLoop == false`.
  - `TEST_CASE("Trim silence shifts markers by the trimmed head", "[edit][markers]")`: markers loop 1000..2000, trim start 200 → loop 800..1800.
  - `TEST_CASE("Gain and normalize leave markers untouched", "[edit][markers]")`: same markers back for `normalize`, `gain`, `fadeIn`, `silenceRegion`, `toMono`, `removeDcOffset`.

- [ ] **Step 2: Run to verify failure**

  Run: `~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[edit][markers]"`
  Expected: compile error, `transformMarkers` not defined.

- [ ] **Step 3: Implement**

  `crop`: subtract `range.start` from every point, drop points outside the new length. `deleteRegion`: drop points inside `[start, end)`, subtract `end - start` from points after. `trimSilence`: the buffer passes the detected head as `range` (start = detected head, end = detected tail) and the transform subtracts `range.start`. `reverse`: mirror every point inside the range as `start + end - 1 - p`. Always drop a loop whose `loopStart >= loopEndExclusive` or bounds fall outside the new length. Wire it into `AudioEditBuffer::apply` (compute the effective range for `trimSilence` before applying the transform).

- [ ] **Step 4: Run to verify pass**

  Run: `~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[edit]"`
  Expected: PASS.

- [ ] **Step 5: Commit**

  ```bash
  git add core/edit/AudioEditMarkers.h core/edit/AudioEditMarkers.cpp tests/TestAudioEditMarkers.cpp core/edit/AudioEditBuffer.cpp CMakeLists.txt tests/CMakeLists.txt
  git commit -m "feat(edit): keep loop and cue markers consistent across edits"
  ```

---

### Task 4: AudioEditWriter — atomic overwrite, unique copies, format preservation

**Files:**
- Create: `core/edit/AudioEditWriter.h`
- Create: `core/edit/AudioEditWriter.cpp`
- Create: `tests/TestAudioEditWriter.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `AudioEditBuffer` (Tasks 1–3), `AudioExporter::validateWavOutput`, `CancelCheck` (`core/scanner/FileScanner.h`).
- Produces:
  ```cpp
  namespace sonicator::edit {
  struct SaveResult { bool success = false; juce::File file; juce::String error; };
  bool canOverwriteInPlace(const juce::File& target);         // wav/wave/aif/aiff/flac
  juce::File uniqueCopyPath(const juce::File& original, const juce::String& stem);
  SaveResult overwrite(const juce::File& target, const AudioEditBuffer&, CancelCheck cancel = {});
  SaveResult saveCopy(const juce::File& original, const juce::String& stem,
                      const AudioEditBuffer&, CancelCheck cancel = {});
  }
  ```

- [ ] **Step 1: Write the failing tests** in `tests/TestAudioEditWriter.cpp`, `[edit][writer]`
  - `TEST_CASE("Overwrite replaces a WAV atomically and keeps its format", "[edit][writer]")`: 24-bit stereo **96 kHz** fixture; edit; `overwrite`; re-open with `juce::WavAudioFormat` and REQUIRE `sampleRate == 96000`, `bitsPerSample == 24`, `numChannels == 2`, frames and a sample value match the buffer.
  - `TEST_CASE("A failed overwrite leaves the original byte-identical", "[edit][writer]")` (macOS/Linux): write a valid WAV, `chmod` the folder to read-only, call `overwrite`, expect `success == false`, file still opens with original frame count; restore permissions in the test; skip on Windows (`#if ! JUCE_WINDOWS`).
  - `TEST_CASE("Overwrite follows a symlink instead of replacing it", "[edit][writer]")` (macOS/Linux only): symlink `link.wav` → `real.wav`; overwrite through the link; `link.wav` is still a symlink and `real.wav` has the new frames.
  - `TEST_CASE("Only WAV, AIFF and FLAC can be overwritten", "[edit][writer]")`: `canOverwriteInPlace` true for `.wav/.aif/.aiff/.flac`, false for `.mp3/.ogg/.rex/.mp4`.
  - `TEST_CASE("Copy names never overwrite", "[edit][writer]")`: `uniqueCopyPath` returns `Clap_15 edit.wav`, then after creating it `Clap_15 edit 2.wav`, then `Clap_15 edit 3.wav`; original untouched.
  - `TEST_CASE("Save copy of a non-WAV source writes WAV next to it", "[edit][writer]")`: source `.mp3`-named file is not required — use an AIFF fixture and assert the copy has `.wav`, opens, and content matches.
  - `TEST_CASE("Writer metadata carries root key and loop", "[edit][writer]")`: markers root 48, loop 100..500; overwrite a WAV; re-read `metadataValues["MidiUnityNote"] == "48"`, `"NumSampleLoops" == "1"`, `"Loop0Start" == "100"`, `"Loop0End" == "499"`; cue count follows `cuePositions`.

- [ ] **Step 2: Run to verify failure**

  Run: `~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[edit][writer]"`
  Expected: compile error.

- [ ] **Step 3: Implement**

  - `overwrite`: resolve symlinks with `target.getLinkedTarget()`/`juce::File::createSymbolicLink` awareness: if `target.isSymbolicLink()`, write next to and replace `target.getLinkedTarget()`. Create the temp with `juce::TemporaryFile(target)`, write through `juce::AudioFormatWriterOptions` using the extension's format (WAV via `juce::WavAudioFormat`, AIFF via `juce::AiffAudioFormat`, FLAC via `juce::FlacAudioFormat`), same `sampleRate`, `channels`, `bitsPerSample` and `floatingPoint`; then `validateWavOutput` for WAV; copy POSIX permissions (`getFilePermissions`/`setFilePermissions`) and, on macOS, extended attributes (`listxattr`/`getxattr`/`setxattr` in the `.cpp` under `#if JUCE_MAC`); finally `overwriteTargetFileWithTemporary()`.
  - `saveCopy`: `uniqueCopyPath(original, stem)`; when `canOverwriteInPlace(original)` is true, write with the original's extension and format; otherwise WAV at 24-bit (or 32-bit float when the buffer is float). Never overwrite.
  - Metadata: `.withMetadata("MidiUnityNote", ...)`, `NumSampleLoops`, `Loop0Start`, `Loop0End` (exclusive − 1), `NumCuePoints`, `Cue<i>Identifier`, `Cue<i>Order`, `Cue<i>Offset` for cues (JUCE's WAV writer defaults `ChunkID` to `data`).
  - Chunked writes of at most 65536 frames, honouring `cancel`.

- [ ] **Step 4: Run to verify pass**

  Run: `~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[edit][writer]"`
  Expected: PASS (symlink/xattr cases skipped on Windows).

- [ ] **Step 5: Commit**

  ```bash
  git add core/edit/AudioEditWriter.h core/edit/AudioEditWriter.cpp tests/TestAudioEditWriter.cpp CMakeLists.txt tests/CMakeLists.txt
  git commit -m "feat(edit): atomic overwrite, unique copies and sampler metadata"
  ```

---

### Task 5: Catalog sync for a rewritten file and a saved copy

**Files:**
- Create: `core/indexing/CatalogFileSync.h`
- Create: `core/indexing/CatalogFileSync.cpp`
- Create: `tests/TestCatalogFileSync.cpp`
- Modify: `core/database/SampleRepository.h`
- Modify: `core/database/SampleRepository.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `SampleRepository`, `BasicAnalyzer::analyzeAndHash`, `readAnalysisAudio` (`core/dsp/AnalysisAudio.h`), `AdaptiveClassifier::extractFeatures` (`core/ml/AdaptiveClassifier.h`), `FeatureExtractorV2::extract` (`core/dsp/FeatureExtractorV2.h`), `SampleRepository::tagsForSample/setSampleTag/setStarred`.
- Produces:
  ```cpp
  // core/database/SampleRepository.h
  std::optional<FeedbackRecord> feedbackForSample(int64_t sampleId) const;
  // Rewrites the audio-derived columns of an existing feedback row: file_hash, features,
  // features_v2, feature_schema and updated_at. Labels, target type and origin stay as they are.
  // Returns false when there is no row.
  bool refreshFeedbackAudio(int64_t sampleId, const juce::String& fileHash,
                            const std::vector<float>& featuresV1,
                            const std::vector<float>& featuresV2);
  ```
  ```cpp
  // core/indexing/CatalogFileSync.h
  namespace sonicator {
  struct CatalogSyncResult { bool ok = false; int64_t sampleId = 0; juce::String error; };
  CatalogSyncResult refreshCatalogFile(SampleRepository&, const juce::File&, const CancelCheck& = {});
  CatalogSyncResult addCatalogCopy(SampleRepository&, const juce::File&, int64_t templateSampleId,
                                   const CancelCheck& = {});
  }
  ```

- [ ] **Step 1: Write the failing tests** in `tests/TestCatalogFileSync.cpp`, `[edit][catalog]`
  Use `Database db(t.dir.getChildFile("catalog.sqlite")); SampleRepository repo(db); repo.createTables();`
  - `TEST_CASE("Refreshing a rewritten file keeps its tags, star and row id", "[edit][catalog]")`: upsert a record for a WAV, tag it, star it, record feedback with features {1,2}; overwrite the WAV with different audio (shorter); `refreshCatalogFile`; `findByPath` returns the same id, `tagsForSample` still has the tag, `findById(id)->starred` true, `feedbackForSample(id)->features` differ from {1,2}, `fileHash` equals the new `SampleRecord::fileHash`, `updatedAt` increased.
  - `TEST_CASE("Refreshing a file without feedback does not create a row", "[edit][catalog]")`: no feedback before; after refresh `feedbackForSample(id)` is `std::nullopt`.
  - `TEST_CASE("A catalog copy inherits tags and the star", "[edit][catalog]")`: source row with tag "Kick" and star; write `Kick edit.wav`; `addCatalogCopy(repo, copy, sourceId)`; the copy row exists, has "Kick", is starred, and its `filePath` is the copy path.
  - `TEST_CASE("A catalog copy of an unknown template still becomes a row", "[edit][catalog]")`: `templateSampleId = 0`; copy row exists without tags and is not starred.

- [ ] **Step 2: Run to verify failure**

  Run: `~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[edit][catalog]"`
  Expected: compile error / missing methods.

- [ ] **Step 3: Implement**

  `refreshCatalogFile`: `auto record = BasicAnalyzer().analyzeAndHash(file, cancel);` then `repo.upsert(record)`; find `repo.findByPath(file.getFullPathName())`; if a feedback row exists, decode up to 30 s with `readAnalysisAudio` and compute v1 (`AdaptiveClassifier::extractFeatures`) and v2 (`FeatureExtractorV2::extract` on the same buffer), then `refreshFeedbackAudio`. Empty decoded audio leaves the existing vectors in place (still update `file_hash`).
  `addCatalogCopy`: analyze the copy, `repo.upsert`, then copy tags with `setSampleTag(copyId, tag.id, true)` for every `tagsForSample(templateId)` and `repo.setStarred({copyId}, true)` when the template is starred. Ignore a template id that has no row.
  `refreshFeedbackAudio`: one `UPDATE ml_feedback_samples SET file_hash=?, features=?, features_v2=?, feature_schema=2, updated_at=? WHERE sample_id=?`, JSON-encoding the vectors exactly as `setFeedbackV2` does.

- [ ] **Step 4: Run to verify pass**

  Run: `~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[edit][catalog]"`
  Expected: PASS.

- [ ] **Step 5: Commit**

  ```bash
  git add core/indexing/CatalogFileSync.h core/indexing/CatalogFileSync.cpp tests/TestCatalogFileSync.cpp core/database/SampleRepository.h core/database/SampleRepository.cpp CMakeLists.txt tests/CMakeLists.txt
  git commit -m "feat(edit): refresh catalog rows and feedback after saving"
  ```

---

### Task 6: Waveform view and zoom math

**Files:**
- Create: `ui/waveform/WaveformView.h`
- Create: `ui/waveform/WaveformView.cpp`
- Create: `tests/TestWaveformView.cpp`
- Modify: `CMakeLists.txt` (`SonicatorCore`), `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `selection::inset` from `ui/waveform/WaveformSelection.h`.
- Produces:
  ```cpp
  namespace sonicator::waveview {
  struct View { double start = 0, end = 0; bool valid() const; double length() const; };
  View whole(double totalLength);
  View clampView(View, double totalLength);
  View zoomAround(View, double anchorSeconds, double factor, double totalLength, double minLength);
  View panBy(View, double seconds, double totalLength);
  double timeAtX(double x, int width, View);
  float xAtTime(double time, int width, View);
  bool isWholeFile(View, double totalLength);
  }
  ```

- [ ] **Step 1: Write the failing tests** in `tests/TestWaveformView.cpp`, `[waveform]`
  - `TEST_CASE("Zoom keeps the anchor under the same pixel", "[waveform]")`: view 0..10, width 1000, anchor 2.5 s; zoom by 2 (in) repeatedly; `timeAtX(xAtTime(2.5, 1000, view), 1000, view)` stays within 1e-6 of 2.5.
  - `TEST_CASE("Zoom cannot go beyond the whole file", "[waveform]")`: zoom out from whole by 0.5 → still whole.
  - `TEST_CASE("Zoom stops at one sample per pixel", "[waveform]")`: total 1 s at 44100 Hz, width 1000 → minLength = 1/1000 s; repeated zoom in never yields a shorter view.
  - `TEST_CASE("Pan clamps to file bounds", "[waveform]")`: view 1..2 of 10 s; `panBy(-5)` → 0..1; `panBy(+50)` → 9..10.
  - `TEST_CASE("Time and pixel mapping round-trip", "[waveform]")`: for views {0..10, 2..3}, a time inside the view maps to a pixel and back within 1e-6.
  - `TEST_CASE("A view equal to the file is whole", "[waveform]")`: `isWholeFile({0,10},10)` true, `{0,9.999}` false.

- [ ] **Step 2: Run to verify failure**

  Run: build, then `~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[waveform]"`
  Expected: compile error.

- [ ] **Step 3: Implement**

  `timeAtX`/`xAtTime` mirror `selection::timeAtX/xAtTime` but map into `[view.start, view.end]`. `zoomAround` clamps `[start, end]` to `[0, totalLength]` and never below `minLength`; `factor > 1` zooms in. `panBy` clamps like `zoomAround`.

- [ ] **Step 4: Run to verify pass**

  Run: `~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[waveform]"`
  Expected: PASS.

- [ ] **Step 5: Commit**

  ```bash
  git add ui/waveform/WaveformView.h ui/waveform/WaveformView.cpp tests/TestWaveformView.cpp CMakeLists.txt tests/CMakeLists.txt
  git commit -m "feat(waveform): view range and zoom math"
  ```

---

### Task 7: WaveformDisplay — zoom, pan, overview and view-aware interaction

**Files:**
- Modify: `ui/waveform/WaveformDisplay.h`
- Modify: `ui/waveform/WaveformDisplay.cpp`
- Modify: `tests/TestUIPolish.cpp` (new cases, `[waveform][ui]`)

**Interfaces:**
- Consumes: `waveview::View` etc. (Task 6); `selection::` helpers.
- Produces on `WaveformDisplay`:
  ```cpp
  waveview::View view() const;
  void setView(waveview::View);
  void zoomToFit();
  void zoomToSelection();
  double sampleRate() const;      // 0 when unknown (thumbnail sources keep 0)
  void setSampleRateForZoom(int); // called by the owner after loading, so depth is per-sample
  ```

- [ ] **Step 1: Write the failing tests** in `tests/TestUIPolish.cpp`, `[waveform][ui]`
  - `TEST_CASE("Waveform view starts at the whole file and zoom selection narrows it", "[waveform][ui]")`: build a `WaveformDisplay` on a temp cache folder, `setFile(t.wave())`, wait for the thumbnail (`juce::MessageManager::getInstance()->runDispatchLoopUntil(50)` is not available in the test binary — instead call `setPeaks` with a known 1 s of samples and `setSampleRateForZoom(44100)`); REQUIRE `view()` equals 0..1 s; `setSelection({0.25, 0.5})`; `zoomToSelection()`; REQUIRE `view()` ≈ 0.25..0.5; `zoomToFit()` restores 0..1.
  - `TEST_CASE("Ctrl-wheel zooms around the pointer", "[waveform][ui]")`: `mouseWheelMove` with `juce::ModifierKeys::ctrlModifier` at x ≈ 80%; view length shrinks and the anchor time under x stays within one sample.
  - `TEST_CASE("A plain wheel still resizes an armed loop", "[waveform][ui]")`: existing behavior: with `setLoopArmed(true)` and a valid selection, a plain wheel changes the selection and leaves `view()` whole.

- [ ] **Step 2: Run to verify failure**

  Run: `~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[waveform][ui]"`
  Expected: FAIL — `view()` not defined.

- [ ] **Step 3: Implement**

  - Replace every `selection::timeAtX/xAtTime(..., totalLength())` used for interaction and drawing with `waveview::timeAtX/xAtTime(..., view)`; keep `totalLength()` as the file length.
  - `mouseWheelMove`: with `e.mods.isCtrlDown() || e.mods.isCommandDown()` zoom via `waveview::zoomAround(view, timeAtX(e.position.x), factor, totalLength(), minLength())`, where `minLength = sampleRate > 0 ? (double) width / sampleRate : totalLength()/1e6`; with `e.mods.isShiftDown()` pan by `-wheel.deltaY * view.length() * 0.5`; otherwise keep the existing loop-resize behavior, and pan when no selection is valid.
  - `mouseMagnify(const juce::MouseEvent&, float scaleFactor)`: `zoomAround` with `factor = scaleFactor`.
  - Clamp `setSelection` to the visible view? No: selection stays in file time; only painting uses the view.
  - Paint: `thumbnail.drawChannels(g, area, view.start, view.end, 0.85f)` for file sources; for edited buffers (Task 8) draw per-column min/max; draw an overview strip of 12 px at the bottom when `! isWholeFile(view, total)`, with a draggable frame (`onMouseDown` inside the strip enters `DragMode::overview`; `mouseDrag` calls `setView(panBy(...))`).
  - `mouseDoubleClick` keeps selecting the whole file.

- [ ] **Step 4: Run to verify pass**

  Run: `~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[waveform]"`
  Expected: PASS, and existing `[ui]` cases still pass.

- [ ] **Step 5: Commit**

  ```bash
  git add ui/waveform/WaveformDisplay.h ui/waveform/WaveformDisplay.cpp tests/TestUIPolish.cpp
  git commit -m "feat(waveform): zoom, pan and overview strip"
  ```

---

### Task 8: WaveformDisplay — edited PCM, stereo peaks and the context-menu hook

**Files:**
- Modify: `ui/waveform/WaveformDisplay.h`
- Modify: `ui/waveform/WaveformDisplay.cpp`
- Modify: `tests/TestUIPolish.cpp`

**Interfaces:**
- Consumes: `SampleAudioPtr`.
- Produces on `WaveformDisplay`:
  ```cpp
  // Shows edited PCM instead of the file thumbnail. sliceStarts is in frames.
  void setEditedAudio(SampleAudioPtr, const std::vector<int64_t>& sliceStarts = {});
  void clearEditedAudio();
  std::function<void(juce::Point<int>)> onContextMenu;   // screen position
  ```

- [ ] **Step 1: Write the failing tests** in `tests/TestUIPolish.cpp`, `[waveform][ui]`
  - `TEST_CASE("Edited audio replaces the thumbnail and its length", "[waveform][ui]")`: build a `SampleAudio` (1 s, 44100, stereo), `setEditedAudio`; REQUIRE `totalLength() == 1.0`; REQUIRE `sampleRate() == 44100`; `clearEditedAudio` restores the thumbnail length.
  - `TEST_CASE("Right-click asks the owner for a menu", "[waveform][ui]")`: set `onContextMenu` to capture a point; send `mouseDown` with `juce::ModifierKeys::rightButtonModifier`; REQUIRE the callback ran.
  - `TEST_CASE("Edited peaks follow the zoom", "[waveform][ui]")`: `setEditedAudio` with a ramp 0..1; `setView({0, 0.1})`; paint into an `juce::Image` (Graphics on image) and REQUIRE the drawn column count is the visible width (no crash, and `view().length() == 0.1`).

- [ ] **Step 2: Run to verify failure**

  Run: `~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[waveform][ui]"`
  Expected: FAIL — `setEditedAudio` undefined.

- [ ] **Step 3: Implement**

  - Store `SampleAudioPtr editedAudio; std::vector<int64_t> editedSlices;` and clear on `setFile`/`setPeaks`. `totalLength()` returns `editedAudio ? frames / sampleRate : ...`.
  - Peaks for the visible view: for each pixel column compute min/max over both channels in `[view.start, view.end]`, cached by `(view, width)` and invalidated on `setEditedAudio`/`setView`; for deep zoom on file sources use the thumbnail first and fall back to reading frames through a `juce::AudioFormatReader` when `view.length() * sampleRate / width < 16`.
  - `mouseDown`: when `e.mods.isPopupMenu()` (or `e.mods.isRightButtonDown()`) call `onContextMenu(e.getScreenPosition())` and return before the selection logic.

- [ ] **Step 4: Run to verify pass**

  Run: `~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[waveform]"`
  Expected: PASS.

- [ ] **Step 5: Commit**

  ```bash
  git add ui/waveform/WaveformDisplay.h ui/waveform/WaveformDisplay.cpp tests/TestUIPolish.cpp
  git commit -m "feat(waveform): edited PCM with stereo peaks and context-menu hook"
  ```

---

### Task 9: PreviewPanel — edit session, context menu, keyboard and saving

**Files:**
- Modify: `ui/preview/PreviewPanel.h`
- Modify: `ui/preview/PreviewPanel.cpp`
- Create: `tests/TestPreviewEditing.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Tasks 1–8, `dialogs::showAlert` (`ui/shell/ThemedDialogs.h`).
- Produces on `PreviewPanel`:
  ```cpp
  // True while edited audio sits in memory and differs from the file on disk.
  bool hasUnsavedEdits() const;
  // True when the current source can be edited at all (see the spec's source table).
  bool sourceIsEditable() const;
  int64_t editingSampleId;                      // catalog row being edited, 0 when unknown
  // Runs `proceed` at once when nothing is unsaved, otherwise asks Save…/Discard/Cancel.
  void withUnsavedEditsConfirmed(std::function<void()> proceed);
  // Cmd+S entry, exposed for tests and the menu.
  void saveEdits();
  enum class EditCommand { undo, redo, deleteRegion, crop, silence, reverse, fadeIn, fadeOut,
                           normalize, gain, toMono, removeDc, trimSilence, zoomToSelection,
                           zoomToFit, save };
  // Public so tests and the context menu use the same path.
  void applyEditCommand(EditCommand);
  // Test-only entry points that take the dialog's branch directly.
  void saveEditsAsCopyForTest();
  void overwriteEditsForTest();
  // Catalog hooks, set by MainComponent.
  std::function<void(const juce::File& target, int64_t sampleId)> onFileSavedInPlace;
  std::function<void(const juce::File& copy, int64_t templateSampleId)> onCopySaved;
  ```
  Internal: `showWaveformMenu`, `ensureEditBuffer`, `afterEditApplied`, `refreshDirtyMark`, `overwriteEdits`, `saveCopyAndSwitch`, `discardEdits`.

- [ ] **Step 1: Write the failing tests** in `tests/TestPreviewEditing.cpp`, `[ui][edit]`
  - `TEST_CASE("An ordinary WAV becomes editable and dirty after a delete", "[ui][edit]")`: `PreviewPanel preview(engine, cache)`; `preview.loadFile(t.wave("a.wav"), false)`; REQUIRE `preview.sourceIsEditable()`; select 0.1–0.2 s; `applyEditCommand(preview, EditCommand::deleteRegion)` — expose a test-only path: make `applyEditCommand` public (it takes the enum); REQUIRE `preview.hasUnsavedEdits()`; REQUIRE `preview.waveformLength()` decreased.
  - `TEST_CASE("Undo clears the dirty state", "[ui][edit]")`: after one delete, `applyEditCommand(undo)` → `hasUnsavedEdits() == false`.
  - `TEST_CASE("A REX or Reason source is not editable", "[ui][edit]")`: `engine.isRex2Mode()` cannot be faked cheaply — instead assert `sourceIsEditable()` is false for a file loaded as metadata-only (`.rex` fixture from `tests/TestRealRex.cpp` is heavy): use `preview.clearSource()` and REQUIRE `sourceIsEditable() == false`; plus REQUIRE false for a tracker module only if a fixture already exists in `SONICATOR_FIXTURE_DIR` (skip otherwise with `SUCCEED`).
  - `TEST_CASE("Edit operations refuse sources with more than two channels", "[ui][edit]")`: load a 3-channel WAV; REQUIRE `sourceIsEditable() == false` and `hasUnsavedEdits() == false`.
  - `TEST_CASE("Saving a copy writes a new file beside the original and reports it", "[ui][edit]")`: edit a WAV; set `onCopySaved` to capture the path; call `preview.saveEditsAsCopyForTest()`; REQUIRE the captured file exists, opens, and `waveformLength()` now equals the copy's length. (Add the test-only helper `void saveEditsAsCopyForTest();` to `PreviewPanel`, which performs the Save-copy branch without a dialog.)
  - `TEST_CASE("Overwriting refreshes from the file and clears dirty", "[ui][edit]")`: edit; `preview.overwriteEditsForTest()`; REQUIRE `hasUnsavedEdits() == false`, the file on disk has the new frame count, and `onFileSavedInPlace` ran with the same path.

- [ ] **Step 2: Run to verify failure**

  Run: `~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[ui][edit]"`
  Expected: FAIL — members missing.

- [ ] **Step 3: Implement**

  - `ensureEditBuffer()`: on the first edit, decide the source (`engine.isRex2Mode()`, `engine.isReasonMode()`, tracker whole-song, `engine.isSamplerInstrumentMode()`, `engine.isDiscMode()`, ordinary file) and either `AudioEditBuffer::load(loadedFile, error)` or `AudioEditBuffer::fromSampleAudio(audio)` with the markers built from the source's loop data (disc sample audio has none); store `editTarget = loadedFile` for OrdinaryFile and `editStem = loadedFile.getFileNameWithoutExtension()` otherwise; decode failure shows the error in `selectionStatus` and leaves the panel unchanged.
  - `applyEditCommand`: map the current selection (seconds) to buffer frames; build `EditRequest`; `gain` asks for decibels with `dialogs::showPrompt(*this, "Gain", "Decibels (-24…+24)", "0.0", ...)` then applies `std::clamp(value, -24.0f, 24.0f)`; after `apply` returns true call `afterEditApplied()`.
  - `afterEditApplied`: `engine.setDiscSample(buffer.snapshot(), false, 0, 0, false)`; `waveform.setEditedAudio(buffer.snapshot(), {})`; `waveform.setVisible(true)`; re-prepare the selection export from the snapshot via `exporter.prepare(editTarget, 0, length, lambda over snapshot)`; clamp the waveform selection to the new length and clear the playhead beyond it; `refreshDirtyMark()`.
  - `showWaveformMenu(position)`: `juce::PopupMenu` with the spec's items and groups; enable per source and selection; `menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({position.x, position.y, 1, 1}), ...)` calling `applyEditCommand`. `Zoom to selection` is enabled only for a valid selection.
  - `keyPressed`: `⌘S`/`Ctrl+S` → `saveEdits()`; `⌘Z`/`Ctrl+Z` → undo; `⇧⌘Z` → redo; `⌘0` → `zoomToFit`; Delete/Backspace → remove the selected region when a selection exists and the buffer exists.
  - `saveEdits()`: no-op when not dirty; themed alert `"Save changes to “<name>”?"` with `Overwrite` (only when `edit::canOverwriteInPlace(editTarget)`), `Save copy`, `Cancel`; callbacks run `overwriteEdits()` / `saveCopyAndSwitch()`. Use `juce::Component::SafePointer<PreviewPanel>`.
  - `overwriteEdits()`: `edit::overwrite(editTarget, *editBuffer)`; on success `editBuffer->clearHistory()`, `refreshDirtyMark()`, reload the file with `loadFile(editTarget, false)` (mtime differs) and call `onFileSavedInPlace(editTarget, editingSampleId)`; on failure show `result.error` in `selectionStatus` and keep the buffer.
  - `saveCopyAndSwitch()`: `edit::saveCopy(source, stem, *editBuffer)`; on success call `onCopySaved(result.file, editingSampleId)`, then `loadFile(result.file, false)` and clear the edit state. For a stereo pair `source` is the pair's first file and `stem` is the pair base name without the L/R suffix (`parseStereoName(file).base`), so the copy is one stereo `… edit.wav`.
  - `withUnsavedEditsConfirmed(proceed)`: call `proceed()` when clean; otherwise themed alert `"“<name>” has unsaved edits."` with `Save…`, `Discard`, `Cancel`; `Save…` shows `saveEdits()` and re-asks `hasUnsavedEdits()` afterwards (proceed only when clean), `Discard` calls `discardEdits()`, `Cancel` does nothing. `discardEdits()` drops the buffer, restores the file source (`loadFile(loadedFile, false)`), clears the mark.
  - `refreshDirtyMark()`: `filename.setText(cleanName + (hasUnsavedEdits() ? " •" : ""))`, remembering `cleanName` on every `filename.setText` call while editing.

- [ ] **Step 4: Run to verify pass**

  Run: `~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[ui][edit]"`
  Expected: PASS.

- [ ] **Step 5: Commit**

  ```bash
  git add ui/preview/PreviewPanel.h ui/preview/PreviewPanel.cpp tests/TestPreviewEditing.cpp tests/CMakeLists.txt
  git commit -m "feat(preview): waveform editing, context menu and save dialogs"
  ```

---

### Task 10: Guarding file switches and quit; catalog wiring

**Files:**
- Modify: `ui/MainComponent.h`
- Modify: `ui/MainComponent.cpp`
- Modify: `app/Main.cpp`
- Modify: `tests/TestPreviewEditing.cpp` (guard tests)

**Interfaces:**
- Consumes: `PreviewPanel::hasUnsavedEdits`, `withUnsavedEditsConfirmed`, `onFileSavedInPlace`, `onCopySaved` (Task 9); `refreshCatalogFile`, `addCatalogCopy` (Task 5).
- Produces:
  - `MainComponent::prepareForQuit(std::function<void()> proceed)`.
  - `PreviewPanel::loadFile/loadDiscSample` return `false` with `selectionStatus` set to `"Save or discard the current edits first"` when called while dirty (defensive fallback for paths that bypass the guard).

- [ ] **Step 1: Write the failing tests** in `tests/TestPreviewEditing.cpp`, `[ui][edit]`
  - `TEST_CASE("Loading another source while dirty is refused", "[ui][edit]")`: edit a WAV, then `REQUIRE(preview.loadFile(t.wave("b.wav"), false) == false)` and the dirty state is still there; `preview.loadFile(sameOriginal, false)` is allowed.
  - `TEST_CASE("The quit hook proceeds immediately when nothing is unsaved", "[ui][edit]")`: `MainComponent` needs a repository and is heavy; instead test the preview-level hook: `withUnsavedEditsConfirmed` runs the callback synchronously for a clean panel (`bool ran = false; preview.withUnsavedEditsConfirmed([&]{ ran = true; }); REQUIRE(ran);`).

- [ ] **Step 2: Run to verify failure**

  Run: `~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[ui][edit]"`
  Expected: FAIL.

- [ ] **Step 3: Implement**

  - `PreviewPanel::loadFile`/`loadDiscSample`: at the top, if `hasUnsavedEdits()` and the incoming target is not the file currently being edited, set `selectionStatus` and return `false`.
  - `MainComponent::selectSample`: split the existing body into `selectSample` (guard) and `loadSelectedSample(const SampleRecord&)`; when dirty, remember the record and `selectedSampleId` in `pendingSample`, then `preview.withUnsavedEditsConfirmed([safe](...){ load or restore selection with library.selectSampleById(editingSampleId) })`.
  - `MainComponent::clearSampleSelection` and `openDiscImage`: route through the same guard.
  - `MainComponent::prepareForQuit(std::function<void()> proceed)`: `preview.withUnsavedEditsConfirmed(std::move(proceed))`.
  - `app/Main.cpp`: `SonicatorApplication::systemRequestedQuit()` → `window->requestQuit()`; `Window::requestQuit()` → `content->prepareForQuit([app]{ app->quit(); })` with a `quitInProgress` flag; `closeButtonPressed` also calls `requestQuit()`. Get `content` with `dynamic_cast<sonicator::MainComponent*>(getContentComponent())`.
  - Wire the catalog callbacks in the `MainComponent` constructor: `preview.onFileSavedInPlace = [this](file, id){ startCatalogSync(file, id, false); }` and `preview.onCopySaved = [this](file, templateId){ startCatalogSync(file, templateId, true); }`; `startCatalogSync` runs `refreshCatalogFile` / `addCatalogCopy` on a `std::thread` captured by `juce::Component::SafePointer`, sets `status` to "Updating catalog…" then "Catalog updated: <name>", and reloads the library when the row was a copy so it appears in the list.

- [ ] **Step 4: Run to verify pass**

  Run: `~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[ui][edit]"`
  Expected: PASS.

- [ ] **Step 5: Commit**

  ```bash
  git add ui/MainComponent.h ui/MainComponent.cpp app/Main.cpp tests/TestPreviewEditing.cpp
  git commit -m "feat(preview): guard unsaved edits on source switch and quit"
  ```

---

### Task 11: Documentation, graph and the full suite

**Files:**
- Modify: `docs/ARCHITECTURE.md`
- Modify: `docs/superpowers/specs/2026-11-13-waveform-editor-design.md` (only if an implementation detail deviates; record the deviation)
- Modify: `graphify-out/` (generated)

- [ ] **Step 1: Document the editor**

  Add a section to `docs/ARCHITECTURE.md`: `core/edit` responsibilities, the save pipeline (temp file → validate → replace → catalog refresh), source kinds that cannot be overwritten, and where the UI hooks live (`PreviewPanel::applyEditCommand`, `MainComponent::prepareForQuit`).

- [ ] **Step 2: Regenerate the knowledge graph**

  Run: `graphify update .`
  Expected: the command completes and `graphify-out/graph.json` is updated.

- [ ] **Step 3: Run the whole test suite**

  Run: `cmake --build --preset release --target SonicatorTests && ~/Library/Caches/sonicator-build/release/tests/SonicatorTests`
  Expected: all tests pass, no failures. Then check that `~/Library/Sonicator/settings.json` content hash is unchanged compared to before the run (`shasum` before/after) — mtime alone is not proof.

- [ ] **Step 4: Commit and merge**

  ```bash
  git add docs/ARCHITECTURE.md graphify-out
  git commit -m "docs: waveform editor architecture and refreshed graph"
  git ls-files | grep -E ' [0-9]+\.' && echo "conflict copies present" || true
  ```
  Merge to `main` and push when the suite is green (the user's standing preference).
