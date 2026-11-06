# Hide duplicates — design

Stage after context menu/stars. Scope: hide byte-identical copies from the library view without
deleting files, and expose the group size and paths.

## Why

The owner's catalog has ~300k files, many of them copies. Every record already stores a full
SHA-256, so exact duplicates can be grouped without re-reading audio. Browsing is more useful when
one representative is shown and the other copies are one action away.

## Goals

- Group files with the same full SHA-256 and a nonzero size.
- Keep one representative; hide the rest behind a `Hide duplicates` filter.
- Show group size in a `COPIES` column and list all paths in the inspector.
- `Show all copies` opens the whole group.
- Rebuild groups after indexing, at startup, after a catalog switch and after starring.
- Nothing is deleted, moved or renamed.

## Non-goals

- No decoded-PCM comparison across containers or metadata; byte equality only.
- No "similar sound" detection, no ML.
- No deletion or automatic cleanup.
- No user-selected representative yet.

## Decisions

- Schema v5 adds a derived `duplicates` table rather than rewriting the 300k `samples` rows:
  `(sample_id, representative_id, group_hash, group_size, is_representative)` with indexes on
  `group_hash` and `representative_id`.
- Equality is the stored SHA-256 plus a nonzero size. A hash group is definitive for byte identity;
  no decode or re-hash pass is required.
- `refreshDuplicates` clears and rebuilds the table in one transaction. It runs at startup, after a
  non-cancelled scan (and after a cancelled one from the UI), after a catalog switch and after a
  star change, since the representative rule depends on stars.
- Representative order: starred, present on disk, analyzed without error, shortest path, lowest ID.
  Missing copies never beat present ones. The rule is catalog-wide.
- `Hide duplicates` excludes rows with `is_representative = 0`; `duplicateHash` shows all members
  of one group and clears other filters so the action really reveals every copy.
- The filter combines with text, folder, tags and stars; counts and paging use the same predicate.

## Risks

- A catalog-wide representative can be in a different folder from the current view. Documented;
  `Show all copies` clears filters and reveals all members.
- Derived data can be stale after a crash mid-scan. Startup and UI-triggered rebuilds bound the
  staleness; the source rows are never modified.
- A group spanning thousands of files (as in the synthetic scale test) makes the table large; the
  rebuild remains one transaction and is measured.

## Verification

Grouping by hash/size and unique-hash exclusion; counts, members and representative path; starred,
missing and shortest-path representative rules; hide and group-hash filters; idempotent rebuild;
automatic refresh after indexing; shell toggle; schema migration to v5; 300k-row scale run.
