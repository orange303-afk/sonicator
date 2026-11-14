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

Metadata-only containers (`.rx2`, `.sf2`, `.exs`, `.nki`, `.sfz` and the tracker candidates `.mod`, `.xm`, `.s3m`, `.xi`) are catalogued through `core/smart/AssetAnalyzer` with `version = metadataOnlyVersion` (-1): file identity and readable container metadata, no DSP descriptors, and the unchanged-file skip accepts that version. Legacy `.rex`/`.rcy` are not indexed. Tracker candidates are confirmed by `core/legacy/LegacyFormatProbe`; an unrecognised candidate keeps a precise error instead of an audio row, and the detected format (for example an S3M renamed to `.mod`) is stored in the format column.

Tracker documents are an independent path: `TrackerDocument` reads a bounded module and exposes sample slots plus lazily decoded immutable PCM; `TrackerSongRenderer` owns libxmp on its worker and a two-second ring; `SampleVoicePlayer` renders the selected source chromatically. Source PCM extraction is deliberately separate from playback, so a format libxmp can play is not automatically claimed as extractable. PCM offsets, frame counts and conversions are checked 64-bit arithmetic, and one source is capped at 128 MiB decoded with a 256 MiB document cache cap.

FastTracker II `.xi` is a fourth reader (`core/tracker/TrackerXiReader.cpp`) with its own magic, one 40-byte sample header at offset 298 and delta PCM at 338. It produces a document with one slot and no song: `AudioEngine` selects that slot directly, `trackerHasSong()` is false, and the preview hides the whole-song controls. The slot publishes the note-60 rate plus `rootKey`; `EmbeddedSampleSet` uses the root key to recover the natural rate for extraction. `docs/XI.md` records the layout and corpus findings.

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

## Waveform editing

The preview waveform doubles as a destructively saving editor. `core/edit/AudioEditBuffer` decodes
one mono/stereo source into float channels and keeps undo snapshots (capped at 256 MB; the oldest
snapshot is dropped first). Every operation takes a half-open frame range: selection when the user
made one, otherwise the whole buffer. Deleting, cropping, reversing and silence trimming move loop
and cue markers through `AudioEditMarkers`, which drops a loop that would become invalid. Fades,
normalize (-0.1 dBFS), gain, DC removal and mono conversion leave the markers alone. Playback of the
edited PCM reuses the decoded-sample engine path, so Play, MIDI Preview, loop and WAV export work
while a buffer exists, and `PreviewPanel::afterEditApplied` re-prepares the selection export from
the snapshot.

Saving is explicit. Cmd/Ctrl+S asks Overwrite / Save copy / Cancel; only WAV, AIFF and FLAC can be
overwritten, and everything else (decoded disc, instrument and tracker sources, MP3/OGG files) can
only be copied. Settings -> Editing has `Always overwrite original files` (off by default): while it
is on, a writable original is replaced without the Overwrite / Save copy / Cancel dialog, and a
source without a writer still gets a copy. The prompt for unsaved edits when leaving a file stays
regardless, because it protects edits rather than confirming a save. `core/edit/AudioEditWriter`
writes a temporary file beside the target, validates WAV, copies POSIX permissions and macOS
extended attributes, then renames over the original, so a failed write leaves the file
byte-identical. Copies are named `<stem> edit<ext>` and numbered, and never overwrite. Sampler chunk
fields (`smpl` root and loop, cue points) travel with the audio.

The waveform context menu and its keys come from one table in `ui/preview/EditCommands`: label,
stable command id, shortcut column text and the working key. The menu shows the key in JUCE's
right-hand shortcut column, and `PreviewPanel::keyPressed` looks keys up in the same table, so a hint
cannot outlive its key. Unmodified keys (N, R, C, S, I, O, G, M, D, T, Z, Backspace) edit audio only
while the preview or the wave owns the keyboard focus; the library list and the search field never
trigger them. Modified keys (Cmd+Z, Shift+Cmd+Z, Cmd+0, Cmd+S) keep working from anywhere. Clicking
the wave focuses the preview. When Settings -> Keyboard Shortcuts is implemented, menus will read
their hints from this table.

Normalizing a library selection is the first slice of the batch processing design. `normalizeFile`
in `core/edit/NormalizeService` reads the source twice in fixed 64k-frame chunks: the first pass
measures the peak, the second writes the same audio with one gain for all channels toward
-0.1 dBFS. The result goes to a temporary file beside the original, is reopened and verified
(length, rate, channels, WAV format), file attributes are copied, and only then is the original
renamed over; a source that changed during the job is left alone. WAV, AIFF and FLAC are replaced
in place; other formats (MP3/OGG and decoded sources) are written as `... edit.wav` copies. A silent
source or a file already on target reports `unchanged` and is not rewritten. Containers, disc
entries, files with more than two channels and a file with unsaved editor changes are excluded from
the batch (the confirmation says how many selected items are skipped). The batch runs on one
background `NormalizeService` thread with progress in the status line and the common Cancel button;
it starts only while no indexer, smart analysis, tagging, split or extract job is running, so Cancel
is never ambiguous. After it finishes, `CatalogFileSync` reanalyzes replaced rows (tags, star and
manual values survive) or adds the copies with the source's tags and star; its queue is drained by
one worker, so a long reanalysis never blocks the message thread. The context menu item and
Shift+Cmd+N use the same command table entry, and the setting above removes the Overwrite originals
/ Save copies question.

After a save, `core/indexing/CatalogFileSync` updates the catalog off the message thread. An
overwrite reanalyzes the same row, which keeps its id, tags and star, and recomputes the audio-derived
columns of an existing ML feedback row (hash, v1, v2) so the contribution pass re-sends the label.
A copy becomes a new row that inherits the template's tags and star but not its feedback. Switching
samples, opening a disc image, clearing the selection or quitting while edits are unsaved asks
Save / Discard / Cancel; Cancel puts the library selection back on the edited file.

## Manual BPM and drag metadata

Tempo can be corrected by hand. The sample context menu has a `BPM` submenu: `Set BPM...` (20-300,
batch over the selection), `Tap BPM...` and `Clear manual BPM`. A value is stored as a `bpm` entry in
`smart_overrides`; `setSmartOverride` re-resolves the row, so `smart_results` and the list show the
manual value with "(manual)" and a later analysis keeps it. Contribution reads the resolved row, so
the manual tempo reaches the model for samples that already have feedback (a type or tags); an
unlabelled sample still sends nothing, and clearing the override restores the analysed tempo.

`core/smart/TapTempo` is a pure counter: the last eight intervals, outliers beyond 25 % of the
median dropped, a value only after two valid intervals (three taps), reset after three seconds
without a tap. `ui/library/TapTempoDialog` owns the window: Space taps (it never starts playback),
Return applies, Esc cancels, and a `Start loop` button asks the preview to loop the waveform
selection or the whole source. For a batch the loop button is unavailable because the preview holds
no audio.

Dragging a row whose BPM or key is trusted (`Resolved` or a manual override) materialises a copy in
`ExternalFileDrag::dragCacheFolder()/<hash>/<stem>.wav`. A WAV source is byte-copied and only the
RIFF `acid` and `smpl` chunks are spliced in (`core/export/RiffChunks`), so unknown chunks, bit
depth and an existing loop survive; other readable formats are decoded to WAV first. A container the
splicer cannot rewrite safely (RF64, unusual chunk layouts) drags unchanged without metadata rather
than disappearing from the drag. The cache prunes hash folders older than seven days. BPM writes
`acid` (tempo, beats, 4/4 meter) and a root writes the `smpl` unity note, each independently: a
BPM-only drag invents no root, a key-only drag writes no tempo. Conflicting, ambiguous and stale
values are not written. Only formats the shared decoder reads are re-encoded; REX2, EBL, soundfonts,
tracker modules and disc images keep their previous drag behaviour.

## Smart metadata (Phase 2, metadata-first)

`core/smart` parses metadata read-only and never decodes PCM for it. Extractors cover WAV chunks (full container, including chunks after `data`), AIFF Apple Loops (`basc`, `cate`, `INST`), Ableton `.asd` (v6 warp markers and `AufTaktData`), REX2 (`GLOB`, `RECY`, `SINF`, `SLCE`) and file/folder names. Every extractor is bounds-checked and returns candidates plus a structural error; a truncated chunk stops parsing and keeps what was already read. The service adds provenance to each candidate: the source file, its size/mtime fingerprint, the extractor id and version that produced it, and a limitation note where the value is not the musical truth (for example a REX2 preview tempo or a sampler root). Fingerprints are taken before and after reading; if they differ, nothing is published and the previous generation is marked stale.

Measured library evidence (see `docs/superpowers/specs/2026-11-07-phase2-metadata-spike.md`) drives the semantics: `smpl`/`inst`/ACID/`basc` root notes are sampler roots, never musical keys; `IKEY` is usually keywords; ASD `UnbiasedTempoEstimate` was `0` in every `IsSet` sample; Apple Loops `basc` BPM and REX2 `GLOB` tempo matched file names in 100% of comparable cases.

`SmartResolver` is a pure function: it picks by source priority, keeps disagreements as `conflicting`, and exposes an explanation string. `SmartAnalysisService` runs the extractors on a background thread with pause/cancel, writes each sample atomically, and stores input fingerprints for sidecar invalidation. It is started after an index run and can refresh a single sample synchronously.

## SoundFont 2 preview

`core/sf2/Sf2Bank` parses RIFF/sfbk, INFO name, sdta 16-bit PCM and pdta headers/bags/generators. Instrument zones become preset zones after intersecting preset key/velocity ranges; empty stub banks are rejected as having no samples. `Sf2KitSampler` copies the preset zones and PCM, preallocates 32 voices, and renders note-on/off from MIDI messages with key/velocity matching, root-key transposition, tuning, loop and exclusive-class choke. Loading and `prepare` run on the message thread; `render` allocates nothing and never reads files.

## Kurzweil banks (`.krz`, `.k25`, `.k26`)

`core/kurzweil/KrzFile` parses the big-endian object bank: the 32-byte `PRAM` header and its PCM
offset, blocks with negative lengths, sample objects (sound block plus 32-byte sound file headers),
keymaps (method and entry size drive the entry layout) and programs (tagged segments; the layer's
`CAL` segment names its keymaps). Every offset is bounded by the block and the file, split sets are
joined from their `.krN` parts (PCM of later parts starts at `0x24`), and sample data is
big-endian 16-bit. `core/kurzweil/KrzInstrument` turns a bank into the shared external-instrument
model: one source per RAM sample header, one program per K2000 program, zones from the keymaps'
keys and velocity levels. Playback therefore reuses `ExternalKitSampler`. Extract lists the sample
objects through `EmbeddedSampleSet` and writes root key and loop into the WAV `smpl` chunk.
`docs/KURZWEIL_KRZ.md` records the layout and every corpus-derived decision.

## SFZ instruments (`.sfz`)

`core/sfz/SfzInstrument` reads the text sections `<control>`, `<master>/<global>`, `<group>` and
`<region>`, inherits opcodes from master to group to region and turns each region into an
`ExternalZone` of the shared external-instrument model. It resolves external samples through
`default_path` and the instrument folder's ancestors (Windows separators and unquoted paths with
spaces are normalised), decodes them with the registered Sonicator formats, and maps key/velocity
ranges, root key, tuning, volume, pan, offset/end and loop points. Missing samples are named in the
warnings and the rest of the instrument still plays; an instrument with no playable region fails
with that list instead of opening as silence. Playback and preview therefore reuse
`ExternalKitSampler` and the existing MIDI/waveform UI. `.sfz` is indexed as a metadata-only row;
nothing is embedded, so it is not an Extract source. `docs/SFZ.md` records the mapping and the
corpus findings.

## Creative Voice files (`.voc`)

`core/voc/VocAudioFormat` is a JUCE `AudioFormat` registered by `registerSonicatorFormats` next to
the basic formats and EBL, so VOC files use the ordinary audio paths: scanning, waveform, transport,
MIDI preview, analysis, export and drag. The decoder reads the 19-byte signature and offset, then
bounds-checks every block: 8-bit PCM and Creative 4/3/2-bit ADPCM sound data, continuation blocks,
silence, marker/text, bounded repeats, block 8 stereo attributes and block 9 new-format 8/16-bit PCM
or ADPCM. A file that concatenates clips at different rates is resampled linearly to one stream;
A-law/u-law, Multi-DAC, endless repeats and malformed/truncated files are refused with a reason.
`decodeVoc` is a pure function used by the reader and the tests; `docs/VOC.md` records the layout,
the ADPCM rule and the corpus findings.

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
label and an `origin` (`user`, `auto` or `legacy`); a row written by AutoTag carries `auto`, and a
pre-schema-14 row is `legacy`. `core/ml/AdaptiveClassifier` ranks the nearest stored prototypes and
publishes them as a `user_adaptive` source, which AutoTag can turn into suggestions. See
`docs/TAGGING.md`.

## Training and shipped prototypes

`ml_feedback_samples.origin` keeps the app's own guesses out of the dataset: only `user` rows and
`approved` rows (an AutoTag proposal the person reviewed and applied) leave the machine, and only
through `contributionRecords`. A machine write never replaces a person's row; a reviewed tag only
refreshes that row's tag list. A tag action exports the tags only; the type is exported when the
person set it themselves (`smart_overrides`), so the app's resolved type is not presented as a
confirmation. Exported tags are read from `sample_tags`, so a renamed tag travels under its new name
and a deleted one stops being learned; `tag_history_schema` keeps assignments from older builds on
the machine instead of uploading them retroactively.
`services/contribute-worker/scripts/export.mjs` turns the bucket into `contribute.jsonl` plus
`report.md`, refusing an unknown feature schema or a value outside 0..1. `services/training/` builds
`resources/ml/global_prototypes.json` with up to eight medoids per label, a label needing three
installs and ten records, and at most twenty records per install per label; custom types are left
out of the shipped labels and stay a manual promotion. The output is deterministic and committed,
so a build never downloads a model. `evaluate.mjs` scores the change leave-one-install-out against
local-only behaviour.

The application loads the file from `BinaryData` after `loadFromRepository` and keeps local
prototypes ahead of global ones: a global score is multiplied by 0.95 before the acceptance
threshold, and a local label wins within 0.02. A global type arrives as the `global_adaptive`
source, ranked immediately below `user_adaptive`, and only when no other type candidate exists, so
it is a fallback rather than a second opinion that turns a confident type into "ambiguous". Global
tag prototypes are loaded too and reach AutoTag through `hasTagPrototypes()`: the review dialog can
then propose a standard tag learned from other libraries, still only as a proposal the person ticks.
The empty shipped file (no labels) leaves today's behaviour unchanged; the About section shows its
date and label count.

The prototype file does not change the analysis version. Files analysed before a build with a
non-empty file keep their stored result until they are analysed again; the release that first ships
labels either accepts that or bumps `smartDspVersion` deliberately, at the cost of decoding every
file once.

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

## Feature schema 2 and the global model

Schema 16 adds `ml_feedback_samples.feature_schema` (1 or 2) and `features_v2` (a BLOB of 84
floats). Existing rows keep schema 1 and an empty v2 blob, so the app starts without recomputing
anything. `core/ml/FeatureV2Backfill` walks the confirmed rows in a background thread at startup,
decodes each file once (the same 10-second prefix the training CLI uses), writes the v2 vector and
leaves a missing or unreadable file on schema 1. A new confirmation computes both vectors from the
same decode and stores them together; `AdaptiveClassifier` keeps both, compares only vectors of the
same schema, and a schema-2 query can use the model without disturbing the schema-1 prototypes.

`resources/ml/global_model.json` is the schema-2 softmax layer (`model.v1`, kind `M2`): feature
selection, `mu`/`sd`, the label matrix, the temperature and the similarity table that turns a
probability into the same "precision at this score" scale as a local cosine. It is loaded next to
`global_prototypes.json`; its type candidates carry the `global_model` source, which sits at the
same resolver rank as `global_adaptive` and is still only a fallback when no other type candidate
exists. The About panel shows the model date, label count and the dataset attribution line.
Contribution payloads now carry `featureSchema` per record and the intake, export and schema-1
training reader accept both lengths; the shipped prototype file stays schema 1.
