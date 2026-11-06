# Phase 1 verification

## Preview, folders and MIDI (2026-11-06)

- Debug and Release: **41 cases / 335 assertions**, all passed. Debug CTest 1/1, 4.55 s. Release bundle ad-hoc signature verified.
- Regression tests first reproduced unwanted selection callbacks and resetting an unchanged preview to zero. Refresh now preserves selection without replay; ordinary non-looping audio reaches EOF and stays silent.
- Recursive folder queries exclude sibling prefixes, retain deep descendants, and paginate from the whole catalog. Tree tests cover Left/Right navigation, selection preservation and overlapping registered roots.
- Real catalog UI check uses an SQLite backup fixture only: 299,918 rows, 10,000 displayed, catalog-derived tree startup **3.16 s Debug / 1.61 s Release**. Owner database was not rescanned or migrated by this stage.
- Rendered MIDI tests cover two simultaneous voices, the 16-voice bound, 2 ms attack / 20 ms release, octave pitch ratio, velocity, buffer offsets, all-notes-off, nonzero onset in the second channel, all-silent samples, Skip Silence next-trigger semantics and explicit 128 MiB rejection with a sparse WAV. Input tests cover ignored 20 kB SysEx and bounded queue overflow.
- Independent review: fixed transport stop-after-output-detach timeout; replaced unrestricted collector with a preallocated bounded queue. Overlapping-root concern was tested: the shared hierarchy retains the nested root's children. No confirmed memory-safety defect.
- Hardware MIDI, audible transient/loop tests, input hot-plug, Windows and Intel macOS are **not verified**. MIDI uses linear interpolation (high upward shifts may alias), 128 MiB decoded mono/stereo limit, and block-quantised MIDI timing. See `docs/PREVIEW.md`.

Environment: macOS, native arm64, Apple Clang 21.0.0, CMake 4.4.0, JUCE 9.0.5. Tests render actual audio buffers; no mocked decoder or invented classification data.

## Automated coverage

- SQLite persistence/reopen, stable upserts, duplicate paths, Unicode strings and open errors.
- Recursive scanner, format filtering, directory-only cancellation, full SHA-256 and AppleDouble exclusion.
- Sine/silence/short-file DSP, anti-phase stereo, corruption, physical truncation and inaccurate outer RIFF sizes.
- WAV/AIFF/FLAC/MP3 decoding and actual preview rendering; resampling, looping, transport, gain and missing-file errors.
- Incremental import, failed-file retry, force reanalysis, cancel, missing-file restoration and analyzer-version invalidation.
- UI filtering/sorting/selection, keyboard transport, persisted startup, minimum/large layout snapshots, Unicode filesystem→index→reopen→preview, settings-dialog teardown.

Final default suite for Phase 1: **23 cases / 205 assertions passed**. CTest: **1/1 passed**, 5.09 s. Current suite after the stability stage: **29 cases / 265 assertions** (see below).

## Scale and large files

- 10,000 generated real short WAV files, final version: initial index **12.6964 s**, unchanged rescan **0.98047 s**; **10,011 assertions passed**. Reopening the database restored all 10,000 records. These are tiny identical synthetic files, **not** representative mixed-library throughput.
- Sparse WAV of 1,200,000,044 bytes: decoded eight 2048-frame blocks, cooperatively cancelled on the ninth checkpoint; 6 assertions passed, elapsed 0.17 s, max RSS 43,728,896 bytes. This proves early bounded access/cancellation, not full real-recording decode throughput.

## Real library supplied by the owner

Read-only source: `/Volumes/1TBSSD/samples`. All manifests, SQLite databases and logs are under `build/manual-validation/`, outside the original library and ignored by Git.

- Filesystem inventory: **615,431 total files**, **322,946 supported-extension candidates** (319,111 WAV, 2,199 AIF, 1,636 MP3). No FLAC was found; FLAC remains covered by synthetic decoder fixtures.
- Extension-only counting includes AppleDouble/resource forks; it is not the same as real audio count. Final production scanner enumerated **299,057** candidates in **46.523 s**, max RSS **92,651,520 bytes**; **2 assertions passed**. This is enumeration, not full DSP analysis of every file.
- Selected 65 candidates using deterministic format/size-stratified sampling, including the largest discovered file (127,533,560 bytes). This is a diagnostic selection, not a statistically representative classification benchmark.
- Initial sample run exposed 5 metadata forks plus 5 complete WAVs with malformed outer container sizes. FFprobe independently confirmed the latter's decodable payloads; validation was corrected and given a synthetic regression test. No original file was repaired or rewritten.
- Each selected audio file is checked for unchanged size, mtime and full SHA-256 before/after analysis. Ten-second per-file analysis limit; database/report writes are separate.
- Final selected run: **60 analyzed, 0 decode errors, 0 time-limited, 5 metadata forks excluded**; **492 assertions passed**, total **63.99 s**, max RSS **35,749,888 bytes**. Actual audio durations ranged from **0.02728 s to 562.266 s**. Sample rates included 25 kHz, 27.778 kHz, 44.1 kHz, 48 kHz and 96 kHz, mono/stereo. All checked original sizes/mtimes/SHA-256 values remained unchanged.

## Build and GUI

- Debug and Release application targets built on native arm64.
- Complete macOS app bundle ad-hoc signature verified with `codesign --verify --verbose`.
- Development application launched natively. Offscreen snapshots use real measured synthetic audio, not a UI-only mockup.
- System Events interaction timed out; native folder-chooser automation and physical listening were **not** established.

## Independent review

One fresh reviewer checked all first-party code. No Critical findings; four user-visible issues were fixed with regression tests in one pass (container truncation, dialog lifetime, directory-only cancellation, per-file hash failure). No review minors deferred. Added real-corpus compatibility fixes were verified with failing synthetic tests and a repeated read-only corpus sample run.

## Not established

Windows / Intel macOS execution, ASan, concurrent UI-writer stress, hardware latency/listening/device switching, full analysis of all ~299k audio candidates, real >1 GB recordings, 500k interactive rendering, notarization/installers, or ML quality. Later-phase functions remain absent rather than simulated.

## Final run

- `cmake --build build -j 6` and `cmake --build build-release --target Sonicator -j 6`: success.
- `build/tests/SonicatorTests`: 23/23 cases, 205/205 assertions.
- `ctest --test-dir build --output-on-failure`: 1/1, 5.09 s.
- `[.benchmark]`: 10,011 assertions, 12.6964 s initial / 0.98047 s unchanged rescan.
- `[.large-file]`: 6 assertions, 0.17 s, bounded/cancelled sparse-file access.
- `[.external]`: 492 assertions; 60 real audio files, no errors or timeouts, 5 metadata forks excluded.
- `[.external-scan]`: 2 assertions; 299,057 candidates enumerated.
- Debug/Release `codesign --verify --verbose`: valid on disk and designated requirement satisfied (ad-hoc).
- `git diff --check`: clean.

## Library stability stage (2026-11-06)

Environment unchanged: macOS native arm64, Apple Clang 21.0.0, CMake 4.4.0, JUCE 9.0.5.

- Default suite: **29 cases / 265 assertions passed** (Debug and Release).
- New coverage: schema v1→v2 migration with an online backup created exactly once and rows preserved; keyset path stream and batch missing updates; missing detection from the discovery set without touching unscanned roots; version-1 results skipped instead of reanalyzed; catalog rows added after startup appearing in the list.
- Synthetic scale test `[.scale]`, 300,000 inserted rows and one real WAV: full scan including reconciliation **4.58 s**, unchanged second scan **0.71 s**, and the 300,000 ghost rows carry `missing = 1`; reconciliation query plan is a primary-key search with no temporary B-tree; the full 20-column page query is ordered by `samples_name_id` with no temporary B-tree.
- Read-only check on a copy of the owner's real catalog (299,057 rows): creating the index **0.179 s**; first 10,000-row page **0.024 s** (previously 0.505 s with a temp sort); a full keyset walk over all 299,057 rows **0.126 s**. Counts and every row were preserved; `user_version` stayed 1 on the original because it was never opened by the new build.
- The original `~/Library/Sonicator/sonicator.sqlite` was not opened for writing, rescanned or reanalyzed in this stage.

### Known limits after this stage

- Search and column sorting still apply to the loaded 10,000-row page, not the whole catalog.
- The first launch of the new build writes a ~150 MB backup beside the database before migrating. To restore it: quit the app, delete the database's `-wal` and `-shm` files, then copy the backup over the database file.
- A file created between discovery and reconciliation is reported missing until the next scan, then restored automatically.
- A record that disappears between discovery and reconciliation is confirmed with a single filesystem probe before it is marked missing.
