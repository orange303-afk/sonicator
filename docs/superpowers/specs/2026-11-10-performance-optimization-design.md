# Performance Optimization Design

**Date:** 2026-11-10  
**Status:** Approved  
**Approach:** Hybrid Optimization (Targeted Elimination of Redundant UI-Thread Work + SQL Indexing & Windowing)

## Problem Statement
The user reported significant latency and UI freezing across four main areas:
1. **Startup (Загрузка приложения):** App takes considerable time before the window appears and becomes responsive.
2. **Starring (Звёздочка):** Toggling star on a sample causes noticeable UI hesitation.
3. **Ignoring (Игнор):** Toggling ignore on files or folders freezes the UI.
4. **Reset Tags (Сброс тэгов):** "Reset tags..." from context menu causes delays and slow execution.

## Root Causes
1. **Starring (`MainComponent.cpp:2726`):**
   - Unconditionally calls `writeRepo->refreshDuplicates()`. This wipes `stereo_pairs` and `duplicates`, executes a cartesian join over all samples without an index on `pair_key`, and executes group-by and insert loops across the entire database.
   - Triggers `reloadLibrary()`, which with default `pageSize = 10000` re-reads 10,000 full records and fires ~100 SQL statements to fetch metadata in 400-item chunks.
2. **Ignoring (`MainComponent.cpp:2700-2722`):**
   - Loops over files executing individual `BEGIN IMMEDIATE ... COMMIT` transactions per file in `ignorePath`/`unignorePath`.
   - Calls full `refreshDuplicates()`.
   - Calls `folders.refresh(...)`, which scans every single sample in the database in 5,000-row chunks on the UI thread (`pathsAfterId`) to build folder tree.
   - Immediately calls `refreshFavorites()`, which repeats the exact same full-database scan on the UI thread.
3. **Reset Tags (`TaggingService.cpp:128-131`, `SampleRepository.cpp:1701`):**
   - `clearTagsBatch` loops through IDs doing single-row `DELETE FROM sample_tags WHERE sample_id=?` queries.
   - Upon completion, triggers heavy `reloadLibrary()`.
4. **Startup (`MainComponent.cpp:470-515`):**
   - Executes `writeRepo->refreshDuplicates()` synchronously on the UI thread for the entire database.
   - Executes `folders.refresh(...)` full-database scan on UI thread.
   - Executes `refreshFavorites()` full-database scan on UI thread.
   - Executes `reloadLibrary()` with `pageSize = 10000`.
5. **Database Configuration and Indexing:**
   - Missing index on `samples(pair_key)`.
   - Missing index on `samples(ignored)`.
   - Missing index on `samples(starred)`.
   - SQLite cache is default 2MB, `PRAGMA synchronous` default.

## Architecture & Targeted Solution

### 1. Database Schema & Pragmas (`core/database/`)
- In `Database.cpp`:
  - `PRAGMA synchronous = NORMAL;` (safe for WAL, eliminates disk flush stalls).
  - `PRAGMA cache_size = -64000;` (64MB memory page cache).
  - `PRAGMA temp_store = MEMORY;`.
- In `SampleRepository::createTables`:
  - `CREATE INDEX IF NOT EXISTS samples_pair_key ON samples(pair_key) WHERE pair_key != '';`
  - `CREATE INDEX IF NOT EXISTS samples_ignored ON samples(ignored);`
  - `CREATE INDEX IF NOT EXISTS samples_starred ON samples(starred) WHERE starred = 1;`

### 2. Targeted Star Update
- In `SampleRepository`:
  - `updateDuplicateRepresentativeForSample(int64_t sampleId)`: checks if sample exists in `duplicates`. If yes, recalculates the representative ONLY for that single `group_hash`. If no, does nothing.
- In `MainComponent.cpp`:
  - In context menu star toggle (`result == 9001`):
    - Call `writeRepo->setStarred(ids, !allStarred)`.
    - For each id in `ids`: call `writeRepo->updateDuplicateRepresentativeForSample(id)`.
    - Do NOT call `writeRepo->refreshDuplicates()`.
    - Update the star flag on the loaded records in `library` directly or reload page without heavy duplicate recalculation.

### 3. Batch Ignore / Unignore & Lightweight Folder Refresh
- In `SampleRepository`:
  - `ignorePaths(const std::vector<juce::String>& paths, bool folder)`: wraps all inserts and sample updates in a single `BEGIN IMMEDIATE ... COMMIT` transaction.
  - `unignorePaths(const std::vector<juce::String>& paths)`: batch unignore in a single transaction.
- In `FolderTree`:
  - `setIgnoredPaths(const std::vector<IgnoredPath>& ignored)`: updates `ignoredEntries` and calls `repaint()` without clearing and re-scanning all paths from the database.
- In `MainComponent.cpp`:
  - Use batch ignore/unignore in a single transaction.
  - Remove `writeRepo->refreshDuplicates()`.
  - Use `folders.setIgnoredPaths(...)` and `favorites.setIgnoredPaths(...)` when toggling ignore on samples instead of full `folders.refresh(...)`.

### 4. Fast Batch Reset Tags
- In `SampleRepository::clearTagsBatch`:
  - Replace row-by-row `DELETE` with chunked `DELETE FROM sample_tags WHERE sample_id IN (?, ?, ..., ?)` (chunks of 200).
- In `MainComponent`:
  - Ensure fast UI reload after tagging completion.

### 5. Startup & Favorites Optimization
- In `MainComponent::MainComponent`:
  - Remove redundant `writeRepo->refreshDuplicates()` on startup.
  - In `refreshFavorites()`: if `appSettings.favoriteFolders.isEmpty()`, do NOT call `favorites.refresh(...)` (skipping the entire second full scan of the DB).
  - Set default `pageSize` in `AppSettings.h` to 1000 (or use virtual windowed loading for large collections).
