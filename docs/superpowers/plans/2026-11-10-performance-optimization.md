# Performance Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate UI latency and freeze during startup, starring, ignoring, and resetting tags by removing redundant full-database scans, optimizing SQLite queries and indexes, and batching operations.

**Architecture:** 
1. Add missing SQLite indexes (`pair_key`, `ignored`, `starred`) and configure WAL-mode pragmas (`synchronous=NORMAL`, 64MB cache).
2. Replace full `refreshDuplicates()` on star toggle with targeted single-hash representative updates.
3. Implement batch ignore/unignore in single SQLite transactions and update `FolderTree` ignored state without re-scanning all samples.
4. Replace row-by-row `DELETE` in `clearTagsBatch` with chunked `IN (...)` deletions.
5. Skip duplicate calculation and empty favorites scans on startup; tune default page size.

**Tech Stack:** C++20, JUCE, SQLite3

**Spec:** `docs/superpowers/specs/2026-11-10-performance-optimization-design.md`

## Global Constraints
- Do NOT build or run build commands until the user explicitly requests it.
- Maintain full backwards compatibility with existing `.sqlite` database files.
- Preserve existing table layouts, schema, and API contracts.

## Review Focus
- Star toggling on samples that have duplicates updates representative correctly without full-table rebuild.
- Star toggling on samples without duplicates touches no duplicate rows.
- Ignoring multiple selected files commits in a single transaction and marks them ignored correctly.
- FolderTree visual state reflects ignored paths without re-scanning all sample rows.
- Reset tags clears all assignments in `sample_tags` for the target samples in batches.

---

### Task 1: SQLite Pragmas & Indexes

**Files:**
- Modify: `core/database/Database.cpp:15-21`
- Modify: `core/database/SampleRepository.cpp:485-502`

- [ ] **Step 1: Update SQLite pragmas in `core/database/Database.cpp`**
Set `PRAGMA synchronous = NORMAL;`, `PRAGMA cache_size = -64000;`, `PRAGMA temp_store = MEMORY;` in `Database::Database`.

- [ ] **Step 2: Add missing indexes in `SampleRepository::createTables`**
Add:
- `CREATE INDEX IF NOT EXISTS samples_pair_key ON samples(pair_key) WHERE pair_key != '';`
- `CREATE INDEX IF NOT EXISTS samples_ignored ON samples(ignored);`
- `CREATE INDEX IF NOT EXISTS samples_starred ON samples(starred) WHERE starred = 1;`

---

### Task 2: Targeted Duplicate Representative Update for Star

**Files:**
- Modify: `core/database/SampleRepository.h`
- Modify: `core/database/SampleRepository.cpp`
- Modify: `ui/MainComponent.cpp:2724-2729`
- Modify: `ui/library/FileListPanel.h`
- Modify: `ui/library/FileListPanel.cpp`

- [ ] **Step 1: Add `updateDuplicateRepresentative(int64_t sampleId)` to `SampleRepository`**
Check if `sampleId` exists in `duplicates`. If yes, query members of that `group_hash`, determine representative using existing comparator, and update `is_representative` in `duplicates`.

- [ ] **Step 2: Add `setStarredInPlace(const std::vector<int64_t>& ids, bool starred)` to `FileListPanel`**
Update in-memory `starred` property on matching records and call `table.repaint()` for immediate feedback.

- [ ] **Step 3: Update star action in `MainComponent.cpp`**
Replace `safe->writeRepo->refreshDuplicates()` with `safe->writeRepo->updateDuplicateRepresentative(...)` and in-place UI update.

---

### Task 3: Batch Ignore/Unignore & Lightweight Folder Refresh

**Files:**
- Modify: `core/database/SampleRepository.h`
- Modify: `core/database/SampleRepository.cpp`
- Modify: `ui/library/FolderTree.h`
- Modify: `ui/library/FolderTree.cpp`
- Modify: `ui/MainComponent.cpp:2700-2722`

- [ ] **Step 1: Add batch ignore/unignore to `SampleRepository`**
Implement `ignorePaths(const std::vector<juce::String>& paths, bool folder)` and `unignorePaths(const std::vector<juce::String>& paths)` running in a single `BEGIN IMMEDIATE ... COMMIT;` transaction.

- [ ] **Step 2: Add `setIgnoredPaths(const std::vector<IgnoredPath>& ignored)` to `FolderTree`**
Update `ignoredEntries` member and repaint without clearing `children` and without iterating `pathsAfterId`.

- [ ] **Step 3: Update Ignore handler in `MainComponent.cpp`**
Use batch ignore/unignore, remove `refreshDuplicates()`, call `setIgnoredPaths` on folder trees, and reload current library page.

---

### Task 4: Fast Batch Reset Tags

**Files:**
- Modify: `core/database/SampleRepository.cpp:1701-1734`

- [ ] **Step 1: Optimize `SampleRepository::clearTagsBatch`**
Delete in chunks of 200 IDs using `DELETE FROM sample_tags WHERE sample_id IN (?, ..., ?)` instead of single-ID queries.

---

### Task 5: Startup & Page Size Tuning

**Files:**
- Modify: `ui/MainComponent.cpp:474-509`
- Modify: `ui/MainComponent.cpp:1586-1596`
- Modify: `core/settings/AppSettings.h:11`
- Modify: `core/settings/AppSettings.cpp:42-43`

- [ ] **Step 1: Remove redundant startup `refreshDuplicates()` in `MainComponent::MainComponent`**
Startup catalog loading does not need to re-index all stereo pairs and duplicate groups unless modified.

- [ ] **Step 2: Guard `refreshFavorites()` when favorites list is empty**
If `appSettings.favoriteFolders.isEmpty()`, skip calling `favorites.refresh(...)` avoiding full database scan.

- [ ] **Step 3: Tune default `pageSize`**
Change default `pageSize` in `AppSettings` from 10,000 to 1,000 to keep initial and page reload latencies under 50ms while retaining extensive scrolling content.
