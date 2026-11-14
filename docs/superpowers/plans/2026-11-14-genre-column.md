# Genre column — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show a musical genre column that comes from file metadata, the file name or the two nearest folders, with a visible source, manual correction and full-catalog sort/filter.

**Architecture:** Genre becomes a seventh smart field with the same storage, override and provenance rules as Type/BPM/Key, using the existing wide `smart_results` table and `smart_overrides`. The existing `GenreContextExtractor` becomes the metadata/path extractor and stops feeding the Type field. The column, sort and filter reuse the `effective*Sql` pattern already used for Type.

**Tech Stack:** C++20, JUCE 9.0.5, SQLite (schema `user_version` 16 → 17), Catch2 3.7.1.

**Spec:** `docs/superpowers/specs/2026-11-14-genre-column-design.md` (approved direction; this plan is its implementation).

## Global Constraints

- Never overwrite a manual genre, an explicit `Unknown`, or any other user value during analysis, rescan or reanalysis.
- Genre is a musical genre. It is never inferred from BPM, key, sample type or structure. A folder that matches nothing stays empty, not "Electronic" by default.
- Only the file name and the two nearest parent folders are consulted; a library or drive name never assigns one genre to everything beneath it.
- Short substrings inside other words never match; token matching is case-insensitive and Unicode-safe.
- Stable sample IDs, tags, stars, Type/BPM/Key results and existing overrides keep their values through the migration.
- The schema step is additive (`ensureColumn`), like versions 4–12. A newer schema is still rejected, not downgraded.
- No audio decoding on the UI thread; sorting and filtering stay single-query SQL like Type.
- Tests use temporary catalogs and settings only.

## Review Focus

1. A library root or drive name such as `Samples`, `1TBSSD`, `Music` must never become a genre for every row.
2. A manual `Unknown` must survive a rescan, a rename and a full reanalysis, and must not be replaced by a confident automatic match.
3. Conflicting strong sources must show `Ambiguous`/empty, never a coin flip.
4. Rows that today carry a genre inside the Type column must lose that value when reanalysed, without losing their real type or any tag.
5. Sorting/filtering 300k rows by genre must not decode audio and must not regress the paged query noticeably.

## File map

**Core**
- `core/smart/SmartTypes.h/.cpp` — `SmartField::Genre`, `smartFieldCount`, key mapping, `SmartDisplay.genre/genreState/genreOverride`.
- `core/smart/SmartResolver.cpp` — genre resolution and conflict state.
- `core/smart/GenreContextExtractor.h/.cpp` — metadata/path extraction rules and dictionary version.
- `core/smart/SmartAnalysisService.cpp` — emit Genre candidates instead of Type.
- `core/database/SampleRepository.cpp/.h` — schema 13, read/write, `effectiveGenreSql`, sort, filter, overrides.
- `core/database/LibraryQuery.h` — `LibrarySort::Genre`.
- `core/database/SearchSyntax.cpp` — `genre:` token.

**UI**
- `ui/library/FileListPanel.cpp` — column, cell, header sort.
- `ui/MainComponent.cpp` — sort mapping, context menu `Genre ▶`, mass assignment, tooltip.
- `ui/settings/*` — nothing user-facing; Genre is not a setting.

**Tests**: `TestSmartGenre.cpp` (extractor + resolver), `TestSmartDatabase.cpp` (migration, override, sort/filter), `TestGenreColumn.cpp` (UI behaviour), plus a corpus case in `TestGenreCorpus.cpp`.

---

### Task 1: Smart field and schema 17

**Files:** `core/smart/SmartTypes.h/.cpp`, `core/database/SampleRepository.cpp`, `tests/TestSmartGenre.cpp`, `tests/CMakeLists.txt`.

**Interfaces:**
- `enum class SmartField { Type, Structure, Bpm, Key, Pitch, RootNote, Genre };` with `smartFieldCount = 7`.
- `smartFieldKey(SmartField::Genre) == "genre"`, `smartFieldFromKey("genre") == SmartField::Genre`.
- New columns on `smart_results`: `genre TEXT`, `genre_state TEXT`, `genre_source TEXT`, `genre_override TEXT` (defaults `''`, `'unknown'`, `''`, `'none'`).

- [ ] Test: a catalog created at version 16 with rows, tags and a Type override reopens at 17 with every value intact and the four new columns present.
- [ ] Test: `smartFieldCount == 7`, key round-trip for every field.
- [ ] Run the migration test against the old schema and confirm it fails before `ensureColumn` calls exist.
- [ ] Implement the enum, key map and the additive columns; bump `user_version` to 17.
- [ ] Commit: `feat(genre): add genre smart field and schema 13`.

### Task 2: Extract genre candidates from metadata, name and nearest folders

**Files:** `core/smart/GenreContextExtractor.h/.cpp`, `core/smart/SmartAnalysisService.cpp`, `core/smart/AcousticClassifierService.cpp`, `tests/TestSmartGenre.cpp`, `tests/TestGenreContext.cpp`.

**Interfaces:** `GenreContextExtractor::extract(file, candidates)` keeps its result shape but gains `matchedFragment` (the exact folder or file-name text) and `dictionaryVersion`. A new helper `std::vector<SmartCandidate> genreCandidates(const GenreContextResult&)` returns at most one `SmartField::Genre` candidate with `source`, `detail`, `confidence` and `limitation`.

- [ ] Test: a file in `…/Loops/House/Deep House/bass.wav` gets `House` or `Deep House` with source `path` and the matched fragment `Deep House`.
- [ ] Test: `…/Samples/1TBSSD/Music/kick.wav` and `…/Music/kick.wav` produce **no** candidate; a library root name never assigns a genre.
- [ ] Test: a file name `trap_kick_01.wav` in a neutral folder produces `Trap` with source `filename`.
- [ ] Test: metadata `TCON`/`IGNR`/INFO genre wins over the path, and both are kept as separate candidates.
- [ ] Test: `dnb`, `drum and bass` and `Drum & Bass` normalise to one value; `housekeeper.wav` does **not** match `house`.
- [ ] Run and confirm the "library root" and "substring" cases fail first.
- [ ] Implement: metadata → filename → nearest two folders; keep a versioned dictionary; store the original fragment.
- [ ] Remove the `SmartField::Type` candidate built from `genreResult` in `SmartAnalysisService`; bump `smartExtractorsVersion`. Keep the classifier's use of genre as a *feature* only.
- [ ] Test: after this change the same file's Type is decided by the acoustic/type logic and is never `House`/`Trap`.
- [ ] Commit: `feat(genre): extract genre candidates from metadata name and folders`.

### Task 3: Resolve genre, including conflicts and overrides

**Files:** `core/smart/SmartResolver.cpp`, `core/smart/SmartTypes.h`, `tests/TestSmartGenre.cpp`.

**Interfaces:** `SmartResult` gains `genre`, `genreState`, `genreSource`. Rules: manual override first; metadata beats path beats filename; two independent groups with different values and comparable confidence produce `SmartState::Ambiguous` and an empty display value; a single match is `SmartState::Resolved`.

- [ ] Test: metadata `Ambient` + folder `Rock` resolves to `Ambient` with source `metadata`.
- [ ] Test: folder `House` and file name `trap_kick.wav` with no metadata produce `Ambiguous`, empty value, both candidates retained.
- [ ] Test: a manual `Rock` override beats a confident `House` candidate.
- [ ] Test: an explicit `Unknown` override yields `Unknown`, not empty-and-guessable.
- [ ] Implement; bump `smartResolverVersion`.
- [ ] Commit: `feat(genre): resolve genre with conflicts and overrides`.

### Task 4: Persist and read genre

**Files:** `core/database/SampleRepository.cpp/.h`, `core/smart/SmartTypes.h`, `tests/TestSmartDatabase.cpp`, `tests/TestSmartGenre.cpp`.

**Interfaces:** `SmartDisplay` gains `genre`, `genreState`, `genreOverride`; `publishSmartAnalysis` writes the new columns and never touches the override; `smartDisplays` reads them; `setSmartOverride(sampleId, SmartField::Genre, …)` and `clearSmartOverride` work unchanged.

- [ ] Test: publishing a genre result then re-publishing a different one leaves a manual override untouched.
- [ ] Test: publishing a reanalysis with no genre candidate clears a stale automatic value but keeps a manual one.
- [ ] Test: a rename changes the path candidate and marks the row stale; a manual override still survives.
- [ ] Implement read/write and the `SmartDisplay` fields.
- [ ] Commit: `feat(genre): store and read the genre result`.

### Task 5: Sort, filter and search the whole catalog

**Files:** `core/database/SampleRepository.cpp`, `core/database/LibraryQuery.h`, `core/database/SearchSyntax.cpp`, `tests/TestSmartDatabase.cpp`, `tests/TestSearchSyntax.cpp`.

**Interfaces:** `LibrarySort::Genre`; `effectiveGenreSql()` follows `effectiveTypeSql()` (override row first, then `smart_results.genre`, `NULL` when unknown); `genre:house` in the search box filters; the page query returns the genre column without a per-row query.

- [ ] Test: sorting by genre puts empty values last and is case-insensitive; the override value wins over the stored one.
- [ ] Test: `genre:house` matches `House` and `Deep House`, not `housekeeper`.
- [ ] Test: a 1000-row page query with a genre sort executes one SQL statement and returns the expected first row.
- [ ] Implement the SQL expression, sort case and search token.
- [ ] Commit: `feat(genre): sort filter and search by genre`.

### Task 6: The column and the context menu

**Files:** `ui/library/FileListPanel.cpp`, `ui/MainComponent.cpp`, `tests/TestGenreColumn.cpp`, `tests/TestUI.cpp`.

**Interfaces:** a `GENRE` column between `KEY` and `FORMAT`; cell text `—` when empty; tooltip shows value, source (`metadata`, `path`, `filename`, `manual`), matched fragment and conflicting candidates. `Genre ▶` menu on one or many rows: `Assign genre…`, `Unknown`, `Clear manual genre`.

- [ ] Test: the header exists and sorting by it sets `LibrarySort::Genre`.
- [ ] Test: an empty genre renders as an em dash and a resolved one as its value.
- [ ] Test: assigning `Jazz` to three selected rows writes a manual override for exactly those rows; `Unknown` writes an unknown override; clearing restores the automatic value.
- [ ] Test: assigning a genre never changes tags, stars or Type.
- [ ] Implement the column, cell, tooltip, sort mapping, menu and the mass action.
- [ ] Commit: `feat(genre): show and edit the genre column`.

### Task 7: Corpus check and performance

**Files:** `tests/TestGenreCorpus.cpp`, `tests/fixtures/genre/…`, `docs/GENRE.md`.

**Interfaces:** a small committed fixture set: 12 synthetic folder/file layouts with the expected value or expected emptiness, plus 20 real entries copied from the owner's catalog paths (names only, no audio).

- [ ] Test: every fixture case matches its expected value or expected empty result.
- [ ] Measure: paged query with a genre sort and filter on the live 300k-row catalog (read-only, timing only) and record the number in `docs/GENRE.md`.
- [ ] Test: a full reanalysis pass never rewrites a manual genre (integration, temporary catalog).
- [ ] Commit: `test(genre): lock the corpus expectations and the query cost`.

### Task 8: Documentation and acceptance

**Files:** `docs/GENRE.md`, `docs/ARCHITECTURE.md`, `docs/COMMANDS.md`, the spec file.

- [ ] Document the dictionary version, the resolution order, the conflict rule and how a manual value is cleared.
- [ ] Update the spec status and the architecture storage paragraph (schema 13).
- [ ] Run `ctest --preset debug`, a Release build, and `graphify update .`.
- [ ] Record the real acceptance results, including anything not verified (Windows, other layouts).
- [ ] Commit: `docs: record the genre column stage`.

## Verification

```bash
cmake --preset debug
cmake --build --preset debug --target SonicatorTests
"$HOME/Library/Caches/sonicator-build/debug/tests/SonicatorTests" "[genre]"
ctest --preset debug
```

Expected after Task 8: every new case passes, the full suite has no new failures, and the paged genre query stays within the measured baseline recorded in `docs/GENRE.md`.
