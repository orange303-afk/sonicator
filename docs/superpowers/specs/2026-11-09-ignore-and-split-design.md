# Library ignore and silence split — design

Status: draft 2026-11-09, awaiting approval.

## Intent

Two independent additions to the library:

1. **Ignore** — a context-menu toggle for files and folders that removes them (and a folder's
   whole subtree) from the library view without touching the disk. A `Show ignored` toggle in the
   `LIBRARY` menu reveals ignored entries so the toggle can be undone.
2. **Split** — cut one file or a multi-selection of files at silence into separate WAV parts,
   written next to the originals by default or into a chosen folder. After a successful run a
   single prompt offers to move the originals to the Trash.

Both features must survive relaunch and rescan, must never modify an original before the user
confirms deletion, and must fit the existing whole-catalog SQL query, background-job and themed
dialog patterns.

## Context found while designing

- Library filtering, counts, sorting and paging are built in SQL (`buildFilterSql` in
  `core/database/SampleRepository.cpp`). Ignoring after the query would break totals and pages.
- The folder tree is materialised from catalog paths (`FolderTree::refresh`), so filtering catalog
  rows is enough to hide ignored folders and their contents.
- `AudioExporter::exportRange` already streams a time range into a validated WAV, preserving
  sample rate, channel count and the existing bit-depth policy. It is the right writer for parts.
- The example folder `/Volumes/1TBSSD/samples/!__OLDSCHOOL/AMG 160dB The Drum and Bass Interface`
  contains 63–139 s stereo 44.1 kHz/16-bit files whose loops are separated by about 2 s of digital
  silence (measured with `silencedetect` on three files). A −60 dBFS / 250 ms detector splits them
  cleanly.
- Catalog schema is version 10; `SampleRepository::createTables` already writes a timestamped
  backup before every upgrade.
- Foreign keys are enabled (`PRAGMA foreign_keys=ON`), so deleting a `samples` row cascades to tags,
  duplicates and smart tables.

## Part 1 — Ignore

### Decisions

- Ignore state lives in the catalog database, not in `settings.json`. It is library content, and
  it must be applied inside SQL so counts, pages and sorting stay consistent.
- Catalogued rows are kept and hidden; they are not deleted. Tags, stars, hashes and smart analysis
  survive, and unignoring is instant.
- Scan discovery skips ignored paths, so new files inside an ignored folder do not re-enter the
  catalog. Files added to a folder while it was ignored are picked up by the next explicit
  `Scan folder` / `Rescan` after unignoring; no scan starts automatically.
- `Show ignored` is a global, persisted setting, like `Hide empty`.

### Storage and migration (schema 11)

```sql
CREATE TABLE IF NOT EXISTS ignored_paths (
    path       TEXT PRIMARY KEY,
    is_folder  INTEGER NOT NULL DEFAULT 0,
    created_at INTEGER NOT NULL DEFAULT 0
);
```

- Paths are absolute, with native separators, exactly as stored in `samples.path`.
- A folder row covers the folder and every descendant through the existing `folderRange` prefix
  range (`path >= lower AND path < upper`).
- A file row covers that exact path only.
- Ignore entries are not cascaded: ignoring a file inside an already ignored folder stores a
  redundant-but-harmless exact row, and unignoring the folder can leave that file ignored. This
  keeps each user action independently reversible.
- `relocateRoots` rewrites `ignored_paths` in the same transaction, using the same old-root to
  new-root relative mapping as sample paths. If a remapped entry already exists, the duplicate is
  merged; an exact entry whose remapped path is already covered by an ignored folder is kept (it
  is harmless and preserves independent unignore).
- `createTables` moves `user_version` from 10 to 11 and creates the table in the same upgrade
  transaction; the existing pre-upgrade backup remains the recovery point.
- `samples.ignored` caches the derived visibility for query performance. `ignorePath` and
  `unignorePath` update it in the same transaction as the table, and every query filters on it
  instead of expanding one SQL condition per ignored path. This keeps large multi-selections well
  inside SQLite's expression and variable limits.

### Repository API

```cpp
struct IgnoredPath { juce::String path; bool folder = false; int64_t createdAt = 0; };
std::vector<IgnoredPath> ignoredPaths() const;
bool isIgnored(const juce::String& path) const;        // exact file, or any ancestor folder
bool hasExactIgnore(const juce::String& path) const;   // the menu tick state for this node
void ignorePath(const juce::String& path, bool folder);
void unignorePath(const juce::String& path);
```

All take `db.mutex`. Query building uses a private unlocked read of `ignored_paths` because
`query()` already holds the mutex.

### Query and scan integration

- `LibraryFilter::showIgnored` (default `false`).
- `buildFilterSql` appends, for every stored ignore entry and only when `showIgnored` is false:
  - folder: `AND NOT (samples.path >= ? AND samples.path < ?)`
  - file: `AND samples.path <> ?`
- `query()` and `queryIds()` are the only repository calls that honour `showIgnored`; every other
  call treats ignored rows as outside the catalog. `count()` gains an `includeIgnored` argument and
  receives `filter.showIgnored`, so the header total matches the view. `countInFolder()`,
  `pageInFolder()`, `idsInFolder()`, `countWithTagInFolder()` and `countTaggedInFolder()` always
  exclude ignored rows: `Show ignored` is a viewing preference, so folder menu counts and bulk
  tagging/AutoTag/reset scopes must never include hidden files.
- `refreshDuplicates()` and `countHash()` ignore ignored rows, so an ignored copy neither creates a
  duplicate marker nor hides a visible representative. Both `ignorePath()` and `unignorePath()`
  run `refreshDuplicates()` once after the change, in the same UI action, so markers are correct
  immediately instead of waiting for the next scan.
- `samplesNeedingSmartAnalysis()` and the smart-analysis skip pass exclude ignored rows.
- `pathsAfterId()` stays unfiltered: it is the raw catalog stream used by folder-tree building and
  missing reconciliation, which apply their own visibility rules. `findMissingIds()` skips ignored
  rows, so a hidden file that disappears is not marked missing.
- `FileScanner::scanDirectory` gains an optional `std::function<bool(const juce::File&)> shouldSkip`
  (called for directories and files) so an ignored folder is never walked. `IndexingService`
  snapshots `ignoredPaths()` when a scan begins, builds the matcher once, and passes it. Discovery
  post-filtering also drops ignored paths as a belt-and-braces check.

### UI

- **File context menu:** `Ignore` with a tick state, applied to the whole selection. Tick when every
  selected path is ignored. Clicking when not all are ignored ignores every selected path not
  already covered. Clicking when all are ignored removes the exact entries. Mixed selections are
  resolved to "ignore all" first.
- **Folder context menu:** the same `Ignore` item, stored with `is_folder = 1`. The virtual
  `All samples` node keeps its early return and never shows the item.
- A file or folder already covered by an ignored ancestor shows the item unticked and disabled as
  `Ignored with folder` / `Ignored with parent folder`.
- Ignoring the currently selected folder clears the folder filter and reloads the whole library.
- **`LIBRARY` menu:** `Show ignored` with a tick, persisted as `AppSettings::showIgnored`. Toggling
  it reloads the library and both folder trees, and repersists through `SettingsStore::update`.
- **Visibility of the selected folder:** one helper, `isFolderVisible(path, showIgnored)`, decides
  whether a folder is hidden by an ignore entry. Ignoring a folder, or turning `Show ignored` off,
  clears `selectedFolder`/`filter.folder` and the saved `lastFolder` when the current folder or one
  of its ancestors is no longer visible, so the library falls back to `All samples` instead of an
  empty page. The same check guards session restore.
- **Folder tree roots:** `FolderTree::refresh` filters rows from `pathsAfterId` with the ignore
  matcher unless `showIgnored`, and omits a registered root that is itself ignored (or under an
  ignored folder). A favorite root stays pinned and visible but muted, with its contents hidden by
  the same rule.
- **Visuals while `Show ignored` is on:** ignored rows are dimmed and their `STATUS` cell reads
  `ignored`; ignored folder-tree nodes are muted and suffixed `[ignored]`. With the toggle off the
  markers are never requested.
- `FileListPanel` receives the ignored path list through a `setIgnoredPaths` call and matches with
  the same prefix rule as the repository.
- Favorites remain user-pinned: a favorite root stays visible, but its contents follow the same
  ignore rules; the root is muted when it is itself ignored.

### Non-goals

- No file or folder is moved, renamed or deleted by Ignore.
- No ignore rules by mask or by tags; paths only.
- No automatic rescan on unignore.
- No per-catalog variant of `Show ignored`.

## Part 2 — Split by silence

### Decisions

- One architecture is sensible: a streaming detector in `core/split`, a background service that
  reuses `AudioExporter`, a themed dialog, and one post-run prompt for deletion. No external tools.
- Parts are always WAV, matching the existing selection export. Source sample rate, channel count
  and bit-depth policy are preserved (16-bit stays 16-bit, wider integer becomes 24-bit, float
  stays 32-bit float, compressed becomes 24-bit).
- Defaults: −60 dBFS peak, minimum silence 250 ms, minimum part 100 ms, 5 ms edge padding and
  5 ms fade in/out. The example folder needs only the first two.
- Output names: `<original stem>_01.wav`, `<original stem>_02.wav`, … zero-padded to at least two
  digits and widened when a file yields 100+ parts. Existing files are never overwritten; a free
  name gets `-1`, `-2`, … and a 99-attempt failure aborts only that file.

### Detector — `core/split/SilenceDetector.h/.cpp`

```cpp
struct SilenceOptions
{
    double thresholdDb     = -60.0;  // peak per grain
    double minSilenceMs    = 250.0;
    double minPartMs       = 100.0;
    double edgePaddingMs   = 5.0;
};
struct SplitPart { double startSeconds = 0, endSeconds = 0; };
struct SplitDetection
{
    std::vector<SplitPart> parts;
    int discardedShortParts = 0;   // non-silent fragments dropped by minPartMs
};

// Fewer than two parts means "no split": the caller leaves the source untouched.
SplitDetection detectSplitParts(juce::AudioFormatReader&, const SilenceOptions&, CancelCheck);
```

- Audio is read in 65 536-frame blocks. Each block is scanned in 1 ms grains; a grain's value is the
  maximum absolute sample across all channels.
- A grain at or below `10^(thresholdDb/20)` starts a silent run; the first louder grain ends it.
  Runs shorter than `minSilenceMs` are ignored and stay inside a part.
- Parts are the spans between accepted gaps, additionally trimmed of leading and trailing silence.
- Each part keeps up to `edgePaddingMs` from the adjacent gap, clamped so parts never overlap, so
  attacks and tails are not shaved.
- Parts shorter than `minPartMs` are dropped and counted (a dropped non-silent fragment means the
  original is never offered for deletion). If fewer than two parts remain, the file is reported as
  "no silence found" and left untouched.
- Detection is O(frames) time and O(parts) memory; cancellation is checked per block.

### Service — `core/split/SplitService.h/.cpp`

A `juce::Thread` with one job at a time, modelled on `IndexingService`:

```cpp
struct SplitOptions { SilenceOptions silence; juce::File outputFolder; bool sameFolder = true; };
enum class SplitFileState { split, noSplit, skipped, failed, cancelled };
struct SplitFileResult
{
    juce::File source;
    std::vector<juce::File> parts;
    juce::String error;                  // empty on success; reason for skipped/failed files
    SplitFileState state = SplitFileState::skipped;
    int detectedParts = 0;               // parts the detector returned
    int discardedShortParts = 0;         // parts dropped by minPartMs
    // True only when the file was detected, every detected part was written and validated, and
    // no part was dropped. This is the only state that may offer the original for deletion.
    bool deletable() const { return state == SplitFileState::split && discardedShortParts == 0; }
};
struct SplitProgress
{
    bool running = false, cancelled = false;
    int total = 0, processed = 0, succeeded = 0, failed = 0, skipped = 0;
    juce::String current, message;
};
bool start(const std::vector<juce::File>&, const SplitOptions&);
void cancel();
SplitProgress progress() const;
std::vector<SplitFileResult> results() const;   // stable after the thread stops
```

- Files run sequentially. A missing, undecodable, metadata-only (REX2/SF2/tracker/disc) or
  read-only-source file is skipped with a reason; the batch continues.
- Each part goes through `AudioExporter::exportRange` into a `.part` file, is validated, then moved
  to its final name. If any part fails, the file's state becomes `failed` (already written parts stay
  on disk and are listed in the summary) and it is never deletable. If the detector dropped a
  fragment shorter than `minPartMs`, the file's state stays `split` but `discardedShortParts > 0`
  and the file is not deletable: some original audio is in no part.
- Cancellation stops before the next chunk; the current `.part` is removed; completed parts stay.
  A cancelled batch never offers deletion.
- The service never deletes, moves or trashes a source. Deletion is a separate user decision.

### Exporter change

`AudioExporter::exportRange` gains a trailing `double fadeSeconds = 0` argument. A linear fade is
applied to the first and last `fadeSeconds * sampleRate` frames while streaming. Existing callers
pass nothing and keep bit-identical output. Split passes `0.005`.

### Dialog — `ui/split/SplitDialog.h/.cpp`

A themed `DialogWindow` content component, following `AutoTagDialog` / `SettingsPanel` patterns.

- Opened from the file context menu item `Split by silence...` (or
  `Split by silence (N)...`), enabled when at least one selected row exists on disk, is not
  metadata-only and is not ignored. Folder-level bulk split is a non-goal for this slice.
- Split is mutually exclusive with library jobs: it cannot start while indexing, smart analysis,
  tagging or AutoTag is running, and those jobs refuse to start while a split is running (the
  status bar says why). This removes any ambiguity about which job owns `Pause`/`Cancel`/status.
- Controls:
  - Threshold: −80…−20 dBFS, default −60, 1 dB steps, live value text.
  - Minimum silence: 50…2000 ms, default 250, 10 ms steps.
  - Minimum part: 20…2000 ms, default 100, 10 ms steps.
  - `Save next to each original file` toggle, default on. When off, a folder field with
    `Browse...`; the starting point is the first source folder.
  - `Split` and `Cancel` buttons. `Split` validates the output folder (exists or is creatable) and
    shows an inline error otherwise.
- Values persist in `settings.json` (`splitThresholdDb`, `splitMinSilenceMs`, `splitMinPartMs`,
  `splitSameFolder`).
- While running, `MainComponent::timerCallback` polls `SplitService::progress()` and shows
  `Splitting 3/12: file.wav` in the status label. The existing `Cancel` button cancels the split;
  `Pause` stays disabled because split has no pause semantics. Because jobs are mutually exclusive,
  no status-priority rule is needed.
- With `Show ignored` on, an ignored source is visible but `Split by silence...` is disabled
  (`Stop ignoring before splitting`): its parts would land in an ignored folder and the post-run
  scan would skip them.

### After a run

1. Show a themed summary: files split, parts written, files skipped and the first few reasons.
2. Index the new files. `IndexingService` gains
   `bool startFolders(const juce::StringArray&, bool force = false)` (discovery of several folders,
   `registerRoots = false`), reusing the `startSubtree` path. `MainComponent` scans the unique output
   folders automatically when the indexer is idle; when another job is running it keeps a
   `pendingSplitScan` list and starts the scan from `timerCallback` once the library job is idle.
   Switching catalogs clears `pendingSplitScan` and any pending split results.
3. If at least one file is `deletable()` (every detected part written and validated, nothing
   dropped, batch not cancelled), show one themed prompt for the whole batch, immediately after the
   split thread stops and independently of the queued part scan:
   - `Move originals to Trash` — for each deletable source, `juce::File::moveToTrash()`. Same-stem
     `.asd` and `.pk` sidecars are moved too. On success the catalog row is removed with a new
     `SampleRepository::removeByPaths` (foreign keys cascade tags, stars, duplicates, smart rows;
     an exact `ignored_paths` row for that path is removed too). Then `refreshDuplicates()` and
     `reloadLibrary()` run.
   - `Keep originals` — nothing on disk changes.
   - A failed trash move keeps the file and its catalog row and is reported in the status line.
   - Files that were skipped, failed, partially written, dropped short fragments or were cancelled
     are never offered for deletion; the summary names them and explains why.

### Non-goals

- No transient/beat detection; only silence as defined above.
- No pre-run preview of detected parts or a waveform overlay.
- No copying of tags, stars or smart overrides from the original to the parts; the automatic scan
  analyzes the new files normally.
- No splitting of REX2, SF2, tracker modules or disc-image entries.
- No folder context-menu bulk split in this slice.
- No permanent delete; only `moveToTrash`.

## Approaches considered

### Ignore

1. **Catalog table + SQL filter (chosen).** One source of truth, correct counts/pages/sorting,
   discoverable by the scanner, per-library semantics, reversible without touching files.
2. **`settings.json` list filtered in C++ after querying.** No migration, but the global list mixes
   catalogs, and post-filtering breaks `COUNT(*)`, paging and whole-catalog virtual rows.
3. **`samples.ignored` flag updated in bulk.** Fast reads, but ignores of folders with no rows yet
   are lost, and new files in ignored folders depend on a second mechanism.

### Split

1. **Streaming detector + background service + existing exporter (chosen).** Bounded memory,
   cancellable, reuses validated WAV writing and the format policy, fits the job/status pattern.
2. **Load the whole decoded file and slice in memory.** Simpler, but a 139 s stereo 32-bit float
   file is ~49 MB per file and a large batch becomes unbounded.
3. **Sheet/REX slice markers as cut points.** Only works for REX2 containers and needs a separate
   decoder; the target files are plain WAV.

## Constraints

- Originals are read-only during detection and export; the only destructive action is the explicit
  Trash prompt, and it runs only after validated parts exist.
- All paths go through `juce::File` and the separator-aware `folderRange`; Windows separators and
  case-insensitive filesystems are handled by the existing conventions.
- No new third-party dependency and no FFmpeg.
- UI work stays off the message thread except dialog updates and the summary prompt.
- The ignore list is snapshot at scan start; concurrent edits during a scan take effect on the next
  scan and on every query immediately.

## Verification

- **Schema:** upgrade 10 → 11 preserves rows and creates `ignored_paths`; reopening persists
  entries.
- **Repository:** `isIgnored` exact/ancestor cases; `query`, `queryIds`, `count`, `countInFolder`,
  `idsInFolder` exclude ignored and include them with `showIgnored`; duplicate refresh excludes
  ignored members; `removeByPaths` cascades.
- **Scanner/indexer:** ignored folders are not walked, ignored files are not inserted, existing
  ignored rows are untouched, and missing reconciliation skips ignored rows.
- **Detector:** synthetic WAVs with known tone/silence layout → expected part boundaries within
  10 ms; threshold, minimum silence, minimum part, leading/trailing trim and cancellation cases.
- **Service/exporter:** parts are named `_01…`, validation passes, no overwrite when a target
  exists, a bad file does not stop the batch, cancellation leaves completed parts and removes the
  `.part`, fade endpoints are near zero and untouched callers are unchanged; only fully complete
  files are deletable.
- **Relocation:** `relocateRoots` rewrites ignore entries together with sample paths and merges a
  collision, so ignored files stay ignored after a move.
- **Job exclusivity:** split refuses to start while a library job runs and library jobs refuse to
  start while split runs; `Cancel` and the status line always target the running job.
- **UI/settings:** `Show ignored` round-trips; split parameter defaults round-trip; context-menu
  tick states and disabled inherited states are covered where the existing UI test harness allows;
  hiding the selected folder falls back to `All samples`.
- **Manual acceptance:** copy one or two files from the AMG example folder to a temporary folder,
  split them, verify the parts and the Trash prompt, and confirm the originals are unchanged until
  the prompt is accepted. Windows remains a separate acceptance check.
- **Regression:** existing indexing, export, tagging, AutoTag, preview and folder-tree tests stay
  green.
