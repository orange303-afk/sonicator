# Batch Processing

Batch Processing converts and prepares ordinary audio files in one job: pick files or a
catalogued folder, build an ordered chain of operations, preview one source, choose one or more
output formats, and read a report at the end. Originals are never edited or removed.

The binding specification is
[`superpowers/specs/2026-11-15-batch-processing-tz.md`](superpowers/specs/2026-11-15-batch-processing-tz.md);
the implementation plan is
[`superpowers/plans/2026-11-15-batch-processing.md`](superpowers/plans/2026-11-15-batch-processing.md).
This page is the user-facing summary of what v1 actually does.

## Where it is

- Library context menu: **Batch Processing...** on the current selection.
- Folder context menu: **Batch Processing...** on a catalogued physical folder; it includes every
  subfolder and is not limited by table filters, stars, tags, duplicate hiding or pagination.
- Commands: `library.batchProcess` and `folders.batchProcess` (no default hotkeys).

Rows that represent a merged mono L/R stereo pair expand into both physical mono files. Ignored,
virtual, container and unsaved-editor sources are listed as skipped with a reason, never exported.

## Operations

An empty chain is a plain conversion. Up to 16 steps run in order; steps can be disabled,
duplicated and reordered. All work happens on the current intermediate result at the source rate.

| Step | Range | Behaviour |
|---|---|---|
| Remove DC offset | - | Subtracts the per-channel mean (double accumulation). Constant audio can become zero. |
| Trim silence | -120..-6 dBFS, 0..1000 ms padding | Keeps `[first - padding, last + 1 + padding)`; a frame counts when any channel is above the threshold. All-silent input keeps its length. |
| Normalize | -24..0 dBFS | One sample-peak coefficient for all channels; peaks at or below 1e-6 are unchanged. Not LUFS and not true peak. |
| Gain | -60..+24 dB | One coefficient for all channels; 0 dB is an exact bypass. |
| Fade in / out | 0..10000 ms, Linear or Cosine | Applied to the current boundaries; `M < 2` is a no-op. The final frame of a fade out is zero. |

`Sample cleanup` is a built-in preset: DC -> Trim (-60 dB, 5 ms) -> Normalize (-0.1 dB) ->
Fade out (2 ms, Linear). `No processing` is the empty chain. Built-in presets are immutable.

## Output

- WAV: 16/24-bit PCM or 32-bit float.
- AIFF: 16/24-bit PCM.
- FLAC: 16/24-bit PCM, compression level 1..8 (default 5).
- MP3: CBR 128/192/256/320 kbps (default 192) or VBR V0-V9 (default V2). ID3v2 text tags
  (TIT2/TPE1/TALB/TCON/TDRC/TRCK, COMM) come from the source's existing tags, UTF-16 for non-ASCII.
- OGG: Ogg Vorbis, quality 0-10 (default 6), through JUCE's embedded encoder. The source tags are
  written as Vorbis comments (TITLE/ARTIST/ALBUM/GENRE/DATE/TRACKNUMBER/COMMENT).
- Lossy output runs no integer dither; the decoded length is validated within the codec
  delay/padding tolerance. No cover art or catalog-only fields are invented.
- Rate: same as source, 44100, 48000, 88200, 96000 or 192000 Hz.
- Channels: same as source, mono (`(L + R) / 2` in double), stereo (mono duplicated).
- Dither: Auto, TPDF, Off. Auto adds TPDF when the target depth is reduced, when the source is
  float/lossy, or after any real amplitude change or resampling; untouched PCM at the same or a
  wider depth is not dithered.
- Clipping: integer output whose final peak is above 0 dBFS is refused unless **Allow integer
  clipping** is on; float WAV may exceed 0 dBFS with a warning.

`Same as source` needs an exact representation; float -> FLAC and float -> AIFF ask for an
explicit 16/24-bit choice instead of silently rounding. WAV/AIFF payloads at or above the 4 GiB
container limit are refused before writing.

Names default to `<stem> processed.<ext>` next to each original, or in the chosen folder while
preserving the path below the selected folder (row selections use their registered root). A batch from several registered roots gets a stable
`<root-name>-<hash8>` group. Existing outputs are renamed (`processed 2`, `processed 3`, ...),
skipped, or replaced only after a confirmation that names the exact count and only if the old file
is unchanged.
No policy can replace an input, a hard-link alias or a symbolic-link target.

## Safety

- Outputs are written to a unique temporary in the destination folder, closed, reopened, checked
  for container facts (rate, channels, depth/float flag, exact frame count), read end to end for
  decode failures and NaN/Inf, and only then published with an atomic same-volume link/rename.
  There is no delete-then-copy fallback.
- A source that changed since the snapshot fails with `source_changed`; the old output and the
  source stay untouched.
- Free space is estimated before Start (256 MiB reserve); an unknown answer is a warning, a known
  shortage blocks Start.
- Cancel is checked while walking inputs, in every analysis/render/validation block and before
  publication. Completed outputs stay; unfinished temporaries are removed; planned outputs that
  never started appear as cancelled in the report.
- The catalog is not modified and new outputs are never indexed automatically. **Open output
  folder**, **Export report** and **Scan new files** are explicit actions after the worker stops.
- WAV -> WAV keeps a verified INFO/sampler-root whitelist; loop, cue and time-reference chunks are
  dropped with a warning when Trim or resampling changed the timeline. AIFF/FLAC and
  cross-container writes report that text/sampler metadata is not carried.
- Sidecars such as `.asd` are neither changed nor copied; they still belong to the original.

## Preview

Preview measures the global chain parameters on the whole selected source in a worker, then writes
a dry and a processed WAV float (at most 10 seconds each; deleted when the preview stops or the
dialog closes) starting at the first frame that survives
Trim. Playback uses the existing AudioEngine; no second audio device is created. The processed
preview skips integer dither and codec round-trip, and says so. After the preview, the previously
loaded source is restored without autoplay.

## Presets and report

Presets live in `<settings-directory>/batch-presets.json` (schema 1). They carry the ordered steps,
outputs, rate/channels, dither, clipping and conflict policy; never a selection, destination or a
replacement confirmation. Saves are atomic; a corrupt or newer file is reported and not overwritten.
Duplicate names require an explicit replace.

The dialog keeps the report until it is closed: per-output state (completed/skipped/failed/
cancelled), actual path, format, depth, rate, channels, frames, peak, step outcomes, warnings and a
structured error code. It exports as JSON or text. A diagnostic JSON copy is kept in
`<settings-directory>/batch-reports` (at most 20 files, 30 days).

## Limits of v1

- No Split, no original overwrite, no nested container/instrument processing, no LUFS/true peak,
  limiter, time stretch, pitch shift or VST processing.
- Lossy output was added after v1: MP3 CBR/VBR with text ID3 and OGG Vorbis with comments. Cover
  art, catalog-only fields (manual Genre/BPM/Key) and gapless-specific VBR tuning are out.
- One batch job and one preview at a time. Other writers and catalog switches are blocked while it
  runs; simple playback remains available.
- macOS debug/release builds and the test suite are verified. A Windows build was not run in this
  environment and is not claimed.
- The dialog is modal for the Sonicator window but runs its job on a worker; the message thread is
  polled at UI rate.

## Dependency

Band-limited resampling uses **libsamplerate 0.2.2** (`SRC_SINC_BEST_QUALITY`), pinned by archive
SHA256, built statically without its examples, tests or install rules. BSD-2-Clause. MP3 output
uses **LAME 3.100**, pinned, encoder sources only, linked statically under LGPL-2.1-or-later. See
[`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).
