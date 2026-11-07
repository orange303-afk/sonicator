# Phase 2 handoff

## Current state

- Metadata-first foundation: name/path, WAV/AIFF, ASD and REX2 candidates,
  resolver, schema v7, overrides storage and background service.
- Foundation hardening (2026-11-07): one atomic publication of candidates,
  extractions, fingerprints and result; independent extractor/resolver/DSP
  versions; revision and catalog guards; structured failures; sidecar
  fingerprint verification with an in-job reprocess pass; override edits that
  never get overwritten by an in-flight job; explicit **Smart metadata** button
  with pause/cancel and progress. See `docs/SMART_ANALYSIS.md`.
- **Analyze selected** in the row context menu queues a chosen set into the smart
  job, including while a pass is already running.
- **Auto Preview / MIDI Preview / Skip Silence / Loop / Gate** persist with
  **Hide empty** and are restored on launch.
- REX2 waveform selections render from the decoded PCM and drag out as normal
  24-bit WAV files; ordinary selections keep the existing export path.
- Library Type/BPM/Key columns with textual state markers and whole-catalog SQL
  sorting; a bottom selection bar shows the full path, effective smart values and
  measured format/rate/duration. Header columns stretch to fit a narrow pane.
- RX2/SF2 assets can be indexed and found by filename masks or extension filters.
- SF2 MIDI preview supports selecting drum and melodic presets; this is a limited
  renderer, not a complete SoundFont synthesizer.
- REX2 has DWOP decode, MIDI slices, Gate/one-shot, waveform and active-slice
  highlighting, plus whole-loop Play/Pause/Stop/Loop.
- REX MIDI positions use fractional frames: verified at 44.1/48/96 kHz after
  fixing integer truncation. Marker-only first slices are promoted to ranges
  instead of being dropped from MIDI mapping.
- Hide empty persists in settings; switching catalogs preserves it and Clear
  filters saves off. Search history opens on clicking the search field, without
  a separate button. History stays limited to 10 entries.

## Next development slice

The foundation and columns are complete for this slice; the next plan continues with
the inspector (provenance, alternatives, conflicts), global metadata filters and the
user override editing UI. Then implement and evaluate DSP BPM
and pitch/key fallback on appropriate material. Do not equate sampler root with key.
Do not rehash/recompute basic features to add smart results.

## Verification and constraints

- Use synthetic fixtures for normal tests; commercial library tests remain opt-in.
- Real-library tests should use a bounded manifest or a narrow folder, with a
  timeout and unfiltered diagnostics. The sampling helper still has to traverse
  the root when a requested extension is absent; do not mistake that for a build.
- macOS Debug is the verified platform. Windows, physical audio acceptance and
  full SoundFont/REX format conformance are separate checks.
- Release readiness/packaging remains deferred. Phase 2 is not complete yet.
- No original library files are modified. No training on commercial samples.
