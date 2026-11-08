# Library Ignore Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an `Ignore` toggle for files and folders that hides them from the library, persists in the catalog, survives rescans, and can be revealed and undone through `LIBRARY → Show ignored`.

**Architecture:** A new `ignored_paths` table in the catalog (schema 11) is the single source of truth. `SampleRepository` applies it inside its SQL filters; the scanner skips ignored subtrees; the folder tree, file list and context menus read the same lock-free matcher. No file is ever deleted or moved by Ignore.

**Tech Stack:** C++20, JUCE 9.0.5, bundled SQLite 3.47.2, Catch2 3.7.1.

**Spec:** `docs/superpowers/specs/2026-11-09-ignore-and-split-design.md` (Part 1)

## Global Constraints

- Schema version goes from 10 to 11; the existing pre-upgrade backup stays the recovery point.
- Ignore stores absolute paths with native separators, exactly as in `samples.path`.
- Catalogued rows are never deleted by Ignore; tags, stars, hashes and smart data survive.
- `query()`/`queryIds()` are the only repository calls that honour `showIgnored`; all counts, folder scopes, bulk tagging and duplicate grouping exclude ignored rows.
- A folder entry covers every descendant through the existing `folderRange` prefix range.
- `Show ignored` is stored in `settings.json` and is global across catalogs.
- No new third-party dependency; no version bump beyond 11.
- Repeated runs of the full test suite must stay green: `ctest --test-dir build --output-on-failure`.

## Review Focus

- A file whose parent folder is ignored: its menu must show `Ignored with parent folder` disabled, never a second independent toggle that silently does nothing.
- `Show ignored` turned off while a hidden folder is selected: the library must fall back to `All samples`, not an empty page or an error.
- Two byte-identical files where one is ignored: the visible one must not be hidden by `Hide duplicates` or show a stale `×2` badge.
- Relocating a catalog root: ignore entries under the old root must move with the rows, or previously hidden files reappear.
- An ignored file deleted from disk: a later scan must not mark its row `Missing` or re-add it.
- A registered root that is fully ignored: the main tree must not keep showing the root as an empty expandable node.

---

### Task 1: Schema 11 and ignore storage

**Files:**
- Modify: `core/database/SampleRepository.h`
- Modify: `core/database/SampleRepository.cpp` (schema constant, `createTables`, new methods)
- Create: `tests/TestIgnore.cpp`
- Modify: `tests/CMakeLists.txt` (add `TestIgnore.cpp`)

**Interfaces:**
- Consumes: `Database`, `detail::Statement`, existing `folderRange`.
- Produces:
  ```cpp
  struct IgnoredPath { juce::String path; bool folder = false; int64_t createdAt = 0; };
  std::vector<IgnoredPath> ignoredPaths() const;
  bool isIgnored(const juce::String& path) const;
  bool hasExactIgnore(const juce::String& path) const;
  void ignorePath(const juce::String& path, bool folder);
  void unignorePath(const juce::String& path);
  // Lock-free matcher shared by repository, tree, list and scanner.
  bool isPathIgnored(const juce::String& path, const std::vector<IgnoredPath>& entries);
  ```

- [ ] **Step 1: Write the failing tests**

Create `tests/TestIgnore.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "core/database/SampleRepository.h"
#include "TestHelpers.h"
using namespace sonicator;

TEST_CASE("Ignore entries persist and match folder descendants", "[ignore][database]")
{
    TestDirectory t;
    auto dbFile = t.dir.getChildFile("ignore.sqlite");
    const auto folder = t.dir.getChildFile("Drums").getFullPathName();
    const auto inside = folder + "/kick.wav";
    const auto nearby = t.dir.getChildFile("Drums2/snare.wav").getFullPathName();
    {
        Database db(dbFile); SampleRepository repo(db); repo.createTables();
        repo.ignorePath(folder, true);
        repo.ignorePath(inside, false);
        REQUIRE(repo.isIgnored(folder));
        REQUIRE(repo.isIgnored(inside));
        REQUIRE(repo.isIgnored(folder + "/deep/more.wav"));
        REQUIRE_FALSE(repo.isIgnored(nearby));
        REQUIRE(repo.hasExactIgnore(folder));
        REQUIRE(repo.hasExactIgnore(inside));
        REQUIRE_FALSE(repo.hasExactIgnore(folder + "/deep/more.wav"));
        REQUIRE(repo.ignoredPaths().size() == 2);
    }
    {
        Database db(dbFile); SampleRepository repo(db); repo.createTables();
        REQUIRE(repo.isIgnored(inside));
        repo.unignorePath(folder);
        REQUIRE_FALSE(repo.isIgnored(folder + "/deep/more.wav"));
        REQUIRE(repo.isIgnored(inside)); // the exact entry survives
    }
}

TEST_CASE("Dropping ignored_paths at schema 10 recreates it and keeps rows", "[ignore][database][migration]")
{
    TestDirectory t;
    auto dbFile = t.dir.getChildFile("migrate.sqlite");
    int64_t id = 0;
    {
        Database db(dbFile); SampleRepository repo(db); repo.createTables();
        SampleRecord r; r.filePath = "/lib/a.wav"; r.fileName = "a.wav";
        id = repo.upsert(r);
    }
    {
        sqlite3* raw = nullptr;
        REQUIRE(sqlite3_open(dbFile.getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
        REQUIRE(sqlite3_exec(raw, "DROP TABLE ignored_paths; PRAGMA user_version=10;", nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(raw);
    }
    {
        Database db(dbFile); SampleRepository repo(db); repo.createTables();
        REQUIRE(repo.findByPath("/lib/a.wav")->id == id);
        REQUIRE(repo.count() == 1);
        repo.ignorePath("/lib/a.wav", false);
        REQUIRE(repo.isIgnored("/lib/a.wav"));
    }
}
```

Add `TestIgnore.cpp` to the `add_executable(SonicatorTests ...)` list in `tests/CMakeLists.txt` and include `<sqlite3.h>` in the test file for the raw steps.

- [ ] **Step 2: Run the tests to confirm they fail**

Run: `cmake --build build --target SonicatorTests --parallel 6`
Expected: compile error — `ignorePath`, `isIgnored`, `IgnoredPath` do not exist.

- [ ] **Step 3: Implement storage and the matcher**

In `SampleRepository.h`, add `struct IgnoredPath` and the five methods plus the free `isPathIgnored`. In `SampleRepository.cpp`:

```cpp
constexpr int schemaVersion = 11;
```

Extend the `CREATE TABLE IF NOT EXISTS` batch in `createTables()` with the table from the spec, and change `PRAGMA user_version=10;` to `PRAGMA user_version=11;`.

Implement `isPathIgnored` with `juce::File`:

```cpp
bool isPathIgnored(const juce::String& path, const std::vector<IgnoredPath>& entries)
{
    const juce::File file(path);
    for (const auto& entry : entries)
    {
        if (file == juce::File(entry.path)) return true;
        if (entry.folder && file.isAChildOf(juce::File(entry.path))) return true;
    }
    return false;
}
```

`ignoredPaths()` locks `db.mutex`, selects `path,is_folder,created_at ORDER BY path`, calls a private `ignoredPathsLocked()` helper so later query code can reuse it without a recursive lock. `ignorePath` uses `INSERT OR REPLACE`. `unignorePath` deletes only the exact row. `isIgnored`/`hasExactIgnore` lock and call the helper.

- [ ] **Step 4: Run the tests to confirm they pass**

Run: `build/tests/SonicatorTests "[ignore]" --reporter compact`
Expected: all ignore tests pass; run `ctest --test-dir build --output-on-failure` to confirm the suite.

- [ ] **Step 5: Commit**

```bash
git add core/database/SampleRepository.h core/database/SampleRepository.cpp tests/TestIgnore.cpp tests/CMakeLists.txt
git commit -m "feat: add catalog ignore storage (schema 11)"
```

---

### Task 2: Filter queries, counts, duplicates and smart analysis

**Files:**
- Modify: `core/database/LibraryQuery.h`
- Modify: `core/database/SampleRepository.cpp` (`buildFilterSql`, `query`, `queryIds`, counts, folder scopes, `refreshDuplicates`, `countHash`, `samplesNeedingSmartAnalysis`)
- Modify: `tests/TestIgnore.cpp`

**Interfaces:**
- Consumes: Task 1's `IgnoredPath`, `isPathIgnored`, `ignoredPathsLocked`.
- Produces:
  ```cpp
  struct LibraryFilter { /* existing fields */ bool showIgnored = false; };
  int SampleRepository::count(bool includeIgnored = false) const;
  ```
  `countInFolder`, `pageInFolder`, `idsInFolder`, `countWithTagInFolder`, `countTaggedInFolder` keep their signatures and always exclude ignored rows.

- [ ] **Step 1: Write the failing tests**

Append to `tests/TestIgnore.cpp`:

```cpp
TEST_CASE("Ignored rows leave queries, counts, folder scopes and duplicate groups", "[ignore][database][query]")
{
    TestDirectory t; Database db(t.dir.getChildFile("query.sqlite"));
    SampleRepository repo(db); repo.createTables();
    const auto root = (t.dir.getFullPathName() + "/lib");
    auto add = [&](const juce::String& path, const juce::String& hash)
    {
        SampleRecord r; r.filePath = path; r.fileName = juce::File(path).getFileName();
        r.fileHash = hash; r.fileSize = 10; r.version = basicFeaturesVersion;
        return repo.upsert(r);
    };
    const auto keep = add(root + "/keep.wav", "same");
    const auto ignoredFile = add(root + "/hidden/hide.wav", "same");
    const auto folderFile = add(root + "/skip/deep/gone.wav", "");

    repo.ignorePath(root + "/hidden/hide.wav", false);
    repo.ignorePath(root + "/skip", true);
    repo.refreshDuplicates();

    LibraryFilter f; f.folder = root;
    REQUIRE(repo.query(f).total == 1);
    REQUIRE(repo.queryIds(f) == std::vector<int64_t>{ keep });
    REQUIRE(repo.count() == 1);
    REQUIRE(repo.count(true) == 3);
    REQUIRE(repo.countInFolder(root) == 1);
    REQUIRE(repo.pageInFolder(root).size() == 1);
    REQUIRE(repo.idsInFolder(root) == std::vector<int64_t>{ keep });

    f.showIgnored = true;
    REQUIRE(repo.query(f).total == 3);
    REQUIRE(repo.queryIds(f).size() == 3);

    REQUIRE_FALSE(repo.duplicateInfo(keep).has_value());
    REQUIRE(repo.duplicateInfo(ignoredFile) == std::nullopt);
    REQUIRE(repo.samplesNeedingSmartAnalysis(0, 100) == std::vector<int64_t>{ keep });
}
```

- [ ] **Step 2: Run to confirm failure**

Run: `cmake --build build --target SonicatorTests --parallel 6 && build/tests/SonicatorTests "[ignore][query]" --reporter compact`
Expected: FAIL — counts include ignored rows; `showIgnored` and `count(bool)` do not exist.

- [ ] **Step 3: Implement filtering**

Add `bool showIgnored = false;` to `LibraryFilter`. Leave `hasTextOrTagFilter()` unchanged (it describes real filters, not visibility).

In `SampleRepository.cpp`, add a helper:

```cpp
struct IgnoreSql { juce::String where; std::vector<juce::String> params; };
IgnoreSql buildIgnoreSql(const std::vector<IgnoredPath>& ignored, const juce::String& pathColumn);
```

For each folder entry append `" AND NOT (" + pathColumn + ">=? AND " + pathColumn + "<?)"` with `folderRange(entry.path)`; for each file append `" AND " + pathColumn + "<>?"`. Return the clause and its parameters.

- `buildFilterSql` keeps its signature and gains a second argument `const std::vector<IgnoredPath>& ignored`; when `! filter.showIgnored`, append `buildIgnoreSql(ignored, "samples.path")` text and params.
- `query()`/`queryIds()` call `ignoredPathsLocked()` once and pass it in.
- `count(bool includeIgnored)`, `countInFolder`, `pageInFolder`, `idsInFolder`, `countWithTagInFolder`, `countTaggedInFolder` build a minimal `FilterSql` (or append the ignore clause) using `ignoredPathsLocked()` and bind the extra text parameters.
- `refreshDuplicates()` selects `path` per member (already selected) and skips members where `isPathIgnored(path, ignored)`, where `ignored = ignoredPathsLocked()` is read once before the transaction loop. Groups left with fewer than two members produce no rows.
- `countHash()` reads the matching paths and counts only non-ignored ones.
- `samplesNeedingSmartAnalysis()` appends `buildIgnoreSql(..., "s.id")` before `ORDER BY`, adjusting bind indexes.

- [ ] **Step 4: Run the tests**

Run: `build/tests/SonicatorTests "[ignore]" --reporter compact`
Expected: PASS. Then `ctest --test-dir build --output-on-failure`.

- [ ] **Step 5: Commit**

```bash
git add core/database/LibraryQuery.h core/database/SampleRepository.cpp tests/TestIgnore.cpp
git commit -m "feat: exclude ignored rows from library queries and duplicate groups"
```

---

### Task 3: Relocate ignore entries with catalog roots

**Files:**
- Modify: `core/database/SampleRepository.cpp` (`relocateRoots`)
- Modify: `tests/TestPathRelocation.cpp`

**Interfaces:**
- Consumes: Task 1 API, existing `pathrelocation::isUnder`, `pathrelocation::remap`.
- Produces: no new public API; `relocateRoots` rewrites `ignored_paths` in its existing transaction.

- [ ] **Step 1: Write the failing test**

Add to `tests/TestPathRelocation.cpp`:

```cpp
TEST_CASE("Root relocation moves ignore entries with the catalog", "[relocate][ignore]")
{
    TestDirectory t;
    const auto oldRoot = t.dir.getChildFile("old");
    const auto newRoot = t.dir.getChildFile("new");
    oldRoot.createDirectory(); newRoot.createDirectory();
    const auto hidden = oldRoot.getChildFile("Drums/kick.wav").getFullPathName();
    Database db(t.dir.getChildFile("relocate.sqlite")); SampleRepository repo(db); repo.createTables();
    SampleRecord r; r.filePath = hidden; r.fileName = "kick.wav"; repo.upsert(r);
    repo.ignorePath(oldRoot.getChildFile("Drums").getFullPathName(), true);
    REQUIRE(repo.relocateRoots({ { oldRoot.getFullPathName(), newRoot.getFullPathName() } }).movedFiles == 1);
    const auto moved = newRoot.getChildFile("Drums/kick.wav").getFullPathName();
    REQUIRE(repo.findByPath(moved).has_value());
    REQUIRE(repo.isIgnored(moved));
    REQUIRE_FALSE(repo.isIgnored(hidden));
}
```

- [ ] **Step 2: Run to confirm failure**

Run: `build/tests/SonicatorTests "[relocate][ignore]" --reporter compact`
Expected: FAIL — the ignore entry stays at the old root.

- [ ] **Step 3: Implement relocation of ignore rows**

After the sample loop and before the `roots` rewrite inside the same transaction, read all `ignored_paths` rows into memory. For each row find the longest matching relocation with `pathrelocation::isUnder(path, relocation.oldRoot)`, compute `pathrelocation::remap(path, oldRoot, newRoot)`, skip unchanged paths, then `DELETE FROM ignored_paths WHERE path=?` and `INSERT OR IGNORE INTO ignored_paths(path,is_folder,created_at) VALUES(?,?,?)` with the stored folder flag and timestamp. Re-read rows after the roots block is not needed.

- [ ] **Step 4: Run the tests**

Run: `build/tests/SonicatorTests "[relocate]" --reporter compact`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add core/database/SampleRepository.cpp tests/TestPathRelocation.cpp
git commit -m "fix: relocate ignore entries with catalog roots"
```

---

### Task 4: Scanner and indexer skip ignored paths

**Files:**
- Modify: `core/scanner/FileScanner.h`
- Modify: `core/scanner/FileScanner.cpp`
- Modify: `core/indexing/IndexingService.h`
- Modify: `core/indexing/IndexingService.cpp`
- Modify: `tests/TestScanner.cpp`
- Modify: `tests/TestIndexing.cpp`

**Interfaces:**
- Consumes: Task 1 `IgnoredPath`, `isPathIgnored`.
- Produces:
  ```cpp
  std::vector<AudioFileInfo> FileScanner::scanDirectory(
      const juce::File&, CancelCheck = {}, const std::function<bool(const juce::File&)>& shouldSkip = {});
  std::vector<int64_t> findMissingIds(
      const std::vector<PathEntry>&, const std::unordered_set<std::string>&,
      const juce::StringArray& roots, const std::vector<IgnoredPath>& ignored = {});
  ```

- [ ] **Step 1: Write the failing tests**

Add to `tests/TestScanner.cpp`:

```cpp
TEST_CASE("Scanner skips excluded folders and files", "[scanner][ignore]")
{
    TestDirectory t;
    t.wave("keep.wav"); t.wave("skip/hide.wav"); t.wave("other.wav");
    const auto skipRoot = t.dir.getChildFile("skip").getFullPathName();
    auto found = FileScanner::scanDirectory(t.dir, {}, [&](const juce::File& f)
    {
        return f.getFullPathName() == skipRoot || f.getFileName() == "other.wav";
    });
    REQUIRE(found.size() == 1);
    REQUIRE(found[0].file.getFileName() == "keep.wav");
}
```

Add to `tests/TestIndexing.cpp`:

```cpp
TEST_CASE("Indexing does not discover ignored files or folders", "[indexing][ignore]")
{
    TestDirectory t;
    auto keep = t.wave("keep.wav");
    t.wave("skip/hide.wav");
    Database db(t.dir.getChildFile("ignored.sqlite")); SampleRepository repo(db); repo.createTables();
    repo.ignorePath(t.dir.getChildFile("skip").getFullPathName(), true);
    IndexingService index(repo);
    REQUIRE(index.start({ t.dir.getFullPathName() })); REQUIRE(index.waitUntilFinished(20000));
    REQUIRE(repo.count() == 1);
    REQUIRE(repo.findByPath(keep.getFullPathName()).has_value());
    REQUIRE_FALSE(repo.findByPath(t.dir.getChildFile("skip/hide.wav").getFullPathName()).has_value());
}

TEST_CASE("An ignored row is not marked missing when its file disappears", "[indexing][ignore][missing]")
{
    TestDirectory t;
    auto gone = t.wave("gone.wav");
    Database db(t.dir.getChildFile("missing-ignore.sqlite")); SampleRepository repo(db); repo.createTables();
    SampleRecord r; r.filePath = gone.getFullPathName(); r.fileName = "gone.wav"; repo.upsert(r);
    repo.ignorePath(gone.getFullPathName(), false);
    REQUIRE(gone.deleteFile());
    IndexingService index(repo);
    REQUIRE(index.start({ t.dir.getFullPathName() })); REQUIRE(index.waitUntilFinished(20000));
    REQUIRE_FALSE(repo.findByPath(gone.getFullPathName())->missing);
}
```

- [ ] **Step 2: Run to confirm failure**

Run: `cmake --build build --target SonicatorTests --parallel 6`
Expected: compile error — `scanDirectory` takes too many arguments; the indexer still inserts the ignored file.

- [ ] **Step 3: Implement the skip predicate**

`FileScanner::scanDirectory` calls `shouldSkip(f)` for each directory before pushing it to `pending`, and for each supported file before adding it. A null predicate means no skipping.

In `IndexingService::run()`, at the top:

```cpp
const auto ignored = repo.ignoredPaths();
const auto skip = [ignored](const juce::File& f) { return isPathIgnored(f.getFullPathName(), ignored); };
```

Pass `skip` to `scanDirectory`, and also drop discovered entries where `skip(f.file)`. Pass `ignored` to `findMissingIds`, which returns early for ignored rows before the existence probe.

- [ ] **Step 4: Run the tests**

Run: `build/tests/SonicatorTests "[scanner][ignore]" "[indexing][ignore]" --reporter compact`
Expected: PASS. Then `ctest --test-dir build --output-on-failure`.

- [ ] **Step 5: Commit**

```bash
git add core/scanner/FileScanner.h core/scanner/FileScanner.cpp core/indexing/IndexingService.h core/indexing/IndexingService.cpp tests/TestScanner.cpp tests/TestIndexing.cpp
git commit -m "feat: skip ignored paths during discovery and reconciliation"
```

---

### Task 5: Persist `Show ignored`

**Files:**
- Modify: `core/settings/AppSettings.h`
- Modify: `core/settings/AppSettings.cpp`
- Modify: `tests/TestSettings.cpp`

**Interfaces:**
- Produces: `bool AppSettings::showIgnored = false;` read/written in `settings.json` as `showIgnored`.

- [ ] **Step 1: Write the failing test**

Add to `tests/TestSettings.cpp` a round-trip case following the existing pattern:

```cpp
TEST_CASE("Show ignored persists and defaults to off", "[settings][ignore]")
{
    TestDirectory t; SettingsStore store(t.dir.getChildFile("settings.json"));
    REQUIRE_FALSE(store.load().showIgnored);
    store.update([](AppSettings& s) { s.showIgnored = true; });
    REQUIRE(store.load().showIgnored);
}
```

- [ ] **Step 2: Run to confirm failure**

Run: `cmake --build build --target SonicatorTests --parallel 6`
Expected: compile error — `showIgnored` does not exist.

- [ ] **Step 3: Implement the field**

Add `bool showIgnored = false;` to `AppSettings`. Read it with the existing `readBool("showIgnored", settings.showIgnored)` helper in `load()` and write `object->setProperty("showIgnored", settings.showIgnored);` in `save()`.

- [ ] **Step 4: Run the test**

Run: `build/tests/SonicatorTests "[settings][ignore]" --reporter compact`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add core/settings/AppSettings.h core/settings/AppSettings.cpp tests/TestSettings.cpp
git commit -m "feat: persist the Show ignored toggle"
```

---

### Task 6: Folder tree honours ignore rules

**Files:**
- Modify: `ui/library/FolderTree.h`
- Modify: `ui/library/FolderTree.cpp`
- Modify: `tests/TestUI.cpp`

**Interfaces:**
- Consumes: Task 1 matcher, `SampleRepository::ignoredPaths()`.
- Produces:
  ```cpp
  void FolderTree::refresh(SampleRepository&);
  void FolderTree::refresh(SampleRepository&, const juce::StringArray& rootPaths,
                           bool showAllSamples, bool showIgnored = false);
  ```

- [ ] **Step 1: Write the failing test**

Add to `tests/TestUI.cpp`:

```cpp
TEST_CASE("Folder tree hides ignored folders unless Show ignored is on", "[ui][folders][ignore]")
{
    TestDirectory t; Database db(t.dir.getChildFile("tree-ignore.sqlite"));
    SampleRepository repo(db); repo.createTables();
    const auto root = t.dir.getChildFile("lib").getFullPathName();
    auto add = [&](const juce::String& path)
    {
        SampleRecord r; r.filePath = path; r.fileName = juce::File(path).getFileName(); repo.upsert(r);
    };
    add(root + "/keep/a.wav");
    add(root + "/hidden/b.wav");
    repo.ignorePath(root + "/hidden", true);
    FolderTree tree; tree.setBounds(0, 0, 300, 400); tree.resized();
    tree.refresh(repo, { root }, true, false);
    REQUIRE_FALSE(tree.selectPath(root + "/hidden"));
    tree.refresh(repo, { root }, true, true);
    REQUIRE(tree.selectPath(root + "/hidden"));

    const auto onlyHidden = t.dir.getChildFile("lib2").getFullPathName();
    SampleRecord hiddenRoot; hiddenRoot.filePath = onlyHidden + "/x.wav"; hiddenRoot.fileName = "x.wav";
    repo.upsert(hiddenRoot); repo.ignorePath(onlyHidden, true);
    tree.refresh(repo, { onlyHidden }, true, false);
    REQUIRE_FALSE(tree.selectPath(onlyHidden));
}
```

- [ ] **Step 2: Run to confirm failure**

Run: `cmake --build build --target SonicatorTests --parallel 6 && build/tests/SonicatorTests "[ui][folders][ignore]" --reporter compact`
Expected: compile error or FAIL — the fourth argument does not exist.

- [ ] **Step 3: Implement tree filtering**

Add `bool showIgnored = false;` to `FolderTree` and the fourth parameter with a default. In `refresh`, read `auto ignored = repo.ignoredPaths();` once. While iterating rows from `pathsAfterId`, `continue` when `! showIgnored && isPathIgnored(row.path, ignored)`.

For top-level roots: when `showAllSamples` is true, skip a root that is ignored (exact folder or under an ignored folder). When `showAllSamples` is false (the favorites tree), keep every pinned root. The rest of the expansion logic is unchanged.

In `Item::paintItem`, when `owner.showIgnored` is true and `isPathIgnored(path, owner.ignoredEntries)`, draw the name with `theme::muted` and append ` [ignored]`. A pinned favorite root that is itself ignored is muted even though it stays visible. Store the matcher list as a `FolderTree` member refreshed in `refresh()` so paint does not query the database.

- [ ] **Step 4: Run the tests**

Run: `build/tests/SonicatorTests "[ui][folders]" --reporter compact`
Expected: PASS, including the existing folder-tree tests.

- [ ] **Step 5: Commit**

```bash
git add ui/library/FolderTree.h ui/library/FolderTree.cpp tests/TestUI.cpp
git commit -m "feat: hide ignored folders from the folder tree"
```

---

### Task 7: File list marks ignored rows

**Files:**
- Modify: `ui/library/FileListPanel.h`
- Modify: `ui/library/FileListPanel.cpp`
- Modify: `tests/TestLibraryDrag.cpp`

**Interfaces:**
- Consumes: `IgnoredPath`, `isPathIgnored`.
- Produces:
  ```cpp
  void FileListPanel::setIgnoredPaths(std::vector<IgnoredPath>);
  juce::String sampleStatusText(const SampleRecord&, bool ignored);
  ```

- [ ] **Step 1: Write the failing test**

Add to `tests/TestLibraryDrag.cpp`:

```cpp
TEST_CASE("Ignored status is reported below decode problems", "[ui][library][ignore]")
{
    SampleRecord r; r.filePath = "/lib/a.wav"; r.fileName = "a.wav";
    REQUIRE(sampleStatusText(r, false) == "Analyzed");
    REQUIRE(sampleStatusText(r, true) == "Ignored");
    r.missing = true;
    REQUIRE(sampleStatusText(r, true) == "Missing");
    r.missing = false; r.error = "bad";
    REQUIRE(sampleStatusText(r, true) == "Decode error");
}
```

- [ ] **Step 2: Run to confirm failure**

Run: `cmake --build build --target SonicatorTests --parallel 6`
Expected: compile error — `sampleStatusText` is missing.

- [ ] **Step 3: Implement the status and dimming**

Add the free function `sampleStatusText(const SampleRecord&, bool ignored)` in `FileListPanel.h`/`.cpp`:

```cpp
juce::String sampleStatusText(const SampleRecord& r, bool ignored)
{
    if (r.missing) return "Missing";
    if (r.compatibility == AssetCompatibility::unsupported) return "Unsupported";
    if (r.compatibility == AssetCompatibility::invalid) return "Invalid";
    if (r.error.isNotEmpty()) return "Decode error";
    return ignored ? "Ignored" : "Analyzed";
}
```

`FileListPanel` stores `std::vector<IgnoredPath> ignoredPaths`, exposes `setIgnoredPaths`, and computes `const bool ignored = isPathIgnored(record.filePath, ignoredPaths)` per painted row. Dim the name and background with `theme::muted.withAlpha(0.55f)` for ignored rows, and use `sampleStatusText` in column 6. Keep error colour precedence unchanged.

- [ ] **Step 4: Run the tests**

Run: `build/tests/SonicatorTests "[ui][library][ignore]" --reporter compact`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add ui/library/FileListPanel.h ui/library/FileListPanel.cpp tests/TestLibraryDrag.cpp
git commit -m "feat: mark ignored rows in the library list"
```

---

### Task 8: Context menus, Library toggle and selection safety

**Files:**
- Modify: `ui/MainComponent.h`
- Modify: `ui/MainComponent.cpp`

**Interfaces:**
- Consumes: Tasks 1–7.
- Produces: no public API. Private additions:
  ```cpp
  void persistShowIgnored();
  void ensureSelectedFolderVisible();
  bool isFolderVisible(const juce::String& path) const;
  ```

- [ ] **Step 1: Add the Library menu toggle**

In `libraryMenu.onClick`, add `menu.addItem(6, "Show ignored", true, filter.showIgnored);` after the Smart metadata item. In the callback, `case 6:` flips `filter.showIgnored`, calls `persistShowIgnored()`, `pageOffset = 0`, `ensureSelectedFolderVisible()`, `reloadLibrary()`, `folders.refresh(...)` and `refreshFavorites()`.

Initialize `filter.showIgnored = appSettings.showIgnored;` in the constructor next to `filter.hideEmpty = appSettings.hideEmpty;`.

- [ ] **Step 2: Add Ignore to the folder menu**

In `showFolderContextMenu`, after the favorites item:

```cpp
const bool exact = readRepo->hasExactIgnore(path);
const bool inherited = ! exact && readRepo->isIgnored(path);
menu.addItem(7007, inherited ? "Ignored with parent folder" : "Ignore",
             ! inherited, exact);
```

In the callback, `result == 7007` toggles: if `exact` call `unignorePath(path)`, else `ignorePath(path, true)`. Then `refreshDuplicates()`, clear the folder selection if it is now hidden, `reloadLibrary()`, refresh both trees, and set a status message.

- [ ] **Step 3: Add Ignore to the file menu**

In `showSampleContextMenu`, build `auto ignoredPaths = readRepo->ignoredPaths();`, compute `allIgnored` over `selection` with `isPathIgnored`, and `anyInherited` when no selected path has an exact entry but at least one is ignored.

```cpp
menu.addItem(9009, anyInherited ? "Ignored with folder" : "Ignore",
             ! anyInherited, allIgnored);
```

In the callback, `result == 9009`: if `allIgnored`, call `unignorePath` for every selected path that `hasExactIgnore`; otherwise call `ignorePath(record.filePath, false)` for every selected path not already ignored. Then `refreshDuplicates()`, `reloadLibrary()`, refresh trees and set a status message.

- [ ] **Step 4: Wire reload and folder visibility**

In `reloadLibrary()`:

```cpp
filter.folder = selectedFolder;
totalCatalogCount = readRepo->count(filter.showIgnored);
library.setIgnoredPaths(readRepo->ignoredPaths());
```

Before loading rows, call `ensureSelectedFolderVisible()` so a hidden selected folder falls back to `All samples`. Replace every existing `folders.refresh(*readRepo);` call (constructor, folder import at line ~585, catalog change at ~727 and ~2120, and the scan-completion branch) with:

```cpp
folders.refresh(*readRepo, readRepo->roots(), true, filter.showIgnored);
```

and change `refreshFavorites()` to call `favorites.refresh(*readRepo, appSettings.favoriteFolders, false, filter.showIgnored);`.

`isFolderVisible(path)` returns `filter.showIgnored || ! isPathIgnored(path, readRepo->ignoredPaths())`. `ensureSelectedFolderVisible()` clears `selectedFolder`, `filter.folder`, `folders.clearSelection()` and saves `lastFolder` as empty through `settingsStore.update` when the folder or an ancestor is hidden.

- [ ] **Step 5: Build and run the full suite**

Run: `cmake --build build --parallel 6 && ctest --test-dir build --output-on-failure`
Expected: build succeeds, all tests pass.

- [ ] **Step 6: Manual acceptance**

Launch the app with a copy of a small library folder. Verify: Ignore a folder from its context menu hides it and its files; `LIBRARY → Show ignored` reveals them with `Ignored` status; unchecking Ignore restores them; Ignore on a multi-selection toggles all; a file inside an ignored folder shows the disabled inherited item; rescan does not re-add ignored files; restart preserves the hidden state.

- [ ] **Step 7: Commit**

```bash
git add ui/MainComponent.h ui/MainComponent.cpp
git commit -m "feat: add Ignore actions and Show ignored to the library UI"
```
