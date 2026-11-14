# Custom Genre Menu Implementation Plan

> **For agentic workers:** Use executing-plans inline, with TDD and one final independent review.

**Goal:** Persist manually entered genres and offer them in both assignment menus.
**Architecture:** Schema 19 adds a catalog-scoped vocabulary populated transactionally by manual
assignments and migration. `SampleRepository::genreNames()` combines canonical and saved names;
each popup captures its own list and uses a separate high ID range for genre selection.
**Tech Stack:** C++20, JUCE, SQLite, Catch2.
**Spec:** `docs/superpowers/specs/2026-11-15-custom-genre-menu-design.md`.

## Global Constraints

- No changes to automatic inference, audio files or unrelated metadata.
- Remember names across restart/clearing; exclude Unknown/not-applicable and failed assignments.
- Trim and deduplicate case-insensitively, including supported Unicode casing.
- Catalogs remain independent; callbacks use the list actually shown.

## Review Focus

- Migration backfills manual names without copying automatic metadata or special states.
- Clearing/deleting assignments does not delete the vocabulary.
- Cancelled/conflicting/failed folder actions do not register a genre.
- More than 40/90 genre entries cannot collide with old folder/row action IDs.
- A menu opened before another assignment uses its frozen name mapping; a catalog switch invalidates it.

### Task 1: Stored vocabulary and menus

**Files:** `core/database/SampleRepository.{h,cpp}`, `ui/MainComponent.cpp`,
`tests/TestCustomGenres.cpp`, `tests/CMakeLists.txt`, schema expectations in existing database tests,
`docs/GENRE.md`.
**Interface:** `juce::StringArray SampleRepository::genreNames() const` returns built-ins plus saved
genres without folded-name duplicates. Existing manual assignment APIs persist names atomically.

- [x] Write failing repository tests for persistence, trimming/case/Unicode deduplication,
  migration from schema 18, catalog independence, exclusion of special/automatic states and
  failed/cancelled folder assignments.
- [x] Build/run to observe missing vocabulary API (RED).
- [x] Implement schema 19, transactional registration, combined names and migrate version tests.
- [x] Add failing row/folder menu reuse tests, including a long list and a stale popup mapping.
- [x] Implement shared list lookup, captured menu snapshots and separate genre ID ranges.
- [ ] Run focused tests and full debug ctest, build release, graphify update and review.

## Ledger

- Baseline: main `86c9f21`; previous merged suite passed 1/1, 1099 cases.
- Ruling: reuse the current clean checkout on feature branch `feat/custom-genre-menu` and cached
  builds rather than allocate another workspace with ~16 GiB free; no worktree created.
- Pre-flight: single integrated task, no inter-task interfaces.
- Ruling: owner approved implementation on the approval card; execute inline without another
  planning handoff. Persist all valid manual names (including canonical ones) and deduplicate
  only the menu list, so first custom spelling is stable without changing assigned values.
- RED: missing `genreNames()` compiler errors; GREEN: 5 repository cases / 28 assertions.
- UI RED: injected custom-genre actions left both row/folder targets unchanged. GREEN: 19
  focused cases / 190 assertions, including 105 saved names, frozen popup name mappings and a
  catalog switch invalidating a row popup.
- Full-suite first pass: 12 migration assertions exposed the old hardcoded user_version=18
  write despite the schema constant being 19. Fixed the write to derive from schemaVersion,
  added an explicit migration-version assertion, rebuilt and reran the whole suite.
- Full debug suite: 1,107 cases / 15,296 assertions passed; `ctest --preset debug` 1/1, 224.17 s.
- Release Sonicator built successfully; `codesign --verify --deep --strict` passed.
- `graphify update .` completed: 13,685 nodes / 41,518 edges; existing zero-node data-file and
  community-label warnings are unrelated to this feature. No paid semantic extraction.
