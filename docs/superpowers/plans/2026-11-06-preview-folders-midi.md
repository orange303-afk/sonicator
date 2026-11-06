# Preview, folders and MIDI implementation plan

Approved scope: fix preview restarts; tree of added folders with recursive catalog filtering and arrow navigation; 16-voice MIDI preview (note 60 native pitch, attack 2 ms, release 20 ms); optional Skip Silence for ordinary and MIDI preview. Never modify source audio or reanalyze the catalog.

## Steps
- [x] Reproduce refresh/reload restarts, suppress restoration callbacks, preserve playback position; run regression tests.
- [x] Add recursive folder queries and catalog-derived tree. Query paths without a schema migration using binary path-prefix ranges, preserving IDs and existing analysis. Test nested folders and prefix collisions.
- [x] Add bounded-memory MIDI sampler, off-audio-thread sample loading, 16 preallocated voices, interpolation, envelopes and bounded MIDI queue; connect available MIDI inputs and Audio settings selection. Test rendered polyphony, pitch, release and silence offset.
- [x] Add MIDI Preview and Skip Silence controls, test integration and ordinary non-looping EOF, build Debug/Release, review, update graph and launch Release.

## Constraints and implementation choices
Tree comes from added roots plus catalog paths, not an uncontrolled filesystem scan. Folder filtering spans the entire catalog with 10k-row pagination. Empty added roots stay visible. No database migration is needed for path-range filtering on the existing unique path index.
MIDI samples exceeding 128 MiB decoded stereo float storage are refused explicitly, never truncated; streaming ordinary preview remains available. Native pitch is note 60; hardware octave labels vary. MIDI and ordinary preview are mutually exclusive. MIDI uses a fixed 2048-event input queue with an atomic-flag nonblocking drain; unsupported SysEx is rejected before enqueueing, overflow requests an all-voice reset to avoid stuck notes, and input timing is quantised to the next audio block. Destination MIDI storage is preallocated. Voices allocate nothing or access disk during render. Loading/changing mode stops transport while the callback can acknowledge its fade, then detaches output before replacing sample data. Skip Silence detects the first strictly nonzero finite frame in any channel and retains one preceding frame; toggling affects the next trigger, not active voices.

## Review and evidence
Both restart regressions were observed failing before fixes (extra onSelected callback and position reset to zero). Folder-query tests first failed to compile for the missing API; MIDI tests first failed for the missing sampler. New rendered tests check pitch ratio, 16-voice bound, release completion, silence handling, ordinary EOF and MIDI queue overflow. Independent review identified stop-after-detach UI timeout and potential collector SysEx allocation; fixed stop ordering and replaced the collector. Review's overlapping-root concern was checked by a regression test: the shared hierarchy already supports nested roots; no schema change or duplicated model required. Startup on a scratch copy of 299,918 actual catalog rows: Debug 3.16 s, Release 1.61 s. Original catalog was opened read-only only for an SQLite backup fixture, never rescanned.
