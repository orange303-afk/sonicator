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

This is an unsigned development application, not an installer/notarized release.

## Use

1. **+ Add folder** imports one directory recursively. Repeat for multiple roots.
2. Indexing runs in the background; **Pause / Resume / Cancel** control it. Completed file results remain saved on cancellation.
3. Select a row to immediately preview its original file. Use **Play / Pause / Stop / Loop**, volume and click/drag in the waveform to seek.
4. Keyboard: **Space** play/pause, **Left / Right** seek one second, **Escape** stop (when not consumed by an editing control).
5. Header clicks sort the current page; search filters filenames on the current page. Pages contain at most 10,000 records; use **< / >** to navigate larger catalogs.
6. **Rescan library** skips unchanged successfully analyzed files. **Reanalyze all** explicitly recomputes them. Missing files remain marked in the catalog; corrupt files show an error rather than invented features.
7. **Audio settings** selects the output device. No microphone input is opened.

## Data

Database and bounded waveform cache live in JUCE's user application-data location under `Sonicator/`:

- macOS: `~/Library/Sonicator/sonicator.sqlite` (JUCE `userApplicationDataDirectory`).
- Windows: `%APPDATA%\Sonicator\sonicator.sqlite`.

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
ui/shell/                    shared dark theme
ui/library/                  sortable paged library
ui/waveform/                 asynchronous thumbnail and bounded disk cache
ui/preview/                  transport and keyboard controls
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
```

The optional benchmark creates/imports/reopens/rescans 10,000 actual short synthetic WAV files in a temporary directory. Its timings do not predict a real mixed-format library's analysis time.
All test audio is synthetic. Fixtures for WAV/AIFF/FLAC/MP3 are checked in; FFmpeg is not needed to build/run tests.

## Limits / next stage

- No instrument/structure classification, BPM, pitch/key, confidence, embeddings, semantic search, map, collections or DAW drag-and-drop in Phase 1. These are not shown as fake functioning controls.
- DSP currently measures duration, sample rate, applicable bit depth, channels, linear peak/RMS, power spectral centroid, 85% power rolloff and power spectral flatness. Not LUFS, spectral density/bands, attack/decay or transient/rhythm analysis yet.
- FFT size 2048, Hann window, non-overlapping frames. These are engineering descriptors, not calibrated semantic labels or a perceptual similarity model.
- Analysis supports up to 64 channels; preview uses the first two channels, not a surround downmix. Compressed bit depth can be unavailable.
- No filesystem watcher; folder changes are discovered by manual rescan. Symbolic directory links are deliberately not followed. Path aliases/case aliases are not merged.
- Waveform cache holds at most 64 thumbnails. UI sorting/filtering is page-local. No 500,000-file or GPU/60 FPS claim is made.
- Files with decode errors are retried on rescan. Missing roots abort that scan with a visible message; records are not deleted.
- Read-ahead keeps decoding away from the real device callback. JUCE's transport/player use internal locks; this is a practical browser preview, not a formally lock-free realtime engine.
- Windows, Intel macOS, DAW integrations, physical listening/latency and release packaging still need platform/user verification.

Phase 2: independently versioned classification/BPM/pitch/key modules, ONNX/model licence review, manually labelled evaluation data, confidence calibration and separate user overrides. Do not begin until the Phase 1 baseline is accepted.
