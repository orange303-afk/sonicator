# Relocate sample folders after importing a catalog — design

Stage after library maintenance and settings. Scope: when a backed-up SONICATOR catalog is opened on
another computer, offer to point each previously analyzed root folder at its new location, so the
catalog becomes usable without reanalysis. Works in both directions between macOS and Windows.

## Context

The catalog stores absolute paths in two places: `samples.path` (every analyzed file) and
`roots.path` (the folders the user added). Preview, waveform loading, the folder tree, drag-out,
export and "Open in Finder/Explorer" all use `samples.path` directly.

"Import" today means **Settings → Open catalog...** (or a saved/default catalog at launch). There is
no path relocation and no import-only flow. A Windows catalog opened on macOS (or vice versa) has
foreign paths such as `C:\Samples\Drums` or `/Users/ilya/Samples/Drums`; those strings are invalid on
the other OS. The `missing` flags, tags, stars, hashes and analysis values are tied to rows, not to
the filesystem, so rewriting paths preserves them and avoids reanalysis.

## Goals

- After opening a catalog whose registered roots do not exist on this computer, show a relocation
  dialog automatically. Only missing roots are listed; roots that already exist are not touched.
- Offer the same dialog on demand from Settings, for a catalog that is already open.
- For each missing root, let the user choose one existing folder on this computer. Preserve the
  relative sub-path and file name below the root (`.../Drums/Kick/01.wav` → `<new>/Kick/01.wav`).
- Rewrite both `roots.path` and every affected `samples.path`, including `samples.search_key`, in one
  transaction. Keep sample IDs, tags, stars, hashes, measured features and duplicate groups.
- Treat both `/` and `\` as separators when reading old paths and write native separators for the
  current OS, so macOS → Windows and Windows → macOS both work.
- Be safe against a wrong mapping: validate the chosen folder, preview a sampled file match, write an
  automatic pre-change database backup, and roll back on any error.
- Do not prompt modally on normal startup. If a catalog has missing roots at launch, show a one-line
  status hint pointing to Settings → Relocate folders...

## Non-goals

- No automatic guessing of new folder locations.
- No per-file relinking, no matching by file name/hash inside a different layout, no merging of
  catalogs. Layout inside each relocated root must be preserved (the case the user selected).
- No schema change, no change to analysis format or feature version.
- No changes, moves or deletion of audio files on disk.
- No relocation during indexing.
- No prompt when the user copies the current catalog with **Move catalog**.

## Decisions

### Trigger

- `MainComponent::switchCatalog(target, false, error)` on success checks `missingRoots()`. If any,
  close the Settings dialog and open the relocation dialog (asynchronously, so the Settings modal
  has finished).
- `SettingsPanel` gets a **Relocate folders...** button next to Move/Open. Disabled while indexing,
  like the other catalog actions. It opens the same dialog for the current catalog.
- If there are no missing roots, the dialog reports that all catalog folders are available.
- Startup keeps the normal status text unless missing roots exist, in which case the status becomes
  a hint: `N catalog folder(s) not found on this computer — Settings → Relocate folders...`.

### Mapping rules

- A root is missing when `juce::File(root).isDirectory()` is false.
- Mapping is `newPath = newRoot + relativeSuffix`, where `relativeSuffix` is the part of the old
  sample path after the old root and its separator. Separators in the suffix are normalized to
  `juce::File::getSeparatorChar()`.
- Root matching is exact after trimming trailing separators and normalizing both `/` and `\`.
  Roots and samples in one catalog were written by one machine, so their case is consistent.
- Targets must be existing directories, must differ from each other, and must not be ancestors or
  descendants of each other (prevents two old roots collapsing onto overlapping trees).
- After a folder is chosen, the dialog counts catalog rows under that root and probes up to 200
  remapped paths with `File::existsAsFile()`. It shows `N files · M of 200 sampled found`; below 90%
  it shows a warning but still allows the user to continue. This catches "picked the wrong folder".

### Repository operation

New `SampleRepository` API:

```cpp
struct RootRelocation { juce::String oldRoot, newRoot; };
struct RelocationResult { int64_t movedFiles = 0, conflicts = 0; };
int countUnderRoot(const juce::String& root) const;
RelocationResult relocateRoots(const std::vector<RootRelocation>&);
```

- `relocateRoots` runs inside `BEGIN IMMEDIATE` / `COMMIT` under the database mutex; any error rolls
  back and rethrows.
- It streams `samples(id,name,path)` by primary key once. For each row it finds the longest matching
  old root, computes the new path with `pathrelocation::remap`, and updates `path` and
  `search_key = searchKey(name, newPath)`.
- `roots`: remove each old root, `INSERT OR IGNORE` each new root. Duplicate new roots are allowed.
- Target path collision (the same new path already exists, e.g. overlapping targets or an existing
  index under the target): keep the existing row, union its tags with the relocated row's tags,
  OR the star flag, then delete the relocated duplicate (its `sample_tags` and `duplicates` rows
  cascade). Count it in `conflicts`. This never loses user tags or stars.
- Do not clear or set `missing`. Those flags describe the old machine; the next **Rescan** is the
  authoritative reconciliation and must stay the only thing that changes them. The relocation
  status tells the user they may rescan to verify.
- Call `refreshDuplicates()` after a successful relocation, because a collision merge can change
  duplicate groups. It is otherwise idempotent.

### Safety

- Before applying, `MainComponent` writes an online SQLite backup beside the catalog:
  `<name>.relocate-<YYYYMMDD-HHMMSS>.sqlite` (same `backupTo` API as migrations). If the backup
  fails, nothing is changed.
- The status line after success reports moved files, relocated roots and the backup path.
- If relocation throws, the dialog stays open and shows the error; the transaction has rolled back.

### Cross-platform path helper

New `core/database/PathRelocation.h/.cpp` with pure string functions and an explicit separator
argument (defaulting to the native one), so both directions can be unit-tested on one host:

```cpp
namespace sonicator::pathrelocation {
    bool isUnder(const juce::String& path, const juce::String& root);
    juce::String relativeTo(const juce::String& path, const juce::String& root);
    juce::String join(const juce::String& root, const juce::String& relative,
                      juce::juce_wchar separator);
    juce::String remap(const juce::String& path, const juce::String& oldRoot,
                       const juce::String& newRoot,
                       juce::juce_wchar separator = juce::File::getSeparatorChar());
}
```

`countUnderRoot` uses SQL with both separator variants (`root + "/"` and `root + "\\"`) and escaped
`LIKE` wildcards.

### UI

New `ui/settings/RelocateFoldersPanel` in a modal `DialogWindow`, same theme and lifecycle as the
Settings dialog (close on outside click, Escape closes). Size about 680×420, scrollable list.

- Title and explanation: the folders came from an imported catalog and are not on this computer;
  analysis, tags, stars and hashes are kept.
- One row per missing root:
  - old path, elided in the middle with the full path as a tooltip;
  - stored file count;
  - **Choose...** folder picker;
  - chosen new path and sample-check result, or `Not chosen`.
- **Relocate** is enabled only when every listed root has a chosen existing folder and the targets
  are distinct/non-overlapping. **Not now** dismisses without changes.
- Applying is synchronous (one transaction, no per-file disk walk beyond the 200-sample check).
  The buttons disable while it runs; failures are shown in the dialog.

### Tests

- Pure path tests: macOS→Windows and Windows→macOS prefixes; mixed separators; root with and
  without a trailing separator; drive roots and `/`; paths equal to the root; non-matching prefix
  (`/Samples2` must not match `/Samples`).
- Repository tests: move a real catalog's root to a temp folder; assert every `samples.path` and
  `roots` row changed, `search_key` matches the new path, and IDs/tags/stars/hash/analysis are
  intact; other roots untouched; rollback on an invalid new root; collision merges tags/star and
  deletes only the duplicate; `missing` flags untouched; `countUnderRoot` accepts both separators.
- UI test: a catalog whose root was deleted switches/ opens and exposes the missing-root list; the
  Relocate button is disabled until a target is chosen. Existing switch tests keep roots present,
  so they still see no dialog.
- Manual: on macOS open a catalog produced on Windows, and on Windows open one produced on macOS;
  verify preview, folder tree, tags, stars and Rescan after relocation. Windows verification is
  separate because the local build is macOS.

## Rejected alternatives

- **Automatic folder guessing** (same path tail, home directory): unreliable across volumes and
  operating systems; a wrong guess silently points the whole catalog at unrelated files.
- **Store an alias table and resolve at read time**: would touch every query, folder filter, drag,
  export and duplicate lookup, and keeps foreign paths in rows forever.
- **Update only `roots` and rescan**: the scan inserts new rows under new paths, losing the link to
  tags, stars, hashes and analysis, and leaving dead old rows.
- **Per-file or per-subfolder relinking**: much larger UI and runtime cost, and unnecessary because
  the selected case preserves the internal structure.

## Files

- `core/database/PathRelocation.h`, `.cpp` (new)
- `core/database/SampleRepository.h`, `.cpp` (relocation, count, collision merge)
- `ui/settings/RelocateFoldersPanel.h`, `.cpp` (new)
- `ui/settings/SettingsPanel.h`, `.cpp` (button/callback)
- `ui/MainComponent.h`, `.cpp` (missing roots, dialog, status hint, switch wiring)
- `CMakeLists.txt`, `tests/CMakeLists.txt`
- `tests/TestPathRelocation.cpp` (new), `tests/TestDatabase.cpp`, `tests/TestShell.cpp`
- `README.md`, `docs/ARCHITECTURE.md`, `docs/TESTING.md`

## Risks and mitigations

- **Wrong folder chosen**: sampled existence check, explicit warning, pre-change backup.
- **Large catalog**: one transaction over moved rows only; no full filesystem validation.
- **Colliding targets**: validation rejects overlapping targets; genuine row collisions merge
  tags/star instead of aborting the whole relocation.
- **Foreign path case / separators**: separator-aware matching and explicit cross-platform tests;
  paths are written with the current OS's native separator.
- **Startup behavior**: no modal on launch, so an unplugged external drive does not nag; a status
  hint remains.
