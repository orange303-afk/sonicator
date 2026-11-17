# Round Tag Menu Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A radial tag picker with a `Tags` pill in the row context menu, Ctrl/Cmd+T on rows and folders, personal usage ranking, and continuous proximity growth.

**Architecture:** A catalog table (`tag_usage`) feeds `rankedTags()`. A transparent full-window overlay inside `MainComponent` paints two orbits of pills with a 60 Hz timer. A custom `PopupMenu` item dismisses the menu and opens the overlay; commands, the folder menu and the preview button use the same overlay.

**Tech Stack:** C++ with JUCE; SQLite via the existing `SampleRepository`; Catch2 tests in `SonicatorTests`.

**Spec:** `docs/superpowers/specs/2026-11-18-round-tag-menu-design.md`

## Global Constraints

- Catalog schema becomes 20; the plug-in is read-only and refuses newer catalogs.
- UI text English, code comments English, no new dependencies.
- Logical pixels only; Manrope fonts via `studio::` helpers.
- No audio-thread work.
- Tests run through the `SonicatorTests` Catch2 target.
- Setting key and field name: `roundTagMenu`.
- Ranking constants: half-life 30 days, `(use_count + 0.5)`; proximity reach 90 px, max growth 1.26, push 13 px; appearance 260 ms, close 90 ms, smoothing 180 ms.

## Review Focus

- The custom `PopupMenu` item on macOS: hover must fire, the menu must dismiss once, and a click must still open the ring. A double open is the likely failure.
- The ring at the window or plug-in edge: the clamp must keep every pill inside 880×580.
- A catalog with zero or one tag: no empty orbits, `All tags…` still reachable.
- Hovering and clicking the same pill in quick succession: one assignment, one close.
- Removing a tag from a large selection by a mistaken click: the pill shows `✓` and the status line names the removal, matching today's `Tag ▸` behaviour.

---

### Task 1: Catalog usage store and ranking

**Files:**
- Modify: `core/database/CatalogSchema.h`
- Modify: `core/database/SampleRepository.h`, `core/database/SampleRepository.cpp`
- Create: `tests/TestTagRanking.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/ARCHITECTURE.md`, `docs/TESTING.md`

**Interfaces:**
- Produces: `void SampleRepository::recordTagUse(int64_t tagId);`
- Produces: `std::vector<Tag> SampleRepository::rankedTags(int limit = 15, int64_t nowMs = -1) const;`
- Produces: schema constant `catalogSchemaVersion == 20`.

- [ ] **Step 1: Write the failing tests**

Copy the `TagFixture` pattern from `tests/TestRecentTags.cpp`. Add:

```cpp
TEST_CASE("Tag usage counts explicit assignments", "[tags][ranking]")
{
    TagFixture f;
    const auto id = f.repo->createTag("Clap", 0xffe0b84a);
    f.repo->recordTagUse(id);
    f.repo->recordTagUse(id);
    // rankedTags surfaces the tag once; a second read must not double count
}
```

Also assert: `rankedTags(15, now)` puts a tag used once today before a tag used 10 times 90 days
ago; a never-used tag sorts after both by name; `limit` truncates. For the seeding test, create
the catalog, `DROP TABLE tag_usage`, `PRAGMA user_version=19`, insert two `recent_tags` rows,
close, reopen, and assert `rankedTags` order follows `MAX(1, 6 - seq)`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build ~/Library/Caches/sonicator-build/debug --target SonicatorTests -- -j8`
Expected: compile error, `recordTagUse` and `rankedTags` not declared.

- [ ] **Step 3: Implement**

Bump `catalogSchemaVersion` to 20. Add the table to the `createTables` SQL. Seed when
`current < 20` with `INSERT OR IGNORE INTO tag_usage(tag_id,use_count,last_used_at) SELECT
tag_id, MAX(1, 6 - seq), used_at FROM recent_tags;`.

`recordTagUse` mirrors `recordRecentTag`: check the tag exists, upsert
`use_count = use_count + 1, last_used_at = ?`, commit.

`rankedTags` joins `tags` with `tag_usage`, reads `id, name, colour, builtin, sample_count,
use_count, last_used_at`, computes the score, sorts by score desc, use_count desc, name
case-insensitive, and truncates to `limit`.

Call `recordTagUse` next to both `recordRecentTag` call sites (`MainComponent.cpp:2621`,
`core/tagging/TaggingService.cpp:185`).

- [ ] **Step 4: Run the tests to verify they pass**

Run: `~/Library/Caches/sonicator-build/debug/SonicatorTests "[tags]"`

- [ ] **Step 5: Commit**

```bash
git add core/database/CatalogSchema.h core/database/SampleRepository.* tests/TestTagRanking.cpp tests/CMakeLists.txt docs/ARCHITECTURE.md docs/TESTING.md
git commit -m "feat: rank tags by personal usage in schema 20"
```

---

### Task 2: Setting and Settings toggle

**Files:**
- Modify: `core/settings/AppSettings.h`, `core/settings/AppSettings.cpp`
- Modify: `ui/settings/SettingsPanel.h`, `ui/settings/SettingsPanel.cpp`
- Test: `tests/TestSettings.cpp`

**Interfaces:**
- Produces: `bool AppSettings::roundTagMenu` (default false), key `roundTagMenu`.

- [ ] **Step 1: Failing test**

Add to `tests/TestSettings.cpp`:

```cpp
TEST_CASE("Round tag menu persists and defaults to off", "[settings][tags]")
{
    TestDirectory t; SettingsStore store(t.dir.getChildFile("settings.json"));
    REQUIRE_FALSE(store.load().roundTagMenu);
    store.update([](AppSettings& s) { s.roundTagMenu = true; });
    REQUIRE(store.load().roundTagMenu);
}
```

- [ ] **Step 2: Run and watch it fail**

- [ ] **Step 3: Implement settings, then the panel**

Add the field, `readBool("roundTagMenu", ...)` and `setProperty("roundTagMenu", ...)`. In
`SettingsPanel`, add `tagsHeading`, `roundTagMenuToggle { "Round tag menu" }`,
`roundTagMenuNote`, a `TAGS` section between `LIBRARY` and `EDITING`, `onClick` writing
`current.roundTagMenu` and calling `notifySettings()`. Do not add these controls to the
`showLibrary` visibility array: the toggle must stay visible in plug-in mode.

- [ ] **Step 4: Run the tests**

- [ ] **Step 5: Commit**

```bash
git commit -am "feat: add the round tag menu setting"
```

---

### Task 3: RoundTagMenu component

**Files:**
- Create: `ui/library/RoundTagMenu.h`, `ui/library/RoundTagMenu.cpp`
- Create: `tests/TestRoundTagLayout.cpp`
- Modify: `tests/CMakeLists.txt`, `CMakeLists.txt` (source list)

**Interfaces:**
- Produces the types below; MainComponent consumes them in Task 4.

```cpp
struct RoundTagItem { int64_t tagId = 0; juce::String label; juce::Colour colour;
                      bool assigned = false; bool action = false; };
struct RoundTagPill { RoundTagItem item; juce::Rectangle<float> bounds;
                      juce::Point<float> centre; float angle = 0.0f;
                      int ring = 0, slot = 0; };
struct RoundTagGeometry { juce::Point<float> centre; juce::Rectangle<float> hub;
                          std::vector<RoundTagPill> pills; };

RoundTagGeometry layoutRoundTagMenu(juce::Point<int> anchor,
    juce::Rectangle<int> available, const std::vector<RoundTagItem>& inner,
    const std::vector<RoundTagItem>& outer, const juce::Font& font);

int roundTagNearest(const std::vector<RoundTagPill>& pills, juce::Point<float> mouse);

class RoundTagMenu final : public juce::Component, private juce::Timer
{
public:
    void open(RoundTagGeometry geometry, juce::String centreTitle, juce::String centreCaption);
    void close();
    bool isOpen() const noexcept;
    void setAnimationDuration(int ms); // 0 in tests and manual plates
    std::function<void(int64_t tagId)> onTagPicked;
    std::function<void()> onAllTagsPicked;
    std::function<void()> onDismissed;
    // paint, mouseMove, mouseDown, keyPressed, timerCallback
};
```

- [ ] **Step 1: Failing layout tests**

```cpp
TEST_CASE("Round tag layout uses two orbits", "[tags][round]")
{
    const auto g = layoutRoundTagMenu({ 400, 400 }, { 0, 0, 800, 800 }, eightItems(), eightItems(),
                                      juce::Font(juce::FontOptions(10.5f)));
    REQUIRE(g.pills.size() == 16);
    REQUIRE(g.pills[0].centre.getDistanceFrom(g.centre) == Approx(122.0f).margin(1.0f));
    REQUIRE(g.pills[8].centre.getDistanceFrom(g.centre) == Approx(200.0f).margin(1.0f));
    REQUIRE(g.pills[0].angle == Approx(-juce::MathConstants<float>::halfPi));
}
```

Also: anchor at `{10, 10}` shifts the centre to at least 12 px from each edge with every pill
inside; 3 inner and 1 outer distribute evenly; `roundTagNearest` returns the closest pill inside
90 px and `-1` outside.

- [ ] **Step 2: Watch them fail**

- [ ] **Step 3: Implement layout, then the component**

Paint, hover growth and the timer follow the spec's numbers. The component is transparent,
non-opaque, and receives clicks over its whole bounds. Dismiss on Esc, a click outside every
rendered pill, and `close()`.

- [ ] **Step 4: Run the layout tests**

- [ ] **Step 5: Commit**

```bash
git commit -am "feat: add the round tag menu overlay"
```

---

### Task 4: Tags pill and row menu integration

**Files:**
- Create: `ui/library/TagsMenuPill.h`, `ui/library/TagsMenuPill.cpp`
- Modify: `ui/MainComponent.h`, `ui/MainComponent.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `RoundTagMenu`, `layoutRoundTagMenu`, `rankedTags`.
- Produces: `MainComponent::showRoundTagMenu(juce::Point<int> anchor, bool forFolder)`;
  `MainComponent::handleRoundTagPick(int64_t)`.

- [ ] **Step 1: Implement `TagsMenuPill`**

A 30 px, menu-width pill labelled `Tags`, neutral colours, `onHover` after a 200 ms dwell and
`onClick` immediately. On hover: capture the screen centre, call
`juce::PopupMenu::dismissAllActiveMenus()`, then `onOpen(anchor)`.

- [ ] **Step 2: Wire the row menu**

In the row context menu builder, when `appSettings.roundTagMenu` is on, skip the recent items and
the `Tag ▸` submenu; add `menu.addCustomItem(9026, std::make_unique<TagsMenuPill>(...), 230, 34,
false)` first. The menu callback `9026` also opens the ring. Build the ring's items from
`rankedTags(15, ...)`: first eight inner, next seven plus `All tags…` outer; mark `assigned` when
every selected row has the tag.

- [ ] **Step 3: Assignment**

`handleRoundTagPick` toggles: assign via `assignTagAndRemember`, remove via `setSampleTag(false)`
and `triggerAdaptiveLearning`, then `reloadLibrary`. Close the ring first. Status line names the
tag and the count.

- [ ] **Step 4: Build and smoke-test**

Run: `cmake --build ~/Library/Caches/sonicator-build/debug --target Sonicator -- -j8`

- [ ] **Step 5: Commit**

```bash
git commit -am "feat: open the round tag menu from a Tags pill"
```

---

### Task 5: Folder, preview and commands

**Files:**
- Modify: `ui/MainComponent.h`, `ui/MainComponent.cpp`
- Modify: `docs/COMMANDS.md`

**Interfaces:**
- Produces: `void MainComponent::confirmTagFolder(const juce::String& folder, int64_t tagId);`

- [ ] **Step 1: Extract the folder confirmation and open the ring**

Move the `dialogs::showAlert` body out of the folder menu callback into `confirmTagFolder`. In the
folder menu, when the setting is on, `Tag folder…` opens the ring (`forFolder = true`) instead of
the tag submenu. A pick calls `confirmTagFolder`.

- [ ] **Step 2: Preview**

`showEditTagsMenu()`: when the setting is on, open the ring at the `editTags` button with one file;
`All tags…` still opens the existing list.

- [ ] **Step 3: Commands**

Register `library.roundTagMenu` and `folders.roundTagMenu` with `CommandScope::library`/`folders`,
category `Tags`, default `juce::KeyPress('t', juce::ModifierKeys::commandModifier, 0)`. State is
unavailable when the setting is off or the selection is empty. Run opens the ring at the mouse
position inside the window, else the table/tree centre.

- [ ] **Step 4: Build and check the Hotkeys page shows both commands**

- [ ] **Step 5: Commit**

```bash
git commit -am "feat: round tag menu for folders, preview and Ctrl/Cmd+T"
```

---

### Task 6: Manual shot, documentation and acceptance

**Files:**
- Modify: `tests/TestManualShots.cpp`
- Modify: `docs/manual/chapters/07-tags.typ`
- Create: `docs/ROUND_TAG_MENU_ACCEPTANCE.md`
- Modify: `docs/TAGGING.md`, `docs/COMMANDS.md`, `AGENTS.md`, `README.md` if the tag paragraph changes

- [ ] **Step 1: Manual plate**

Add a `[.manual-shots]` case that seeds three selected rows, opens the ring with
`setAnimationDuration(0)`, and captures `round-tag-menu.png` with the same helper the other plates
use. Rebuild the manual with the Typst command from `docs/manual/README.md`.

- [ ] **Step 2: Manual chapter**

Add a "Round tag menu" section: the `Tags` pill, hover and click, the two orbits, the ranking
rule, Ctrl/Cmd+T, folders, `All tags…`, and that the filter chip is a different feature.

- [ ] **Step 3: Acceptance document**

`docs/ROUND_TAG_MENU_ACCEPTANCE.md` lists the checks from the plan's acceptance list, each with
its expected result and a checkbox.

- [ ] **Step 4: Commit**

```bash
git commit -am "docs: document the round tag menu"
```

---

### Task 7: Whole-suite verification

- [ ] **Step 1:** `cmake --build ~/Library/Caches/sonicator-build/debug --target SonicatorTests -- -j8`
- [ ] **Step 2:** `~/Library/Caches/sonicator-build/debug/SonicatorTests` — all tests green.
- [ ] **Step 3:** `graphify update .`
- [ ] **Step 4:** Manual acceptance checklist completed; note any deferrals in the acceptance doc.

## Acceptance list

- Setting off: the context menu and shortcuts behave exactly as before.
- Setting on: one `Tags` pill at the top of the row menu; hover (200 ms) and click both open the
  ring from the pill; the rest of the menu is unchanged.
- Ctrl/Cmd+T on rows and on the folder tree; unavailable with an empty selection.
- Inner orbit holds recent/frequent tags; outer holds the next seven and `All tags…`.
- Appearance, proximity growth and dismissal match the spec's numbers.
- No clipping at the window edge, in the plug-in at 880×580, or at 125/150% scale.
- Folder picks use the confirmation and background batches; a repeat is idempotent.
- `Edit tags…` opens the ring for one file.
- Schema 20 migrates with a backup and seeds from `recent_tags`; the plug-in test still passes.
