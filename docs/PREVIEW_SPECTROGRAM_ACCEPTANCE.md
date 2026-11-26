# Preview spectrogram — acceptance

Design: `docs/superpowers/specs/2026-11-26-preview-spectrogram-design.md`.
Verified against a Debug build with `SonicatorTests` (Catch2).

## Automated

| Check | Test |
|---|---|
| A sine lands on the row that names its frequency | `TestSpectrogram.cpp` |
| Loudest bin normalises to 0 dB; silence stays on the floor and finite | `TestSpectrogram.cpp` |
| Anti-phase stereo keeps its power (channels sum in the power domain) | `TestSpectrogram.cpp` |
| Log/linear frequency positions and out-of-range rejection | `TestSpectrogram.cpp` |
| Long range becomes a budgeted `overview`; short source still fills columns | `TestSpectrogram.cpp` |
| File request reaches `ready` with a valid grid and the right key | `TestSpectrogramService.cpp` |
| Latest request wins; a cancelled request publishes nothing | `TestSpectrogramService.cpp` |
| Missing file reports an explicit failure, not a silent grid | `TestSpectrogramService.cpp` |
| A decoded `SampleAudio` snapshot needs no file | `TestSpectrogramService.cpp` |
| Switching modes keeps the selection and produces an image | `TestPreviewSpectrogram.cpp` |
| A card without a source has no spectrum | `TestPreviewSpectrogram.cpp` |
| The preview shows the tabs and remembers mode/scale | `TestPreviewSpectrogram.cpp` |

Run:

```bash
build/tests/Debug/SonicatorTests.exe "[spectrogram]"
```

## Result (Debug build, 2026-11-26)

- `"[spectrogram]"` — the v1 cases, still green inside the merged `"[spectral],[spectrogram]"` run.
- Regression: `"[ui]"` 227 cases / 2 047 assertions, `"[waveform]"` 19 / 63,
  `"[commands],[theme],[edit]"` 106 / 847 — all passed.
- One pre-existing environmental failure outside this work: `TestDiscAcceptance` needs
  `SONICATOR_DISC_IMAGE` and skips without it.

## Manual

- [ ] Ordinary WAV: switch to Spectrum with the title-row tabs on a whole file and on a 0.2 s zoom; zooming and panning
      settle within about a fifth of a second without blocking the playhead.
- [ ] Selection drag, Loop on: moving/resizing the range works in Spectrum exactly as in Waveform;
      the accent overlay and the range label stay readable over bright cells.
- [ ] External drag from the card still starts a prepared WAV while Spectrum is shown.
- [ ] REX2: slice lines and the active-slice highlight draw over the spectrogram.
- [ ] SF2/EXS/NKI sample, tracker sample, disc sample: Spectrum shows the selected source.
- [ ] Whole tracker song: the tabs are absent, the progress bar keeps its height.
- [ ] Unsupported format (Reason patch): the tab is disabled and the card keeps its explanation.
- [ ] `V` toggles the view with the preview focused; Settings → Hotkeys lists
      `preview.toggleSpectrum`.
- [ ] Context menu: Waveform view / Spectrum view / Log frequency scale; the scale pill follows.
- [ ] Light and dark theme: the heat ramp and the frequency guides stay readable.
- [ ] Restart: the mode and the scale return as they were.
- [ ] Plug-in editor: the same tabs work on the matched preview; no catalog write is attempted.
