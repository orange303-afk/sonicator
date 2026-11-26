# Preview spectrogram — design

**Status:** approved in conversation on 2026-11-26; implemented in `stage-preview-spectrum`.

## Intent

The preview card shows peaks. For sound design that is only half the picture: a hit, a pad and a
noise sweep can look identical in amplitude while their spectra differ completely. The card gains a
**Spectrum** tab beside **Waveform**. Switching tabs changes only what is drawn inside the card —
the visible time range, selection, playhead, overview strip and every mouse gesture stay shared, so
nothing has to be relearned and no editing behaviour forks.

## Goals

- One click (or `V`, or the card's context menu) between the waveform and a spectrogram of the same
  visible range.
- The spectrogram reads musically: log frequency by default, 20 Hz to Nyquist, guides at 100 Hz,
  1 kHz and 10 kHz, with a Linear switch.
- Every existing gesture keeps working in spectrum mode: select, seek, wheel zoom, magnify, pan,
  loop drag, external drag, context menu, zoom to fit/selection.
- Every source the card can show gets a spectrum: ordinary files, edited PCM, REX2, SF2/EXS/NKI
  samples, tracker samples, disc samples and the plug-in's matched preview.
- Analysis never blocks the message thread and never touches the audio callback.
- Honest states: "Computing spectrum…", "updating…", an explicit reason when a source cannot be
  analysed, and an "overview" note when time detail had to be aggregated.
- The mode and the scale persist in `settings.json`, in the standalone and in the plug-in editor.

## Non-goals

- Editing in the frequency domain (spectral repair, brush, freeze).
- Two channel lanes; channels are combined in the power domain, matching the catalog analyzer.
- A spectrum of a whole tracker song: the card shows the progress bar there, as before.
- Peak picking, harmonic cursors, a second analyser window, exporting images.
- New colour pickers: the heat ramp is part of the theme.

## Constraints

- No schema change, no catalog access, no export writes.
- `SpectrogramService` runs one persistent worker per card, latest-wins. It never shares the
  preview's `zoomReader`: it opens its own reader, or reads an immutable `SampleAudio` snapshot.
- The mouse mapping uses the full card width (`waveview::xAtTime(..., getWidth(), view)`), so the
  frequency guides are an overlay, not a left gutter: no geometry change between tabs.
- All sizes are logical pixels; the interface scale is applied by JUCE afterwards.
- UI text is English.

## Approaches considered

**A separate `SpectrogramView` sibling swapped with `WaveformDisplay`.** Rejected: it duplicates
selection, zoom, playhead, loop drag, external drag and the overview strip (roughly 200 lines of
gesture code), and the two tabs would drift apart with every future change.

**A render mode inside `WaveformDisplay` with a separate renderer and service.** Chosen. The card
keeps one interaction implementation, and `paint()` gains a content branch. `WaveformDisplay` owns
the service and the image cache; `SpectrogramRenderer` owns only pixels and labels; `core/dsp`
owns the maths.

**Analysis on the message thread in `paint()`.** Rejected: a 4 s stereo file at 48 kHz is roughly
2 000 FFTs, and a long file seen whole is far more; the preview would visibly stutter.

## Design

### Layers

1. `core/dsp/Spectrogram` — pure STFT: Hann window, power-domain channel sum, one-sided magnitudes,
   row mapping (log or linear), column aggregation, dB against the loudest bin of the request.
   No JUCE GUI types, no file access; the reader is a `std::function`.
2. `core/preview/SpectrogramService` — one worker thread per card, latest-wins, cancel, and a state
   that carries an immutable grid keyed by the request. A stale or cancelled job publishes nothing.
3. `ui/waveform/SpectrogramRenderer` — ARGB image at grid resolution, heat ramp from `Theme`, and
   the frequency guides/overview note overlay.
4. `ui/waveform/WaveformDisplay` — `RenderMode { peaks, spectrum }`, debounced requests on view,
   size and scale changes, and the "computing/updating/unavailable" captions.

### Budget

- Columns: the card's pixel width, clamped to 64..4096; rows: its height, clamped to 32..512.
- A short range is analysed densely (hop at most a quarter of the FFT size); a long range grows the
  hop and stops at 8192 frames, which sets `overview = true` and draws the note.
- The grid is at most 512 × 4096 floats (8 MB); the ARGB image is built once per grid and reused
  across repaints. The previous image stays on screen while a new one is computed.
- Requests are debounced by 150 ms, between the selection export (300 ms) and Match (250 ms).
- A file that cannot be read, an empty source or an empty range produces a reason, never silence.

### Theme

`theme::spectroLow/Mid/High` and `theme::spectrogramColour(level)` form a magma-like ramp that is
redefined for the light theme (quiet = surface, loud = ink), so the card stays readable in both
variants. Selection, playhead and the range label keep their accent-on-panel treatment and stay
readable over bright cells.

### UI

The tab pair closes the card's title row: `Waveform | Spectrum` plus a `Log`/`Linear` pill that
appears only in spectrum mode. A row of their own above the card was rejected after the first
render: at the default 156 px preview it took a third of the card's height. The tabs disappear with
the card (whole tracker song, unsupported format), and the same three choices are in the card's
context menu, so the keyboard path is one `V` press.

## Testing

- `TestSpectrogram`: sine peak position on the log row, normalisation to 0 dB, silence on the floor,
  anti-phase stereo power, log/linear position round-trip, overview under a frame budget, short
  source with more columns than frames.
- `TestSpectrogramService`: file → ready grid, latest-wins, cancel without a late publish, missing
  file → explicit failure, decoded snapshot without any file.
- `TestPreviewSpectrogram`: switching modes keeps the selection and produces an image; a card with
  no source reports no spectrum; the preview shows the tabs and remembers mode and scale.
