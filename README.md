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
3. Select a row to immediately preview its original file. Use **Play / Pause / Stop / Loop**, volume and click/drag in the waveform to seek.
4. Keyboard: **Space** play/pause, **Left / Right** seek one second, **Escape** stop (when not consumed by an editing control).
5. Search covers the entire catalog by filename and path. Typing is debounced by 250 ms; Enter applies immediately and Escape clears it. Header clicks sort the whole result, not the current page. Pages contain at most 10,000 records; use **< / >** to navigate.
6. **Tags** and **Starred** filter the catalog. Select a sample and **Edit tags...** to assign tags; create, rename, recolour and delete tags from the tag menu.
7. Select several rows with **Cmd-click** (toggle) or **Shift-click** (range). Right-click any row for **Copy** (files on macOS, paths on other platforms), **Tag** (coloured submenu, applies to the whole selection), **Star/Unstar** and **Open in Finder** (**Explorer** on Windows).
8. **Rescan** asks for confirmation, then analyzes new, changed and previously failed files; unchanged analyzed files are skipped. **New files** is the additive scan: it walks the folders but analyzes only paths that are not in the catalog yet, without reconciling missing records. **Reanalyze all** asks for a stronger confirmation and recomputes every file. Cancel keeps completed results and never stores a partial row.
9. **Backup** writes a consistent SQLite copy to `Sonicator/backups/` beside the database. The live database and its WAL are never copied as raw files.
10. **Settings** opens the theme (system/dark/light), interface scale, catalog location, audio output and the About/support links. Theme, scale, language, catalog path and the last window position/size persist between launches.
11. **Audio output** inside Settings selects the output device. No microphone input is opened.

## Data

Database and bounded waveform cache live in JUCE's user application-data location under `Sonicator/`:

- macOS: `~/Library/Sonicator/sonicator.sqlite` (JUCE `userApplicationDataDirectory`).
- Windows: `%APPDATA%\Sonicator\sonicator.sqlite`.
- Settings: `Sonicator/settings.json` in the same application-data location. It stores theme, scale, language, an optional catalog path and the last window geometry.

No original audio is changed, renamed or deleted. Duplicate copies remain distinct file-path records, identified by full SHA-256.
Do not delete a live SQLite database or its `-wal`/`-shm` sidecars. Close the application before making filesystem backups.

## Modules

```text
app/Main.cpp                 application lifecycle and window
core/database/               RAII SQLite connection, prepared queries, schema version
core/scanner/                recursive discovery and cancellable full-file SHA-256
core/dsp/                    chunked audio decoding and measured descriptors
core/audio/                  preview transport, sample-rate conversion, read-ahead
core/indexing/               background import, pause/cancel, incremental updates
core/settings/               persisted application settings
ui/shell/                    shared dark theme
ui/library/                  global search, tags and paged library
ui/waveform/                 asynchronous thumbnail and bounded disk cache
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

- No instrument/structure classification, BPM, pitch/key, confidence, embeddings, semantic search, map, collections or DAW drag-and-drop in Phase 1. These are not shown as fake functioning controls.
- DSP currently measures duration, sample rate, applicable bit depth, channels, linear peak/RMS, power spectral centroid, 85% power rolloff and power spectral flatness. Not LUFS, spectral density/bands, attack/decay or transient/rhythm analysis yet.
- FFT size 2048, Hann window, non-overlapping frames. These are engineering descriptors, not calibrated semantic labels or a perceptual similarity model.
- Analysis supports up to 64 channels; preview uses the first two channels, not a surround downmix. Compressed bit depth can be unavailable.
- No filesystem watcher; folder changes are discovered by manual rescan. Symbolic directory links are deliberately not followed. AppleDouble metadata forks are excluded by their magic bytes, not merely a filename prefix. Path aliases/case aliases are not merged.
- Waveform cache holds at most 64 thumbnails. Search, sorting and tag filters run over the whole catalog; a page still contains at most 10,000 rows. Text search folds ASCII, Latin, Greek and Cyrillic case; scripts without case (CJK) match literally. No 500,000-file or GPU/60 FPS claim is made.
- Files with decode errors are retried on rescan. Missing roots abort that scan with a visible message; records are not deleted. The additive **New files** scan deliberately does not mark missing records.
- The manual **Backup** copy runs through SQLite's backup API on the UI thread, so a very large catalog can pause the window briefly while it copies. No automatic rotation or deletion of user backups is performed.
- Interface scale is limited to 100%, 110%, 125% and 150%. Only English is available; the language setting is stored for future translations. **Move catalog** leaves the previous database file in place instead of deleting it.
- `Copy` writes real file references only on macOS; other platforms fall back to newline-separated paths until a native file clipboard is added. Multi-selection covers the current 10,000-row page. **Open in Finder/Explorer** reveals the clicked row's file, not every selected file.
- A saved catalog that is missing or is not a SONICATOR catalog falls back to the default location at startup; the fallback is written to the log.
- WAV/AIFF physical audio-chunk boundaries are checked before analysis/preview; inaccurate outer RIFF/FORM sizes and appended ancillary metadata are tolerated when the actual audio payload is complete. Compressed-format integrity still depends on the decoder; this is not a forensic file-repair validator. RF64 extended tables for non-data chunks are not supported.
- Read-ahead keeps decoding away from the real device callback. JUCE's transport/player use internal locks; this is a practical browser preview, not a formally lock-free realtime engine.
- Windows, Intel macOS, DAW integrations, physical listening/latency and release packaging still need platform/user verification.

Phase 2: independently versioned classification/BPM/pitch/key modules, ONNX/model licence review, manually labelled evaluation data, confidence calibration and separate user overrides. Do not begin until the Phase 1 baseline is accepted.
