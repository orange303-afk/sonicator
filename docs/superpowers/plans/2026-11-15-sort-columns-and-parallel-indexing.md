# Sort columns and parallel analysis

Approved by the owner. Two performance stages for the catalog:

1. Materialise the effective Type/BPM/Key/Genre values on `samples` (schema 18)
   and index them, so whole-catalog sorting stops running correlated subqueries
   over every row.
2. Replace the sequential analyse-and-write loop in `IndexingService::run()`
   with a bounded worker pipeline (analysis in 4 workers, one writer), so a
   full reanalysis is not pinned to one core.

Spec: the plan approved in chat on 2026-11-15. This file is its binding copy.

## Global constraints

- Schema version becomes 18; every existing migration row keeps its identity,
  tags, stars, measured features, overrides and revision.
- Effective values are refreshed in exactly one place: `writeResult(...)`.
  `setSmartResult`, `publishSmartAnalysis`, `setSmartOverride` and
  `clearSmartOverride` all flow through it, so they cannot drift.
- Unknown stays `NULL` and sorts last (unknown-last order is unchanged).
- Analysis workers never touch the database; one writer thread does every
  `upsert` and `countHash`.
- Cancel, pause, NewOnly, skip rules, duplicate counting, progress counters
  and missing/disc phases behave exactly as before.
- Full suite command:
  `cmake --build ~/Library/Caches/sonicator-build/debug --target SonicatorTests -j 8`
  then `~/Library/Caches/sonicator-build/debug/tests/SonicatorTests`.

## Task 1: Schema 18 effective sort columns

### Steps

1. Write `tests/TestCatalogSort.cpp` (register it in `tests/CMakeLists.txt`) with:
   - `Effective sort values follow publish, override and clear`: after
     `publishSmartAnalysis` the five `eff_*` columns carry the resolved values;
     a manual override replaces them; clearing restores the automatic value; an
     explicit-unknown override stores NULL.
   - `Sorting uses the effective-value indexes`: `EXPLAIN QUERY PLAN` for the
     exact ORDER BY shapes returns a plan naming `samples_eff_type`,
     `samples_eff_bpm`, `samples_eff_key` and `samples_eff_genre`.
   - `Schema 17 upgrades t...` — create a current catalog, insert publish data,
     drop the five columns and their indexes, set `user_version=17`, reopen and
     verify the columns are re-added and backfilled.
   Expected: the test binary fails (missing columns/indexes, ordering).
2. Implement in `core/database/SampleRepository.cpp`: bump `schemaVersion` to
   18, `ensureColumn` for `eff_type`, `eff_bpm`, `eff_key_tonic`,
   `eff_key_mode`, `eff_genre`; backfill with one `UPDATE samples SET ...`
   using the existing `effective*Sql()` expressions when `current < 18`;
   create the expression indexes and the Format/Duration/Rate/Channels/Status
   indexes; add `refreshEffectiveValues(sqlite3*, int64_t)` and call it at the
   end of `writeResult`; switch `orderBy()` for Type/Bpm/Key/Genre to the new
   columns.
   Expected: the new tests pass.
3. Update every existing schema-version expectation from 17 to 18
   (`tests/TestDatabase.cpp`, `tests/TestFeatureSchema16.cpp` if it asserts the
   version).
   Expected: full suite passes.
4. Commit: `perf(catalog): index the effective sort values`.

## Task 2: Parallel analysis pipeline

### Steps

1. Add a failing test in `tests/TestIndexing.cpp`:
   `A scan reports its analysis workers`: after a scan of a few files on a
   machine with at least two cores, `progress().workers` equals
   `min(4, hardware_concurrency)` and is at least 2.
   Expected: fails (`workers` is always 0 before the pipeline exists).
2. Add a regression test `Parallel analysis stores the same rows as sequential
   analysis`: analyse 24 WAVs sequentially with `BasicAnalyzer().analyzeAndHash`,
   then scan the same folder and compare every stored path, hash, duration,
   rate, channels, format and error flag, plus that duplicate groups and
   NewOnly skip counts are unchanged.
   Expected: passes after the pipeline, and must keep passing.
3. Implement the pipeline in `core/indexing/IndexingService.cpp`:
   discovery (unchanged), a single-threaded precheck producing the analysis
   list and skip counters, a pool of `min(4, hardware_concurrency)` workers
   (priority below normal) with a bounded result queue, and a writer loop in
   the service thread doing `upsert`, `countHash`, counters and `current`.
   Catch per-file exceptions into an error row. Cancelled results are dropped;
   pause is honoured between files. Add `workers` to `IndexingProgress`.
   Expected: new and existing indexing tests pass.
4. Measure: generate 2000 short WAVs in a temp folder and time a forced scan;
   record the result in the ledger. Expected: 2-3x faster than the sequential
   baseline measured on the same folder (pre-change binary or sequential
   reference run).
5. Commit: `perf(indexing): analyze files in parallel with one writer`.

## Task 3: Documentation and full verification

### Steps

1. Update `docs/ARCHITECTURE.md`: schema 18 effective columns and indexes in
   the schema paragraph; the indexing pipeline (discovery, precheck, workers,
   single writer) near the indexing section; the sorting note.
   Expected: prose matches the code.
2. Run the full suite.
   Expected: all tests pass.
3. Commit: `docs: record schema 18 and the parallel analysis pipeline`.
