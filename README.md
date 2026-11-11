# SONICATOR — Sample Browser

A local sample library browser and explorer for macOS and Windows: indexing, search, tags, smart
metadata, waveform preview and drag-and-drop into a DAW, without a cloud account or a network
dependency. This is a real working application, not an ML/map mockup.

## Build

Requirements: CMake >=3.22, Git, C++20 compiler, macOS Command Line Tools/Xcode or Visual Studio 2022 with Desktop C++ workload and Windows SDK.
The first configure downloads pinned JUCE 9.0.5 and Catch2 3.7.1; subsequent application use is offline.
SQLite 3.47.2 is bundled. See [dependency/licensing notes](docs/DEPENDENCIES.md) and the
[third-party notices](THIRD_PARTY_NOTICES.md) before distribution.

### macOS

Build outside `~/Documents`: when that folder is synchronised by iCloud, the file provider turns the
build tree into placeholder files and compilers stall on reads. `CMakePresets.json` keeps the build in
`~/Library/Caches/sonicator-build/<preset>` for exactly that reason.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
open ~/Library/Caches/sonicator-build/debug/Sonicator_artefacts/Debug/SONICATOR.app
```

Use `release` for an optimised build with tests, and `universal` for a Release build of both
architectures. A catalog with 300k rows takes several gigabytes in Debug; `release` is enough for daily
use.

Ninja is the preset generator; Ninja 1.13 or newer is recommended. For a one-off configure without
presets, any generator works, but choose a binary directory outside iCloud:
`cmake -S . -B <dir> -G Ninja -DCMAKE_BUILD_TYPE=Debug`.

### Windows

Windows is built in CI, and its current state is recorded in `docs/WINDOWS_PORT.md`. A local build needs
Visual Studio 2022 with the Desktop C++ workload, zlib (for example through vcpkg) and the ONNX Runtime
DLL next to the executable.

macOS builds receive a local ad-hoc bundle signature. This is not Developer ID signing, a Windows signature, an installer or a notarized release.

## Use

1. **+ Add folder** imports one directory recursively. Repeat for multiple roots. Right-click any folder in the sidebar and choose **Remove from library...** to drop that folder's catalog rows, tags and analysis without touching files on disk. A registered root is unregistered, and any removed folder stays out of later scans (including a parent root's rescan) until you add it again with **+ Add folder**.
2. Indexing runs in the background; **Pause / Resume / Cancel** control it. Completed file results remain saved on cancellation.
3. Select a row to immediately preview its original file. Use **Play / Pause / Stop / Loop**, volume, and drag on the waveform to select a time range. A plain click on the waveform seeks and clears the selection.
4. Keyboard: **Space** play/pause, **Left / Right** seek one second, **Escape** stop and clear the waveform selection (when not consumed by an editing control).
5. Search covers the entire catalog by filename and path. Typing is debounced by 250 ms; Enter applies immediately and Escape clears it (or dismisses an open history menu). Click the search field to choose from the last 10 applied searches, persisted in `settings.json`. Masks (`*.rx2`, `kick_*`) match the complete file name; `ext:wav`, `ext:rx2,sf2` and the compact `*.rex/rx2/sf2` filter by format; `*.rx2|*.sf2` alternation is also accepted. `type:pad` and `key:Am` use smart metadata; `note:C4` matches a resolved sampler root note, not a default 60. Bare words keep the all-tokens text behaviour, and a known type or note (`pad`, `C4`) also checks smart metadata. Header clicks sort the whole result, not the current page. Choose **100 / 1 000 / 10 000 / All** from the menu next to the **< / >** arrows. **All** is virtualized: only matching IDs are prepared, and records are fetched in small blocks as you scroll, so a 300k catalog stays responsive.
6. **Tags** and **Starred** filter the catalog. Select a sample and **Edit tags...** to assign tags; create, rename, recolour and delete tags from the tag menu.
7. Select several rows with **Cmd-click** (toggle) or **Shift-click** (range). Right-click any row for **Copy** (files on macOS, paths on other platforms), **Tag** (coloured submenu, applies to the whole selection), **Star/Unstar** and **Open in Finder** (**Explorer** on Windows). Drag one or more selected rows out of the library, or drag from inside a waveform selection: Finder/desktop copies, messengers attach, and DAWs import. Library originals are never moved or deleted; a selection drag sends a prepared WAV of exactly that range.
8. **Hide empty** hides files quieter than -60 dBFS (peak below 0.001), which covers digital silence and noise-floor files. Its checkbox is saved between launches and preserved when switching catalogs; **Clear filters** turns it off and saves that choice. Metadata-only RX2/SF2 entries remain visible. The `DUPLICATES` column shows `×N` for byte-identical copies (same full SHA-256 and size); right-click **Show all copies** reveals a whole group. Nothing is deleted.
9. **Rescan** asks for confirmation, then analyzes new, changed and previously failed files; unchanged analyzed files are skipped. **New files** is the additive scan: it walks the folders but analyzes only paths that are not in the catalog yet, without reconciling missing records or rebuilding image contents. **Reanalyze all** asks for a stronger confirmation and recomputes every file. Cancel keeps completed results and never stores a partial row. The library row context menu has **Analyze selected** for a chosen set of files; it queues them without stopping a running job. **Smart metadata** reads Type/BPM/Key candidates for missing, changed or old-version files in the background. Only when no usable metadata BPM exists does it decode the audio for a local rhythm pass (no network, no model download), which adds a probable Structure (`loop` / `one-shot`) and, for a rhythmic loop only, a BPM; ambiguous tempi are reported as such instead of being forced to a number. It can run without a rescan, and Pause/Cancel apply to it while it is the running job. The library shows Type/BPM/Key columns; Structure and the selected file's states appear in the bottom selection bar. Header clicks sort the whole catalog. A bottom bar shows the selected file path and its metadata.
10. **Backup** writes a consistent SQLite copy to `Sonicator/backups/` beside the database. The live database and its WAL are never copied as raw files.
11. **Settings** opens the theme (system/dark/light), interface scale, catalog location, Audio/MIDI Settings and the About/support links. Theme, scale, language, catalog path and the last window position/size persist between launches. Opening a catalog whose folders are missing on this computer offers **Relocate folders...** (also available in Settings): each old root is mapped to a new folder, relative structure is preserved, analysis/tags/stars/hashes are kept, separators convert between macOS and Windows, and a `<catalog>.relocate-<timestamp>.sqlite` backup is written first.
12. **Audio/MIDI Settings** inside Settings selects the output device. No microphone input is opened.

## Data

Database and bounded waveform cache live in JUCE's user application-data location under `Sonicator/`:

- macOS: `~/Library/Sonicator/sonicator.sqlite` (JUCE `userApplicationDataDirectory`).
- Windows: `%APPDATA%\Sonicator\sonicator.sqlite`.
- Settings: `Sonicator/settings.json` in the same application-data location. It stores theme, scale, language, an optional catalog path, last window geometry, Hide empty and the last 10 searches. Click the search field to open history; Escape dismisses the menu. There is no separate History button.

No original audio is changed, renamed or deleted. Duplicate copies remain distinct file-path records, identified by full SHA-256.
Do not delete a live SQLite database or its `-wal`/`-shm` sidecars. Close the application before making filesystem backups.

REX2 (`.rx2`) and SoundFont 2 (`.sf2`) containers are indexed as metadata-only catalog rows: they are searchable by name, format and mask, and carry file identity, duration/format metadata where readable, but no decoded Phase 1 descriptors. Selecting an SF2 loads its first drum preset (or the first preset when the bank has no drum kit) into the MIDI kit sampler and enables MIDI Preview; the preset menu switches between every preset in the bank, including melodic instruments. Selecting an RX2 decodes the DWOP audio, draws the waveform with slice markers and maps visible slices to consecutive MIDI notes from 36; the preview shows the mapped range and the last received MIDI note (marked outside when it does not match a slice). **Gate** stops a slice on NoteOff, off means one-shot. Notes outside the mapped range wrap into the slice set, so any key auditions a slice; **Play** auditions the whole decoded loop sequentially and **Loop** repeats it. Ordinary Play is not used for either container. Legacy `.rex`/`.rcy` files are not indexed.

Waveform selection exports are new WAV files under `Sonicator/exports/` in the same application-data location:

- macOS: `~/Library/Sonicator/exports`.
- Windows: `%APPDATA%\Sonicator\exports`.

The source sample rate and channel count are preserved; 16-bit sources stay 16-bit PCM, 24/32-bit integer sources become 24-bit PCM, float sources become 32-bit float, and compressed sources become 24-bit PCM. Exported files are not deleted automatically because a DAW project or a message may reference them, so the folder grows until you remove files manually. Source originals are opened read-only and are never moved by a drag.

## Reason Packs

On macOS, `~/Library/Application Support/Propellerhead Software/Packs` is registered once when present. The global `reasonPacksAdded` setting prevents it returning after removal. WAV/AIFF/RX2 files use normal indexing and preview; `_audio previews` and `Audio Previews` directories inside packs are excluded, regardless of audio format.

`.repatch` XML is indexed as a metadata card and shows its device product only. Combinator (`.cmb`) and ReFill (`.rfl`) files are not indexed: they contain no embedded audio and cannot be decoded outside Reason. Existing CMB/RFL catalog rows are dropped when a catalog is opened; files on disk are unchanged. ReFill archives are never decoded. The bounded Combinator reader remains available for files opened directly, but nothing is synthesized. Combinators are not an **Extract samples** source because they contain references, not embedded audio.

## Ableton Packs

On macOS, `~/Music/Ableton/Factory Packs` is registered once when present. The global `abletonPacksAdded` setting prevents it returning after **Remove**. Playable WAV/AIFF and the rendered OGG previews under `Ableton Folder Info/Previews` are indexed normally and appear in **All samples** and the general search. A preview row is labelled **Ableton Preview** in the preview panel with the rack, clip or device name it demonstrates.

Ableton factory AIFC samples compressed with the closed `able` codec (marked "Ableton Content") are skipped: only Live can decode them, so they are not catalogued and produce no error rows. Catalogs created by an earlier build have those error rows purged once on open; the files themselves stay untouched. Omni-present `.asd` sidecars, Live presets (`.adg`, `.adv`, `.alc`, `.als`) and Max devices (`.amxd`) are not indexed in this stage; the previews are the only audio representation of those presets.

## Disc images

ISO/IMG/BIN/CUE images with an ISO-9660 filesystem or a raw Akai volume are indexed while a scan walks their folder. Only entries this build can open are catalogued: WAV/AIFF/FLAC/MP3/OGG files inside ISO, and Akai sample records. Programs, unsupported records and text files stay in the disc browser but do not clutter the library. A `.bin` that has a sibling `.cue` is represented by the cue, so its contents are indexed once. MDX remains unsupported.

Each playable entry becomes a metadata-only catalog row whose synthetic path is `<image>::<internal path>`; name and internal folder are searched like any other sample. Selecting such a row decodes it through the existing bounded disc reader and loads the ordinary preview transport, waveform and loop. **Enter** or a double-click opens the image browser on that entry's folder. Nothing is extracted to disk, no PCM is decoded during indexing, and duration/rate stay empty until a row is selected. Tags, stars and search work normally. Removing a folder deletes its disc rows and index state; **Ignore** on the image hides its contents. Images are re-walked when their identity changes: size and modification time, plus the data file for a `.cue`. Unchanged images are skipped. A missing image marks its contents missing and keeps their tags and stars; an explicit **Remove** deletes them. Drag from the waveform exports a temporary WAV; disc rows themselves do not become OS file paths.

## Modules

```text
app/Main.cpp                 application lifecycle and window
plugin/                      VST3/AU plugin hosting MainComponent (off by default)
core/database/               SQLite connection, schema migrations, repository, path relocation, search syntax
core/scanner/                recursive discovery and cancellable full-file SHA-256
core/indexing/               background import, pause/cancel, incremental updates
core/dsp/                    chunked decode, measured descriptors, rhythm, key, pitch, acoustic features
core/audio/                  transport, read-ahead, MIDI voices, stereo-pair grouping
core/smart/                  metadata extractors, resolver, background smart-analysis service
core/ml/                     mel spectrogram, ONNX classifier wrapper, adaptive prototypes
core/tagging/                tag storage, AutoTag rules and batches
core/split/                  silence detection and WAV splitting
core/extract/                embedded sample extraction from containers
core/export/                 selection WAV export, background preparation, external file drag
core/sf2/                    SoundFont 2 parsing and kit sampler
core/rex/                    REX2 DWOP decoding and slice player
core/sampler/                EXS24 and Kontakt (NKI) instrument readers
core/tracker/                MOD/XM/S3M document parsing, source PCM and whole-song rendering
core/legacy/                 content-based tracker recognition
core/disc/                   read-only image probing, filesystems, decoding and content indexing
core/reason/                 bounded patch metadata and one-time Packs registration
core/ableton/                Ableton preview naming, protected-sample probe and one-time Packs registration
core/settings/               persisted application settings
ui/shell/                    theme, window geometry and themed dialogs
ui/library/                  global search, tags, folder tree, paged/virtualized library, row drag
ui/waveform/                 asynchronous thumbnail, bounded disk cache and range selection
ui/preview/                  transport, keyboard controls and song progress
ui/settings/                 Settings panel, catalog switching, relocation and About links
ui/tagging/                  AutoTag review dialog
ui/split/                    split dialog
ui/extract/                  extract-samples dialog
ui/disc/                     disc browser panel
ui/platform/                 native file clipboard (macOS) with a text-path fallback
ui/MainComponent.*           orchestration, inspector, device settings
resources/sqlite/            official SQLite amalgamation
resources/branding/          application icon and wordmark (originals included)
cmake/                       macOS bundle signing
tests/                       core, audio-render, UI and integration tests
docs/                        architecture, testing evidence and stage plans
```

Architectural details and numerical conventions: [ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Tests

```sh
cmake --build --preset debug
ctest --preset debug
~/Library/Caches/sonicator-build/debug/tests/SonicatorTests '[.benchmark]'
~/Library/Caches/sonicator-build/debug/tests/SonicatorTests '[.large-file]'
```

`ctest --preset release` runs the same suite against the optimised build; both configurations are
checked, because a Release-only compile error has happened before.

The optional benchmark creates/imports/reopens/rescans 10,000 actual short synthetic WAV files in a temporary directory. Its timings do not predict a real mixed-format library's analysis time.
All test audio is synthetic. Fixtures for WAV/AIFF/FLAC/MP3 are checked in; FFmpeg is not needed to build/run tests.

Opt-in real-library tests are separate from the synthetic suite:

```sh
SONICATOR_EXTERNAL_ROOT=/Volumes/1TBSSD/samples ~/Library/Caches/sonicator-build/debug/tests/SonicatorTests '[.external-scan]'
SONICATOR_EXTERNAL_MANIFEST=/absolute/path/to/selection.json \
SONICATOR_EXTERNAL_OUTPUT=/absolute/path/to/separate/output \
~/Library/Caches/sonicator-build/debug/tests/SonicatorTests '[.external]'
```

The selection manifest is a JSON array of objects with absolute `path` strings. The external test reads those originals, checks unchanged size/mtime/full SHA-256, limits each analysis to 10 seconds, and writes a separate SQLite database and JSON report to the output directory. Never choose an output directory inside the sample library. Local validation artifacts are kept outside the build tree, in `~/Library/SonicatorDev/manual-validation/`, and are not committed.
The large-file test creates a sparse 1.2 GB WAV, decodes eight blocks and cancels; it verifies bounded/cancellable access, **not** full-length analysis of a real 1.2 GB recording.

## Limits and open work

- Preview toggles (**Auto Preview**, **MIDI Preview**, **Skip Silence**, **Loop**, **Gate**) persist in `settings.json` together with **Hide empty**.
- Smart metadata (Type/BPM/Key candidates from WAV/AIFF/ASD/REX2 chunks and names) lives in `core/smart`, with versioned extractors, a resolver, atomic publication, structured failures, sidecar fingerprint verification, a background **Smart metadata** job, Type/BPM/Key library columns with whole-catalog sorting, and a selection bar with the full path, metadata and resolved Pitch. Values keep their provenance and are usable by `type:`, `key:` and `note:` search. One bounded PCM decode feeds a rhythm fallback (structure/BPM) and a tonal pass: key from a chromagram with tuning, percussion suppression and 24 profiles, and one-shot pitch from YIN with MIDI and cents. Drums, non-harmonic hits, chords and relative-key disputes stay unknown. Type can be set by hand from the row context menu (**Type ▶**), which also stores the edit as a learned prototype. There is no single inspector panel and no global metadata filter bar yet. See `docs/SMART_ANALYSIS.md` and `docs/key-pitch-accuracy.md`.
- Measured DSP: duration, sample rate, applicable bit depth, channels, linear peak/RMS, power spectral centroid, 85% power rolloff and power spectral flatness, plus offline rhythm structure, key, one-shot pitch and the acoustic descriptor vector. Not LUFS, spectral density/bands or a perceptual similarity model.
- FFT size 2048, Hann window, non-overlapping frames. These are engineering descriptors, not calibrated semantic labels or a perceptual similarity model.
- Analysis supports up to 64 channels; preview uses the first two channels, not a surround downmix. Compressed bit depth can be unavailable.
- Tracker support covers MOD (15- and 31-sample, M.K./M!K!/FLT/multichannel signatures), XM and S3M for whole-song playback and per-sample PCM export. Playback uses libxmp; PCM extraction is a separate bounded reader, so a format can play without every variant being extractable. AdLib/FM S3M instruments, packed S3M samples, stereo XM samples and unsupported variants keep an explicit status. There is no song overview waveform yet, no tracker editing, no stems and no MT2/IT/MDX; see `docs/PREVIEW.md`.
- SF2 preview is a bounded sampler: key and velocity ranges, root key, tuning, loop, attenuation, pan and exclusive-class choke. It is not a full SoundFont synthesis engine (no modulators, filters or NRPN). REX2 support is `.rx2` only: DWOP is decoded in memory, at most 128 slices are mapped, and there is no tempo sync or time-stretch. No filesystem watcher; folder changes are discovered by manual rescan. Symbolic directory links are deliberately not followed. AppleDouble metadata forks are excluded by their magic bytes, not merely a filename prefix. Path aliases/case aliases are not merged.
- Waveform cache holds at most 64 thumbnails. Search, sorting and tag filters run over the whole catalog; a page still contains at most 10,000 rows. Text search folds ASCII, Latin, Greek and Cyrillic case; scripts without case (CJK) match literally. No 500,000-file or GPU/60 FPS claim is made.
- Files with decode errors are retried on rescan. Missing roots abort that scan with a visible message; records are not deleted. The additive **New files** scan deliberately does not mark missing records.
- The manual **Backup** copy runs through SQLite's backup API on the UI thread, so a very large catalog can pause the window briefly while it copies. No automatic rotation or deletion of user backups is performed.
- Interface scale is limited to 100%, 110%, 125% and 150%. Only English is available; the language setting is stored for future translations. **Move catalog** leaves the previous database file in place instead of deleting it.
- `Copy` writes real file references only on macOS; other platforms fall back to newline-separated paths until a native file clipboard is added. Multi-selection covers the current 10,000-row page. **Open in Finder/Explorer** reveals the clicked row's file, not every selected file.
- Duplicate grouping compares full file bytes only (SHA-256 + size); different containers or metadata with identical decoded audio are not grouped. The `DUPLICATES` column shows the group size and **Show all copies** reveals it; no file is deleted or moved. **Hide empty** uses the measured peak only: files at or above -60 dBFS stay visible, quieter files are hidden.
- With **All**, changing the filter or sort runs one ID query over the whole catalog (measured ~35 ms for 300k rows by name and ~130 ms with a text filter in Debug); rows themselves are loaded in 400-row blocks (~2 ms) and cached around the viewport. The list is never materialized as 300k full records.
- A saved catalog that is missing or is not a SONICATOR catalog falls back to the default location at startup; the fallback is written to the log.
- Disc contents are metadata-only rows: indexing never decodes PCM, so length/rate/peak stay empty until a sample is selected. Only playable audio entries are catalogued; a changed image is rebuilt, and paths that survive keep tags and stars.
- OGG Vorbis is indexed as ordinary audio. Ableton factory AIFC samples with the closed `able` codec are skipped by design; they can only be decoded inside Live. Ableton preset/device files are not indexed yet.
- **Remove from library...** deletes catalog rows only; it never deletes, moves or renames source files.
- A catalog moved between computers needs **Relocate folders...** once per machine. It rewrites stored paths and does not copy audio. Folder structure inside each root must be preserved; a 200-path existence sample warns about a wrong choice, and the automatic `.relocate-<timestamp>.sqlite` backup is the recovery point.
- WAV/AIFF physical audio-chunk boundaries are checked before analysis/preview; inaccurate outer RIFF/FORM sizes and appended ancillary metadata are tolerated when the actual audio payload is complete. Compressed-format integrity still depends on the decoder; this is not a forensic file-repair validator. RF64 extended tables for non-data chunks are not supported.
- Read-ahead keeps decoding away from the real device callback. JUCE's transport/player use internal locks; this is a practical browser preview, not a formally lock-free realtime engine.
- Windows, Intel macOS, physical listening/latency and release packaging still need platform/user verification. External file drag (library rows and selection WAVs) is verified on macOS only; Windows uses the same JUCE file-URL path and must be checked separately.
- The VST3/AU target builds and loads in Cubase; the other hosts in its acceptance checklist, the shared catalog with the application, and Match BPM / Match Scale are not done. See `docs/plans/2026-11-11-vst3-au-plugin-spike.md`.

## License

SONICATOR is free and open source under the **GNU Affero General Public License v3.0 or later**
(AGPL-3.0-or-later); see [LICENSE](LICENSE). Copyright (C) 2026 Ilya Orange.

The licence follows the JUCE dependency: this project uses JUCE under its open-source option, which
requires a combined work to be released under AGPL. The components compiled into the binary and their
licences are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Roadmap and open feature work: [docs/BACKLOG.md](docs/BACKLOG.md).
