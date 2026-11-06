# Phase 1 architecture

## Boundaries

`SonicatorCore` owns filesystem discovery, SQLite records, streaming DSP and preview transport. No core source includes a project UI header. `SonicatorUI` owns widgets and action wiring; the standalone entry point owns the window. JUCE's audio-utils dependency currently brings framework GUI modules transitively into core; this does not couple core services to SONICATOR widgets.

## Threads and lifecycle

- Message thread: controls, device setup, file selection/reader replacement, paged catalog queries, rendering.
- Indexing thread: directory enumeration, chunked analysis, full-file hashing, database writes, reconciliation of missing paths.
- Dedicated preview read-ahead thread: JUCE buffering source, independent of the analyzer.
- Dedicated thumbnail cache thread: waveform extraction, independent of preview.
- Device callback: JUCE AudioSourcePlayer → AudioTransportSource → buffering/resampling source. No project DB, FFT analysis, file scanning or ML runs here. JUCE transport internally locks; do not claim strict lock-free safety.

UI and worker use separate SQLite connections in WAL mode. Each connection's repository operations are serialized by its own mutex. Progress is a small locked snapshot polled at 10 Hz, not a callback posted for every discovered file. No callbacks capture a raw window pointer. Folder chooser uses a JUCE SafePointer.

Shutdown stops the UI timer, cancels/joins indexing without force-killing a thread, then destroys repositories/connections. Audio callbacks detach before source/read-ahead teardown. Thumbnail listeners detach before thumbnail/cache teardown.

## Storage

Schema `user_version=1`. Stable numeric ID, unique path, full SHA-256, file size/time, format metadata, all implemented DSP values, analyzer version, error and missing flag. Root folders persist separately. Schema versions newer than the application are rejected, not overwritten.

Upsert by path preserves sample ID. Two files with the same content keep independent paths. Skip only when size/time and successful analyzer version match. Files changed during analysis are marked for retry, not recorded as successful. Startup reads the database; it does not initiate reanalysis.

Analyzer version 2 adds WAV/AIFF physical audio-payload validation, invalidating earlier cached successes on explicit rescan. It deliberately tolerates incorrect outer container sizes seen in real commercial sample packs. AppleDouble forks are excluded by a filename-prefix fast path plus a magic-byte check. Per-file hashing I/O failures become error records and do not abort the rest of an import.

Future machine predictions and user overrides must be stored independently of these measured file records. Model upgrades must not overwrite user annotations.

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
