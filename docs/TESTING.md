# Phase 1 verification

## Auto Preview, focus and enlarged branding (2026-11-06)

- Debug and Release: **46 cases / 383 assertions**, all passed. Debug CTest 1/1, 5.88 s; Release ad-hoc signature verified.
- New regression tests first failed for absent Auto Preview, buttons taking focus, and MIDI output silencing ordinary transport. Auto Preview now defaults on and operates independently of MIDI. Manual Play remains available with Auto Preview off.
- Actual rendering tests exercise simultaneous ordinary transport and MIDI voice, including a 512-frame callback after preparation for 128 frames. Both render paths use preallocated chunking. Independent review found the transport also needed chunking to avoid JUCE resampler growth; a probe-source regression failed at 512 >64, then passed with eight 64-frame calls and preserved output offsets.
- Preview and main toolbar tests verify click-focus and keyboard-focus policies. Modal dialogs/choosers remain exceptions; text search and folder-tree navigation are not globally intercepted.
- Offscreen 1440x900 rendering inspected: larger wordmark, separate header icon removed, Auto Preview visible alongside MIDI Preview, Skip Silence and Loop. App bundle icon and branding originals unchanged.

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

## Context menu, stars and window geometry (2026-11-06)

Environment unchanged: macOS native arm64, Apple Clang 21.0.0, CMake 4.4.0, JUCE 9.0.5.

- Default suite: **67 cases / 637 assertions passed** in Debug and Release; Debug and Release CTest 1/1; Release ad-hoc bundle signature verified.
- New coverage: schema v1/v2 upgrades now reach version 4 with `starred` defaulting to false; batched star assignment and the `Starred` filter; a reanalysis upsert preserves the star; batch tag assignment; multi-row selection preserved by ID across a sorted refresh; the starred filter in the real shell; window geometry clamping, off-screen recentring, minimum size and display fit; independent settings updates keep window fields, theme and catalog path.
- The native macOS file clipboard is not asserted automatically because a test would overwrite the user's clipboard. Manual check is required.

### Known limits after this stage

- `Copy` produces real file references only on macOS; elsewhere it writes paths as text.
- Multi-selection covers the current 10,000-row page.
- `Open in Finder/Explorer` reveals the clicked row's file only; other selected folders are not opened.
- A right-click on an unselected row selects it and therefore triggers the normal auto-preview.
- The context menu itself is built in the shell and is not screenshot-tested; its database actions are covered.

## Settings and About stage (2026-11-06)

Environment unchanged: macOS native arm64, Apple Clang 21.0.0, CMake 4.4.0, JUCE 9.0.5.

- Default suite: **62 cases / 593 assertions passed** in Debug and Release; Debug and Release CTest 1/1; Release ad-hoc bundle signature verified.
- New coverage: settings JSON round-trip, defaults and invalid theme/scale; theme/scale callbacks from the dialog; the four support URLs (Gumroad, both Bandcamp pages, Ko-fi); Settings-dialog destruction before the application; live switch to an existing catalog, copy-and-switch, settings persistence, foreign-file rejection without modification; light and dark theme snapshots and main-window rendering at 1200x800.
- Rendered Settings panel checked at 560x700: theme, scale, language note, catalog controls, audio entry and the complete About block with links and Close button.
- Light theme checked at 1200x800: white panels, readable text, tags and table, typeset branding instead of the silver wordmark.

### Known limits after this stage

- Only English exists; the language value is stored for future translations.
- Scale is a fixed set of four values and applies to the whole application.
- **Move catalog** keeps the previous database file; it is not deleted automatically.
- The system dark-mode listener is wired but a live OS appearance change was not automated in tests.

## Library maintenance stage (2026-11-06)

Environment unchanged: macOS native arm64, Apple Clang 21.0.0, CMake 4.4.0, JUCE 9.0.5.

- Default suite: **57 cases / 540 assertions passed** in Debug and Release; Debug and Release CTest 1/1; Release ad-hoc bundle signature verified.
- New coverage: `NewOnly` analyzes only unknown paths, counts known files as skipped, leaves missing records unreconciled and does not reanalyze a known file changed on disk; a cancelled reanalysis of a 600-second sparse WAV keeps the previous hash/duration and stores no "Cancelled"; user backup reopens with the same rows, tag assignments and `user_version`, and refuses the live database or an existing destination; Rescan/Reanalyze confirmation dialogs open and cancel without starting a scan; the Backup button creates a readable copy in `<db folder>/backups/`.
- UI snapshots at 880x580 and 1440x900 include the new `Rescan` / `New files` row and footer `Backup` button.

### Known limits after this stage

- `New files` never reconciles missing paths and never updates known files; use **Rescan** for that.
- A user backup runs on the UI thread and may pause the window while a very large database is copied.
- User backups are never rotated or deleted automatically.

## Search and tags stage (2026-11-06)

Environment unchanged: macOS native arm64, Apple Clang 21.0.0, CMake 4.4.0, JUCE 9.0.5.

- Default suite: **52 cases / 480 assertions passed** in Debug and Release; Debug and Release CTest 1/1; Release ad-hoc bundle signature verified.
- New coverage: whole-catalog substring search (ASCII/Unicode case, multiple AND tokens), `%`/`_` escaping, folder+tag+text combinations, all/any/exclude tags, whole-result sort and paging, tag CRUD, assignment survival across upsert/reopen, tag seeding without duplication, schema v2→v3 migration with preserved rows and a backfilled search key, header-sort callback without a page-reload loop, debounced typing through the real shell, and tag-strip snapshots at 880x580 and 1440x900.
- `[.scale]` 300,000 rows: worst-case search (**all rows match**, 10,000-row page) **105.85 ms**; simulated v2→v3 upgrade including online backup and backfill **6.86 s** Debug. Existing reconciliation **4.66 s**, unchanged rescan **0.72 s**.
- The owner's real database was not opened for writing in this stage. First launch of this build will back it up and backfill `search_key` once.

### Known limits after this stage

- Search folding covers ASCII, Latin, Greek and Cyrillic; other case-bearing scripts are compared as written. Case-less scripts (CJK) need no folding.
- Tag filters are not saved between launches; tag data itself is persisted.
- A 300k-row first index upgrade needs roughly the database size in free space for the migration backup.

## Library stability stage (2026-11-06)

Environment unchanged: macOS native arm64, Apple Clang 21.0.0, CMake 4.4.0, JUCE 9.0.5.

- Default suite: **29 cases / 265 assertions passed** (Debug and Release).
- New coverage: schema v1→v2 migration with an online backup created exactly once and rows preserved; keyset path stream and batch missing updates; missing detection from the discovery set without touching unscanned roots; version-1 results skipped instead of reanalyzed; catalog rows added after startup appearing in the list.
- Synthetic scale test `[.scale]`, 300,000 inserted rows and one real WAV: full scan including reconciliation **4.58 s**, unchanged second scan **0.71 s**, and the 300,000 ghost rows carry `missing = 1`; reconciliation query plan is a primary-key search with no temporary B-tree; the full 20-column page query is ordered by `samples_name_id` with no temporary B-tree.
- Read-only check on a copy of the owner's real catalog (299,057 rows): creating the index **0.179 s**; first 10,000-row page **0.024 s** (previously 0.505 s with a temp sort); a full keyset walk over all 299,057 rows **0.126 s**. Counts and every row were preserved; `user_version` stayed 1 on the original because it was never opened by the new build.
- The original `~/Library/Sonicator/sonicator.sqlite` was not opened for writing, rescanned or reanalyzed in this stage.

### Known limits after this stage

- The first launch of the new build writes a ~150 MB backup beside the database before migrating. To restore it: quit the app, delete the database's `-wal` and `-shm` files, then copy the backup over the database file.
- A file created between discovery and reconciliation is reported missing until the next scan, then restored automatically.
- A record that disappears between discovery and reconciliation is confirmed with a single filesystem probe before it is marked missing.
