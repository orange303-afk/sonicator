# Library search and tags — design

Stage after library stability / preview-folders-MIDI. Scope: global text search, whole-catalog
sorting, folder+text+tag filtering, and a tag system with standard and custom tags.

## Why

A 300,000-record catalog is only useful when it can be searched as a whole. Today the text box
and header sorting operate on the fetched 10,000-row page, so a sample is unreachable unless the
user happens to be on its page. User annotations (tags) are also the prerequisite for later
classification: model predictions must never overwrite what the user wrote.

## Goals

- Search the entire catalog, not the current page, by filename and path.
- Sort, filter and paginate in SQL so a page always contains the true first rows of the result.
- Standard tags (Kick, Snare, Loop, Pad, ...) and user-created tags, several per sample.
- Every tag has its own editable colour. Tag assignment is independent of analysis.
- Combinations: folder + text + several included tags + excluded tags.
- Tag assignment survives rescan/reanalysis (same sample row, same ID).
- Read-only when browsing: nothing deletes or renames original audio.

## Non-goals

- No ML classification, embeddings, BPM/key, duplicate detection, Settings or plugins here.
- No 500k+ / FTS claim. The measured scan is used first; an index is added only if data demands it.
- No tag import/export, no tag hierarchy, no saved filters yet.

## Decisions

### Search implementation: SQL scan over a folded search key

Measured on a synthetic 300,001-row catalog: substring `LIKE` count + 10,000-row ordered page
took 14–25 ms per query. That is below a perceptible UI hitch at a 250 ms typing debounce, so an
FTS5 index (extra write path, triggers, `SQLITE_ENABLE_FTS5`, larger database) is unjustified.

SQLite's `LIKE` only folds ASCII case. Sample libraries contain Cyrillic, Greek and CJK names, so
the catalog gains a stored column `samples.search_key`: lower-cased `name` + newline + lower-cased
`path`, produced by JUCE's Unicode-aware `String::toLowerCase()`. Tokens are lower-cased the same
way, and each token must occur in the key (`LIKE %token%` with `%`, `_`, `\` escaped). This keeps
Unicode case-insensitive substring search without a per-row C++ SQL function.

Cost: roughly 15–30 MB in the owner's catalog, a one-time backfill during the v2→v3 upgrade, and
rescanning is not required.

### Tag storage

```sql
tags(id, name TEXT COLLATE NOCASE UNIQUE, colour INTEGER, builtin INTEGER)
sample_tags(sample_id, tag_id, PRIMARY KEY(sample_id, tag_id)) WITHOUT ROWID
sample_tags_tag(tag_id, sample_id)
```

Sample IDs are stable across upserts, so tags survive rescan and reanalysis. Standard tags are
seeded only when the `tags` table is created; a rename is therefore never undone on the next
launch. Standard tags can be renamed and recoloured but not deleted; custom tags can be deleted.

### Filter semantics

- Text: whitespace-separated tokens, all must match (AND), case- and accent-preserving substring.
- Folder: existing path-prefix restriction from the folder tree.
- Included tags: default "all selected" (AND); an explicit toggle switches to "any selected" (OR).
- Excluded tags: sample must have none of them.
- Filters combine with AND. Total count and page come from the same predicate.

### UI

- Search box stays in the header; placeholder becomes "Search entire library..." and typing is
  debounced by 250 ms. Enter applies immediately; Escape/clear restores the catalog.
- A `Tags` button above the table opens the tag filter menu: include toggles, an exclude submenu,
  All/Any, create tag, and per-tag colour/rename/delete actions. Active filters are named in the
  count line, and `Clear filters` resets text and tags.
- The inspector shows the selected sample's tags as coloured chips plus `Edit tags...`, which opens
  a ticked tag menu with `New tag...`.
- The table gains a `TAGS` column: the sample's tag names drawn in their tag colours.

## Risks

- One-time migration cost on 300k rows (backup + column rewrite). Measured in the scale test.
- `search_key` and `name`/`path` can drift if a future writer bypasses `upsert`; tests assert the
  key is written on every upsert, and raw inserts in tests are not a supported catalog path.
- LIKE wildcards in user input: escaped explicitly and covered by a test.

## Verification

Repository tests for global/multi-token/Unicode/wildcard search, folder+tag combinations, sorting
and counts; tag CRUD and assignment survival; v2→v3 migration with a real backfilled catalog.
UI tests for the tag strip, filter state and a whole-catalog search through `MainComponent`.
Scale test records search timings on 300k rows. Build and full CTest on macOS arm64.
