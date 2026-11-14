# Folder Set Genre Implementation Plan

> **For agentic workers:** Use executing-plans inline; checkbox steps track progress.

**Goal:** Assign a manual Genre recursively from a folder's context menu without changing inference.

**Architecture:** Add one atomic repository operation that checks conflicting manual overrides
before writing. MainComponent launches it with std::async, polls completion from its existing
timer, and asks for replacement confirmation when necessary.

**Tech Stack:** C++20, JUCE, SQLite, Catch2; existing CMake presets/build cache.

**Spec:** `docs/superpowers/specs/2026-11-15-folder-set-genre-design.md`

## Global Constraints

- No changes to audio files, other smart fields, tags, stars, inference, or database schema.
- Recursive catalog scope independent of table filters; ignored paths excluded.
- No permanent inheritance; conflicts must be confirmed before any changes.

## Review Focus

- Folder prefix siblings and SQL wildcard characters must not widen scope.
- Unknown/not-applicable manual overrides require confirmation; cancel leaves all rows unchanged.
- Reanalysis must preserve assigned genre and unrelated metadata.
- Empty/custom Unicode genres and an empty/unavailable folder must behave safely.
- Async shutdown/catalog switching and stale confirmation must not use destroyed repositories.

### Task 1: Atomic folder assignment

**Files:** `core/database/SampleRepository.{h,cpp}`, `tests/TestFolderGenre.cpp`, `tests/CMakeLists.txt`.

**Interfaces:** `FolderGenreAssignment { total, assigned, conflicts }`;
`SampleRepository::setGenreInFolder(folder, genre, replaceManual=false)`.

- [x] Write tests for recursion, prefix boundaries/wildcards, hidden pair members, ignored paths,
  conflict refusal/confirmation (including Unknown), preservation, reanalysis, one-time semantics,
  whitespace/custom Unicode text and empty scope.
- [x] Build/run tests before implementation; expect missing API (RED).
- [x] Implement scope discovery, conflict preflight and writes in one transaction, with rollback.
- [x] Run `[folder-genre]` tests; expect all pass (GREEN).

### Task 2: Folder menu and async owner

**Files:** `ui/MainComponent.{h,cpp}`, `tests/TestFolderGenre.cpp`, `docs/GENRE.md`.

**Interfaces:** consumes Task 1's API; menu routes to a private async folder assignment action.

- [x] Add failing UI integration tests for menu assignment, custom text, refusal/cancel/replace,
  catalog changes and shutdown while work is outstanding.
- [x] Add Set genre submenu and custom prompt, async result handling, replacement warning,
  refresh/status handling and catalog/shutdown lifecycle guards.
- [x] Run focused tests and full `ctest --preset debug`; build release Sonicator.
- [x] Document behavior, run `graphify update .`, review diff and run final verification.

## Execution ledger

- Baseline: `ctest --preset debug` passed 1/1 (35.61 s).
- Ruling: use branch `feat/folder-set-genre` in the current clean checkout and reuse cached builds
  rather than duplicate the workspace; only ~16 GiB free. No worktree created without consent.
- Pre-flight: Task 2 consumes exactly the result and method in Task 1; no interface conflict.
- Ruling: implement inline as requested; keep the small approved design and plan together with
  the changes instead of stopping for another planning approval.
- Task 1: complete. RED: compiler reported missing `setGenreInFolder`; GREEN: 6 cases / 86
  assertions passed. Fixed fixture-only assumptions (stereo duration and UTF-8 construction).
- Task 2: UI RED: folder menu action left the genre unset; GREEN: all 11 `[folder-genre]` cases /
  125 assertions passed, including recursive menu action, custom text, cancel/replace,
  catalog-switch confirmation guard and owner shutdown.
- Final review: independent read-only reviewer approved pending full-suite/release results;
  no Critical or Important findings.
- Final: minor (deferred): lifecycle tests pump messages before destruction and do not
  deterministically hold an active worker; a controlled barrier could strengthen coverage.
- Final: Ruling: the reviewer did not validate visual polish or large-library performance —
  existing themed menus/dialogs are reused and the transaction runs off the UI thread;
  no visual acceptance or large-library timing claim is made. Cost if wrong: a long-running
  assignment may need a future cancellation/progress enhancement.
- `graphify update .` completed: 13,659 nodes / 41,468 edges. Existing zero-node JSON-file and
  community-label-refresh warnings remain; no paid semantic extraction was run.
- The first parallel full-suite/release commands exceeded the shell's 120-second timeout;
  restarted as background jobs to allow completion without that limit.
- Full debug test binary passed: 1,099 test cases / 15,233 assertions. Some existing test inputs
  emit JUCE String debug assertions (the test process still passes); no warning-free claim made.
- Release `Sonicator` build completed successfully; app linked and ad-hoc signed at
  `~/Library/Caches/sonicator-build/release/Sonicator_artefacts/Release/SONICATOR.app`.
- Feature committed as `6373c38`, fast-forwarded to `main`; merged-result
  `ctest --preset debug` passed 1/1 (77.97 s).
- Task 2: complete. No blocking review findings. Minor lifecycle-test determinism enhancement
  is deferred; no claim of manual visual acceptance or large-library timing.
