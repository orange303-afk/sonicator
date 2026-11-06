# Phase 1 execution ledger

Spec: SONICATOR technical specification in the conversation. Scope: Core Browser only.

- [x] 1. CMake/JUCE scaffold and dependencies
- [x] 2. SQLite repository and migrations
- [x] 3. Recursive scanner
- [x] 4. Audio playback engine
- [x] 5. Streaming DSP analyzer
- [x] 6. Sortable library UI
- [x] 7. Preview and waveform
- [x] 8. Background indexing, persistence, verification

## Pre-flight decisions

- Ruling: work in the supplied empty project directory, on a new feature branch; there is no existing checkout or user work to isolate.
- Ruling: verify JUCE 9.0.5 directly against upstream; use the AGPLv3 route for this source project, not an assumed commercial entitlement. Closed-source distribution requires a separate licensing decision.
- Ruling: full SHA-256 for duplicates; path + size + modification time + analysis version for incremental skipping. Distinct paths with identical content remain separate records.
- Ruling: explicit analysis error/status, not zero-filled success; persist all implemented spectral metrics and an analysis version.
- Ruling: polling thread-safe progress rather than unbounded callAsync callbacks prevents lifetime and UI-queue flooding problems.
- Ruling: audio engine has optional device initialization for headless deterministic render tests; UI opens the actual device and reports errors.
- Ruling: no plugin targets, APVTS, ML, or map in Phase 1. The JUCE plugin guide applies only to shared build/audio safety guidance.

## Shared interfaces

- Database → scanner/UI: SampleRecord and repository upsert by path, paged queries, persisted roots.
- Scanner → analyzer: cancellable traversal and chunked decode, explicit errors.
- Analyzer → repository/UI: sample metadata plus DSP results, consistent units (linear peak/RMS, spectral Hz, dimensionless flatness).
- AudioEngine → UI: load/play/pause/stop/seek/loop/gain; reader ownership changes on message thread; read-ahead thread independent of indexing.
- Indexer → UI: bounded batches and polling snapshot, no SQLite work on the audio callback.

## Verification record

- Tasks 1/2: macOS arm64 application linked. Repository tests first failed on missing implementations, then passed. Unicode fixtures corrected to use explicit UTF-8 (JUCE narrow strings are ASCII).
- Tasks 3/5: tests first failed on missing scanner/analyzer symbols. After implementation and fixing fixture overwrite semantics: `build/tests/SonicatorTests` → 8 cases / 62 assertions passed; `ctest --test-dir build --output-on-failure` → 1/1 passed.
- Ruling: Task 5 executed before Task 4 because its test contract and dependencies were already ready; both are independent core services. No feature scope change.
- Ruling: repository uses exceptions for database errors and upsert-by-path instead of silent bool failures; UI/indexer must report these exceptions. Queries are capped at 50,000 records per page.
- Tasks 4/8: missing-symbol RED runs for AudioEngine and IndexingService, then complete suite GREEN (11 cases / 109 assertions).
- Tasks 6/7: missing-symbol RED for UI, then missing-constructor RED for integrated shell; complete suite GREEN (14 cases / 125 assertions).
- Format verification: generated synthetic fixtures; WAV/AIFF/FLAC/MP3 decode and render actual samples. MP3 test initially inspected only encoder priming silence, corrected to inspect 0.18 s of rendered output.
- Final baseline: 15 cases / 157 assertions passed; CTest 1/1 passed. UI snapshots render at 880x580 and 1440x900. Optional 10,000-file test: 10,011 assertions passed, initial index 12.3733 s, unchanged rescan 0.999065 s (Debug, native arm64, short identical WAVs; not a real-library quality/performance claim).
- Application launched natively via `open`; process confirmed with `pgrep` (PID 38510). System Events UI inspection timed out, so no claim of automated native chooser interaction or physical audible-output verification.
- Ruling: current-page name search/sort and 10k-row pagination are explicit Phase 1 limits; global indexed search belongs to later work. Cost: users with >10k samples navigate pages.
- Ruling: JUCE's internal transport locks are retained for practical browser preview; no claim of a formally lock-free audio engine. Cost: strict worst-case realtime bounds remain unproven.
- `graphify update .` completed (4916 nodes, 25400 edges), generated output ignored by git.
- Final independent review pending.
