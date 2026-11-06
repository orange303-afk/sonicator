# Library stability implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a 300k-file catalog appear immediately and finish scanning quickly, and keep existing analyzer results valid across upgrades.

**Architecture:** SQLite schema v2 adds an index matching the UI sort and takes an online backup before migrating. The indexer validates missing files against the discovery set in one primary-key pass instead of re-sorting and stat-ing the table. The analyzer version becomes the version of the measured feature set it actually describes (1), so current rows skip. The UI refreshes while a job runs.

**Tech Stack:** C++20, JUCE 9.0.5, SQLite (bundled amalgamation), Catch2 v3, CMake.

**Spec:** `docs/superpowers/specs/2026-11-06-library-stability-design.md`

## Global Constraints

- The user's `~/Library/Sonicator/sonicator.sqlite` is never reset, recreated, or reanalyzed by this work.
- Original audio files are never modified.
- Schema version must never decrease; a database with `user_version > 2` is refused.
- `PRAGMA journal_mode=WAL` is already on; backups must use the SQLite backup API, not a file copy.
- Every task ends with a commit on `feature/core-browser`.

## Review Focus

- **A v1 database with 299k rows migrating while WAL pages are pending** — must produce a backup that contains every row, and must not lose rows.
- **A root that becomes unreachable or a scan that is cancelled during discovery** — records under untouched roots must not be marked missing.
- **A rescan where nothing changed (298 899 skipped)** — the finalization pass must still finish in seconds, not minutes.
- **Two instances of the app on one database** — `BEGIN IMMEDIATE` + busy timeout must fail cleanly rather than corrupt.
- **A file appearing between discovery and reconciliation** — it may be flagged missing until the next scan, but must never be deleted from the catalog.

---

### Task 1: Schema version 2 with a pre-migration backup

**Files:**
- Modify: `core/database/Database.h`, `core/database/Database.cpp`
- Modify: `core/database/SampleRepository.cpp:19-31`
- Test: `tests/TestDatabase.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `const juce::File& Database::file() const` — the path the database was opened with.
- Produces: schema version 2 containing index `samples_name_id`.

- [ ] **Step 1: Write the failing test**

Append to `tests/TestDatabase.cpp` (include `<sqlite3.h>`):

```cpp
static void execRaw(sqlite3* db, const char* sql) { REQUIRE(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK); }

TEST_CASE("Schema v1 migrates in place, keeps rows, and backs up once", "[database][migration]")
{
    TestDirectory t;
    auto file = t.dir.getChildFile("legacy.sqlite");
    {
        sqlite3* raw = nullptr;
        REQUIRE(sqlite3_open(file.getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
        execRaw(raw, "CREATE TABLE samples (id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE,"
                     "name TEXT NOT NULL, hash TEXT NOT NULL, format TEXT NOT NULL, error TEXT NOT NULL,"
                     "size INTEGER, modified INTEGER, analyzed INTEGER, duration REAL, rate REAL, bits INTEGER,"
                     "channels INTEGER, version INTEGER, peak REAL, rms REAL, centroid REAL, rolloff REAL, flatness REAL, missing INTEGER);");
        execRaw(raw, "INSERT INTO samples(path,name,hash,format,error,version) VALUES('/a.wav','a.wav','h1','WAV','',1),"
                     "('/b.wav','b.wav','h2','WAV','',1);");
        execRaw(raw, "PRAGMA user_version=1;");
        sqlite3_close(raw);
    }
    {
        Database db(file); SampleRepository repo(db); repo.createTables();
        REQUIRE(repo.count() == 2);
    }
    auto backups = t.dir.findChildFiles(juce::File::findFiles, false, "legacy.sqlite.backup-*");
    REQUIRE(backups.size() == 1);
    {
        sqlite3* raw = nullptr;
        REQUIRE(sqlite3_open(file.getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
        sqlite3_stmt* s = nullptr;
        REQUIRE(sqlite3_prepare_v2(raw, "PRAGMA user_version", -1, &s, nullptr) == SQLITE_OK);
        REQUIRE(sqlite3_step(s) == SQLITE_ROW); REQUIRE(sqlite3_column_int(s, 0) == 2); sqlite3_finalize(s);
        REQUIRE(sqlite3_prepare_v2(raw, "SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name='samples_name_id'", -1, &s, nullptr) == SQLITE_OK);
        REQUIRE(sqlite3_step(s) == SQLITE_ROW); REQUIRE(sqlite3_column_int(s, 0) == 1); sqlite3_finalize(s);
        sqlite3_close(raw);
    }
    { Database db(file); SampleRepository repo(db); repo.createTables(); }
    REQUIRE(t.dir.findChildFiles(juce::File::findFiles, false, "legacy.sqlite.backup-*").size() == 1);
}
```

`TestDirectory` is in `tests/TestHelpers.h`; the file already includes it via other tests — add the include if missing.

- [ ] **Step 2: Run the test and see it fail**

Run: `cmake --build build-debug --target SonicatorTests -j8 && ./build-debug/tests/SonicatorTests "Schema v1 migrates in place, keeps rows, and backs up once"`
Expected: FAIL — no `samples_name_id`, `user_version` stays 1, no backup.

- [ ] **Step 3: Implement**

`core/database/Database.h`: add `const juce::File& file() const { return path; }` and a private `juce::File path;`.
`core/database/Database.cpp`: initialise `path { f }` before opening.

`core/database/SampleRepository.cpp`, anonymous namespace:

```cpp
void backupDatabase(sqlite3* source, const juce::File& destination)
{
    sqlite3* out = nullptr;
    if (sqlite3_open_v2(destination.getFullPathName().toRawUTF8(), &out,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
    { if (out) sqlite3_close(out); throw std::runtime_error("Cannot create database backup"); }
    auto* backup = sqlite3_backup_init(out, "main", source, "main");
    if (backup == nullptr) { auto message = juce::String(sqlite3_errmsg(out)); sqlite3_close(out); throw std::runtime_error(message.toStdString()); }
    sqlite3_backup_step(backup, -1);
    auto rc = sqlite3_backup_finish(backup);
    sqlite3_close(out);
    if (rc != SQLITE_OK) throw std::runtime_error("Database backup failed");
}
```

`createTables()` becomes:

```cpp
std::lock_guard lock(db.mutex);
detail::Statement version(db.handle, "PRAGMA user_version");
const int schemaVersion = version.step() ? (int) version.integer(0) : 0;
if (schemaVersion > 2) throw std::runtime_error("Database schema is newer than this application");
if (schemaVersion >= 1 && schemaVersion < 2)
{
    auto target = db.file().getSiblingFile(db.file().getFileName() + ".backup-"
        + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S"));
    backupDatabase(db.handle, target);
}
detail::exec(db.handle,
    "BEGIN IMMEDIATE;"
    "CREATE TABLE IF NOT EXISTS samples (...same columns as today...);"
    "CREATE INDEX IF NOT EXISTS samples_hash ON samples(hash);"
    "CREATE INDEX IF NOT EXISTS samples_name_id ON samples(name COLLATE NOCASE, id);"
    "CREATE TABLE IF NOT EXISTS roots(path TEXT PRIMARY KEY);"
    "PRAGMA user_version=2;"
    "COMMIT;");
```

If any statement throws, roll back with `detail::exec(db.handle, "ROLLBACK;")` guarded by a try/catch.

- [ ] **Step 4: Run tests**

Run: `./build-debug/tests/SonicatorTests "[database]"`
Expected: all pass, including the new migration test.

- [ ] **Step 5: Commit**

```bash
git add core/database/Database.* core/database/SampleRepository.cpp tests/TestDatabase.cpp
git commit -m "feat: schema v2 with sort index and pre-migration backup"
```

---

### Task 2: Primary-key path stream and batch missing updates

**Files:**
- Modify: `core/database/SampleRepository.h`, `core/database/SampleRepository.cpp`
- Test: `tests/TestDatabase.cpp`

**Interfaces:**
- Produces:
  - `struct PathEntry { int64_t id = 0; juce::String path; bool missing = false; };`
  - `std::vector<PathEntry> pathsAfterId(int64_t lastId, int limit = 5000) const;`
  - `void setMissing(const std::vector<int64_t>& ids);`

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("Path stream walks by primary key and marks missing in batches", "[database]")
{
    TestDirectory t; Database db(t.dir.getChildFile("c.sqlite")); SampleRepository repo(db); repo.createTables();
    SampleRecord r; r.filePath = "/x/c.wav"; r.fileName = "c.wav"; r.fileHash = "hc";
    auto idC = repo.upsert(r); r.filePath = "/x/a.wav"; r.fileName = "a.wav"; r.fileHash = "ha"; auto idA = repo.upsert(r);
    r.filePath = "/x/b.wav"; r.fileName = "b.wav"; r.fileHash = "hb"; auto idB = repo.upsert(r);
    auto first = repo.pathsAfterId(0, 2); REQUIRE(first.size() == 2);
    REQUIRE(first[0].id < first[1].id);
    auto second = repo.pathsAfterId(first.back().id, 2); REQUIRE(second.size() == 1);
    REQUIRE(second[0].path == "/x/b.wav");
    repo.setMissing({idA, idB});
    REQUIRE(repo.findByPath("/x/a.wav")->missing);
    REQUIRE(repo.findByPath("/x/b.wav")->missing);
    REQUIRE_FALSE(repo.findByPath("/x/c.wav")->missing);
}
```

Note: rows are returned in `id` order, which is insertion order (`c`, `a`, `b`), so `second[0].path == "/x/b.wav"`.

- [ ] **Step 2: Run and see it fail** — compile error: `pathsAfterId` not declared.

- [ ] **Step 3: Implement**

`pathsAfterId`: `SELECT id,path,missing FROM samples WHERE id>? ORDER BY id LIMIT ?`.
`setMissing`: `UPDATE samples SET missing=1 WHERE id=?` inside one `BEGIN`/`COMMIT`; ids may be empty (return early).

- [ ] **Step 4: Run tests** — `./build-debug/tests/SonicatorTests "[database]"` passes.

- [ ] **Step 5: Commit**

```bash
git add core/database/SampleRepository.* tests/TestDatabase.cpp
git commit -m "feat: keyset path stream and batch missing updates"
```

---

### Task 3: Reconcile missing files against the discovery set

**Files:**
- Modify: `core/indexing/IndexingService.h`, `core/indexing/IndexingService.cpp:72-89`
- Test: `tests/TestIndexing.cpp`

**Interfaces:**
- Consumes: `SampleRepository::pathsAfterId`, `SampleRepository::setMissing` (Task 2).
- Produces: `IndexingProgress::finalizing` (`bool`), true only while reconciliation runs.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("Missing files are detected from the discovery set without touching other roots", "[indexing]")
{
    TestDirectory t;
    auto audio = t.dir.getChildFile("audio"); audio.createDirectory();
    t.wave("audio/keep.wav", 440); t.wave("audio/gone.wav", 550);
    Database db(t.dir.getChildFile("catalog.sqlite")); SampleRepository repo(db); repo.createTables();
    IndexingService index(repo);
    REQUIRE(index.start({audio.getFullPathName()})); REQUIRE(index.waitUntilFinished(10000));
    REQUIRE(repo.count() == 2);
    SampleRecord outside; outside.filePath = "/elsewhere/not-scanned.wav"; outside.fileName = "not-scanned.wav";
    outside.fileHash = "h"; outside.version = basicFeaturesVersion; repo.upsert(outside);
    REQUIRE(audio.getChildFile("gone.wav").deleteFile());
    REQUIRE(index.start({audio.getFullPathName()})); REQUIRE(index.waitUntilFinished(10000));
    REQUIRE(repo.findByPath(audio.getChildFile("gone.wav").getFullPathName())->missing);
    REQUIRE_FALSE(repo.findByPath(audio.getChildFile("keep.wav").getFullPathName())->missing);
    REQUIRE_FALSE(repo.findByPath("/elsewhere/not-scanned.wav")->missing);
    REQUIRE_FALSE(index.progress().finalizing);
}
```

- [ ] **Step 2: Run and see it fail** — `finalizing` does not exist; `/elsewhere` would be unaffected today but `gone.wav` stays not-missing until fixed only if the old path loop misses it — the test fails to compile first.

- [ ] **Step 3: Implement**

Add `bool finalizing = false;` to `IndexingProgress`, reset by `state = {}` in `start()`.

In `run()`, keep the discovery path set:

```cpp
std::unordered_set<std::string> foundPaths;   // replaces the temporary `seen`
```

Insert every accepted `f.file.getFullPathName().toStdString()` into it (it currently deduplicates the same way).

Replace the `for (int offset = 0;; ...)` block with:

```cpp
if (! cancelled)
{
    { std::lock_guard lock(stateMutex); state.finalizing = true; state.message = "Checking for missing files"; }
    int64_t lastId = 0;
    for (;;)
    {
        if (checkpoint()) break;
        auto rows = repo.pathsAfterId(lastId, 5000);
        if (rows.empty()) break;
        std::vector<int64_t> missingIds;
        for (const auto& row : rows)
        {
            lastId = row.id;
            if (row.missing) continue;
            const juce::File file(row.path);
            bool inRoot = false;
            for (const auto& root : folders) if (file.isAChildOf(juce::File(root))) { inRoot = true; break; }
            if (inRoot && foundPaths.find(row.path.toStdString()) == foundPaths.end()) missingIds.push_back(row.id);
        }
        if (! missingIds.empty()) repo.setMissing(missingIds);
    }
    { std::lock_guard lock(stateMutex); state.finalizing = false; }
}
```

The final message stays `"Scan complete"`.

- [ ] **Step 4: Run tests** — `./build-debug/tests/SonicatorTests "[indexing]"` passes, including the existing missing/restore test.

- [ ] **Step 5: Commit**

```bash
git add core/indexing/IndexingService.* tests/TestIndexing.cpp
git commit -m "fix: reconcile missing files in one keyset pass"
```

---

### Task 4: Name the analyzer version after the feature set it describes

**Files:**
- Modify: `core/database/SampleRecord.h:4`
- Modify: `core/indexing/IndexingService.cpp:57-59`
- Modify: `tests/TestDatabase.cpp:18,40`, `tests/TestIndexing.cpp:39`
- Test: `tests/TestIndexing.cpp`

**Interfaces:**
- Produces: `inline constexpr int basicFeaturesVersion = 1;` replacing `analysisVersion`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("Rows written with the current feature version are skipped, not reanalyzed", "[indexing][version]")
{
    TestDirectory t; auto file = t.wave("audio/tone.wav");
    auto dbPath = t.dir.getChildFile("catalog.sqlite");
    Database db(dbPath); SampleRepository repo(db); repo.createTables();
    auto analyzed = BasicAnalyzer().analyzeAndHash(file);
    analyzed.fileSize = file.getSize(); analyzed.dateModified = file.getLastModificationTime().toMilliseconds();
    analyzed.version = 1; repo.upsert(analyzed);
    REQUIRE(basicFeaturesVersion == 1);
    IndexingService index(repo);
    REQUIRE(index.start({file.getParentDirectory().getFullPathName()})); REQUIRE(index.waitUntilFinished(10000));
    REQUIRE(index.progress().skipped == 1);
    REQUIRE(index.progress().analyzed == 0);
}
```

- [ ] **Step 2: Run and see it fail** — `basicFeaturesVersion` is not declared.

- [ ] **Step 3: Implement**

```cpp
// Version of the measured DSP feature set stored in SampleRecord:
// duration, rate, bits, channels, peak, rms, centroid, rolloff, flatness, hash.
// Bump only when that output changes, and say so in docs/ARCHITECTURE.md.
inline constexpr int basicFeaturesVersion = 1;
```

Update the comparison in `IndexingService::run()` and the two test references.

- [ ] **Step 4: Run tests** — `./build-debug/tests/SonicatorTests "[indexing]"` and `"[database]"` pass.

- [ ] **Step 5: Commit**

```bash
git add core/database/SampleRecord.h core/indexing/IndexingService.cpp tests
git commit -m "fix: keep measured v1 results valid across builds"
```

---

### Task 5: Show the catalog immediately and refresh it during a scan

**Files:**
- Modify: `ui/MainComponent.h`, `ui/MainComponent.cpp:47-60,126-143`
- Test: `tests/TestShell.cpp`

**Interfaces:**
- Consumes: `SampleRepository::count()` (existing).
- Produces: nothing other tasks use.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("An existing catalog is visible on startup and grows during indexing", "[ui][integration]")
{
    TestDirectory t; auto audio = t.wave("a.wav"); auto dbPath = t.dir.getChildFile("library.sqlite");
    Database db(dbPath); SampleRepository repo(db); repo.createTables();
    auto r = BasicAnalyzer().analyze(audio); repo.upsert(r); repo.addRoot(t.dir.getFullPathName());
    MainComponent app(dbPath, false);
    auto* library = dynamic_cast<FileListPanel*>(app.findChildWithID("library"));
    REQUIRE(library != nullptr); REQUIRE(library->getNumRows() == 1);
    juce::TextButton* rescan = nullptr;
    for (auto* child : app.getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*>(child)) if (b->getButtonText() == "Rescan library") rescan = b;
    REQUIRE(rescan != nullptr); REQUIRE(rescan->isEnabled());
    rescan->onClick();
    auto deadline = juce::Time::getMillisecondCounter() + 10000;
    while (library->getNumRows() < 2 && juce::Time::getMillisecondCounter() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    REQUIRE(library->getNumRows() == 2);
}
```

The second row comes from a second `t.wave("b.wav")` created **before** `rescan->onClick()`; add it after constructing the app:

```cpp
t.wave("b.wav", 660);
```

- [ ] **Step 2: Run and see it fail** — the list stays at 1 because the timer only reloads after the job ends and 10 s may pass before the test observes it; on the current code the job ends quickly and reloads, so this test may pass. Run it and record the result. If it passes on the old code, keep it as a regression guard and continue — the timing assertion is what matters.

- [ ] **Step 3: Implement**

In `timerCallback()`, after the status block:

```cpp
if (p.running)
{
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastRefreshMs >= 1500)
    {
        lastRefreshMs = now;
        if (readRepo && readRepo->count() != catalogCount) reloadLibrary();
    }
}
if (wasRunning && ! p.running) reloadLibrary();
```

Add `uint32_t lastRefreshMs = 0;` to the header. Call `reloadLibrary()` at the end of `beginImport()` and `scanRoots()` when a job starts.

In the constructor, replace the unconditional ready text:

```cpp
status.setText(catalogCount > 0
    ? juce::String(catalogCount) + " samples loaded"
    : "Ready. Add a folder to begin.", juce::dontSendNotification);
```

- [ ] **Step 4: Run tests** — `./build-debug/tests/SonicatorTests "[ui]"` passes.

- [ ] **Step 5: Commit**

```bash
git add ui/MainComponent.* tests/TestShell.cpp
git commit -m "fix: show saved samples immediately and refresh during indexing"
```

---

### Task 6: Prove the fix at 300k rows

**Files:**
- Modify: `tests/TestScale.cpp`
- Test: the new tagged case

**Interfaces:**
- Consumes: Tasks 1–4.

- [ ] **Step 1: Write the test (tagged, excluded from the default run)**

```cpp
TEST_CASE("Three hundred thousand rows finalize quickly with correct missing flags", "[.scale]")
{
    TestDirectory t;
    auto audio = t.dir.getChildFile("audio"); audio.createDirectory();
    t.wave("audio/keep.wav", 440);
    auto dbPath = t.dir.getChildFile("catalog.sqlite");
    Database db(dbPath); SampleRepository repo(db); repo.createTables();
    {
        sqlite3* raw = nullptr; REQUIRE(sqlite3_open(dbPath.getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
        REQUIRE(sqlite3_exec(raw, "BEGIN", nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_stmt* s = nullptr;
        REQUIRE(sqlite3_prepare_v2(raw, "INSERT INTO samples(path,name,hash,format,error,size,modified,version,missing)"
            " VALUES(?,?,'h','WAV','',0,0,1,0)", -1, &s, nullptr) == SQLITE_OK);
        for (int i = 0; i < 300000; ++i)
        {
            auto path = audio.getChildFile("ghost_" + juce::String(i) + ".wav").getFullPathName();
            sqlite3_bind_text(s, 1, path.toRawUTF8(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(s, 2, ("ghost_" + juce::String(i) + ".wav").toRawUTF8(), -1, SQLITE_TRANSIENT);
            REQUIRE(sqlite3_step(s) == SQLITE_DONE);
            sqlite3_reset(s);
        }
        sqlite3_finalize(s);
        REQUIRE(sqlite3_exec(raw, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(raw);
    }
    IndexingService index(repo);
    const auto started = juce::Time::getMillisecondCounterHiRes();
    REQUIRE(index.start({audio.getFullPathName()})); REQUIRE(index.waitUntilFinished(120000));
    const auto seconds = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;
    REQUIRE(repo.count() == 300001);
    REQUIRE(repo.findByPath(audio.getChildFile("keep.wav").getFullPathName())->missing == false);
    REQUIRE(index.progress().analyzed >= 1);
    REQUIRE(seconds < 60.0);
    std::cout << "300k reconciliation: " << seconds << " s\n";
}
```

- [ ] **Step 2: Run it and check the query plan**

Run:
```bash
./build-debug/tests/SonicatorTests "[.scale]" -s
sqlite3 :memory: "ATTACH 'catalog.sqlite' AS x" # not needed; instead run the plan check below
```

Plan check goes in the test itself: before scanning, assert the reconciliation query uses the primary key:

```cpp
    {
        sqlite3* raw = nullptr; REQUIRE(sqlite3_open(dbPath.getFullPathName().toRawUTF8(), &raw) == SQLITE_OK);
        sqlite3_stmt* s = nullptr;
        REQUIRE(sqlite3_prepare_v2(raw, "EXPLAIN QUERY PLAN SELECT id,path,missing FROM samples WHERE id>0 ORDER BY id LIMIT 5000", -1, &s, nullptr) == SQLITE_OK);
        bool tempSort = false;
        while (sqlite3_step(s) == SQLITE_ROW) if (juce::String((const char*) sqlite3_column_text(s, 3)).containsIgnoreCase("TEMP B-TREE")) tempSort = true;
        sqlite3_finalize(s); sqlite3_close(raw);
        REQUIRE_FALSE(tempSort);
    }
```

Add the same check for `SELECT id,path FROM samples ORDER BY name COLLATE NOCASE,id LIMIT 10000` and assert the plan mentions `samples_name_id`.

- [ ] **Step 3: Record the numbers** in `docs/TESTING.md` (date, seconds, row count) and update `docs/phase1-progress.md` with the stage summary.

- [ ] **Step 4: Run the full suite and both builds**

```bash
cmake --build build-debug -j8 && ./build-debug/tests/SonicatorTests
cmake --build build-release -j8 && ./build-release/tests/SonicatorTests
```

Expected: all non-tagged tests pass in both configurations.

- [ ] **Step 5: Commit**

```bash
git add tests/TestScale.cpp docs/TESTING.md docs/phase1-progress.md
git commit -m "test: verify 300k-row reconciliation and query plans"
```

---

### Task 7: Branding assets (added by the user during execution)

**Files:**
- Create: `resources/branding/originals/*`, `resources/branding/icon-tile-1024.png`, `icon-mark-1024.png`, `icon-header.png`, `logo-silver.png`, `README.md`
- Modify: `CMakeLists.txt`, `ui/MainComponent.h`, `ui/MainComponent.cpp`

- [x] Copy the three owner PNGs unmodified into `resources/branding/originals/`.
- [x] Derive a 1024 app icon from the rounded tile, a trimmed 1024 flat-mark canvas, a 96 px header mark and a trimmed wordmark.
- [x] `juce_add_binary_data(SonicatorAssets ...)` for the header mark and wordmark; `juce_add_gui_app(... ICON_BIG ...)` from the tile.
- [x] Draw the mark and wordmark in the header, hiding the text labels when the wordmark loads; keep the drawn fallback otherwise.
- [x] Build the bundle and confirm `AppIcon.icns` is in `Contents/Resources` and `CFBundleIconFile` is set.

### Task 8: Verify against the real catalog without modifying it

**Files:** none.

- [ ] **Step 1: Copy the real database to a scratch location**

```bash
mkdir -p /private/var/folders/2c/8l0mt2ns0jjdy98w7j_2h76m0000gp/T/opencode/sonicator-verify
sqlite3 ~/Library/Sonicator/sonicator.sqlite ".backup '/private/var/folders/2c/8l0mt2ns0jjdy98w7j_2h76m0000gp/T/opencode/sonicator-verify/catalog.sqlite'"
```

- [ ] **Step 2: Open the copy with the new build** (no scan) and confirm the grid shows samples and the status reads `299057 samples loaded`.

- [ ] **Step 3: Compare counts before and after**

```bash
sqlite3 .../catalog.sqlite "SELECT COUNT(*), SUM(missing), MIN(version), MAX(version) FROM samples"
```
Expected: 299057, 0, 0, 2 (version 0 belongs to the 158 error rows; `MAX` stays 1 unless the copy is rescanned).

- [ ] **Step 4: Report** the numbers to the user. The real database must still have exactly 299057 rows and its `user_version` only changes when the user launches the new build.

---

## Self-review

- Spec goals 1–5 map to Tasks 5, 3, 4, 1, 6.
- Types: `PathEntry`, `pathsAfterId`, `setMissing`, `basicFeaturesVersion`, `IndexingProgress::finalizing` are defined once and used consistently.
- Review Focus items: migration under WAL (Task 1 test), untouched roots (Task 3 test), unchanged rescan speed (Task 6), concurrent instances (`BEGIN IMMEDIATE` + existing busy timeout, noted in Task 1), file appearing mid-scan (documented; restored on next scan by existing test).
