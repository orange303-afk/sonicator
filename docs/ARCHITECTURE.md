# Architecture

## Boundaries

`SonicatorCore` owns filesystem discovery, SQLite records, streaming DSP and preview transport. No core source includes a project UI header. `SonicatorUI` owns widgets and action wiring; the standalone entry point owns the window. JUCE's audio-utils dependency currently brings framework GUI modules transitively into core; this does not couple core services to SONICATOR widgets.

## Threads and lifecycle

- Message thread: controls, device setup, file selection/reader replacement, debounced whole-catalog queries, user-triggered database backup, rendering. Search, sorting and tag filtering stay on this thread because the measured 300k-row query is below a perceptible hitch; no UI-queue callbacks are posted per keypress.
- Indexing thread: directory enumeration, chunked analysis, full-file hashing, database writes, reconciliation of missing paths.
- Dedicated preview read-ahead thread: JUCE buffering source, independent of the analyzer. While **Loop** is on with a waveform selection, the ordinary preview is re-attached without read-ahead so the range loop is sample-accurate and live edits are heard immediately; clearing the loop restores read-ahead.
- Dedicated tracker render thread: libxmp decodes the whole module into a bounded preallocated ring; the device callback only copies stereo frames from that ring. Loading, seeking and source decoding run on the message thread while the output is detached, or on the render thread, never inside the device callback.
- Dedicated thumbnail cache thread: waveform extraction, independent of preview.
- Device callback: JUCE AudioSourcePlayer → AudioTransportSource → buffering/resampling source, or the tracker ring, or a preallocated MIDI voice bank. No project DB, FFT analysis, file scanning or ML runs here. JUCE transport internally locks; do not claim strict lock-free safety.

UI and worker use separate SQLite connections in WAL mode. Each connection's repository operations are serialized by its own mutex. Progress is a small locked snapshot polled at 10 Hz, not a callback posted for every discovered file. No callbacks capture a raw window pointer. Folder chooser uses a JUCE SafePointer.

Shutdown stops the UI timer, cancels/joins indexing without force-killing a thread, then destroys repositories/connections. Audio callbacks detach before source/read-ahead teardown. Thumbnail listeners detach before thumbnail/cache teardown.

## Storage

Schema `user_version=12`. Stable numeric ID, unique path, full SHA-256, file size/time, format metadata, all implemented DSP values, feature-set version, error and missing flag. Root folders persist separately. Schema versions newer than the application are rejected, not overwritten. Version 2 adds `samples_name_id`, an index on `(name COLLATE NOCASE, id)` that matches the UI sort, so paged reads no longer build a temporary B-tree. Version 3 adds `samples.search_key` (Unicode-folded name + path for whole-catalog text search), `tags` and `sample_tags`. Version 4 adds `samples.starred`, a user favourite flag; analysis upserts deliberately do not update it, so a rescan can never clear a star. Version 5 adds the `duplicates` table: `(sample_id, representative_id, group_hash, group_size, is_representative)` with indexes on the group hash and representative. It is derived data, rebuilt from the catalog; the audio rows themselves are not rewritten. Version 6 adds the smart-analysis tables `smart_candidates`, `smart_results`, `smart_overrides` and `smart_inputs`. They are independent of measured features: `basicFeaturesVersion` stays 1, a rescan never clears tags, stars or overrides, and a reanalysed audio row deletes only its derived `smart_results` row so the background job rebuilds it. Candidates keep field, source, status, raw detail and numeric/text value; overrides are stored separately and can express an explicit unknown. `smart_inputs` records the size and modification time of the audio and its `<audio>.asd` sidecar so a changed sidecar is detectable even when the audio did not change. Version 7 hardens that storage: `smart_candidates` carries extractor id/version, raw value, source file, dependency fingerprint, limitation and a variable-tempo flag; `smart_results` carries per-field sources and override states, separate Pitch fields, revision, generation, pipeline state, the three analyzer versions and structured error fields; `smart_extractions` records that each source ran (including "absent"); `smart_failures` keeps code, message and attempts; `smart_meta` holds the monotonic generation counter. Version 8 adds `recent_tags`, the five-entry
recently-used tag history. Version 9 seeds the extended built-in tag set (Crash, Ride, Tom, Rim, Fill,
Riser, Downfilter, Upfilter, Snap, Shaker, Tamb) without touching user tags. Version 10 adds
`samples.compatibility`, the stored reason a container cannot be opened (`unknown` for legacy rows,
missing files and transient I/O); an incompatible row is never hidden by a guess. Version 11 adds
`ignored_paths` as the single source of truth for ignored files and folders, the cached `samples.ignored`
column derived from it, the stereo-pair grouping (`stereo_pairs`, `samples.pair_key`,
`samples.pair_side`), and the `custom_types` and `ml_feedback_samples` tables used by adaptive learning.
Version 12 adds the disc-image columns on `samples` (`container_path`, `disc_entry`, `disc_parent`,
`disc_partner`, `disc_path`) and `disc_index`, which records each image's size and modification time so an
unchanged image is not walked again. Every step is cumulative: statements use `CREATE TABLE IF NOT
EXISTS` and `ensureColumn`, so a database created by any older build reaches version 12 with its rows,
tags, stars, overrides and measured features intact.
A reanalysed audio row now marks its smart result `stale` instead of deleting it, so the last completed values stay inspectable but are never presented as current. Publishing candidates, extractions, fingerprints and the resolved result happens in one `BEGIN IMMEDIATE` transaction guarded by the expected revision and the catalog identity. A job that read an older revision, or an override edit that happened meanwhile, is never overwritten. Each result also stores the extractor, resolver and DSP versions independently; a version mismatch makes it a candidate for the next pass without re-hashing audio or recomputing measured features. Standard tags are seeded only when the tag table is created, so a rename is not undone; sample IDs are stable across upserts, so assignments survive rescan and reanalysis. Upgrading first writes an online SQLite backup beside the database (`<name>.backup-<timestamp>`); the backup uses the SQLite API because WAL may still hold committed pages. A user-triggered backup uses the same API into `Sonicator/backups/sonicator-<timestamp>.sqlite` and refuses to overwrite an existing file or the live database. Settings updates load-modify-save only the fields they own, so window geometry and theme/catalog writers do not overwrite each other. Saved window bounds are validated against connected displays and clamped to a minimum size and the current work area.

Catalog portability: when a catalog's stored roots do not exist on this computer, the UI offers to relocate them. `SampleRepository::relocateRoots` streams the sample rows once inside one transaction, picks the longest matching old root for each path, and rewrites `path` plus `search_key` through the pure `pathrelocation` helpers. Both `/` and `\` are treated as separators on input and `File::getSeparatorChar()` is written on output, so macOS and Windows catalogs work in both directions. IDs, tags, stars, hashes and measured features are untouched; `missing` is not changed and Rescan remains the reconciliation authority. A target-path collision merges the relocated row's tags and star into the existing row and deletes the duplicate. Before applying, the UI writes `<name>.relocate-<timestamp>.sqlite` with the SQLite backup API.

Upsert by path preserves sample ID. Two files with the same content keep independent paths. Skip only when size/time and the feature-set version match. Files changed during analysis are marked for retry, not recorded as successful. Startup reads the database; it does not initiate reanalysis.

`basicFeaturesVersion = 1` describes the measured feature set stored in `SampleRecord`: duration, rate, bits, channels, peak, RMS, centroid, rolloff, flatness and hash. The earlier bump to 2 was reverted: every stored version-1 row already contains that full set, so keeping 2 would have invalidated 298,899 valid results without adding data. Future analyzers get their own versioned storage instead of forcing a global bump.

Metadata-only containers (`.rx2`, `.sf2`, `.exs`, `.nki` and the tracker candidates `.mod`, `.xm`, `.s3m`) are catalogued through `core/smart/AssetAnalyzer` with `version = metadataOnlyVersion` (-1): file identity and readable container metadata, no DSP descriptors, and the unchanged-file skip accepts that version. Legacy `.rex`/`.rcy` are not indexed. Tracker candidates are confirmed by `core/legacy/LegacyFormatProbe`; an unrecognised candidate keeps a precise error instead of an audio row, and the detected format (for example an S3M renamed to `.mod`) is stored in the format column.

Tracker documents are an independent path: `TrackerDocument` reads a bounded module and exposes sample slots plus lazily decoded immutable PCM; `TrackerSongRenderer` owns libxmp on its worker and a two-second ring; `SampleVoicePlayer` renders the selected source chromatically. Source PCM extraction is deliberately separate from playback, so a format libxmp can play is not automatically claimed as extractable. PCM offsets, frame counts and conversions are checked 64-bit arithmetic, and one source is capped at 128 MiB decoded with a 256 MiB document cache cap.

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

## Paging and the virtual all view

Finite page sizes use `query(limit, offset)` with SQL filtering, sorting and counting. The `All`
option never loads every record: `queryIds` returns only the matching primary keys in catalog order
(measured ~35 ms for 300k rows by name, ~130 ms with a text filter in a Debug build), and the table
reports that length as its row count. Painting a row outside the cache asks the owner for a
400-row block; `recordsByIds` plus the tag and duplicate lookups fill that block (~2 ms), and rows
within ±2000 of the current position are kept up to a 4000-row cap. Selection is stored by sample
ID, and a reload keeps the selected row on screen instead of jumping to the top. The row itself
remains virtualized by `TableListBox`; scrolling does not build widgets for off-screen rows.

## Duplicate groups

Byte-identical duplicates are grouped by the full SHA-256 already stored at analysis time plus a
nonzero size. `refreshDuplicates` rebuilds the derived table in one transaction: one representative
per group, chosen by starred, present on disk, analyzed without error, shortest path, then lowest
ID. Missing copies never beat present ones. The `DUPLICATES` column shows the group size and
`Show all copies` filters by the group hash. The internal `hideDuplicates` filter still exists but
has no toolbar control in this stage. `LibraryFilter::hideEmpty` is the one **Hide empty and
unsupported** control: it adds `peak >= 0.001` (or metadata-only `version = -1`) and excludes
`compatibility IN (unsupported, invalid)`. `AssetCompatibility` is stored per sample (schema 10,
default `unknown`); legacy rows, missing files, cancellations and transient I/O stay `unknown` and
are never hidden by a guess. An incompatible row draws a red prohibition sign in the 14x14 star
cell instead of a star; the stored star is untouched and returns once the asset is compatible. The
status column shows `Unsupported` or `Invalid` separately. The table is refreshed
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

## Smart metadata (Phase 2, metadata-first)

`core/smart` parses metadata read-only and never decodes PCM for it. Extractors cover WAV chunks (full container, including chunks after `data`), AIFF Apple Loops (`basc`, `cate`, `INST`), Ableton `.asd` (v6 warp markers and `AufTaktData`), REX2 (`GLOB`, `RECY`, `SINF`, `SLCE`) and file/folder names. Every extractor is bounds-checked and returns candidates plus a structural error; a truncated chunk stops parsing and keeps what was already read. The service adds provenance to each candidate: the source file, its size/mtime fingerprint, the extractor id and version that produced it, and a limitation note where the value is not the musical truth (for example a REX2 preview tempo or a sampler root). Fingerprints are taken before and after reading; if they differ, nothing is published and the previous generation is marked stale.

Measured library evidence (see `docs/superpowers/specs/2026-11-07-phase2-metadata-spike.md`) drives the semantics: `smpl`/`inst`/ACID/`basc` root notes are sampler roots, never musical keys; `IKEY` is usually keywords; ASD `UnbiasedTempoEstimate` was `0` in every `IsSet` sample; Apple Loops `basc` BPM and REX2 `GLOB` tempo matched file names in 100% of comparable cases.

`SmartResolver` is a pure function: it picks by source priority, keeps disagreements as `conflicting`, and exposes an explanation string. `SmartAnalysisService` runs the extractors on a background thread with pause/cancel, writes each sample atomically, and stores input fingerprints for sidecar invalidation. It is started after an index run and can refresh a single sample synchronously.

## SoundFont 2 preview

`core/sf2/Sf2Bank` parses RIFF/sfbk, INFO name, sdta 16-bit PCM and pdta headers/bags/generators. Instrument zones become preset zones after intersecting preset key/velocity ranges; empty stub banks are rejected as having no samples. `Sf2KitSampler` copies the preset zones and PCM, preallocates 32 voices, and renders note-on/off from MIDI messages with key/velocity matching, root-key transposition, tuning, loop and exclusive-class choke. Loading and `prepare` run on the message thread; `render` allocates nothing and never reads files.

## REX2 preview

`core/rex/Rex2Decoder` ports the documented DWOP predictive decoder: five cumulative predictors, per-channel delta/average state, MSB-first 32-bit words, mono and mid-stereo layouts, 16- and 24-bit output. It reads the SDAT payload recorded by `Rex2Metadata`, decodes in blocks of up to 0x100000 frames and converts to float. `Rex2SlicePlayer` maps visible SLCE boundaries onto consecutive MIDI notes from 36, preallocates 16 voices, applies velocity, a 2 ms attack, 20 ms release in Gate mode and a 5 ms tail fade; one-shot mode ignores NoteOff. At most 128 slices are mapped. Both containers are published with the output detached, so `render` never touches files or allocates.

## DSP conventions

Decode in 2048-frame blocks, bounded by 64 channels. Peak = maximum absolute channel value. RMS = square root of total channel/sample power divided by the number of channel samples (not average of chunk RMS values). Values in SQLite are linear; UI displays dBFS. Silence displays -inf dBFS.

For each channel/frame: zero-pad final partial frame; apply non-normalized Hann window; compute FFT magnitude and accumulate squared magnitudes. Channels are combined in the power domain, so anti-phase stereo does not disappear. Centroid is power weighted, rolloff includes 85% of accumulated spectral power, flatness is geometric/arithmetic mean of power with a 1e-30 log floor. Silence descriptors are zero, never NaN. No claim of psychoacoustic loudness or pitch accuracy.

The smart DSP stage decodes a bounded prefix once (`core/dsp/AnalysisAudio`) and feeds both `RhythmAnalyzer` and `TonalAnalyzer`. Key uses an 8192-point chromagram with a circular tuning estimate, transient/flatness weighting, a chroma median filter and 24 Krumhansl–Kessler/Temperley profiles plus a low-frequency bass prior. Pitch uses FFT-accelerated YIN over the most stable segment of a one-shot, with clarity, stability and harmonicity gates. Both are pure, bounded, read-only functions called from the smart worker; nothing runs on the audio callback. `smartDspVersion` is 2. See `docs/SMART_ANALYSIS.md`.

## Plugin (VST3 and AU)

`plugin/` builds a VST3 and an AudioUnit around the same `MainComponent`. It compiles only with
`-DSONICATOR_BUILD_PLUGIN_SPIKE=ON`; without the option the targets and the application build are
unchanged. The processor opens no device: `processBlock` renders through the `AudioEngine` host mode
(`startHostAudio`, `processHostBlock`, `stopHostAudio`), which takes sample rate and block size from the
host, routes host MIDI into the same `MidiEventQueue` the application uses, and renders under the audio
callback lock. `MainComponent` accepts an external `AudioEngine*` and an `embeddedUi` flag; in a plugin
the settings, tag, split, extract and relocation dialogs are drawn inside the editor, because hosts close
or steal focus from separate `DialogWindow`s. The editor owns the `MainComponent` and creates it only
when the catalog file exists. `Desktop::setGlobalScaleFactor` is not applied in a plugin, since it is
process-wide. Product and manufacturer identifiers, plugin codes and the data directory are temporary
spike values; see `docs/plans/2026-11-11-vst3-au-plugin-spike.md`.

## Tags, AutoTag and sample types

`core/tagging` owns tag definitions, assignments, the recent-tag history and the rule engine. AutoTag
proposes tags from file and folder names, embedded metadata and resolved smart candidates, then applies
them in background batches that roll back an unfinished batch on cancellation. A rule maps onto an
existing tag name, so renaming a built-in tag makes its rule report `tag-missing`. When a user sets a type
or assigns tags, `ml_feedback_samples` stores the acoustic descriptor vector alongside the confirmed
label; `core/ml/AdaptiveClassifier` ranks the nearest stored prototypes and publishes them as a
`user_adaptive` source, which AutoTag can turn into suggestions. See `docs/TAGGING.md`.

## Training-data contribution

`core/contribute` turns rows of `ml_feedback_samples` into one JSON batch — features, labels, key
and tempo, never a path, a hash or a sample id — and posts it to the Worker in
`services/contribute-worker/`. `Uploader` owns one cycle and moves the catalog's watermark
(`contribution_state`) only after a 2xx; `Scheduler` runs it on a background thread about once a
day. Consent, the toggle, the preview and deletion live in Settings → Library and are gated in
`MainComponent`; the plugin never sends, it only shares the catalog. See `docs/PRIVACY.md`.

## Acoustic classification

`core/dsp/AcousticFeatures` extracts a descriptor vector from one bounded decode. `FastAcousticRules`
answers drums and sub-bass from cheap features, and `core/smart/AcousticClassifierService` publishes the
result as Type candidates that `SmartResolver` weighs against metadata and path evidence; a genre or
instrument name in the path is arbitrated rather than accepted blindly. An ONNX classifier exists behind
`OnnxAudioClassifier`, but no model file ships and nothing in the application calls `loadModel`, so that
branch is inactive in the shipped build. Nothing here runs on the audio thread.

## Disc images

`core/disc` reads ISO/IMG/BIN/CUE images and raw Akai volumes read-only through bounded byte/block
sources, so a 600 MB image is never loaded whole. Only entries this build can open become catalog rows:
audio files inside ISO-9660 and Akai sample records, with a synthetic path `<image>::<internal path>`.
They are metadata-only rows (`version = -1`); PCM is decoded on selection, never during indexing. Every
partition owns its own volume table and FAT with partition-relative start blocks, the FAT fields are
big-endian while the file fields are little-endian, and a `.bin` with a sibling `.cue` is represented by
the cue once. `disc_index` stores each image's identity so an unchanged image is skipped. See
`docs/DISC_IMAGES.md`.

## Deliberate changes from the initial chat plan

- Full hashing, not prefix hashing; equal-hash files are not silently dropped.
- Explicit decode/database errors rather than zero-filled success.
- Persist every implemented descriptor, including rolloff and flatness.
- Polling progress instead of one callAsync per file; separate read/write connections.
- Persistent roots, missing markers, analysis/schema versions, 10k-row paging and no device dependency in offline audio tests.
- Standalone GUI target first; the VST3/AU plugin hosts the same `MainComponent` behind an off-by-default CMake option, with no APVTS or plugin parameters.

See `phase1-progress.md` for execution evidence and decisions.
