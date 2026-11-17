#import "../style.typ": *

#chapter(21, "Batch Processing",
  lead: [One job, one ordered chain, several output formats: convert and prepare a folder or a selection without editing a single original.],
  inthis: (
    [Entry points and what a job can contain],
    [The six steps and their ranges],
    [Output formats, rate, channels and dither],
    [Naming, conflicts and Split inside a chain],
    [Preview, safety and the report],
  ),
)[

Open it from the row context menu (*Batch Processing…*) or from a folder's context menu. A row
selection exports the selected rows; a folder job includes every subfolder and is not limited by
table filters, stars, tags, duplicate hiding or pagination. Rows that represent a merged mono L/R
stereo pair expand into both physical files. Ignored, virtual, container and unsaved-editor sources
are listed as skipped with a reason and never exported.


#section("The chain")

An empty chain is a plain conversion. A chain holds at most 16 steps. Steps can be disabled,
duplicated and reordered; they run in order on the current intermediate result at the source rate.

#data-table(
  header: ("Step", "Range", "What it does"),
  widths: (1.3fr, 1.5fr, 3fr),
  rows: (
    ("Remove DC offset", "—", "Subtracts the per-channel mean. Constant audio can become zero."),
    ("Trim silence", "−120…−6 dBFS, 0…1000 ms padding", "Keeps the first to last frame above the threshold, plus padding. All-silent input keeps its length."),
    ("Normalize", "−24…0 dBFS", "One sample-peak coefficient for all channels. Not LUFS and not true peak."),
    ("Gain", "−60…+24 dB", "One coefficient for all channels; 0 dB is an exact bypass."),
    ("Fade in / out", "0…10 000 ms, Linear or Cosine", "Applied to the current boundaries. The last frame of a fade out is zero."),
    ("Split by silence", "−120…−6 dBFS, 10…10 000 ms", "One Split per recipe. Steps before it run on the whole stream; steps after it run on every part."),
  ),
)

*Sample cleanup* is a built-in preset: DC → Trim (−60 dB, 5 ms) → Normalize (−0.1 dB) → Fade out
(2 ms, Linear). *No processing* is the empty chain. Built-in presets cannot be edited; your own presets
store the chain and the output settings but never the selection, destination or a replace confirmation.

#plate("/docs/manual/assets/batch.png", [Figure 21.1 — A batch job with the *Sample cleanup* chain and one WAV output. The chain runs top to bottom; each row can be disabled, duplicated, reordered or removed.])

#section("Output")

A job can write several formats at once:

- WAV: 16/24-bit PCM or 32-bit float.
- AIFF: 16/24-bit PCM.
- FLAC: 16/24-bit PCM, compression 1–8 (default 5).
- MP3: CBR 128/192/256/320 kbps or VBR V0–V9, with text ID3 tags taken from the source.
- OGG Vorbis: quality 0–10, with Vorbis comments taken from the source.

Rate is the source rate or 44 100, 48 000, 88 200, 96 000 or 192 000 Hz. Channels are the source
channels, mono (average of L and R) or stereo (mono duplicated). Dither is *Auto*, *TPDF* or *Off*;
Auto adds TPDF when the depth is reduced, when the source is float or lossy, or after a real amplitude
change or resampling.

Lossy output runs no integer dither; the decoded length is validated within the codec delay and
padding tolerance. Cover art and catalog-only fields are not invented.

#section("Names and conflicts")

Names default to `<stem> processed.<ext>` next to each original, or in the chosen folder while
preserving the path below the selected folder. A source that Split turns into several parts uses
`<stem> part 001`, `part 002`, and so on; one part keeps the ordinary name.

Existing outputs are renamed (`processed 2`, `processed 3`), skipped, or replaced — but a replace
needs a confirmation that names the exact count and is only allowed when the old file is unchanged.
No policy can replace an input, a hard-link alias or a symbolic-link target.

#section("Preview")

*Prepare preview* measures the chain on the whole selected source in a worker, then writes a dry and a
processed WAV of at most ten seconds each. Playback uses the existing audio engine; no second device
is created. The processed preview skips integer dither and the codec round-trip, and says so. After
the preview, the previously loaded source returns without autoplay.

With Split in the chain, the preview auditions the first detected part after the post-split steps.

#section("Safety")

- Outputs are written to a unique temporary file in the destination folder, closed, reopened and
  checked for rate, channels, depth, frame count, decode errors and NaN/Inf. Only then are they
  published with an atomic same-volume rename.
- A source that changed since the snapshot fails with `source_changed`; the old output and the source
  stay untouched.
- Free space is estimated before Start; a known shortage blocks the job, an unknown answer is a
  warning.
- Cancel is checked while walking inputs and inside every render block. Completed outputs stay;
  unfinished temporaries are removed.
- The catalog is not modified and new outputs are not indexed automatically. *Open output folder*,
  *Export report* and *Scan new files* are explicit actions after the job stops.

#section("The report")

The dialog keeps a per-output report until it closes: completed, skipped, failed or cancelled; the
actual path, format, depth, rate, channels, frames, peak, step outcomes, warnings and a structured
error code. It exports as JSON or text, and a diagnostic copy is kept in `batch-reports` (at most 20
files, 30 days).

#section("Limits")

- No original overwrite, no processing inside containers or instruments, no LUFS/true peak, limiter,
  time stretch, pitch shift or VST processing.
- Split v2 has no part-level presets, padding or nested splits.
- One batch job and one preview at a time. Other writers and catalog switches are blocked while it
  runs; simple playback stays available.
- macOS builds and the test suite are verified; a Windows build is not claimed in this edition.


]