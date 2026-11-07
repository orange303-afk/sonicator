# Reset tags and favorite folders — design

Status: approved 2026-11-08, implemented. Favorites are global across catalogs.

## Intent

Make it possible to clear tag assignments from a selection or a folder subtree, and to pin any
catalog folder — with its existing subfolders — in a separate favorite tree below the normal folder
tree.

## Reset tags

- **Files:** a `Reset tags...` item in the file context menu clears every tag assignment from the
  selected catalog ids after a confirmation showing how many selected files currently have tags.
- **Folders:** a `Reset tags...` item in the folder context menu clears every tag assignment from
  the catalogued files in that folder and all descendants. The confirmation shows the path, that
  subfolders are included, the catalogued count and the currently tagged count.
- Tag definitions are not deleted. Stars, measured features, smart overrides, originals and the
  recent-tag history are untouched. Removing tags never updates recent-tag history.
- Work runs through the existing background tagging service in bounded transactions, with progress
  and cancellation. A repeated reset is a no-op. The unfinished batch rolls back on cancellation;
  completed batches stay, and the actual removed count is reported.
- Scope uses the existing separator-aware folder range, independent of search, filters, stars,
  Hide empty, duplicate hiding and pagination.

## Favorite folders

- The folder context menu toggles `Add to favorites` / `Remove from favorites`.
- Favorites are global for all catalogs and are stored in `settings.json`. Adding a parent removes
  redundant descendant favorites; adding a path already visible under a favorite is a no-op.
  Switching catalogs keeps the list; a favorite that has no rows in the current catalog remains
  visible but muted.
- A `FAVORITES` section appears below the normal folder tree only when at least one favorite exists.
  It is a second expandable tree: each favorite path is a root, and its catalogued subfolders are
  materialized on expansion exactly like the main tree.
- Clicking any node filters the library to that folder and clears the main tree selection, so only
  one tree shows a selected folder. A missing favorite remains visible but muted and reports its
  unavailable path when clicked.
- Right-clicking a favorite root offers `Remove from favorites`; the main folder menu offers the
  same toggle for any folder. Original folders and files are never moved or modified.

## Approaches considered

1. **Second tree in the sidebar (chosen).** Reuses the existing folder hierarchy and expansion
   behavior, supports nested favorites, and keeps favorites visually below the normal tree.
2. **Flat list with indentation.** Cheaper to draw, but loses the familiar expand/collapse behavior
   and becomes noisy for deep libraries.
3. **Star markers in the main tree plus a flat section.** Adds a second visual state to every row
   and does not provide the requested separate expandable list.

## Storage and migration

Favorites use the existing `settings.json` mechanism (`favoriteFolders` and `favoritesPanelHeight`),
so favorites need no catalog migration. The list is global; folder paths from another catalog stay
in the list but have no matching rows until that catalog is opened. A separate follow-up uses schema
9 to add the new built-in instrument tags; reset tags itself does not change the schema. The
favorites divider height is persisted as well.

## Follow-up (2026-11-08)

Schema 9 adds the built-in Crash, Ride, Tom, Rim, Fill, Riser, Downfilter, Upfilter, Snap, Shaker
and Tamb tags, and AutoTag uses the matching classes. Crash no longer falls back to Percussion. The
favorites divider is draggable and its position persists as `favoritesPanelHeight` in the global
settings file.

## Verification

- Repository: clear-tag batch counts, idempotence, missing ids, folder scope and tagged counts.
- Settings: favorites round-trip through `settings.json` and survive reopening.
- Services: recursive reset, cancellation between batches, stars/overrides/recent history preserved.
- UI: favorite tree shows the folder and its descendants, expands like the main tree, clicking
  filters, missing path is muted, no section when empty.
- Regression: existing tag, AutoTag, preview, REX/MIDI/SF2 and folder actions remain green.
- Windows remains a separate acceptance check.
