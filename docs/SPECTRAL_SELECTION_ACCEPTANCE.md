# Spectral cache and 2D selection — acceptance

Design: `docs/superpowers/specs/2026-11-26-spectral-cache-and-selection-design.md`.
Verified against a Debug build with `SonicatorTests` (Catch2).

## Automated

| Check | Test |
|---|---|
| A cached spectrogram round-trips its grid and PNG | `TestSpectrogramCache.cpp` |
| A changed or truncated file is a miss, never a wrong picture | `TestSpectrogramCache.cpp` |
| Pruning keeps the newest entries and deletes both files | `TestSpectrogramCache.cpp` |
| Rectangle covers exactly its time and frequency band | `TestSpectralMask.cpp` |
| Subtract empties the mask; growing the window resamples it | `TestSpectralMask.cpp` |
| A lasso polygon fills the same band | `TestSpectralMask.cpp` |
| A magic-wand raster maps grid cells without holes | `TestSpectralMask.cpp` |
| A full mask reconstructs the signal (RMS error < 2 %) | `TestSpectralRender.cpp` |
| A band mask removes everything outside its band | `TestSpectralRender.cpp` |
| An empty mask is refused, not exported as silence | `TestSpectralRender.cpp` |
| The exported WAV is exactly the range length | `TestSpectralRender.cpp` |
| Batch caches both files, a rerun skips them, failures are counted | `TestSpectralAnalysis.cpp` |
| A cached file has its image before any timer runs | `TestPreviewSpectrumExtra.cpp` |
| The overlay covers its band and nothing else (columns and rows measured on a snapshot) | `TestPreviewSpectrumExtra.cpp` |
| Lasso and wand fill the region under the pointer | `TestPreviewSpectrumExtra.cpp` |
| A press on the selection lifts it; outside it draws; Shift edits on top | `TestPreviewSpectrumExtra.cpp` |
| Each tool has its own cursor and the selection answers with a hand | `TestPreviewSpectrumExtra.cpp` |
| A looping spectral audition keeps sounding past its first pass | `TestPreviewSpectrumExtra.cpp` |
| MIDI preview plays the masked fragment, and clearing restores the source | `TestPreviewSpectrumExtra.cpp` |
| A prepared mask becomes the MIDI fragment and clearing drops it | `TestPreviewSpectrumExtra.cpp` |
| A mask prepares the masked export and auditions it | `TestPreviewSpectrumExtra.cpp` |

Run:

```bash
build/tests/Debug/SonicatorTests.exe "[spectral],[spectrogram]"
```

## Result (Debug build, 2026-11-26)

- `"[spectral],[spectrogram]"` — 38 cases, 16 597 assertions, all passed.
- Regression: `"[ui]"` 227 cases / 2 047 assertions, `"[preview],[waveform]"` 40 / 254,
  `"[commands],[theme],[edit]"` 106 / 847, `"[audio],[midi]"` 26 / 206 — all passed. The only
  unrelated failure in the preview filter remains `TestDiscAcceptance`, which needs
  `SONICATOR_DISC_IMAGE`.
- Release build: `SONICATOR.exe`, `SonicatorTests.exe` and `SONICATOR.vst3` build, and the Release
  `"[spectral],[spectrogram]"` run passes with the same 38 cases.

## Manual

- [ ] Load a file, switch to Spectrum, restart the app and select it again: the card shows the
      spectrum before the "Computing" caption ever appears.
- [ ] Right-click a folder with more than one sample → `Spectral analysis (N)…`: progress appears,
      files open instantly afterwards, a second run reports "already cached".
- [ ] Settings → Library: `Cache spectrograms` off stops new entries, `Clear cache` reports 0 files.
- [ ] Rectangle, Lasso and Wand: draw, add with Shift, subtract with Alt, Clear and Escape; the
      overlay stays exactly on the selected time and band, follows zoom and pan, and each tool has
      its own cursor.
- [ ] Wand tolerance cycles 3/6/12/24 dB and the filled region stops at the requested level.
- [ ] Grab the highlighted area and drag it into a DAW: the cursor turns into a hand, the WAV
      contains only the selected band and time, and the mask stays after the drag.
- [ ] Play auditions the masked result; Loop wraps it without dropouts on the second and later
      passes; Stop returns to the original source.
- [ ] MIDI Preview with a 2D selection: a key plays the masked fragment and stops at its end;
      clearing the selection returns the whole sample.
- [ ] A time selection inside the mask still seeks and loops without re-preparing the export.
- [ ] Match applied: the spectral audition refuses with a clear message.
- [ ] Light theme: the mask overlay and the toolbar stay readable.
- [ ] Plug-in editor: cache and batch are hidden with the other processing jobs; 2D selection,
      drag and audition work on the matched preview after Match is off.
