# SONICATOR — Core Browser

Phase 1 of the Intelligent Sample Explorer: a real local audio browser, not an ML/map mockup.

## Build

Requirements: CMake >=3.22, Git, C++20 compiler, macOS Command Line Tools/Xcode or Visual Studio 2022 with Desktop C++ workload and Windows SDK.
The first configure downloads pinned JUCE 9.0.5 and Catch2 3.7.1; subsequent application use is offline.
SQLite 3.47.2 is bundled. See [dependency/licensing notes](docs/DEPENDENCIES.md) before distribution.

### macOS

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 6
ctest --test-dir build --output-on-failure
open build/Sonicator_artefacts/Debug/SONICATOR.app
```

Ninja is optional: omit `-G Ninja` to use the default generator.
For Release, use a separate `build-release` directory and `-DCMAKE_BUILD_TYPE=Release`.
For Intel + Apple Silicon, configure a separate directory with `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`.
The local verification run is native Apple Silicon; universal binaries and Windows must be verified separately.

### Windows (Developer PowerShell)

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --parallel 6
ctest --test-dir build -C Debug --output-on-failure
& .\build\Sonicator_artefacts\Debug\SONICATOR.exe
```

macOS builds receive a local ad-hoc bundle signature. This is not Developer ID signing, a Windows signature, an installer or a notarized release.

## Use

1. **+ Add folder** imports one directory recursively. Repeat for multiple roots.
2. Indexing runs in the background; **Pause / Resume / Cancel** control it. Completed file results remain saved on cancellation.
3. Select a row to immediately preview its original file. Use **Play / Pause / Stop / Loop**, volume, and drag on the waveform to select a time range. A plain click on the waveform seeks and clears the selection.
4. Keyboard: **Space** play/pause, **Left / Right** seek one second, **Escape** stop and clear the waveform selection (when not consumed by an editing control).
5. Search covers the entire catalog by filename and path. Typing is debounced by 250 ms; Enter applies immediately and Escape clears it. The **History** button lists the last 10 applied searches, persisted in `settings.json`. Masks (`*.rx2`, `kick_*`) match the complete file name; `ext:wav`, `ext:rx2,sf2` and the compact `*.rex/rx2/sf2` filter by format; `*.rx2|*.sf2` alternation is also accepted. `type:pad` and `key:Am` use smart metadata; `note:C4` matches a resolved sampler root note, not a default 60. Bare words keep the all-tokens text behaviour, and a known type or note (`pad`, `C4`) also checks smart metadata. Header clicks sort the whole result, not the current page. Choose **100 / 1 000 / 10 000 / All** from the menu next to the **< / >** arrows. **All** is virtualized: only matching IDs are prepared, and records are fetched in small blocks as you scroll, so a 300k catalog stays responsive.
6. **Tags** and **Starred** filter the catalog. Select a sample and **Edit tags...** to assign tags; create, rename, recolour and delete tags from the tag menu.
7. Select several rows with **Cmd-click** (toggle) or **Shift-click** (range). Right-click any row for **Copy** (files on macOS, paths on other platforms), **Tag** (coloured submenu, applies to the whole selection), **Star/Unstar** and **Open in Finder** (**Explorer** on Windows). Drag one or more selected rows out of the library, or drag from inside a waveform selection: Finder/desktop copies, messengers attach, and DAWs import. Library originals are never moved or deleted; a selection drag sends a prepared WAV of exactly that range.
8. **Hide empty** hides files quieter than -60 dBFS (peak below 0.001), which covers digital silence and noise-floor files. The `DUPLICATES` column shows `×N` for byte-identical copies (same full SHA-256 and size); right-click **Show all copies** reveals a whole group. Nothing is deleted.
9. **Rescan** asks for confirmation, then analyzes new, changed and previously failed files; unchanged analyzed files are skipped. **New files** is the additive scan: it walks the folders but analyzes only paths that are not in the catalog yet, without reconciling missing records. **Reanalyze all** asks for a stronger confirmation and recomputes every file. Cancel keeps completed results and never stores a partial row.
10. **Backup** writes a consistent SQLite copy to `Sonicator/backups/` beside the database. The live database and its WAL are never copied as raw files.
11. **Settings** opens the theme (system/dark/light), interface scale, catalog location, Audio/MIDI Settings and the About/support links. Theme, scale, language, catalog path and the last window position/size persist between launches. Opening a catalog whose folders are missing on this computer offers **Relocate folders...** (also available in Settings): each old root is mapped to a new folder, relative structure is preserved, analysis/tags/stars/hashes are kept, separators convert between macOS and Windows, and a `<catalog>.relocate-<timestamp>.sqlite` backup is written first.
12. **Audio/MIDI Settings** inside Settings selects the output device. No microphone input is opened.

## Data

Database and bounded waveform cache live in JUCE's user application-data location under `Sonicator/`:

- macOS: `~/Library/Sonicator/sonicator.sqlite` (JUCE `userApplicationDataDirectory`).
- Windows: `%APPDATA%\Sonicator\sonicator.sqlite`.
- Settings: `Sonicator/settings.json` in the same application-data location. It stores theme, scale, language, an optional catalog path and the last window geometry.

No original audio is changed, renamed or deleted. Duplicate copies remain distinct file-path records, identified by full SHA-256.
Do not delete a live SQLite database or its `-wal`/`-shm` sidecars. Close the application before making filesystem backups.

REX2 (`.rx2`) and SoundFont 2 (`.sf2`) containers are indexed as metadata-only catalog rows: they are searchable by name, format and mask, and carry file identity, duration/format metadata where readable, but no decoded Phase 1 descriptors. Selecting an SF2 loads its first drum preset (or the first preset when the bank has no drum kit) into the MIDI kit sampler and enables MIDI Preview; the preset menu switches between every preset in the bank, including melodic instruments. Selecting an RX2 decodes the DWOP audio, draws the waveform with slice markers and maps visible slices to consecutive MIDI notes from 36; the preview shows the mapped range and the last received MIDI note (marked outside when it does not match a slice). **Gate** stops a slice on NoteOff, off means one-shot. Notes outside the mapped range wrap into the slice set, so any key auditions a slice; **Play** auditions the whole decoded loop sequentially and **Loop** repeats it. Ordinary Play is not used for either container. Legacy `.rex`/`.rcy` files are not indexed.

Waveform selection exports are new WAV files under `Sonicator/exports/` in the same application-data location:

- macOS: `~/Library/Sonicator/exports`.
- Windows: `%APPDATA%\Sonicator\exports`.

The source sample rate and channel count are preserved; 16-bit sources stay 16-bit PCM, 24/32-bit integer sources become 24-bit PCM, float sources become 32-bit float, and compressed sources become 24-bit PCM. Exported files are not deleted automatically because a DAW project or a message may reference them, so the folder grows until you remove files manually. Source originals are opened read-only and are never moved by a drag.

## Modules

```text
app/Main.cpp                 application lifecycle and window
core/database/               RAII SQLite connection, prepared queries, schema version
core/scanner/                recursive discovery and cancellable full-file SHA-256
core/dsp/                    chunked audio decoding and measured descriptors
core/audio/                  preview transport, sample-rate conversion, read-ahead, selection boundaries
core/export/                 selection WAV export, background preparation, external file drag
core/indexing/               background import, pause/cancel, incremental updates
core/settings/               persisted application settings
ui/shell/                    shared dark theme
ui/library/                  global search, tags, paged/virtualized library and original-file row drag
ui/waveform/                 asynchronous thumbnail, bounded disk cache and range selection
ui/preview/                  transport and keyboard controls
ui/settings/                 Settings dialog, catalogue switching and About links
ui/platform/                 native file clipboard (macOS) with a text-path fallback
ui/MainComponent.*           orchestration, inspector, device settings
resources/sqlite/            official SQLite amalgamation
tests/                       core, audio-render, UI and integration tests
```

Architectural details and numerical conventions: [ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Tests

```sh
cmake --build build --parallel 6
ctest --test-dir build --output-on-failure
build/tests/SonicatorTests '[.benchmark]'
build/tests/SonicatorTests '[.large-file]'
```

The optional benchmark creates/imports/reopens/rescans 10,000 actual short synthetic WAV files in a temporary directory. Its timings do not predict a real mixed-format library's analysis time.
All test audio is synthetic. Fixtures for WAV/AIFF/FLAC/MP3 are checked in; FFmpeg is not needed to build/run tests.

Opt-in real-library tests are separate from the synthetic suite:

```sh
SONICATOR_EXTERNAL_ROOT=/Volumes/1TBSSD/samples build/tests/SonicatorTests '[.external-scan]'
SONICATOR_EXTERNAL_MANIFEST=/absolute/path/to/selection.json \
SONICATOR_EXTERNAL_OUTPUT=/absolute/path/to/separate/output \
build/tests/SonicatorTests '[.external]'
```

The selection manifest is a JSON array of objects with absolute `path` strings. The external test reads those originals, checks unchanged size/mtime/full SHA-256, limits each analysis to 10 seconds, and writes a separate SQLite database and JSON report to the output directory. Never choose an output directory inside the sample library. Local validation artifacts are ignored under `build/manual-validation/`.
The large-file test creates a sparse 1.2 GB WAV, decodes eight blocks and cancels; it verifies bounded/cancellable access, **not** full-length analysis of a real 1.2 GB recording.

## Limits / next stage

- Phase 2 metadata extraction (Type/BPM/Key candidates from WAV/AIFF/ASD/REX2 chunks and names) is implemented in `core/smart` and runs after an index run. DSP fallback, the Type/BPM/Key columns and manual overrides UI are not wired yet, so Phase 1 still shows no smart fields.
- DSP currently measures duration, sample rate, applicable bit depth, channels, linear peak/RMS, power spectral centroid, 85% power rolloff and power spectral flatness. Not LUFS, spectral density/bands, attack/decay or transient/rhythm analysis yet.
- FFT size 2048, Hann window, non-overlapping frames. These are engineering descriptors, not calibrated semantic labels or a perceptual similarity model.
- Analysis supports up to 64 channels; preview uses the first two channels, not a surround downmix. Compressed bit depth can be unavailable.
- SF2 preview is a bounded sampler: key and velocity ranges, root key, tuning, loop, attenuation, pan and exclusive-class choke. It is not a full SoundFont synthesis engine (no modulators, filters or NRPN). REX2 support is `.rx2` only: DWOP is decoded in memory, at most 128 slices are mapped, and there is no tempo sync or time-stretch. No filesystem watcher; folder changes are discovered by manual rescan. Symbolic directory links are deliberately not followed. AppleDouble metadata forks are excluded by their magic bytes, not merely a filename prefix. Path aliases/case aliases are not merged.
- Waveform cache holds at most 64 thumbnails. Search, sorting and tag filters run over the whole catalog; a page still contains at most 10,000 rows. Text search folds ASCII, Latin, Greek and Cyrillic case; scripts without case (CJK) match literally. No 500,000-file or GPU/60 FPS claim is made.
- Files with decode errors are retried on rescan. Missing roots abort that scan with a visible message; records are not deleted. The additive **New files** scan deliberately does not mark missing records.
- The manual **Backup** copy runs through SQLite's backup API on the UI thread, so a very large catalog can pause the window briefly while it copies. No automatic rotation or deletion of user backups is performed.
- Interface scale is limited to 100%, 110%, 125% and 150%. Only English is available; the language setting is stored for future translations. **Move catalog** leaves the previous database file in place instead of deleting it.
- `Copy` writes real file references only on macOS; other platforms fall back to newline-separated paths until a native file clipboard is added. Multi-selection covers the current 10,000-row page. **Open in Finder/Explorer** reveals the clicked row's file, not every selected file.
- Duplicate grouping compares full file bytes only (SHA-256 + size); different containers or metadata with identical decoded audio are not grouped. The `DUPLICATES` column shows the group size and **Show all copies** reveals it; no file is deleted or moved. **Hide empty** uses the measured peak only: files at or above -60 dBFS stay visible, quieter files are hidden.
- With **All**, changing the filter or sort runs one ID query over the whole catalog (measured ~35 ms for 300k rows by name and ~130 ms with a text filter in Debug); rows themselves are loaded in 400-row blocks (~2 ms) and cached around the viewport. The list is never materialized as 300k full records.
- A saved catalog that is missing or is not a SONICATOR catalog falls back to the default location at startup; the fallback is written to the log.
- A catalog moved between computers needs **Relocate folders...** once per machine. It rewrites stored paths and does not copy audio. Folder structure inside each root must be preserved; a 200-path existence sample warns about a wrong choice, and the automatic `.relocate-<timestamp>.sqlite` backup is the recovery point.
- WAV/AIFF physical audio-chunk boundaries are checked before analysis/preview; inaccurate outer RIFF/FORM sizes and appended ancillary metadata are tolerated when the actual audio payload is complete. Compressed-format integrity still depends on the decoder; this is not a forensic file-repair validator. RF64 extended tables for non-data chunks are not supported.
- Read-ahead keeps decoding away from the real device callback. JUCE's transport/player use internal locks; this is a practical browser preview, not a formally lock-free realtime engine.
- Windows, Intel macOS, physical listening/latency and release packaging still need platform/user verification. External file drag (library rows and selection WAVs) is verified on macOS only; Windows uses the same JUCE file-URL path and must be checked separately.

Phase 2: independently versioned classification/BPM/pitch/key modules, ONNX/model licence review, manually labelled evaluation data, confidence calibration and separate user overrides. Do not begin until the Phase 1 baseline is accepted.
