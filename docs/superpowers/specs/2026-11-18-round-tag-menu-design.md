# Round tag menu — design

**Status:** approved in conversation on 2026-11-18; implemented in `stage-round-tag-menu`.

## Intent

Assigning a tag is the most frequent tagging action in the library. The current path is
right-click → `Tag ▸` → scan a long alphabetical list, or one of five recent tags at the top of
the context menu. The round tag menu replaces that list with a radial menu that opens where the
user is already looking, puts the tags they actually use near the centre, and grows the pill under
the approaching cursor so pointing stays forgiving.

The same entry is available on the keyboard with Ctrl/Cmd+T for the selected rows or the selected
folder, and from `Edit tags…` in the preview.

## Goals

- One gesture from a row or folder selection to a tag assignment.
- Ranking reflects the person's own use: recent and frequent near the centre, old and rare
  outside.
- The menu stays readable at 55 tags in the live catalog without a scrolling list.
- Motion is calm: one appearance transition, continuous proximity growth, no bouncing.
- Everything works in the standalone application and in the SONICATOR plug-in editor.

## Non-goals

- Type-to-filter inside the ring, arrow-key navigation between pills, several tags per opening.
- Replacing the toolbar *Tags* filter (that is filtering, not assignment).
- Feeding AutoTag application into the usage history.
- A custom context menu for every action: only the tags entry becomes a pill.

## Anti-goals

- A ring that hides the tag list entirely and makes rare tags unreachable.
- A hover threshold that jumps: growth must follow distance continuously.
- A full-screen scrim or a window-owning popup that steals focus from the host.

## Constraints

- The catalog gains a `tag_usage` table; schema goes 19 → 20. Creating the table is a migration,
  so every catalog the new application opens becomes schema 20, and plug-in builds older than this
  one refuse it with the existing "newer catalog" message. Application and plug-in ship together
  in a release, so the cost is a user who updates only one of them.
- The plug-in never migrates the shared catalog; the application does. No change to
  `CatalogCompatibility`.
- UI text is English; the code stays dependency-free (JUCE and the standard library only).
- All sizes are logical pixels; the interface scale is applied by JUCE afterwards.
- No audio-thread work: the ring is a UI overlay with its own timer.

## Approaches considered

**Where the ring lives.** A full-window transparent child of `MainComponent` was chosen.
`PopupMenu` cannot paint custom sectors; `CallOutBox` brings its own frame, arrow and animation;
a separate top-level window breaks focus in plug-in editors. The child overlay catches its own
clicks, is clipped by the window, and is testable without an OS window.

**Pie sectors or pills.** Pills on two orbits were chosen after a side-by-side preview: tag names
read freely, the colour sits in a dot, and the approach enlargement is a natural property of a
pill. Sectors are faster to hit at the rim but cramp long names at 45 degrees.

**Ranking data.** A `tag_usage` counter in the catalog was chosen over reusing `sample_tags`
counts (that is corpus popularity, not the person's use) and over `settings.json` (wrong scope,
breaks with catalog moves and the plug-in).

**Two rings plus `All tags…`.** The live catalog has 55 tags. Eight pills on the inner orbit,
seven plus `All tags…` on the outer, and the existing list behind `All tags…` covers everything.
Fifteen visible tags is the honest readable maximum.

**Proximity.** Continuous distance-based growth was chosen over a 24 px threshold: the pill starts
growing as the cursor approaches, so there is no pop and neighbours grow by their own distance.

**Context menu entry.** The five recent tags were removed in favour of a single `Tags` pill at the
top of the row context menu, as requested. `PopupMenu` has no hover rows, so the pill is a custom
menu item (`PopupMenu::addCustomItem`) with a 200 ms dwell: hovering or clicking it dismisses the
menu and the ring grows from the pill's centre. Clicking stays as the fallback where hover is
unavailable.

## Data

New table, created in the same migration block as the others:

```sql
CREATE TABLE IF NOT EXISTS tag_usage (
    tag_id INTEGER PRIMARY KEY REFERENCES tags(id) ON DELETE CASCADE,
    use_count INTEGER NOT NULL DEFAULT 0,
    last_used_at INTEGER NOT NULL DEFAULT 0
) WITHOUT ROWID;
```

On `current < 20`, seed once from `recent_tags` so an existing catalog starts with a sensible
order instead of an alphabet:

- `use_count = MAX(1, 6 - seq)`
- `last_used_at = used_at`
- `INSERT OR IGNORE`

`recordTagUse(tagId)` increments `use_count` and sets `last_used_at`. It is called exactly where
`recordRecentTag` is called today: `MainComponent::assignTagAndRemember` and the successful end of
a `TaggingService` run. AutoTag application and Reset tags do not count, matching the existing
recent-tag rule.

`rankedTags(limit, nowMs)` reads tags left-joined with `tag_usage` and sorts in C++ by

```
score = (use_count + 0.5) * 2^(-ageDays / 30)
```

where `ageDays = max(0, (nowMs - last_used_at) / 86 400 000)`. Never used means score 0. Ties go
to the higher `use_count`, then name, case-insensitively. `nowMs` is a parameter so tests do not
depend on the clock. Half-life and the +0.5 are tuning constants; they are not user settings.

## Ring

Two orbits: inner radius 122, outer 200 logical px, at 100%. Each orbit distributes its items
evenly: inner starts at -90 degrees, outer starts at -90 plus half a step (so eight outer items
start at -67.5). Pills: 26 px high, 13 px radius, 10.5 px Manrope, colour dot 7 px, `✓` for a tag
every selected file already has. `All tags…` is a neutral dashed pill in the last outer slot.

Centre hub: 84 px, `N files` (or `1 file`) above `TAGS`; for a folder, the folder name and the
catalogued count. The centre is clamped into the window with a 12 px margin so no pill is cut off;
the clamp moves the ring, not the content.

**Appearance:** 260 ms, scale 0.86 → 1, opacity 0 → 1, ease-out. **Close:** 90 ms, scale 0.94.
**Proximity:** distance from the cursor to the pill's rectangle; at 0 growth is 1.26 with a 13 px
outward push, at 90 px it is 0; smoothstep between. Time smoothing is exponential with a 180 ms
constant; the cursor itself is filtered at 60 ms. The nearest pill rises above its neighbours;
neighbours grow only by their own distance, so they are naturally weaker. **Click:** the rendered
pill only, not the growth halo. One pick closes the ring.

**Dismiss:** Esc, a click outside the pills, a right click, a selection change, a window resize,
a catalog reload. **Actions:** `onTagPicked(id)`, `onAllTagsPicked()`, `onDismissed()`.

## Entry points

- Row context menu, setting on: the five recent items and the `Tag ▸` submenu are replaced by one
  `Tags` custom item at the top. The rest of the menu is unchanged. Hover with a 200 ms dwell or a
  click dismisses the menu and opens the ring at the pill's centre.
- Folder context menu: `Tag folder…` keeps its place and opens the ring on click. A pick runs the
  existing confirmation and background batches (`confirmTagFolder`, extracted from the menu
  callback). Folder picks only assign, never toggle.
- Preview `Edit tags…`: the ring opens at the button, one file, toggle semantics.
- Commands `library.roundTagMenu` and `folders.roundTagMenu`, both default Ctrl/Cmd+T, scopes
  `library` and `folders`, category `Tags`. Both are unavailable while the setting is off or the
  selection is empty. The anchor is the mouse position inside the window, else the table or tree
  centre.
- `All tags…` closes the ring and opens the existing tag menu with ticks and `New tag…`.

## Settings

`AppSettings::roundTagMenu`, default false, persisted as `roundTagMenu`. A `TAGS` section after
`LIBRARY` in Settings → General holds the `Round tag menu` checkbox and one note line. It stays
visible in plug-in mode because tagging works there.

## What is left to the builder

- Exact pill padding and the menu item's inset inside `PopupMenu`.
- Whether the dwell is 180 or 220 ms; 200 ms is the starting value.
- Whether the ring should also be reachable from the plugin's compact layout at small editor sizes;
  the minimum editor is 880×580, which fits the ring with the clamp.
- Tuning the ranking constants after living with it for a week.

## Acceptance

Manual checks and their expected results live in `docs/ROUND_TAG_MENU_ACCEPTANCE.md`.
