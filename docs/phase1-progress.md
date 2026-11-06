# Phase 1 execution ledger

Spec: SONICATOR technical specification in the conversation. Scope: Core Browser only.

- [ ] 1. CMake/JUCE scaffold and dependencies
- [ ] 2. SQLite repository and migrations
- [ ] 3. Recursive scanner
- [ ] 4. Audio playback engine
- [ ] 5. Streaming DSP analyzer
- [ ] 6. Sortable library UI
- [ ] 7. Preview and waveform
- [ ] 8. Background indexing, persistence, verification

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

Pending.
