# Library stability and additive analyzer versioning

Date: 2026-11-06
Status: proposed

## Intent

The scan of the real library reached `299057/299057` and the catalog was written to
SQLite, yet the window showed `0 samples` and the buttons stayed disabled. The measured
results are intact; the build never reached its completion path, and the UI only reloads
the list once the whole job has finished.

This stage makes the catalog usable during and immediately after indexing, removes the
finalization bottleneck, and changes analyzer versioning so that a future analyzer can be
added without silently invalidating or recomputing the existing results of 298 899 files.

## Goals

1. Saved samples appear within the first seconds of startup and fill in while indexing
   is still running.
2. Finishing a scan does not block on repeated sorting of the whole table.
3. Existing `version = 1` measured results stay valid and are never reanalyzed implicitly.
4. The schema upgrade is forward-only, idempotent, and leaves a recoverable backup.
5. Behaviour is covered by tests at realistic table size (300k synthetic rows).

## Non-goals

- ML classification, embeddings, similarity search, sound map.
- Whole-catalog search (search remains page-local; separate backlog item).
- Tags, duplicates toggle, settings window, themes, localization.
- VST3/AU builds. ARA stays deferred.

## Constraints

- The user's `~/Library/Sonicator/sonicator.sqlite` must not be reset, overwritten, or
  reanalyzed. Record IDs, hashes and measured DSP values are preserved.
- Original audio files are never modified.
- The database may be opened by a new build while a 299k-row catalog exists.

## Approaches considered

### Missing-file reconciliation

- **Rejected: keep paging with OFFSET.** `ORDER BY name COLLATE NOCASE, id LIMIT ? OFFSET ?`
  has no matching index, so every page builds a temporary B-tree over 299k rows. Adding
  the index alone fixes the sort but still leaves `File::existsAsFile` per record — on an
  external SSD that is the larger cost.
- **Rejected: stat every record before showing the list.** Correct, but keeps the UI
  hostage to the slowest phase.
- **Chosen: compare against the discovery set.** Discovery already produces every path
  under the scanned roots. After analysis, stream the catalog by primary key and mark
  records that are under a scanned root but absent from that set. One index-ordered pass,
  no per-row filesystem call, no sort. Costs roughly 25 MB of path strings for 300k files.

### Analyzer versioning

- **Rejected: keep `analysisVersion = 2` and treat 1 as legacy-but-valid.** Two "valid"
  versions would live forever, and the stored number would no longer mean what it says.
- **Rejected: introduce a general per-analyzer result table now.** There is no analyzer to
  fill it yet; the schema would be guesswork and would need another migration soon.
- **Chosen: name the version after the feature family it actually describes.** The stored
  `version` column covers the measured DSP feature set only. All 298 899 version-1 rows
  contain that set (hash, duration, rate, channels, peak, RMS, centroid, rolloff,
  flatness). Set the constant to 1 so the data is described truthfully and skipped.
  Future analyzers add their own versioned storage when they land.

## Design

### Schema version 2

`SampleRepository::createTables()` reads `PRAGMA user_version`:

- `> 2` — refuse to open, as today.
- `< 2` — copy the database to `sonicator.sqlite.backup-YYYYMMDD-HHMMSS` next to it,
  then, in one transaction, create
  `samples_name_id ON samples(name COLLATE NOCASE, id)` and set `user_version = 2`.
- `= 2` — create tables and indexes if absent, no backup.

The backup uses the SQLite online backup API, not a file copy, because the database runs
in WAL mode and a raw copy can miss committed pages still in the WAL.

### Reconciliation

`IndexingService::run()` keeps the discovered path set from the discovery phase. After the
analysis loop, if not cancelled:

- chunked `SELECT id, path, missing FROM samples WHERE id > ? ORDER BY id LIMIT 5000`
  (primary-key range, no temp sort);
- for each row under one of the scanned roots and absent from the set, set `missing = 1`;
- records under other roots, or already marked missing, are untouched;
- batch the updates in a transaction per chunk.

`IndexingProgress` gains a `finalizing` flag, and the message reads
`Checking for missing files` during this phase, so the status line no longer says
`Analyzing` while the scan is past the analysis stage.

### Repository

- `page(limit, offset)` stays the UI read path, now index-ordered.
- New `forEachPathChunk`/`recordsAfterId`-style read for reconciliation, plus
  `setMissing(ids)` for batch updates.

### UI

- `MainComponent` reloads on `count()` change (a cheap indexed `COUNT(*)`) and at most
  every 1.5 s while a job runs; it also reloads immediately after a job starts and when it
  ends.
- The catalogue is reloaded in the constructor as today, so reopening the same build shows
  the 299k samples without any scan.
- Buttons stay disabled while `running || finalizing`; the status line shows the phase.

### Versioning

- `analysisVersion` becomes `basicFeaturesVersion = 1`, documented as the version of the
  measured feature set stored in `SampleRecord`.
- The unchanged-file skip condition keeps comparing equality, so `version = 1` rows skip.
- A future change to `BasicAnalyzer` output bumps this number deliberately; `Reanalyze all`
  remains the explicit way to force recomputation.

## Tests

- Migration: build a version-1 database, reopen with the new code, assert the index exists,
  `user_version = 2`, rows unchanged, and exactly one backup created; reopening again
  creates no second backup.
- Compatibility: a row with `version == basicFeaturesVersion` is skipped on rescan; a row
  with `version - 1` is reanalyzed (existing test keeps its meaning).
- Reconciliation: 300k synthetic rows, two real files on disk; scan finishes with correct
  `missing` flags, no temp B-tree in the query plan, and bounded runtime.
- UI: an existing catalog is visible before any scan; the list picks up rows inserted
  while a job is running.

## Decisions left to the builder

- Backup file naming and retention (one per migration, never deleted automatically).
- Chunk size 5000 for reconciliation; 1.5 s reload cadence.
- Page size stays 10 000.

## Risks

- Marking `missing` from the discovery set is only valid for roots that were fully
  traversed; a cancelled or failed discovery must skip reconciliation, as today.
- A file created between discovery and reconciliation is treated as missing until the next
  scan; it is restored automatically then.
- The first launch after the upgrade writes a ~150 MB backup beside the database.
