# Relocate Imported Folders Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** When a backed-up catalog is opened on a new computer, offer to point its previously analyzed root folders at their new locations and rewrite all stored paths without reanalysis, in both macOS → Windows and Windows → macOS directions.

**Architecture:** A pure string helper does separator-aware prefix matching and remapping. A transactional repository method streams the affected rows, rewrites `samples.path`/`search_key` and `roots`, and merges rare target collisions. A modal Swing-style JUCE panel lists missing roots, validates chosen folders and applies through the repository. MainComponent wires the offer to catalog switching and Settings.

**Tech Stack:** C++20, JUCE 9.0.5, bundled SQLite, Catch2 3.7.1, CMake.

**Spec:** `docs/superpowers/specs/2026-11-07-relocate-imported-folders-design.md`

## Global Constraints

- Preserve the internal structure below each root; no per-file relinking and no automatic folder guessing.
- Treat both `/` and `\` as separators in stored paths; write `juce::File::getSeparatorChar()` for the current OS.
- Do not change the SQLite schema or `user_version`.
- Do not change `missing` flags during relocation.
- Never move, rename or delete an audio file on disk.
- Refuse relocation while indexing is running.
- Relocation is one transaction; any error rolls back all path and root changes.
- The dialog is English, like the rest of the UI.
- Tests must be portable: pure path tests pass an explicit separator so both OS directions are verified on one host.

## Review Focus

- A root that is a string prefix of another (`/Samples` vs `/Samples2`) must not move sibling files; covered in Task 1.
- Foreign drive/volume roots and mixed separators (`C:\`, `/`, `C:/x\y`) must map correctly; covered in Tasks 1 and 2.
- A target path collision must keep the tags and star from both rows, not delete user data; covered in Task 2.
- A nonexistent or wrong target must abort and leave every path, root and tag unchanged; covered in Task 2.
- After relocation, path search must use the new paths (`search_key`), unrelated roots and missing flags must stay unchanged; covered in Task 2. The Relocate button must stay disabled until every row has a valid target; covered in Task 3.

---

### Task 1: Separator-aware path helper

**Files:**
- Create: `core/database/PathRelocation.h`
- Create: `core/database/PathRelocation.cpp`
- Create: `tests/TestPathRelocation.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  namespace sonicator::pathrelocation {
      juce::String trimTrailingSeparators(const juce::String& path);
      juce::String withSeparator(const juce::String& root, juce::juce_wchar separator);
      bool isUnder(const juce::String& path, const juce::String& root);
      juce::String relativeTo(const juce::String& path, const juce::String& root);
      juce::String join(const juce::String& root, const juce::String& relative,
                        juce::juce_wchar separator);
      juce::String remap(const juce::String& path, const juce::String& oldRoot,
                         const juce::String& newRoot,
                         juce::juce_wchar separator = juce::File::getSeparatorChar());
  }
  ```

- [ ] **Step 1: Write the failing test**

Create `tests/TestPathRelocation.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "core/database/PathRelocation.h"
using namespace sonicator::pathrelocation;

TEST_CASE("Path relocation is separator-aware in both directions", "[relocation][paths]")
{
    REQUIRE(isUnder("/Users/me/Drums/Kick.wav", "/Users/me/Drums"));
    REQUIRE_FALSE(isUnder("/Users/me/Drums2/Kick.wav", "/Users/me/Drums"));
    REQUIRE(isUnder("C:\\Samples\\Drums\\Kick.wav", "C:\\Samples\\Drums"));
    REQUIRE(isUnder("C:/Samples/Drums/Kick.wav", "C:\\Samples\\Drums"));
    REQUIRE(isUnder("C:\\a\\b.wav", "C:\\"));
    REQUIRE(isUnder("/a/b.wav", "/"));
    REQUIRE_FALSE(isUnder("/other/b.wav", "/a"));

    REQUIRE(remap("C:\\Samples\\Drums\\Kick\\01.wav", "C:\\Samples\\Drums",
                  "/Users/me/Drums", '/') == "/Users/me/Drums/Kick/01.wav");
    REQUIRE(remap("/Users/me/Drums/Kick/01.wav", "/Users/me/Drums",
                  "D:\\Samples\\Drums", '\\') == "D:\\Samples\\Drums\\Kick\\01.wav");
    REQUIRE(remap("C:\\a\\b.wav", "C:\\", "D:\\Library", '\\') == "D:\\Library\\a\\b.wav");
    REQUIRE(remap("/a/b.wav", "/", "/mnt/library", '/') == "/mnt/library/a/b.wav");
    REQUIRE(remap("/Users/me/Drums/Kick.wav", "/Users/me/Drums/",
                  "/new/Drums", '/') == "/new/Drums/Kick.wav");
    REQUIRE(remap("/other/b.wav", "/a", "/mnt/library", '/') == "/other/b.wav");
    REQUIRE(join("/new", "Kick\\01.wav", '/') == "/new/Kick/01.wav");
    REQUIRE(trimTrailingSeparators("/Users/me/Drums/") == "/Users/me/Drums");
    REQUIRE(trimTrailingSeparators("C:\\") == "C:");
    REQUIRE(trimTrailingSeparators("/") == "/");
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --target SonicatorTests --parallel 6`
Expected: compile error because `core/database/PathRelocation.h` does not exist.

- [ ] **Step 3: Implement the helper**

`PathRelocation.h` declares the functions above. `PathRelocation.cpp` implements them with these rules:

- Normalize only for comparison: replace every `\` with `/`; lengths stay one-to-one.
- `trimTrailingSeparators` drops `/` and `\` while length > 1, so `/` and `C:\` become `/` and `C:`.
- `isUnder` compares the normalized, trimmed root; equal paths are under; after a prefix match the next character must be `/` or `\`.
- `relativeTo` returns an empty string when not under; for root `/` it drops one character, otherwise it drops `trimmedRoot.length()` and one following separator.
- `join` converts separators in `relative` to `separator`; a root of `/` or `\` is used as-is, otherwise `trimmedRoot + separator + relative`.
- `remap` returns the input unchanged when not under the old root.
- `withSeparator` returns the root unchanged when it already ends with `separator`, returns `/` or `\` for a bare root, and otherwise appends `separator`.

Add both `.cpp` files to `SonicatorCore` in `CMakeLists.txt` and `TestPathRelocation.cpp` to the test executable in `tests/CMakeLists.txt`.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build --parallel 6 && build/tests/SonicatorTests "[relocation][paths]"`
Expected: all assertions pass.

- [ ] **Step 5: Commit**

```bash
git add core/database/PathRelocation.h core/database/PathRelocation.cpp tests/TestPathRelocation.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add separator-aware catalog path relocation helper"
```

---

### Task 2: Transactional repository relocation

**Files:**
- Modify: `core/database/SampleRepository.h`
- Modify: `core/database/SampleRepository.cpp`
- Modify: `tests/TestDatabase.cpp`

**Interfaces:**
- Consumes: `pathrelocation::isUnder`, `relativeTo`, `remap`, `trimTrailingSeparators`, `withSeparator` from Task 1; `searchKey(name, path)` already exists.
- Produces:
  ```cpp
  struct RootRelocation { juce::String oldRoot, newRoot; };
  struct RelocationResult { int64_t movedFiles = 0, conflicts = 0; };
  int SampleRepository::countUnderRoot(const juce::String& root) const;
  RelocationResult SampleRepository::relocateRoots(const std::vector<RootRelocation>&);
  ```

- [ ] **Step 1: Write the failing tests**

Append to `tests/TestDatabase.cpp`:

```cpp
TEST_CASE("Relocating a root rewrites paths, keeps identity and leaves other roots alone",
          "[database][relocation]")
{
    TestDirectory t;
    auto oldRoot = t.dir.getChildFile("old-root"); oldRoot.createDirectory();
    auto newRoot = t.dir.getChildFile("new-root"); newRoot.createDirectory();
    Database db(t.dir.getChildFile("relocate.sqlite")); SampleRepository repo(db); repo.createTables();

    SampleRecord first; first.filePath = oldRoot.getChildFile("Kick/01.wav").getFullPathName();
    first.fileName = "01.wav"; first.fileHash = "h1"; first.version = basicFeaturesVersion;
    const auto firstId = repo.upsert(first);
    SampleRecord second; second.filePath = oldRoot.getChildFile("Snare/02.wav").getFullPathName();
    second.fileName = "02.wav"; second.fileHash = "h2"; second.version = basicFeaturesVersion;
    const auto secondId = repo.upsert(second);
    SampleRecord foreign; foreign.filePath = oldRoot.getFullPathName() + "\\Foreign\\x.wav";
    foreign.fileName = "x.wav"; foreign.fileHash = "h3"; const auto foreignId = repo.upsert(foreign);
    SampleRecord outside; outside.filePath = t.dir.getChildFile("elsewhere/out.wav").getFullPathName();
    outside.fileName = "out.wav"; outside.fileHash = "h4"; const auto outsideId = repo.upsert(outside);
    repo.addRoot(oldRoot.getFullPathName()); repo.addRoot(t.dir.getFullPathName());
    const auto tag = repo.createTag("Keep", 0xff112233);
    repo.setSampleTag(firstId, tag, true);
    repo.setStarred({ firstId }, true);
    repo.setMissing({ secondId });
    REQUIRE(repo.countUnderRoot(oldRoot.getFullPathName()) == 3);

    const auto result = repo.relocateRoots({{ oldRoot.getFullPathName(), newRoot.getFullPathName() }});
    REQUIRE(result.movedFiles == 3);
    REQUIRE(result.conflicts == 0);

    REQUIRE_FALSE(repo.findByPath(first.filePath).has_value());
    const auto moved = repo.findByPath(newRoot.getChildFile("Kick/01.wav").getFullPathName());
    REQUIRE(moved.has_value());
    REQUIRE(moved->id == firstId);
    REQUIRE(moved->fileHash == "h1");
    REQUIRE(moved->starred);
    REQUIRE(repo.tagsForSample(firstId).size() == 1);
    REQUIRE(repo.findByPath(newRoot.getChildFile("Snare/02.wav").getFullPathName())->id == secondId);
    REQUIRE(repo.findByPath(newRoot.getChildFile("Snare/02.wav").getFullPathName())->missing);
    REQUIRE(repo.findByPath(newRoot.getChildFile("Foreign/x.wav").getFullPathName())->id == foreignId);
    REQUIRE(repo.findByPath(outside.filePath)->id == outsideId);
    REQUIRE(repo.roots().contains(newRoot.getFullPathName()));
    REQUIRE_FALSE(repo.roots().contains(oldRoot.getFullPathName()));
    REQUIRE(repo.roots().contains(t.dir.getFullPathName()));

    LibraryFilter filter; filter.text = "new-root";
    REQUIRE(repo.query(filter).total == 3);
}

TEST_CASE("A relocation collision keeps tags and stars from both rows", "[database][relocation]")
{
    TestDirectory t;
    auto oldRoot = t.dir.getChildFile("old"); oldRoot.createDirectory();
    auto newRoot = t.dir.getChildFile("new"); newRoot.createDirectory();
    Database db(t.dir.getChildFile("collision.sqlite")); SampleRepository repo(db); repo.createTables();
    const auto movingPath = oldRoot.getChildFile("Kick/same.wav").getFullPathName();
    const auto existingPath = newRoot.getChildFile("Kick/same.wav").getFullPathName();
    SampleRecord moving; moving.filePath = movingPath; moving.fileName = "same.wav"; moving.fileHash = "m";
    const auto movingId = repo.upsert(moving);
    SampleRecord existing; existing.filePath = existingPath; existing.fileName = "same.wav"; existing.fileHash = "e";
    const auto existingId = repo.upsert(existing);
    repo.addRoot(oldRoot.getFullPathName());
    const auto movingTag = repo.createTag("Moving", 0xff111111);
    const auto existingTag = repo.createTag("Existing", 0xff222222);
    repo.setSampleTag(movingId, movingTag, true);
    repo.setSampleTag(existingId, existingTag, true);
    repo.setStarred({ movingId }, true);

    const auto result = repo.relocateRoots({{ oldRoot.getFullPathName(), newRoot.getFullPathName() }});
    REQUIRE(result.conflicts == 1);
    REQUIRE_FALSE(repo.findByPath(movingPath).has_value());
    const auto survivor = repo.findByPath(existingPath);
    REQUIRE(survivor.has_value());
    REQUIRE(survivor->id == existingId);
    REQUIRE(survivor->starred);
    auto tags = repo.tagsForSample(existingId);
    REQUIRE(tags.size() == 2);
}

TEST_CASE("A relocation with a missing target changes nothing", "[database][relocation]")
{
    TestDirectory t;
    auto oldRoot = t.dir.getChildFile("old"); oldRoot.createDirectory();
    Database db(t.dir.getChildFile("rollback.sqlite")); SampleRepository repo(db); repo.createTables();
    SampleRecord row; row.filePath = oldRoot.getChildFile("a.wav").getFullPathName();
    row.fileName = "a.wav"; row.fileHash = "h"; const auto id = repo.upsert(row);
    repo.addRoot(oldRoot.getFullPathName());
    const auto absent = t.dir.getChildFile("absent");
    REQUIRE_THROWS(repo.relocateRoots({{ oldRoot.getFullPathName(), absent.getFullPathName() }}));
    REQUIRE(repo.findByPath(row.filePath)->id == id);
    REQUIRE(repo.roots().contains(oldRoot.getFullPathName()));
    REQUIRE_FALSE(repo.roots().contains(absent.getFullPathName()));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build --parallel 6`
Expected: compile error because `countUnderRoot` and `relocateRoots` are not declared.

- [ ] **Step 3: Implement the repository API**

In `SampleRepository.h`, add `<cstdint>` (already covered by other headers) and the two structs plus methods.

In `SampleRepository.cpp`:

- `countUnderRoot`: hold `db.mutex`; build two `LIKE` prefixes with `pathrelocation::withSeparator(trimmed, '/')` and `'\\'`, escape with the existing `escapeLike`, and run
  `SELECT COUNT(*) FROM samples WHERE path LIKE ? ESCAPE '\' OR path LIKE ? ESCAPE '\'`.
- `relocateRoots`: hold `db.mutex`; throw `std::runtime_error("New folder does not exist")` when a `newRoot` is not a directory; `BEGIN IMMEDIATE`; catch, `ROLLBACK`, rethrow.
- Stream all rows once with `SELECT id,name,path FROM samples WHERE id>? ORDER BY id LIMIT 2000`, keyset by `id`.
- For each row, choose the provided old root with the longest `oldRoot.length()` that satisfies `pathrelocation::isUnder`.
- Update with `UPDATE OR IGNORE samples SET path=?, search_key=? WHERE id=?` using `searchKey(name, newPath)`.
- When `sqlite3_changes` is 0, look up the existing target row by path; union tags with
  `INSERT OR IGNORE INTO sample_tags(sample_id,tag_id) SELECT ?,tag_id FROM sample_tags WHERE sample_id=?`, set
  `starred = starred | COALESCE((SELECT starred FROM samples WHERE id=?),0)`, delete the relocated row, and increment `conflicts`. Otherwise increment `movedFiles`.
- After the row pass, for each relocation run `DELETE FROM roots WHERE path=?` and `INSERT OR IGNORE INTO roots(path) VALUES(?)`, then `COMMIT`.
- Do not touch `missing`, `id`, `name`, hashes, measurements, tags or stars.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build --parallel 6 && build/tests/SonicatorTests "[relocation]"`
Expected: pure path tests and all three repository tests pass.

- [ ] **Step 5: Commit**

```bash
git add core/database/SampleRepository.h core/database/SampleRepository.cpp tests/TestDatabase.cpp
git commit -m "feat: relocate catalog roots transactionally"
```

---

### Task 3: Relocation dialog panel

**Files:**
- Create: `ui/settings/RelocateFoldersPanel.h`
- Create: `ui/settings/RelocateFoldersPanel.cpp`
- Create test additions in `tests/TestUI.cpp`
- Modify: `CMakeLists.txt` (`SonicatorUI` sources)

**Interfaces:**
- Consumes: `RootRelocation` from Task 2; `pathrelocation::isUnder` from Task 1; `theme` colours.
- Produces:
  ```cpp
  struct RelocateRootInfo { juce::String path; int fileCount = 0; };
  struct RelocationProbe { int sampled = 0; int found = 0; };
  class RelocateFoldersPanel final : public juce::Component
  {
  public:
      explicit RelocateFoldersPanel(std::vector<RelocateRootInfo>);
      void paint(juce::Graphics&) override;
      void resized() override;
      void lookAndFeelChanged() override;
      void setError(const juce::String&);
      bool setTarget(size_t index, const juce::File& folder); // false when not a directory
      bool canApply() const;
      std::vector<RootRelocation> relocations() const;
      std::function<RelocationProbe(const juce::String& oldRoot, const juce::String& newRoot)> onProbe;
      std::function<juce::String(const std::vector<RootRelocation>&)> onApply;
  };
  ```

- [ ] **Step 1: Write the failing panel test**

Append to `tests/TestUI.cpp`:

```cpp
TEST_CASE("Relocation panel enables applying only after every folder is chosen", "[ui][relocation]")
{
    TestDirectory t;
    auto target = t.dir.getChildFile("target"); target.createDirectory();
    RelocateFoldersPanel panel({{ "/missing/Drums", 12 }, { "/missing/Bass", 7 }});
    REQUIRE_FALSE(panel.canApply());
    REQUIRE_FALSE(panel.setTarget(0, t.dir.getChildFile("absent")));
    REQUIRE(panel.setTarget(0, target));
    REQUIRE_FALSE(panel.canApply());
    auto other = t.dir.getChildFile("other"); other.createDirectory();
    REQUIRE(panel.setTarget(1, other));
    REQUIRE(panel.canApply());
    auto rows = panel.relocations();
    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].oldRoot == "/missing/Drums");
    REQUIRE(rows[0].newRoot == target.getFullPathName());

    REQUIRE_FALSE(panel.setTarget(1, target)); // duplicate target
    REQUIRE_FALSE(panel.canApply());
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --target SonicatorTests --parallel 6`
Expected: compile error because `RelocateFoldersPanel` does not exist.

- [ ] **Step 3: Implement the panel**

`RelocateFoldersPanel` owns a `juce::Viewport` with a content component and one row per missing root. Row controls: old path label (elided middle, full path tooltip), `N files` label, `Choose...` button, chosen path label, and a result label that shows `M of K sampled found` after `onProbe`, with `theme::accent` below 90% and `theme::muted` otherwise. Header text explains that paths, not files, are changed and that analysis, tags, stars and hashes are kept.

- `setTarget(index, folder)`: reject non-directories and a target equal to or overlapping another row's target in either direction (`pathrelocation::isUnder`); store, call `onProbe(oldRoot, newRoot)`, update labels, and repaint.
- `canApply`: true only when every row has a target and all targets are pairwise distinct/non-overlapping.
- `relocations`: one `RootRelocation{path, target.getFullPathName()}` per row; empty `newRoot` where unchosen.
- Relocate button text `Relocate`, secondary `Not now`. Relocate is enabled from `canApply`. On click call `onApply(relocations())`; a non-empty returned string goes to `setError`, an empty result closes the parent `DialogWindow` with `exitModalState(0)`.
- `setError` shows red text (`0xffff8b8b` dark, `0xffb3261e` light) in a label above the buttons.
- Sizes: panel 680×460; header 54 px, viewport flexible, error 24 px, footer 40 px; rows 86 px.
- `lookAndFeelChanged` applies `theme::text`/`theme::muted` to labels and repaints.

Add `ui/settings/RelocateFoldersPanel.cpp` to `SonicatorUI` in `CMakeLists.txt`.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build --parallel 6 && build/tests/SonicatorTests "[ui][relocation]"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add ui/settings/RelocateFoldersPanel.h ui/settings/RelocateFoldersPanel.cpp tests/TestUI.cpp CMakeLists.txt
git commit -m "feat: add catalog folder relocation dialog"
```

---

### Task 4: Settings entry, MainComponent wiring and startup hint

**Files:**
- Modify: `ui/settings/SettingsPanel.h`
- Modify: `ui/settings/SettingsPanel.cpp`
- Modify: `ui/MainComponent.h`
- Modify: `ui/MainComponent.cpp`
- Modify: `tests/TestShell.cpp`

**Interfaces:**
- Consumes: `RelocateFoldersPanel`, `RelocateRootInfo`, `RelocationProbe` (Task 3); `relocateRoots`, `countUnderRoot` (Task 2); `pathrelocation::remap` (Task 1).
- Produces: `juce::StringArray MainComponent::missingRoots() const;` and `void MainComponent::showRelocateDialog();` (public for integration tests).

- [ ] **Step 1: Write the failing tests**

Append to `tests/TestShell.cpp`:

```cpp
TEST_CASE("A catalog with missing roots shows a relocation hint at startup", "[ui][relocation]")
{
    TestDirectory t;
    auto dbPath = t.dir.getChildFile("missing-roots.sqlite");
    {
        Database db(dbPath); SampleRepository repo(db); repo.createTables();
        SampleRecord r; r.filePath = "/gone/Drums/kick.wav"; r.fileName = "kick.wav"; repo.upsert(r);
        repo.addRoot("/gone/Drums");
    }
    MainComponent app(dbPath, false);
    auto* status = dynamic_cast<juce::Label*>(app.findChildWithID("importStatus"));
    REQUIRE(status != nullptr);
    REQUIRE(status->getText().contains("not found"));
    REQUIRE(status->getText().contains("Relocate"));
}

TEST_CASE("Relocation dialog lists every missing root", "[ui][relocation]")
{
    TestDirectory t;
    auto dbPath = t.dir.getChildFile("dialog.sqlite");
    {
        Database db(dbPath); SampleRepository repo(db); repo.createTables();
        SampleRecord r; r.filePath = "/gone/Drums/kick.wav"; r.fileName = "kick.wav"; repo.upsert(r);
        repo.addRoot("/gone/Drums");
    }
    MainComponent app(dbPath, false);
    app.showRelocateDialog();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    auto* modal = juce::Component::getCurrentlyModalComponent();
    REQUIRE(modal != nullptr);
    REQUIRE(modal->getName() == "Relocate folders");
    modal->exitModalState(0);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build --parallel 6`
Expected: compile error for `showRelocateDialog`.

- [ ] **Step 3: Implement the wiring**

`SettingsPanel`:
- Add `juce::TextButton relocate { "Relocate folders..." };` and `std::function<void()> onRelocateRequested;`.
- Include it in the component arrays, disable while indexing, set tooltip `Rewrite catalog paths for folders that moved to this computer`.
- In the catalog-button row, split `right` into three equal buttons with 8 px gaps: Move, Open, Relocate.
- `relocate.onClick = [this] { if (onRelocateRequested) onRelocateRequested(); };`

`MainComponent`:
- Add the two public methods:
  - `missingRoots()` returns `readRepo->roots()` filtered by `!juce::File(root).isDirectory()`; catch exceptions and return empty.
  - `showRelocateDialog()` refuses while the indexer is running (status message), returns when `readRepo` is null, and shows an informational `AlertWindow` titled `Relocate folders` with `All catalog folders are available on this computer.` when the list is empty. Otherwise build `std::vector<RelocateRootInfo>` with `oldRoot` and `countUnderRoot(oldRoot)`.
- Create a `RelocateFoldersPanel`, set `onProbe` to a lambda that remaps up to 200 paths for the root (use `readRepo->pathsAfterId` in blocks, filter with `pathrelocation::remap`, count `juce::File::existsAsFile()`) and returns `RelocationProbe`, and `onApply` to:
  1. refuse while indexing by returning an error string;
  2. create a backup path `dbPath.getSiblingFile(dbPath.getFileName() + ".relocate-" + timestamp + ".sqlite").getNonexistentSibling()` and call `writeRepo->backupTo(backup)`;
  3. call `writeRepo->relocateRoots(relocations)` and `writeRepo->refreshDuplicates()`;
  4. clear `selectedFolder`, reset `pageOffset`, call `folders.refresh(*readRepo)` and `reloadLibrary()`;
  5. set the status to `Relocated N file(s) in M folder(s). Backup: <path>`, return an empty string.
  Catch `std::exception` and return the message; the panel keeps itself open.
- Wrap the panel in a `DialogWindow` titled `Relocate folders`, set the look and feel, use native title bar, `setResizable(false, false)`, centre around `this`, and `enterModalState(true, nullptr, true)`.
- In `showSettings`, set `panel->onRelocateRequested` to close the Settings dialog and `MessageManager::callAsync` `showRelocateDialog()`.
- In `switchCatalog`, after the successful switch block and before returning true, when `! moveCurrent && ! missingRoots().isEmpty()` schedule `MessageManager::callAsync` to call `showRelocateDialog()`. The Settings callback already closes the Settings dialog in that case (add `if (! moveCurrent && ! missingRoots().isEmpty() && settingsDialog != nullptr) settingsDialog->exitModalState(0);` after the switch succeeds), so the async dialog is not nested.
- Add the startup hint in the constructor after the initial `reloadLibrary()`: if `catalogCount > 0` and `missingRoots()` is non-empty, set `status` to `N catalog folder(s) not found on this computer — Settings → Relocate folders...` and set the tooltip to the joined roots.
- Keep `switchCatalog`'s existing callers and error text unchanged.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build --parallel 6 && build/tests/SonicatorTests "[ui][relocation]"`
Expected: both tests pass. Existing catalog-switch tests still pass because their roots exist and no dialog opens.

- [ ] **Step 5: Commit**

```bash
git add ui/settings/SettingsPanel.h ui/settings/SettingsPanel.cpp ui/MainComponent.h ui/MainComponent.cpp tests/TestShell.cpp
git commit -m "feat: offer folder relocation when opening a catalog"
```

---

### Task 5: Documentation and full verification

**Files:**
- Modify: `README.md`
- Modify: `docs/ARCHITECTURE.md`
- Modify: `docs/TESTING.md`

- [ ] **Step 1: Update the user documentation**

In `README.md`, extend the Settings/catalog item: opening a catalog whose folders are missing offers **Relocate folders...** (automatic after **Open catalog**, manual in Settings); each missing root maps to a new folder, relative structure is preserved, analysis/tags/stars are kept, separators convert between macOS and Windows, and a `.relocate-<timestamp>.sqlite` backup is written first.

In `docs/ARCHITECTURE.md`, add a paragraph next to catalog switching: `samples.path`/`search_key` and `roots` are rewritten in one transaction by `relocateRoots`; path matching treats `/` and `\` as separators and writes native separators; target collisions merge tags/star; `missing` is untouched and Rescan remains authoritative.

In `docs/TESTING.md`, record the new coverage: both-direction path remapping, prefix-sibling safety, root relocation with identity/tags/stars preserved, collision merge, rollback on a missing target, startup hint, and panel enablement.

- [ ] **Step 2: Build and run the full suite**

Run: `cmake --build build --parallel 6 && ctest --test-dir build --output-on-failure`
Expected: build succeeds, 1/1 CTest target passes with all cases green.

- [ ] **Step 3: Run the relocation and UI-focused filters**

Run: `build/tests/SonicatorTests "[relocation]" && build/tests/SonicatorTests "[ui][relocation]"`
Expected: all matching cases pass.

- [ ] **Step 4: Commit**

```bash
git add README.md docs/ARCHITECTURE.md docs/TESTING.md docs/superpowers/specs/2026-11-07-relocate-imported-folders-design.md docs/superpowers/plans/2026-11-07-relocate-imported-folders.md
git commit -m "docs: document catalog folder relocation"
```

---

## Windows verification note

The local build is macOS. The path helper tests pass explicit separators on either host, and the
repository tests use temporary directories as the new roots, so the Windows → macOS direction is
covered on macOS. Before release, repeat on Windows: create a catalog on macOS, copy the `.sqlite`
to Windows, open it, relocate to `D:\...`, check preview/folder tree/tags/stars, then copy that
catalog back to macOS and relocate in the other direction. Windows builds are separate and are not
claimed by the local verification run.
