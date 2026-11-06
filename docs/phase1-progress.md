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
- Independent review at e4c1a16: no Critical findings; two Important findings (truncated payload handling, settings-dialog lifetime), two initially Minor findings (directory-only cancellation, per-file hash failures).
- Final: Ruling: promoted both review minors to Important for this product — cancellation/shutdown responsiveness and not abandoning healthy files after one I/O failure are user-visible correctness requirements. Both enter the single fix pass; no deferred review minors remain.
- Final: fixed audio-container completeness — valid-header/truncated AIFF regression RED→GREEN; WAV and patched outer-header cases also covered. Real library revealed incorrect RIFF sizes in complete commercial samples; a separate oversized-RIFF regression failed first, then passed with physical AUDIO-payload validation that tolerates outer size discrepancies.
- Final: fixed settings-dialog lifetime — dialog-still-live-after-app-destruction test RED→GREEN, tracked SafePointer dialog synchronously deleted before engine destruction; duplicate settings windows prevented.
- Final: fixed directory-only cancellation — callback-count test initially 0, now checkpoints directory entries as well as files.
- Final: fixed per-file hash open failures — analyzer+hash service test fails before implementation, then handles POSIX unlink-after-decode as an error result rather than aborting the import. Database/root failures remain scan-wide errors.
- Final: Ruling: analyzer version bumped to 2 so explicit rescans invalidate earlier successes; database schema stays 1. Cost: previously analyzed files need one reanalysis when rescanned.
- Final: Ruling: tolerate incorrect outer WAV/AIFF sizes and trailing ancillary metadata when audio payload bytes are complete; reject physically truncated data/SSND chunks. Compressed formats remain decoder-validated, not forensically verified. Cost: malformed ancillary metadata is not exhaustively diagnosed.
- Final: Ruling: exclude AppleDouble metadata by magic bytes after a ._ prefix check; real audio is not rejected merely for that prefix. macOS native directory enumeration can independently hide such names. Cost: cross-platform enumeration differences remain possible.
- Additional coverage: missing-file restoration, analyzer-version invalidation, Unicode filesystem→index→SQLite reopen→preview. Sparse 1.2 GB WAV reads eight blocks and cancels: 0.17 s, max RSS 43,728,896 bytes; not a full-file decode performance claim.
- macOS build validation caught linker-only ad-hoc signature without sealed resources. Added complete bundle ad-hoc signing after removing only signing-incompatible FinderInfo on the generated bundle root. Debug and Release `codesign --verify` pass. No Developer ID/notarization claim.
- Declined-to-judge rulings: later-phase ML/maps/DAW/global search remain out of Phase 1; practical transport locks and same-size/same-mtime cache policy remain documented limits; Windows/Intel, physical listening/latency, notarization and final legal distribution compliance are unverified and explicitly not release claims. Cost: owner acceptance and platform/legal checks remain necessary before shipping.
- Final external-library verification and final suite counts: see TESTING.md.
- Final: 23 cases / 205 assertions GREEN; CTest 1/1 GREEN. Optional 10k benchmark: 10,011 assertions GREEN, 12.6964 s initial / 0.98047 s repeat. External subset: 60 audio files analyzed successfully, 5 metadata forks excluded, all original hash/size/mtime checks GREEN. Production external enumeration: 299,057 candidates in 46.523 s.
- Final: Ruling: this repository was created here and has no base branch or remote; there is nothing to merge into or publish to. Preserve committed work on feature/core-browser; no merge/push performed. Cost: remote/integration setup remains the owner's choice.
- Deferred minors: none from independent review. Unverified platform/stress/release checks remain documented limitations, not claimed completed work.

## Library stability stage (2026-11-06)

- Symptom: after `299057/299057` the window still showed `0 samples`. Read-only diagnosis of the owner's database found every row persisted (298,899 analyzed, 158 errors); the list only reloaded after the job ended, and finalization re-sorted the whole table once per 1,000-row page while probing every path on disk.
- Schema v2: `samples_name_id` on `(name COLLATE NOCASE, id)` matches the UI sort; an online SQLite backup is written beside the database before a v1→v2 migration. The owner's database was never opened for writing in this stage.
- Reconciliation now compares the catalog against the path set produced by the same discovery pass, walking rows by primary key. No sort, no offset paging, no per-record filesystem call. Cancelled scans skip it, and records under unscanned roots are untouched.
- `basicFeaturesVersion = 1` replaces the unjustified bump to 2. Verified read-only that all 298,899 version-1 rows contain the full measured feature set, so they skip instead of triggering a global reanalysis.
- UI: the saved catalog is shown at startup with an explicit count, and the list refreshes at most every 1.5 s while a job runs when the row count changes.
- Branding: the three owner-supplied PNGs are stored unmodified in `resources/branding/originals/`; derived header mark and wordmark are embedded through JUCE BinaryData, and the app bundle icon is generated from the rounded tile.
- Verification: 29 cases / 265 assertions in Debug and Release; `[.scale]` 300,000 rows reconciled in 4.58 s, unchanged rescan 0.71 s; on a copy of the real catalog the index build took 0.179 s, the first 10,000-row page 0.024 s, and a full keyset walk 0.126 s.
- Ruling: keep the measured feature set at version 1 rather than inventing a second valid version — the stored rows already describe exactly what version 1 means. Cost: any hidden difference in old rows would stay undetected; mitigated by inspecting the real catalog before the decision and by documenting that a real output change must bump the constant.
- Ruling: detect missing files from the discovery set instead of probing the filesystem per record. Cost: a file created between discovery and reconciliation is reported missing until the next scan, then restored automatically.
- Ruling: one backup per schema migration, written beside the database and never deleted automatically. Cost: the first launch of the new build needs roughly 150 MB of free space.
- Ruling: the app icon uses the rounded blue-violet tile, while the flat mark is preserved as variant A. Cost: if the owner prefers variant A, it is a one-line change in `CMakeLists.txt` and `icon-header.png`.
- Deferred minors: none from this stage. Whole-catalog search, tags, Hide Duplicates, Settings, VST3/AU and ML remain backlog, not claimed work.
- Independent review of the stage raised one Critical, five Important and several Minor findings. Fixed in one pass with tests: (1) a record absent from discovery is now confirmed with a filesystem probe before being marked missing, so a partial or failed enumeration cannot flag the catalog (`findMissingIds` unit test RED→GREEN); (2) the migration backup now retries on lock, requires `SQLITE_DONE`, uses a unique name, and deletes a partial artifact instead of leaving a zero-byte file; the migration test opens the backup and checks its rows and `user_version`; (3) rows written by the short-lived version-2 build are relabelled to 1 during the one-time upgrade instead of being reanalyzed; (4) the selected sample survives a list refresh during indexing; (5) the 300k test asserts `missing` flags and a fast unchanged rescan; the page-query plan is asserted with the real column list.
- Deferred minors: the ~156 MB backup and index build still run synchronously on the first launch before the first paint; repeated 1.5 s refreshes still re-fetch and re-sort the page; status text can overwrite a database error with a device error. Each is a polish item, not a correctness defect.

## Search and tags stage (2026-11-06)

- Scope: whole-catalog text search, whole-result sorting, folder+text+tag filters, standard and custom tags with editable colours. No analysis-version bump: stored DSP output is unchanged.
- Ruling: search is a `LIKE` scan over `samples.search_key` (Unicode-folded name + path) instead of FTS5. Measured worst case in the Debug scale test — all 300,000 rows match and a 10,000-row page is sorted — was **105.85 ms**; the query count + page took 14–25 ms in a separate SQLite timing probe. Cost: the column adds tens of MB on a large catalog and the first v2→v3 launch rewrites it.
- Ruling: `search_key` is folded here, not with SQL `lower()`, because SQLite's default case folding is ASCII-only and the C locale's `towlower` does not cover Cyrillic. ASCII, Latin-1/Extended-A, Greek and Cyrillic are folded explicitly; case-less scripts match literally. Cost: other case-bearing scripts are not folded.
- Ruling: tags live in separate `tags`/`sample_tags` tables keyed by the stable sample ID, so rescan, reanalysis and upsert never clear user annotations. Standard tags are seeded only when the tag table is first created; built-ins can be renamed/recoloured but not deleted. Cost: user data and model predictions must keep separate storage as later analyzers arrive.
- Ruling: text tokens are ANDed; included tags default to AND with an explicit Any toggle; excluded tags subtract. Filtering, counting and paging use one predicate, so the count matches the pages.
- UI: 250 ms debounced search, global sort on header clicks, tag filter menu, `TAGS` column, inspector tag chips, create/rename/recolour/delete, tag assignment menu.
- Verification: **52 cases / 480 assertions** in Debug and Release; CTest 1/1; Release ad-hoc signature verified. New coverage: global/multi-token/Unicode/wildcard search, whole-result sorting and paging, all/any/exclude tag combinations, assignment survival across reanalysis and reopen, tag seeding/rename without duplication, v2→v3 migration with rows preserved and a backfilled Unicode key, header-sort callback without a reload loop, debounced search through the real shell, tag-strip layout and snapshots at 880x580 and 1440x900.
- Scale: `[.scale]` 300,000 rows — worst-case search 105.85 ms; simulated v2→v3 upgrade including the online backup and 300k-row backfill **6.86 s** (Debug). Reconciliation 4.66 s and unchanged rescan 0.72 s remain unchanged.
- The owner's real catalog was **not opened, migrated or rescanned** in this stage. Its first launch of this build will write the usual migration backup and backfill `search_key`.
- Deferred: saved filters, tag import/export and tag hierarchy; search-index tuning only if real use shows the scan is slow.
