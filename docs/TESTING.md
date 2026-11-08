# Phase 1 verification

## Tags, AutoTag and folder actions (2026-11-08)

- macOS Debug: **253 cases / 2041 assertions**, all passed; CTest 1/1, about 24 s. Windows, Explorer integration and physical acceptance were not run.
- Reset tags: selection and recursive folder scope, tag links only, tag definitions/stars/overrides/recent history preserved, idempotence and cancellation rollback with the actual removed count.
- Favorite folders: global `settings.json` round-trip and unrelated-update preservation; second favorite tree shows a root and its catalogued descendants, expands like the normal tree, preserves click callback and keeps a missing root visible without fallback. The divider is draggable, persists its height, and the favorites tree grows when dragged.
- Schema **8** adds `recent_tags`; schema **9** adds the built-in Crash, Ride, Tom, Rim, Fill, Riser, Downfilter, Upfilter, Snap, Shaker and Tamb tags. Migration uses the existing SQLite backup, preserves user tags and keeps ids, tags, stars, duplicates, overrides and measured features. Migration/reopen/isolation covered.
- Recent tags: five unique newest-first entries, repeat assignment moves a tag up, rename and delete are reflected, persistence and per-catalog isolation, batch assignment counts only new links and never removes links, quick items never untoggle. The context menu shows no empty section; AutoTag does not update the history.
- Folder scope: recursive ids, separator-aware sibling-prefix rejection, independence from filters/pagination, empty and unavailable folders, subtree Scan without a new root, missing reconciliation restricted to the subtree.
- Analyze: folder pass covers only the subtree and leaves `version`, `fileHash`, `duration` and other measured fields untouched; it does not import files.
- Tag folder: background batches, idempotent repeat, real applied counts, cancellation rolls back the unfinished batch and keeps completed ones, missing ids do not abort a batch.
- AutoTag rules: all supported instrument tokens and abbreviations, Crash/Ride/Tom/Rim/Shaker/Tamb/Snap/Fill/Riser/filters classes, compound `bass drum`/`sub bass`/`808` rules, whole-token matching, case/separators/plurals, folder-only evidence, metadata and resolved smart evidence, stale/invalid candidates, manual Type override contradictions, unsupported classes, missing tags, deterministic reasons.
- Crash resolves to the Crash tag end-to-end, and does not also receive Percussion.
- AutoTag review/application: preparation writes nothing, already-assigned rows are marked, include/exclude control works in the virtualized dialog, empty and cancelled preparation, changed file and deleted tag are rejected before applying, apply is idempotent, cancellation keeps earlier batches and reports the actual count.
- Known limits: applied AutoTag reasons are not persisted; AutoTag maps onto existing tag names, so renaming a built-in instrument tag yields `tag-missing`; folder names are context only; Windows remains a separate acceptance check. See `docs/TAGGING.md`.
- A pre-existing `jassert` from a preview callback grabbing keyboard focus while `MainComponent` was not on screen was fixed by guarding the callback with `isShowing()`; the full suite now prints no JUCE assertions.
- **Session and filters:** Hide empty defaults on, loads on when the settings key is absent or malformed, and persists its toggled state. The last opened folder and selected file are restored on launch; the folder tree opens and selects the path, the row and details are restored without firing the row action, and the preview loads the waveform without autoplay.
- **Settings and dialogs:** `Backup database` is in Settings and writes the same consistent SQLite copy; the toolbar no longer has a Backup button. Confirmation and prompt AlertWindows use the application LookAndFeel (theme colours, fonts, buttons), and Settings, Relocate, Audio/MIDI and AutoTag windows use a non-native title bar drawn by the app theme. Tests verify the themed look, non-native title bar, and the Settings backup button. Settings close controls are plain cross icons rather than coloured discs.

## Catalog folder relocation (2026-11-07)

- Debug suite: **118 cases / 1029 assertions**, all passed; CTest 1/1. Release and Windows were not rerun in this stage.
- New coverage: pure path mapping in both directions (`C:\...` → `/...` and `/...` → `D:\...`), mixed separators, drive and `/` roots, sibling-prefix rejection (`/Samples2` is not `/Samples`); transactional root relocation preserving IDs, tags, stars, hashes, analysis and `search_key`; other roots and `missing` flags untouched; target collision merges tags/star and deletes only the duplicate; a nonexistent target leaves every path and root unchanged; `countUnderRoot` matches both separator styles; relocating a `C:\...` root on a POSIX host; the relocation panel enables **Relocate** only when every row has a distinct, non-overlapping target and asks for confirmation when a probe matches under 90%; the startup status hint; the automatic offer after opening a catalog with missing roots; and the full dialog apply path (backup file, relocated rows/roots, status text, dialog dismissal). The new UI tests use a temporary settings file, so the real `Sonicator/settings.json` is never written.
- UI inspected: Settings with Move/Open/Relocate at 560×700 and the relocation dialog at 680×470, both rendered from the real components.
- Known limits: relocation is prefix-based and requires the folder structure inside each root to be preserved; no automatic folder discovery; `missing` is not reset by the relocation (Rescan remains authoritative); the pre-change backup is a full database copy; Windows → macOS and macOS → Windows still need the manual round-trip described in the plan.

## Filter bar: Hide empty and DUPLICATES (2026-11-07)

- Debug and Release: **107 cases / 935 assertions**, all passed.
- The **Hide duplicates** toolbar button was removed. Byte-identical grouping, the `DUPLICATES` column (renamed from `COPIES`) and **Show all copies** remain; **Clear filters** still resets the duplicate-hash view. The internal `hideDuplicates` filter remains covered by database tests but has no UI control.
- **Hide empty** is a SQL filter (`peak >= 0.001`), so it applies to the whole catalog, pagination and virtual `All`, not only the loaded page. Files quieter than -60 dBFS are hidden; this covers digital silence and noise-floor files such as a 0.1 s placeholder with peak 0.000427. It now defaults on, persists across launches, and Clear filters resets it and saves the off state.
- New coverage: silence, noise-floor and normal-level filtering through `query` and `queryIds`; filter-bar presence and no-focus policy; clearing restores the full list.

## Waveform selection and drag-and-drop (2026-11-06)

- Debug and Release: **105 cases / 925 assertions**, all passed. CTest 1/1 in both; Debug 22.98 s, Release 21.37 s. Release bundle ad-hoc signature verified (`valid on disk`, `satisfies its Designated Requirement`).
- This stage was ported onto `main` (catalog schema v5). The owner's live catalog was already at `user_version = 5` and was not migrated, restored or rewritten by the port; the earlier schema-v2 branch build correctly refused to open a newer catalog. Existing backups were left untouched.
- `cmake/SignMacBundle.sh` now clears Finder/FileProvider extended attributes and signs in one shell step; macOS also attaches those attributes inside `~/Documents`, and clearing only `com.apple.FinderInfo` (or leaving a gap before `codesign`) was not enough for a reliable signature.
- New coverage: waveform pixel↔time round-trips, clamping/ordering, 3 px threshold, range label, clear on new file/click; library row drag path collection with multi-selection, missing-file skipping, 5 px threshold, `canMoveFiles = false` and no-start-on-empty; WAV export at 16/24-bit PCM and 32-bit float with rate/channel/frame/sample checks and unchanged source size/mtime/SHA-256; compressed FLAC conversion; range clamping and NaN/zero-length/missing rejection; no `.part` on failure; 64-channel refusal; name sanitization/uniqueness; chunked cancellation; background generation supersession; PreviewPanel Preparing/Ready/error, Clear/Escape and focus policy; selection Play start, stop-at-end and Loop wrap; rendered snapshot with a visible selection and Ready status.
- Export tests decode real WAV fixtures and synthesized WAVs; no mocked decoder. Worker and playback tests call the same functions the 30 Hz timer and transport use, directly, so the tests stay deterministic instead of depending on wall-clock timer dispatch.
- Independent review found and fixed five integration defects, each with a regression test: row mouse events never reached the drag hooks (now a recursive listener); a selection ending at EOF could loop through the file start (transport looping is now suspended while a selection owns the loop); macOS CoreAudio-decoded MP3 was exported as 32-bit float instead of 24-bit PCM; replacing a selection joined the in-flight worker on the message thread (now one persistent worker with a replaceable pending request); and finalization ignored `flush` and could publish a truncated file (now validated for format/frames/payload before rename).
- The rendered selection snapshot (`0.200–0.600 s`, Ready status, Clear button) was inspected at 900x156. `graphify update .` rebuilt 5,692 nodes / 26,739 edges / 142 communities.
- Manual external drag into Finder/desktop/messenger/DAW was **not** automated and has not yet been recorded by the owner. The Debug application was launched for that check. Until it is recorded here, the Finder/messenger/DAW delivery acceptance criterion is pending, not established.
- Exported selections are deliberately not cleaned up; the exports folder is documented as growing until manual removal.

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

## Page sizes and virtual all view (2026-11-06)

- Default suite: **73 cases / 695 assertions passed** in Debug and Release; Debug and Release CTest 1/1; Release ad-hoc bundle signature verified.
- New coverage: settings persistence for 100/1000/10000/All with invalid-value fallback; ID queries and batched records preserve catalog order and ignore missing IDs; virtual rows load on demand with tags, stars and duplicate counts; multi-selection survives a virtual refresh; the shell switches 100→All→100, loads row 119 on demand, pages to the remaining 20 and persists the choice.
- `[.scale]` with 300k rows: unfiltered ID query **34.4 ms**, filtered ID query **128.2 ms**, a 400-row block **1.9 ms**; whole-catalog search and duplicate refresh unchanged.
- Scrolling uses JUCE's virtualized table plus a 4000-row cache; no 300k-record vector is built.

### Known limits after this stage

- A filter or sort change still performs one synchronous ID query; on 300k rows this can be a brief ~130 ms hitch in Debug. Scrolling and record loading are block-based.
- Very fast scrollbar drags across the whole catalog may fetch several blocks, each a few milliseconds.

## UI polish: resize, warm palette and search field (2026-11-06)

- Fixed the window resize regression: `Window::resized()` had overridden the base implementation and only scheduled geometry saving, so the content component kept its initial size while the window grew. It now calls `DocumentWindow::resized()` before scheduling the save.
- Reworked both themes around neutral grays: dark graphite with an orange accent and neutral gray selection; light neutral gray with a burnt-orange accent. Tag colours remain semantic.
- The library search field now uses `Justification::centredLeft` and a 10 px left indent, so placeholder and typed text are vertically centred.
- Default suite unchanged: **69 cases / 664 assertions** in Debug and Release; Debug and Release CTest 1/1; Release ad-hoc signature verified. Rendered dark and light snapshots at 1200x800 were inspected.
- Manual check after relaunch: drag-resize both dimensions and confirm the sidebar, table, inspector and preview fill the window; the last geometry is restored on the next launch.

## Hide duplicates (2026-11-06)

Environment unchanged: macOS native arm64, Apple Clang 21.0.0, CMake 4.4.0, JUCE 9.0.5.

- Default suite: **69 cases / 664 assertions passed** in Debug and Release; Debug and Release CTest 1/1; Release ad-hoc bundle signature verified.
- New coverage: schema upgrades reach version 5 and create the `duplicates` table while preserving rows, tags and stars; byte-identical grouping uses hash + size and ignores unique hashes; duplicate counts, members and representative path; representative preference for starred, missing and shortest-path cases; `Hide duplicates` and group-hash filters; idempotent rebuild; indexing populates groups automatically; the shell hides one of two identical copies and `Clear filters` restores both.
- `[.scale]` still passes with 300,000 same-hash rows: reconciliation 4.79 s, unchanged rescan 0.81 s, whole-catalog search 106.3 ms, v2→v5 backfill 6.96 s.
- The representative is catalog-wide; folder-filtered views can therefore show the copy from another folder. `Show all copies` is the escape hatch.

### Known limits after this stage

- Only byte-identical files are grouped; identical decoded audio in different containers is not detected.
- Duplicate data is derived and rebuilt; there is no user override for the representative yet.
- Nothing is deleted, moved or renamed.

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

## Sampler source stage (2026-11-08)

Environment unchanged: macOS native arm64, Apple Clang, CMake, JUCE 9.0.5.

- Default suite: **270 cases / 2263 assertions passed** on this machine; CTest runs the same binary.
- New tags: `[sf2][samples]` descriptor/PCM extraction, `[sf2][audition]` immutable PCM transport,
  `[sf2][telemetry]` last-trigger seqlock and voice/layer rules, `[sf2][ui]` sample selector, MIDI
  follow, pinning, Clear/Escape and silent restore, `[sf2][export]` 16-bit source-PCM WAV export.
- Synthetic SF2 fixtures live in `tests/Sf2TestBank.h`; no external SSD or commercial library is
  required. The real-library checks in the format plan remain opt-in and are not part of CI.
- Run `./build/tests/SonicatorTests "[sf2]"` for the sampler increment, then the full suite and
  `ctest --test-dir build --output-on-failure`.

## External instrument stage (2026-11-08)

- Default suite: **289 cases / 2456 assertions** after the MIDI tails/gain fix; CTest runs the same binary.
- New tags: `[sampler][exs]` synthetic EXS24 blocks and path fallback, `[sampler][nki]` zlib XML K2/NiSS,
  `[sampler][external]` file-backed sampler, `[sampler][ui]` sample browser on EXS/NKI,
  `[sampler][indexing]` metadata-only `.exs`/`.nki` indexing, `[sampler][export]` source bit depth.
- `TestRealSamplerFormats.cpp` is hidden with `[.real]`: 8 cases / 137 assertions read the
  Samplephonics EXS24/Kontakt kits, a newer EXS variant, the 96/104/108/136-byte zone variants and
  a real NiSS drum map from `/Volumes/1TBSSD` read-only. They are not part of the normal run.
- Synthetic fixtures create EXS24 blocks and zlib-compressed NKI XML in the test temp directory; no
  commercial library or external SSD is required for CI. The loader uses the system zlib library.

## MIDI tails, polyphony and level (2026-11-08)

- `[midi-tails]` covers a two-second SF2/EXS/NKI release, 128 voices, rejection at capacity without
  stealing existing voices, AllSoundOff, and the separate short SF2 exclusive-group choke.
- Two `[.device][midi-tails]` regressions use synthetic banks with the real AudioSourcePlayer
  lifecycle. They failed when MIDI-follow reset two voices to zero and pass with source-only
  publication. They hold the existing manager callback lock during deterministic render/selection
  and require a local audio device; the default suite does not run them.
- `[midi-gain]` verifies x1.2 MIDI amplitude below the safety knee for ordinary WAV, SF2 and external
  instruments, unchanged simultaneous audition and transport, buffer offsets/oversized chunking,
  bounded dense-chord output, routed overflow panic and clearing a dropped sustain pedal-up.
- `./build/tests/SonicatorTests "[midi-tails],[midi-gain]"`: **9 cases / 85 assertions**, including
  the two opt-in device regressions. Explicit Catch2 tag filters also select hidden matching tests.
- `./build/tests/SonicatorTests "[.real][sampler]"`: **8 cases / 137 assertions**, including real
  IK_KICKS overlap after NoteOff, MIDI-follow, pin/unpin during WAV preparation and explicit Stop.
- With `SONICATOR_RX2` set to a real file, run `"[.real-rex-gain],[.real-rex-rate],[.real-rex-player]"`:
  **3 cases / 48 assertions**. REX gain is applied to MIDI slices only; whole-loop audition remains
  unchanged, and AllSoundOff preserves audition while stopping MIDI voices.
- Default suite and CTest pass. Existing JUCE non-ASCII string-literal debug assertions still print
  in preview tests; these are not new test failures. Physical listening/MIDI hardware acceptance,
  Windows and formal realtime profiling remain unverified by these render tests.
