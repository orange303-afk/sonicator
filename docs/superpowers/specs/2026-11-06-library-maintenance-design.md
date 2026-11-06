# Library maintenance — design

Stage after search and tags. Scope: explicit confirmations for expensive scans, an additive
new-files scan, a user-triggered consistent database backup, and verified cancellation behaviour.

## Why

A catalog of 300,000 files makes a wrong click expensive. "Rescan" and "Reanalyze all" must say
what they do and how long they may take before they start. Downloading a new sample pack needs a
scan that only adds unknown paths, without touching existing records. Before maintenance work the
owner also needs a copy of the database that does not corrupt under WAL, and a cancelled job must
never destroy or half-replace the result it was recomputing.

## Goals

- Confirm `Rescan` and `Reanalyze all`, with distinct wording and time warnings.
- `New files`: discover the registered roots and analyze only paths absent from the catalog.
- Never reconcile or retouch known records in the additive mode.
- Write a consistent backup through SQLite's backup API; never copy the live `.sqlite`/WAL by hand.
- Keep the previous successful row when a reanalysis is cancelled; never persist "Cancelled".
- Keep completed results when any scan is cancelled.

## Non-goals

- No scheduling, retention policy or automatic deletion of user backups.
- No Settings dialog, database relocation, duplicate hiding or ML in this stage.
- No background/threaded backup UI yet; the copy is explicit and synchronous.

## Decisions

- `New files` starts immediately: the action is already narrow and explicit. `Rescan` and
  `Reanalyze all` use an asynchronous confirmation with 1 = confirm, 0 = cancel.
- `ScanMode::NewOnly` checks each discovered path with `findByPath`; known paths (including failed
  and older-version rows) count as skipped. Discovery still registers roots. Missing-file
  reconciliation is skipped entirely.
- A cancelled analysis never reaches `upsert`: the analyzer returns "Cancelled", the scan loop
  breaks, and the previous row remains. Cancellation is checked during discovery, hashing and
  every decode/FFT chunk.
- Backups go to `<database folder>/backups/sonicator-<timestamp>.sqlite`, use a nonexistent path,
  and refuse an existing destination or the live database. No auto-deletion.
- The backup runs on the message thread under the repository mutex. It is user-initiated and rare;
  the cost is a brief UI pause on very large catalogs, documented rather than hidden behind a fake
  progress indicator.

## Verification

Service tests: additive scan counters and unchanged known records; missing rows untouched; known
changed files skipped; cancellation of a slow reanalysis keeps the original hash/duration and
stores no "Cancelled"; backup opens with the same rows, tags and schema version and rejects unsafe
destinations. UI tests: both confirmation dialogs appear and can be cancelled; the Backup button
creates a readable copy beside the database. Debug and Release suites plus CTest.
