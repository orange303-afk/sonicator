# Reset tags and favorite folders — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (native) or
> superpowers:subagent-driven-development. Steps use checkbox (`- [x]`) syntax.

**Goal:** Add a confirmed `Reset tags...` action for files and folders, and a global favorite-folder
tree below the normal folder tree.

**Architecture:** Reuse the existing separator-aware folder scope, `TaggingService` batching and
`FolderTree` hierarchy. Reset is a second operation in `TaggingService`. Favorites are a
`StringArray` in `AppSettings`, rendered by a second `FolderTree` in favorites mode; the dialog
and normal tree share the existing catalog hierarchy builder.

**Tech Stack:** C++20, JUCE 9, SQLite, Catch2.

**Spec:** `docs/superpowers/specs/2026-11-08-reset-tags-favorites-design.md`

## Global Constraints

- Schema stays at 8; favorites live in `settings.json`.
- Reset removes only `sample_tags` links; tag definitions, stars, smart overrides, originals and
  recent-tag history are untouched.
- Folder scopes are separator-aware and independent of search, filters and pagination.
- Bulk work is batched, cancellable, and reports the actual removed count.
- Favorites are global across catalogs; a favorite with no rows in the current catalog stays visible
  but muted.
- macOS Debug is the verified platform; Windows remains separate.

## Review Focus

- Empty tag scope: reset must be a no-op with an honest 0 count, not an error.
- Sibling-prefix folder (`Drums` vs `DrumsExtra`) must never be included.
- A favorite that is an ancestor of another favorite must not duplicate the subtree.
- Closing the catalog or switching catalogs during reset must not leave stale UI state.
- A missing favorite folder must stay listed and must not be silently replaced by another path.

---

### Task 1: Repository clear-tag batches and counts

**Files:**
- Modify: `core/database/SampleRepository.h`, `core/database/SampleRepository.cpp`
- Test: `tests/TestTagging.cpp`

**Interfaces:**
- Produces:
  - `TagAssignmentOutcome clearTagsBatch(const std::vector<int64_t>& sampleIds, const std::function<bool()>& isCancelled);`
  - `int countTaggedInFolder(const juce::String& folder) const;`
  - `int countTaggedSamples(const std::vector<int64_t>& sampleIds) const;`

- [x] Write failing tests for recursive clear, sibling exclusion, idempotence, counts and batch
  cancellation rollback.
- [x] Implement transactional deletion with `sqlite3_changes` counting and separator-aware counts.
- [x] Run `[tagging]` tests until green.

### Task 2: Reset operation in TaggingService

**Files:**
- Modify: `core/tagging/TaggingService.h`, `core/tagging/TaggingService.cpp`
- Test: `tests/TestTagging.cpp`

**Interfaces:**
- Produces:
  - `bool startResetFolder(const juce::String& folder);`
  - `bool startResetSelected(const std::vector<int64_t>& sampleIds);`
  - `TaggingProgress::clearing`

- [x] Write failing tests for recursive reset, idempotence, recent-history/stars preservation and
  cancellation with an actual removed count.
- [x] Add the clear operation to the existing worker without duplicating batch/cancel logic.
- [x] Run `[tagging]` tests until green.

### Task 3: Global favorites in settings

**Files:**
- Modify: `core/settings/AppSettings.h`, `core/settings/AppSettings.cpp`
- Test: `tests/TestSettings.cpp`

**Interfaces:**
- Produces: `AppSettings::favoriteFolders` (`juce::StringArray`), saved and loaded under
  `favoriteFolders`.

- [x] Write a failing round-trip/update test.
- [x] Read and write the JSON array; preserve order; ignore empty entries.
- [x] Run `[settings]` tests until green.

### Task 4: FolderTree favorites mode

**Files:**
- Modify: `ui/library/FolderTree.h`, `ui/library/FolderTree.cpp`
- Test: `tests/TestUI.cpp`

**Interfaces:**
- Produces: `void refresh(SampleRepository&, const juce::StringArray& rootPaths, bool showAllSamples);`
  and the existing `refresh(SampleRepository&)` wrapper.
- Behavior: favorite roots show catalogued descendants; expansion uses the existing lazy child
  materialization; missing roots paint muted.

- [x] Write failing tests for favorite roots, nested expansion, no All samples node and click
  callback.
- [x] Generalize the hierarchy builder; keep the All-samples path unchanged.
- [x] Run `[ui][folders]` tests until green.

### Task 5: Sidebar favorites section and menus

**Files:**
- Modify: `ui/MainComponent.h`, `ui/MainComponent.cpp`, `CMakeLists.txt` if needed
- Test: covered by repository/settings/FolderTree tests; manual UI check for menu wiring.

**Interfaces:**
- Consumes: Tasks 1–4.

- [x] Add `FAVORITES` label and second `FolderTree` below the normal tree; hide both when empty.
- [x] Add folder-menu toggle `Add to favorites` / `Remove from favorites` / `Already in favorites`
  and right-click removal on a favorite root.
- [x] Persist via `SettingsStore::update`; refresh the favorite tree; clear the other tree's
  selection on click.
- [x] Add `Reset tags...` to file and folder menus with confirmations showing scope and tagged
  counts; start the reset job; refresh after completion.
- [x] Wire cancellation/status text and catalog switching.

### Task 6: Verification and documentation

- [x] Update `docs/TAGGING.md`, `docs/phase2-progress.md`, `docs/TESTING.md`.
- [x] Run `cmake --build build -j8`, `ctest --test-dir build --output-on-failure`.
- [x] Run `graphify update .`.
- [x] Commit on `main`.
