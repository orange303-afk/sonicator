# Spectral cache, batch analysis and 2D selection — design

**Status:** approved in conversation on 2026-11-26; implemented in `stage-spectral-selection`.

## Intent

The spectrum view from the previous stage is correct but starts cold: every selection recomputes
the visible range, and the card can only select a time range. This stage makes it open instantly,
lets a folder or a multi-selection be prepared ahead of time, and adds Photoshop-like selection
inside the spectrum — rectangle, lasso and a magic wand — whose result can be dragged into a DAW
and auditioned.

## Goals

- A selected file shows its spectrum immediately after the first view and on every later launch.
- `Spectral analysis (N)…` in the sample and folder menus precomputes the same cache for a large
  selection, with progress and cancel; a fresh entry is skipped.
- The spectrum gains `Time / Rect / Lasso / Wand` tools, a translucent mask overlay, add and
  subtract modifiers, and a visible tolerance for the wand.
- A 2D selection exports and drags as audio: only the selected time and frequency survive
  (masked inverse STFT); everything else inside the time window is silence.
- Play/Stop audition the masked result through the engine's matched transport without replacing
  the card or the waveform.
- Light and dark themes keep the overlay and the labels readable.

## Non-goals

- Spectral repair, brush, healing, undo of mask edits (Clear and reselect only).
- Saving masks in project state or in the catalog.
- Cache entries for containers (REX2, SF2, EXS/NKI, tracker, disc): they have no source file; their
  spectra are computed per view and never disk-cached.
- A linear-frequency overview cache: the cache stores the log grid, so Linear mode analyses the
  visible range directly.

## Cache

- One pair per source in `~/Library/Sonicator/spectra/<2 hex>/`:
  - `<hash>-v1.sps`: header (`SPS1`, version, identity, rows, columns, fftSize, start/end, min/max
    Hz, floor, log/overview) plus one byte per cell over the floor..0 dB range (~0.35 dB step);
  - `<hash>-v1.png`: 2048 × 256 heat map with a fixed palette (a theme change must not invalidate
    the cache; the card itself always draws from the float grid).
- Identity is path + size + mtime + format version, stored in the header as well as the file name.
  A missing, truncated or stale file is a miss and is recomputed; nothing else is trusted.
- The overview is 8192 columns × 256 log rows, computed by the same `core/dsp/Spectrogram` with a
  frame cap, so a long file becomes an honest `overview` rather than a truncation.
- Pruning keeps the newest 2000 entries and 512 MB; the folder is never removed. Settings → Library
  has `Cache spectrograms`, a size/count line and `Clear cache`.
- The card reads `.sps` in `setFile` on the message thread (2 MB, milliseconds) and draws at once;
  a detail pass for a zoom is requested after the usual 150 ms debounce and stays in memory.

## Batch analysis

`core/spectral/SpectralAnalysisService` runs two workers over a list of files, computes the same
overview and writes both files. Progress is atomics plus the current file name; cancel stops after
the current file; the summary counts done, skipped and failed. The menus disable the action while
it runs or while Batch/Split owns the catalog, and the plugin hides it with the other processing
jobs.

## 2D selection

- `core/spectral/SpectralMask` keeps a canonical 256-row log raster over 20 Hz..Nyquist and a time
  window of about 2000 columns per second (capped 64..4096). Every tool rasterises into it, so
  zoom and pan never move the selection.
- Rectangle: drag defines time × frequency. Lasso: a freehand polygon closed on mouse-up, scanline
  filled. Magic wand: flood fill over the displayed dB grid with a dB tolerance, 4-connected,
  capped at 300 000 cells, then copied into the mask as blocks so coarse grids leave no holes.
- Default replaces the mask; Shift adds, Alt subtracts. The mask is drawn as a translucent accent
  fill built once into an image and cropped per view. Clearing it (Clear, Escape, the menu item)
  also stops the audition.
- The time selection stays independent: Play/Loop/MIDI still use it; the mask owns the export.

## Masked export and audition

- `core/spectral/SpectralRender` decodes the range (file reader or decoded `SampleAudio`), applies
  a Hann STFT (2048, hop 512), scales every bin by the mask with a one-row feather, overlap-adds
  with the same window and divides by the accumulated window energy. Frames outside the mask's
  time window are skipped entirely, so the mask's time edges keep their level.
- The output is exactly as long as the time window, with silence outside the mask, so a DAW places
  it where the selection was. Files longer than 60 s are refused with a reason; the work runs on
  the selection exporter's worker and honours its cancel flag.
- `SelectionExporter` gained a `tag`: the panel stores the mask's identity (window, columns, cell
  count) and refuses a prepared WAV whose tag differs, so a drag can never carry an old mask.
- Audition uses `AudioEngine::setMatchedTransport` (the prepared "Match" transport): the card keeps
  showing the original source, the playhead is drawn as `maskStart + engine.position()`, Loop is
  applied to the matched reader, and Stop clears the transport and returns the ordinary source.
  Match and the spectral audition never coexist.

## Testing

- Cache round-trip, changed file, truncated file, prune ordering and PNG creation.
- Mask rectangle/lasso/raster coverage, subtract, window resampling.
- ISTFT: full mask reconstructs the signal (RMS error < 2 %), a 300–700 Hz mask removes a 3 kHz
  tone while keeping 440 Hz, empty mask refused, exported WAV length matches the range.
- Batch: two files cached, a rerun skips both, an unreadable file counts as failed.
- UI: a cached file opens with an image before any timer runs; a mask prepares the masked export
  and auditions it; Stop leaves the audition.
