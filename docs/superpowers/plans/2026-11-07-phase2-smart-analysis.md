# Smart Analysis (Type / BPM / Key) — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans. Steps use `- [ ]`.

**Goal:** Add metadata-first Type/BPM/Key analysis with stored provenance, honest conflicts, user overrides and DSP fallback, without touching Phase 1 measured features.

**Architecture:** Five independent, bounded read-only extractors (name/path, WAV chunks, AIFF Apple Loops, Ableton ASD, REX2) write per-source candidates. A pure resolver turns candidates plus overrides into one display result per field. A background job extracts and resolves incrementally. DSP fallback (BPM, pitch/key) runs only where metadata is absent.

**Tech Stack:** C++20, JUCE 9.0.5, bundled SQLite, Catch2 3.7.1.

**Spec:** `docs/superpowers/specs/2026-11-07-phase2-metadata-spike.md` and the Phase 2 TZ in `/Users/ilyaorange/.opencode/plan/2026-11-07-sonicator-phase2-smart-analysis.md`.

## Status (2026-11-07)

Implemented and verified on macOS Debug:

- Task 1 smart types + resolver (`core/smart/SmartTypes.*`, `SmartResolver.*`)
- Task 2 name/folder heuristics (`NameHeuristics.*`)
- Task 3 WAV metadata (`BinaryReader.h`, `WavMetadata.*`)
- Task 4 AIFF/Apple Loops metadata (`AiffMetadata.*`)
- Task 5 ASD metadata (`AbletonAsd.*`)
- Task 6 REX2 metadata (`Rex2Metadata.*`)
- Task 7 schema v6, repository CRUD, input fingerprints (`SampleRepository.*`)
- Task 8 `SmartAnalysisService` and its start-after-index wiring in `MainComponent`

Implemented after the first plan slice: SF2 bank parser and MIDI kit sampler with a full preset menu (drums and melodic), REX2 DWOP decoder and slice player with Gate/One-shot, and metadata-only cataloguing of `.rx2`/`.sf2`. Not done yet: Type/BPM/Key UI columns, inspector and manual overrides (Task 9), DSP fallback (Tasks 10-11), final documentation/commit (Task 12).

Real-library smoke test: `SONICATOR_METADATA_ROOT=/Volumes/1TBSSD/samples build/tests/SonicatorTests '[.real-metadata]'` — 120 WAV, 120 AIFF, 120 RX2, 120 ASD, 0 structural errors, 2510 playable REX2 slices.

## Global Constraints

- Schema migration adds tables only; `basicFeaturesVersion` stays 1; no Phase 1 reanalysis.
- All parsing is read-only, bounded, and never touches the audio callback or UI thread.
- Numeric BPM/key values must be finite; out-of-range values are `Invalid`, never silently used.
- `smpl`/`inst` root note, ACID root, `basc` root are **sampler roots**, never musical key.
- `IKEY` is keywords unless its value parses as a musical key; it is stored raw.
- ASD `UnbiasedTempoEstimate == 0` or `IsSet == false` is not a tempo.
- Conflicts are stored and explained; the resolver never invents agreement.
- Overrides survive rescan, reanalysis and catalog relocation.
- macOS and Windows paths; no `std::filesystem` assumptions beyond JUCE.

## Review Focus

- A 2–5 minute loop whose name says 128 but ACID says 120: must show a conflict, not silently pick one.
- A one-shot kick with `smpl` unity note 60: must not appear as C major.
- A truncated WAV where `data` claims a huge size: extractor must stop without reading past EOF.
- An ASD from a newer Live with an unknown version byte: must be `Unsupported`, never crash.
- A sidecar deleted after the first run: its candidates must disappear from the result.

---

### Task 1: Smart types and resolver

**Files:**
- Create: `core/smart/SmartTypes.h`, `core/smart/SmartResolver.h`, `core/smart/SmartResolver.cpp`
- Test: `tests/TestSmartResolver.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `enum class SmartField { Type, Structure, Bpm, Key, RootNote }`, `enum class CandidateStatus { Valid, DefaultValue, Invalid, Unsupported }`, `struct SmartCandidate { SmartField field; juce::String source, detail, textValue; double numericValue; CandidateStatus status; }`, `struct SmartResult { ... }`, `SmartResult resolveSmart(const std::vector<SmartCandidate>&, const SmartOverrideMap&)`.

- [ ] **Step 1: Write failing tests** for: single BPM candidate resolves; two BPM candidates within 1.0 resolve; two differing by >1.0 set `bpmState = "conflicting"` and keep the highest-priority source (`rx2` > `acid` > `aiff_basc` > `id3` > `filename`); BPM outside 20–400 becomes unknown; `smpl` root never sets key; override wins and is marked `overridden`; explicit unknown override yields empty value.
- [ ] **Step 2: Run `ctest` — expect link/compile failure for missing resolver.**
- [ ] **Step 3: Implement the resolver** as a pure function with the priority table and the 1.0 BPM agreement window.
- [ ] **Step 4: Run tests — pass.**
- [ ] **Step 5: Commit.**

### Task 2: Name and folder heuristics

**Files:**
- Create: `core/smart/NameHeuristics.h`, `core/smart/NameHeuristics.cpp`
- Test: `tests/TestNameHeuristics.cpp`

**Interfaces:**
- Produces: `std::vector<SmartCandidate> extractNameCandidates(const juce::String& path)`.
- Consumes: `SmartCandidate` from Task 1.

- [ ] **Step 1: Failing tests** for: `keyC#min` → C# minor; `BbMaj` → Bb major; `Emin`/`Cmaj7`; `105bpm`; `BPM 128`; `140` alone → no BPM; `Windows_95_Startup` → no BPM; `kick`/`snare`/`hi-hat` type tokens; `loop` vs `one-shot` structure; multiple keys in one name → two candidates and resolver conflict; nearest folder `Drums/Kicks/` contributes type only as `folder` source.
- [ ] **Step 2: Run — fail.**
- [ ] **Step 3: Implement token-boundary matching** with a fixed vocabulary and mode normalization. No substring matches inside words.
- [ ] **Step 4: Run — pass.**
- [ ] **Step 5: Commit.**

### Task 3: Bounded binary reader and WAV metadata

**Files:**
- Create: `core/smart/BinaryReader.h`, `core/smart/WavMetadata.h`, `core/smart/WavMetadata.cpp`
- Test: `tests/TestWavMetadata.cpp`
- Create test fixtures by writing synthetic RIFF files in the test.

**Interfaces:**
- Produces: `std::vector<SmartCandidate> extractWavCandidates(const juce::File&, juce::String& error)`.
- Produces: `class BinaryReader` with checked `u16/u32/u64/f32/f64/bytes/skip` and explicit `ok()`.

- [ ] **Step 1: Failing tests** for: RIFF with `fmt`/`data`/`smpl`/`inst`/`acid`/`LIST INFO`/`bext`/`iXML`/`id3`/`cue`; chunk after `data` read correctly; declared size beyond EOF → parse stops, prior candidates kept, error reported; odd-sized chunk padding; `acid` tempo 140.0 and beats 16; `smpl` unity 60 → RootNote candidate only; INFO `IKEY=Am` → key candidate only if it parses, `IKEY=Cataract` → stored invalid/raw; RF64 → `Unsupported`; no PCM read.
- [ ] **Step 2: Run — fail.**
- [ ] **Step 3: Implement** the walker with seek, bounds and 8 MiB metadata cap.
- [ ] **Step 4: Run — pass.**
- [ ] **Step 5: Commit.**

### Task 4: AIFF / Apple Loops metadata

**Files:** Create `core/smart/AiffMetadata.h/.cpp`; Test `tests/TestAiffMetadata.cpp`.

**Interfaces:** Produces `std::vector<SmartCandidate> extractAiffCandidates(const juce::File&, juce::String& error)`.

- [ ] **Step 1: Failing tests** for `basc` version 1, beats 16, root 55, 4/4 → BPM from beats/duration within 0.01; root 0 → no key candidate; `root=55, scale=major` → G major only when scale is defined; `cate`/`coll` text; `trns` count; `MARK` count; truncated FORM.
- [ ] **Step 2–5: TDD and commit.**

### Task 5: Ableton ASD metadata

**Files:** Create `core/smart/AbletonAsd.h/.cpp`; Test `tests/TestAbletonAsd.cpp`.

**Interfaces:** Produces `std::vector<SmartCandidate> extractAsdCandidates(const juce::File&, juce::String& error)` and `bool asdFrameCountMatches(const juce::File&, int64_t frames)`.

- [ ] **Step 1: Failing tests** for: v6 header; two warp markers → implied BPM; one marker → none; `AufTaktData` with `IsSet=true, tempo=0` → `Invalid`; unset `DBL_MAX` → ignored; unknown version → `Unsupported`; truncated file → no crash; frame-count mismatch detected.
- [ ] **Step 2–5: TDD and commit.**

### Task 6: REX2 metadata

**Files:** Create `core/smart/Rex2Metadata.h/.cpp`; Test `tests/TestRex2Metadata.cpp`.

**Interfaces:** Produces `struct Rex2Info { double previewTempo; int sliceCount; std::vector<int64_t> sliceStarts, sliceLengths; int channels, sampleRate; bool truncated; };` and `std::optional<Rex2Info> readRex2(const juce::File&, juce::String& error)`.

- [ ] **Step 1: Failing tests** for: CAT/REX2; GLOB tempo 140000 → 140.0; SLCE boundaries; muted slice filtered; slice beyond frames rejected; nested CAT DEVL skipped; >128 slices reported; truncated chunk stops.
- [ ] **Step 2–5: TDD and commit.**

### Task 7: Schema v6 and repository

**Files:** Modify `core/database/SampleRepository.h/.cpp`, `core/database/SampleRecord.h` (only if a read model is added), `docs/ARCHITECTURE.md`; Test `tests/TestSmartDatabase.cpp`.

**Interfaces:**
- Produces: `SampleRepository::replaceSmartCandidates(int64_t sampleId, const std::vector<SmartCandidate>&)`, `smartCandidates(int64_t)`, `setSmartResult(const SmartResult&)`, `smartResult(int64_t)`, `setSmartOverride(...)`, `smartOverride(...)`, `clearSmartOverride(...)`, `samplesNeedingSmartAnalysis(int extractorVersion, int limit)`.
- Tables: `smart_candidates`, `smart_results`, `smart_overrides` (see plan body).

- [ ] **Step 1: Failing tests** for migration from v5 → v6 with backup file; IDs/tags/stars preserved; candidates replaced atomically; result survives reopen; override survives candidate replacement; cascade delete.
- [ ] **Step 2–5: TDD and commit.**

### Task 8: Background smart-analysis service

**Files:** Create `core/smart/SmartAnalysisService.h/.cpp`; Modify `core/indexing/IndexingService.*`, `ui/MainComponent.*`; Test `tests/TestSmartIndexing.cpp`.

- [ ] **Step 1: Failing tests** with synthetic files: pause/resume/cancel; completed files kept after cancel; sidecar change invalidates; no basic-feature rewrite; progress monotonic.
- [ ] **Step 2–5: TDD and commit.**

### Task 9: UI columns, filters, sorting and inspector

**Files:** Modify `ui/library/FileListPanel.*`, `ui/MainComponent.*`, `ui/settings/SettingsPanel.*` if needed; Test `tests/TestUI.cpp`.

- [ ] **Step 1: Failing tests / UI assertions** for Type/BPM/Key columns, unknown `—`, conflict marker, whole-catalog filter and sort, override editing and reset.
- [ ] **Step 2–5: TDD and commit.**

### Task 10: DSP BPM fallback

**Files:** Create `core/dsp/TempoEstimator.h/.cpp`; Test `tests/TestTempoEstimator.cpp`.

- [ ] **Step 1: Failing tests** on synthetic click tracks at 80/120/174 BPM, silence, noise, one-shots; half/double ambiguity reported, not hidden.
- [ ] **Step 2–5: TDD and commit.**

### Task 11: DSP pitch/key fallback

**Files:** Create `core/dsp/PitchKeyEstimator.h/.cpp`; Test `tests/TestPitchKeyEstimator.cpp`.

- [ ] **Step 1: Failing tests** on synthetic sines (A440 → A), chords, noise, silence, detuned and polyphonic material; no key for noise; finite output only.
- [ ] **Step 2–5: TDD and commit.**

### Task 12: Documentation, graphify, verification

- [ ] Update `README.md` limits, `docs/ARCHITECTURE.md` schema/threads, add `docs/SMART_ANALYSIS.md`.
- [ ] Run `cmake --build build` and `ctest --test-dir build --output-on-failure`.
- [ ] Run `graphify update .`.
- [ ] Report macOS-only verification honestly.

## Separate plans (do not mix into this one)

- `2026-11-07-rex2-midi-preview.md` — decode, slice mapping, Gate/One-shot, MIDI routing.
- `2026-11-07-sf2-drumkit-preview.md` — bank/preset entities, key ranges, renderer, MIDI routing.
