# Phase 2 handoff

## Current state

- Metadata-first foundation: name/path, WAV/AIFF, ASD and REX2 candidates,
  resolver, schema v6, overrides storage and background service.
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

Continue the existing smart-analysis plan in
`docs/superpowers/plans/2026-11-07-phase2-smart-analysis.md`, starting with
Type/BPM/Key columns, inspector/provenance/conflict explanations, global filters
and sorting, and user override editing. Then implement and evaluate DSP BPM and
pitch/key fallback on appropriate material. Do not equate sampler root with key.

Before extending the pipeline, verify atomic publication of candidates/results/
inputs, extractor version tracking, automatic sidecar invalidation and cancellation
coverage: the current foundation is not a declaration that those requirements are
fully complete. Do not rehash/recompute basic features to add smart results.

## Verification and constraints

- Use synthetic fixtures for normal tests; commercial library tests remain opt-in.
- Real-library tests should use a bounded manifest or a narrow folder, with a
  timeout and unfiltered diagnostics. The sampling helper still has to traverse
  the root when a requested extension is absent; do not mistake that for a build.
- macOS Debug is the verified platform. Windows, physical audio acceptance and
  full SoundFont/REX format conformance are separate checks.
- Release readiness/packaging remains deferred. Phase 2 is not complete yet.
- No original library files are modified. No training on commercial samples.
