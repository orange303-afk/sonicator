# Phase 1 architecture

## Boundaries

`SonicatorCore` owns filesystem discovery, SQLite records, streaming DSP and preview transport. No core source includes a project UI header. `SonicatorUI` owns widgets and action wiring; the standalone entry point owns the window. JUCE's audio-utils dependency currently brings framework GUI modules transitively into core; this does not couple core services to SONICATOR widgets.

## Threads and lifecycle

- Message thread: controls, device setup, file selection/reader replacement, debounced whole-catalog queries, user-triggered database backup, rendering. Search, sorting and tag filtering stay on this thread because the measured 300k-row query is below a perceptible hitch; no UI-queue callbacks are posted per keypress.
- Indexing thread: directory enumeration, chunked analysis, full-file hashing, database writes, reconciliation of missing paths.
- Dedicated preview read-ahead thread: JUCE buffering source, independent of the analyzer.
- Dedicated thumbnail cache thread: waveform extraction, independent of preview.
- Device callback: JUCE AudioSourcePlayer → AudioTransportSource → buffering/resampling source. No project DB, FFT analysis, file scanning or ML runs here. JUCE transport internally locks; do not claim strict lock-free safety.

UI and worker use separate SQLite connections in WAL mode. Each connection's repository operations are serialized by its own mutex. Progress is a small locked snapshot polled at 10 Hz, not a callback posted for every discovered file. No callbacks capture a raw window pointer. Folder chooser uses a JUCE SafePointer.

Shutdown stops the UI timer, cancels/joins indexing without force-killing a thread, then destroys repositories/connections. Audio callbacks detach before source/read-ahead teardown. Thumbnail listeners detach before thumbnail/cache teardown.

## Storage

Schema `user_version=5`. Stable numeric ID, unique path, full SHA-256, file size/time, format metadata, all implemented DSP values, feature-set version, error and missing flag. Root folders persist separately. Schema versions newer than the application are rejected, not overwritten. Version 2 adds `samples_name_id`, an index on `(name COLLATE NOCASE, id)` that matches the UI sort, so paged reads no longer build a temporary B-tree. Version 3 adds `samples.search_key` (Unicode-folded name + path for whole-catalog text search), `tags` and `sample_tags`. Version 4 adds `samples.starred`, a user favourite flag; analysis upserts deliberately do not update it, so a rescan can never clear a star. Version 5 adds the `duplicates` table: `(sample_id, representative_id, group_hash, group_size, is_representative)` with indexes on the group hash and representative. It is derived data, rebuilt from the catalog; the audio rows themselves are not rewritten. Standard tags are seeded only when the tag table is created, so a rename is not undone; sample IDs are stable across upserts, so assignments survive rescan and reanalysis. Upgrading first writes an online SQLite backup beside the database (`<name>.backup-<timestamp>`); the backup uses the SQLite API because WAL may still hold committed pages. A user-triggered backup uses the same API into `Sonicator/backups/sonicator-<timestamp>.sqlite` and refuses to overwrite an existing file or the live database. Settings updates load-modify-save only the fields they own, so window geometry and theme/catalog writers do not overwrite each other. Saved window bounds are validated against connected displays and clamped to a minimum size and the current work area.

Upsert by path preserves sample ID. Two files with the same content keep independent paths. Skip only when size/time and the feature-set version match. Files changed during analysis are marked for retry, not recorded as successful. Startup reads the database; it does not initiate reanalysis.

`basicFeaturesVersion = 1` describes the measured feature set stored in `SampleRecord`: duration, rate, bits, channels, peak, RMS, centroid, rolloff, flatness and hash. The earlier bump to 2 was reverted: every stored version-1 row already contains that full set, so keeping 2 would have invalidated 298,899 valid results without adding data. Future analyzers get their own versioned storage instead of forcing a global bump.

Scan modes: a full rescan analyzes new, changed and failed paths and reconciles missing records; `NewOnly` is additive — it analyzes paths absent from the catalog and never reconciles or retouches known rows. Cancellation is checked during discovery, hashing and each decode/FFT chunk; a result is written only after the file has been analyzed completely, so a cancelled reanalysis keeps the previous successful row.

After analysis, missing files are reconciled in one pass ordered by primary key. The discovered path set from the same scan is the reference; a record that was not discovered is probed on disk before it is marked missing, so a partial or failed enumeration cannot flag the whole catalog. Records under roots that were not scanned are untouched. WAV/AIFF physical audio-payload validation deliberately tolerates incorrect outer container sizes seen in real commercial sample packs. AppleDouble forks are excluded by a filename-prefix fast path plus a magic-byte check. Per-file hashing I/O failures become error records and do not abort the rest of an import.

Future machine predictions and user overrides must be stored independently of these measured file records. Model upgrades must not overwrite user annotations.

## Settings, theme and catalogue switching

Settings are stored as JSON at `Sonicator/settings.json` (atomic temp-file replace). The theme is a
runtime palette: `theme::apply` selects dark, light or the current system appearance, and
`Desktop::addDarkModeSettingListener` follows a live system change. Custom-painted components read
the palette at paint time; components with constructor-set colours re-apply them in
`lookAndFeelChanged`. In the light theme the silver wordmark is replaced by typeset product text.
Interface scale uses `Desktop::setGlobalScaleFactor` at startup and immediately after a change.

The Settings dialog can copy the current catalog to a new path through `backupTo` and switch every
repository to it, or open an existing catalog. The old file is kept. Switching is refused while
indexing and for files that fail `SampleRepository::isCatalog` (a `samples` and `roots` schema with
a nonzero `user_version`), so a foreign SQLite database is never altered. A saved catalog path that
is missing, invalid or unreadable falls back to the default location at startup.

## Duplicate groups

Byte-identical duplicates are grouped by the full SHA-256 already stored at analysis time plus a
nonzero size. `refreshDuplicates` rebuilds the derived table in one transaction: one representative
per group, chosen by starred, present on disk, analyzed without error, shortest path, then lowest
ID. Missing copies never beat present ones. The `Hide duplicates` filter excludes rows with
`is_representative=0`; `Show all copies` filters by the group hash instead. The table is refreshed
after every non-cancelled index run, at startup, after a catalog switch and after starring (which
can change the representative). This is byte equality only; decoded-PCM equality across containers
is not implemented.

## Selection, context menu and clipboard

`TableListBox` uses JUCE's native multi-selection: Cmd toggles a row, Shift extends from the last
lead row. Selection is restored by stable sample ID after a list refresh, so tagging or starring
does not lose it. The context menu is built from the current selection: tag ticks mean the tag is
present on every selected sample, and toggling applies to all of them in one transaction. Star is a
sample field rather than a tag, so the two annotations stay independent. Copy uses `NSPasteboard`
file URLs on macOS and newline-separated paths elsewhere; no decoder or database work happens on
the audio thread.

## DSP conventions

Decode in 2048-frame blocks, bounded by 64 channels. Peak = maximum absolute channel value. RMS = square root of total channel/sample power divided by the number of channel samples (not average of chunk RMS values). Values in SQLite are linear; UI displays dBFS. Silence displays -inf dBFS.

For each channel/frame: zero-pad final partial frame; apply non-normalized Hann window; compute FFT magnitude and accumulate squared magnitudes. Channels are combined in the power domain, so anti-phase stereo does not disappear. Centroid is power weighted, rolloff includes 85% of accumulated spectral power, flatness is geometric/arithmetic mean of power with a 1e-30 log floor. Silence descriptors are zero, never NaN. No claim of psychoacoustic loudness or pitch accuracy.

## Deliberate changes from the initial chat plan

- Full hashing, not prefix hashing; equal-hash files are not silently dropped.
- Explicit decode/database errors rather than zero-filled success.
- Persist every implemented descriptor, including rolloff and flatness.
- Polling progress instead of one callAsync per file; separate read/write connections.
- Persistent roots, missing markers, analysis/schema versions, 10k-row paging and no device dependency in offline audio tests.
- Standalone GUI target only, no unnecessary plugin/APVTS scaffold.

See `phase1-progress.md` for execution evidence and decisions.
