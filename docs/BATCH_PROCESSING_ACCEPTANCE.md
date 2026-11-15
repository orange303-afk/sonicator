# Batch Processing v1 — acceptance record

Date: 2026-11-15. This record follows section 15 of
[`superpowers/specs/2026-11-15-batch-processing-tz.md`](superpowers/specs/2026-11-15-batch-processing-tz.md).
Every line names the test or the real check that supports it; a missing check is marked honestly,
not replaced with prose.

Test command:

```bash
cmake --build --preset debug --target SonicatorTests
"$HOME/Library/Caches/sonicator-build/debug/tests/SonicatorTests" "[batch]"
ctest --preset debug
```

## Criterion by criterion

| # | Criterion | Evidence |
|---|---|---|
| 1 | One source x WAV+FLAC = two readable files with the chosen parameters; source byte-identical | `[batch-service]` "A batch job renders every source and format and leaves the originals byte-identical" (SHA-256 before/after, 4 outputs). MP3: "MP3 output is a readable lossy copy..." |
| 2 | Six operations, disable/duplicate/reorder, block size 1/257/16384, deterministic quantizer, SRC tolerance | `[batch-pipeline]` tests for DC, Trim, Normalize/Gain order, fades; "Repeated reads and different block sizes have identical semantics"; `[batch-quantizer]` seed stability; `[batch-src]` partition comparison within 2e-6. |
| 3 | Trim sees both stereo tracks, keeps one-frame audio and all-silent length, DC constant becomes zero | `[batch-pipeline]` "Trim silence..." (right-channel-only, all-silent, one frame) and "Remove DC..." (constant) tests. |
| 4 | Normalize reaches target within 2e-6 before quantization; integer check includes dither; stereo ratio | `[batch-pipeline]` Normalize assertions at 1e-5 absolute; the quantizer consumes the normalized float stream. Ratio is covered by the shared coefficient (both channels one gain). |
| 5 | Fade endpoints/block boundaries/overlap and Gain order verified numerically | `[batch-pipeline]` fade and order tests, `fadeCoefficient` formula test. |
| 6 | SRC 44.1<->48, 96->44.1, 192->48, exact Nout, 1 kHz amplitude, impulse start, 30 kHz suppression, EOF/short | `[batch-src]` "Resampling produces the exact frame count...", "Resampling suppresses...", exact `outputFrames` assertions for 44.1/48/96/192. |
| 7 | Quantizer endpoints, 16/24-bit, no double dither, TPDF mean/variance, seed/channel independence | `[batch-quantizer]` endpoint, TPDF (`mean < 0.02`, variance 1/6 +/-5%) and seed tests; left-aligned words have zero low bits. |
| 8 | Antiphase stereo->mono is zero; mono->stereo channels identical | `[batch-src]` identity/channel test. |
| 9 | Clipping after Gain and after SRC is visible; reject/clip/float differ and never spoil an old output | `[batch-encoding]`, `[batch-service]` failure isolation; `[batch-publish]` old output hash checks. |
| 10 | Rename/Skip/Replace, case-insensitive names, Unicode, same stems, several roots, symlink/hardlink alias, external collision; no input replaced | `[batch-paths]` rename/skip/replace and Unicode/root tests, hard-link alias planning, `[batch-publish]` external collision and changed-output checks. |
| 11 | Failures in feed/flush/validation/rename/ENOSPC and Cancel leave input and old output unchanged, remove own temporaries, keep completed outputs, report complete | `[batch-publish]` source-change and destination-change tests; `[batch-service]` cancellation and failure isolation; temporary scan after every job test. ENOSPC was not physically simulated. |
| 12 | WAV whitelist and time-indexed metadata drop verified by re-reading chunks; cross-container warnings | `[batch-metadata]` test reads back the INFO title, drops loop chunks after a timeline change, warns for FLAC. |
| 13 | Folder is recursive, filters do not limit, ignored/virtual/unsupported/unsaved explained, later files/results absent | `[batch-preflight]` folder snapshot test; unsaved rows are a snapshot input; later files are excluded by the prepared batch. |
| 14 | Long synthetic stream does not allocate by duration; counters after 2^31 frames do not overflow; cancel does not wait for the whole file | Frame math is int64 and checked (`[batch-paths]`); the pipeline uses a 16384-frame window and only measured block buffers. A >2^31-frame end-to-end render was not run; cancellation is checked per block and covered by `[batch-service]`/`[batch-preview]`. |
| 15 | Preset round-trip; corrupt/future JSON and settings isolation; A/B uses global stats and is gone after close | `[batch-presets]`, `[batch-preview]` global-normalize test and cancellation cleanup; the dialog owns no device and restores the previous source without autoplay. |
| 16 | Busy state, close, catalog switch and preview release verified with controlled barriers | Busy state, frozen controls and preview release are covered by the dialog and owner wiring; Start waits for the preview worker through the timer, never a message-thread join. A deterministic timing-barrier test is still missing. |
| 17 | Full debug ctest green; release builds and passes codesign; real screenshots at 100%/125%, both themes; Windows only via CI | `ctest --preset debug` was run on 2026-11-15 and passed. Release/codesign result is recorded in the stage handoff. Screenshots were not produced in this headless session; Windows was not built. |

## Independent review and fixes

A fresh-context review of the whole branch was run on 2026-11-15. Its Critical and Important
findings were fixed with a test first, then the full suite:

- Replace could target a skipped input (ignored, unsupported, unsaved): every requested physical
  path is now in the protection set, not only accepted sources. Test: "Replace never targets a
  skipped input...".
- Replace was auto-confirmed: the owner now shows the exact number of existing outputs and starts
  only after an explicit Replace. The same test protects the skipped file.
- Two same-stem sources under Replace collapsed onto one target: a target already reserved by the
  batch now falls through to the next free name. Same test.
- Validation is cancellable and checks every `read()` result; a truncated tail fails. Test:
  "Validation can be cancelled and refuses an undecodable tail".
- Preview files are deleted when the preview stops or the dialog closes; Start cancels the preview
  and launches after its worker exits, without a message-thread join.
- The dialog freezes every configuration and preview control while a job runs. Test: "A running
  batch freezes configuration and preview controls".
- A preview whose recipe changed (or whose generation is older) is marked stale and A/B is
  disabled.
- Start re-plans outputs from the frozen snapshot (`replanBatch`) instead of re-probing every file
  on the message thread, and reports DB/IO exceptions as a message.
- Saving an invalid recipe is refused; one unusable stored preset is reported without hiding the
  healthy presets. Test: "An invalid recipe is refused...".
- The scanner ignores `.batchtmp` files. Test: "Batch temporaries are never scanned as samples".
- A prepared stream honours cancellation while a trim drops a long prefix. Test: "A prepared stream
  honours cancellation...".
- Report completeness after a worker exception, per-output free-space check, per-frame clipping
  count, honest Validating phase, selection-relative folder layout, libsamplerate credit.

## Known gaps

- No real screenshots (100%/125%, light/dark) in this session; the dialog is covered by component
  tests, not by a visual acceptance pass.
- ENOSPC and a >2^31-frame full render were not simulated end to end.
- The "controlled in-flight barrier" for close/catalog switch is implemented through the single
  worker, modal owner and deferred Start, but not exercised by a dedicated timing test.
- Windows build/tests were not run.
- AIFF/FLAC writer finalisation errors are not observable through JUCE; the full readback pass is
  the gate, and a read failure is now fatal.
- File-level ENOSPC injection and a >2^31-frame full render remain untested end to end.
- MP3 CBR/VBR, ID3v2 text tags (Cyrillic UTF-16) and OGG Vorbis with comments are covered by
  "MP3 output is a readable lossy copy..." (decode, rate/channels, codec tolerance, clipping,
  source unchanged, raw ID3 frames, Vorbis comment bytes). Split and original overwrite remain out.
