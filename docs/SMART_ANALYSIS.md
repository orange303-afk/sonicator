# Smart metadata — foundation

This document describes the smart-analysis pipeline as implemented after the Phase 2
metadata spike. It covers storage, provenance, invalidation, the background job, the
library Type/BPM/Key columns and the bottom selection bar. The rhythm DSP pass is
described under [Rhythm DSP](#rhythm-dsp); pitch/key DSP, the inspector and global
metadata filters are later slices.

## What runs

`SmartAnalysisService` is a background thread owned by `MainComponent`. It reads
metadata first, then decodes a bounded audio prefix for the rhythm DSP pass. It never
touches measured Phase 1 features (`basicFeaturesVersion` stays 1).

Two modes:

- **Incremental**: finds samples with no result, a stale result, a structured error,
  or a result produced by another extractor/resolver/DSP version. It then verifies
  the stored size/mtime fingerprints of every completed result, so a changed, added
  or deleted `<audio>.asd` sidecar is discovered when the pass starts. If verification
  found changes, one more analysis pass runs in the same job.
- **Selected**: a bounded queue (10 000 ids) for explicit refreshes. `requestSample()`
  only enqueues; parsing and disk I/O never run on the message thread.

The **Smart metadata** button starts an incremental pass. Pause and Cancel apply to
whichever background job is running. A scan and smart analysis do not run at the same
time.

## Publication

Reading candidates and resolving a result happen outside the database transaction.
One `BEGIN IMMEDIATE` transaction then writes, for one sample:

- candidates;
- per-source extraction records;
- audio/sidecar fingerprints;
- the resolved result and its pipeline metadata.

The transaction checks that the catalog is still the one the job started on and that
the result revision is still the one the job read before analysis. If either changed,
nothing is written: another job or a manual override already won. This is what stops
an old job from overwriting a newer result.

On cancellation or failure the previous completed generation stays readable. A
failure is recorded in `smart_failures` (code, message, attempts) and the result's
`pipeline_state` becomes `error`; its values are not erased. A successful
publication clears the failure.

## Provenance

Each candidate stores:

- field and musical semantics;
- raw and normalized value;
- source (`filename`, `wav_acid`, `aiff_basc`, `asd_warp`, `rx2_glob`, `dsp`, …);
- source file and its size/mtime fingerprint;
- extractor id and version that produced it;
- candidate status (`valid`, `default`, `invalid`, `unsupported`);
- a limitation note where the value is not the musical truth.

Agreement is computed by the resolver from independent source groups (name, container
metadata, sidecar, DSP). Two rows from the same exporter are one group, not two
confirmations. DSP candidates also carry a documented heuristic confidence score
(0–1) in their aux value. It is a quality score, not a probability, and it is shown
next to the resolved BPM rather than used as evidence in the resolution itself.

## Versions

| Version | Bump when |
|---|---|
| `smartExtractorsVersion` | a metadata parser or its interpretation changes |
| `smartResolverVersion` | selection rules change; stored candidates can be re-resolved without reading audio |
| `smartDspVersion` | the DSP stage changes (currently 1: rhythm structure and BPM) |

Results store all three. A mismatch makes the sample a candidate for the next
incremental pass. It does not re-hash audio or recompute basic features.

## Result states

Per field, the automatic state is one of: `not_analyzed`, `unknown`, `resolved`,
`ambiguous`, `conflicting`, `unsupported`, `not_applicable`, `stale`, `error`,
`default`. `queued`/`analyzing` are pipeline-level states reported by the progress
object for the current job. `overridden` remains readable for old rows but new
writes do not use it.

Whole-record pipeline state: `not_analyzed`, `queued`, `analyzing`, `complete`,
`stale`, `error`.

A manual override is a separate attribute (`none`, `value`, `unknown`,
`not_applicable`) and never replaces the automatic state. The effective value is the
automatic value with the override applied; the last completed automated value remains
visible underneath, including when it is stale.

## Resolver policy in this slice

- BPM 20–400; values within 1 BPM agree; wider differences conflict.
- A ≈2× ratio (60 vs 120) is `ambiguous` (half/double-time), not a conflict.
- A DSP pass with competing grids stores the tempo as an `invalid` candidate: the
  resolved BPM becomes `ambiguous`, never an invented single number. A valid
  metadata BPM still wins over it by source priority.
- DSP structure (`loop` / `one-shot`) is a normal text candidate. A name or container
  value with stronger priority wins, and disagreement is reported as `ambiguous`.
- Variable tempo does not resolve to a single BPM.
- Enharmonic keys (`C#` / `Db`) are not a conflict.
- Relative major/minor (`C major` / `A minor`) is `ambiguous`, not equal values.
- A tonic without a mode never gains an invented mode.
- Pitch is a separate field and never becomes a musical key; a sampler root never
  becomes a key.

## Known limits

- Change detection relies on size + modification time. A rewrite that keeps both
  identical is not detected without an explicit reanalysis.
- There is no filesystem watcher: changes are found when an incremental pass runs.
- Audio is not parsed for embedded metadata beyond the implemented containers
  (WAV, AIFF, ASD sidecar, REX2). Other formats record `unsupported` for the
  container extractor and still get name candidates.
- The rhythm DSP reads at most the first 60 seconds, or fewer when the decoded PCM
  would exceed 64 MB (very high sample rates); a truncated file keeps the limitation
  note and is never presented as a whole-file verdict.
- Rhythm thresholds were tuned on synthetic patterns. Real one-shots, sparse
  syncopation and dense textures can still be `undetermined`; that is the intended
  answer when the evidence is insufficient, not a failure to be papered over.
- Pitch/key DSP, Type taxonomy, the inspector and global metadata filters are later
  slices. `docs/superpowers/plans/2026-11-07-smart-catalog-foundation.md` tracks the
  foundation; the columns use its stored results.

## Library columns and selection bar

The library shows **Type**, **BPM** and **Key** after the sample name. Values are the
effective ones: a manual override wins, and the automatic state is still available
underneath. Cell markers are textual, never colour-only:

| Marker | Meaning |
|---|---|
| `--` | not analyzed yet |
| `?` | analyzed, value unknown |
| `edited unknown` / `n/a` | explicit manual unknown / not applicable |
| `*` | manual override |
| `!` | sources conflict |
| `?` (suffix) | ambiguous interpretation |
| `~` | stale: dependencies or analyzer version changed |

Sorting by these headers runs in SQL over the whole catalog, with unknown values last
in both directions and a stable id tie-break. The bottom bar shows the selected
file's full path (also as its tooltip), effective Type/Structure/BPM/Key with their
state, tags, format, rate, channels, bit depth, duration, star and any missing/error
note. Structure is shown in the selection bar only; the library keeps its three
metadata columns.

## Rhythm DSP

`core/dsp/RhythmAnalyzer` is a pure function over decoded audio plus a bounded file
reader. `core/smart/RhythmCandidates` maps its result to smart candidates. Nothing
runs on the audio thread, and the pass uses no network, model weights or cache files.

1. Frames of about 11.6 ms (`44100 / 512`, adapted to the file rate). Per channel
   spectra are averaged as powers, so anti-phase stereo is not cancelled.
2. Log spectral flux, detrended by a local mean, normalised with the 95th percentile
   and a floor of 35% of the maximum (a sparse loop must not be silenced).
3. Onsets: local maxima above an adaptive threshold, thinned by strength with a 60 ms
   minimum separation.
4. Candidate periods: autocorrelation peaks between 40 and 240 BPM, each refined
   against a comb grid with fractional spacing and a phase search. Half and double
   candidates are evaluated explicitly instead of being assumed.
5. Ranking uses mean onset strength on the grid times the share of onset strength the
   grid explains, so a half-tempo grid that lands on every other hit is not a rival.
6. Structure:
   - `RhythmicLoop` when at least four onsets, at least 3.5 beats of context, ≥45%
     weighted grid coverage, ≥35% filled grid positions and grid strength ≥0.30;
   - `OneShot` when there are at most three onsets, at most four active seconds and
     peak/active-RMS ≥3 (a pad or texture stays below that);
   - `Undetermined` otherwise, including short rhythmic fragments.

Tempo is `Determined` only for a loop with grid strength ≥0.40 and no competing grid
within 80% of the winner's quality (or an unrelated grid within 92%). Otherwise it is
`Ambiguous`, and the smart layer stores an invalid candidate with the primary value
and its alternatives. The BPM confidence is `0.55 × grid strength + 0.45 × coverage`,
reduced by 30% when ambiguous. Both scores are heuristics, not probabilities.

A hit with a reverb tail, a two-hit stab and a sparse syncopated phrase deliberately
stay away from "loop + tempo" unless the grid evidence is there. The unit tests in
`tests/TestRhythmAnalyzer.cpp` pin silence, sustained tones, one-shots, accented bars,
eighth-note hats, anti-phase stereo, leading silence, quiet material, several sample
rates, short files, white noise and WAV reading.
