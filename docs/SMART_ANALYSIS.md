# Smart metadata — foundation

This document describes the smart-analysis pipeline as implemented after the Phase 2
metadata spike. It covers storage, provenance, invalidation, the background job, the
library Type/BPM/Key columns and the bottom selection bar. The rhythm DSP pass is
described under [Rhythm DSP](#rhythm-dsp) and [Tonal DSP](#tonal-dsp); the inspector,
manual override editing UI and global metadata filters are later slices.

## What runs

`SmartAnalysisService` is a background thread owned by `MainComponent`. It reads
metadata first and then runs one bounded PCM decode only when at least one DSP
question is still open: the rhythm fallback, a musical key, or a one-shot pitch.
It never touches measured Phase 1 features (`basicFeaturesVersion` stays 1).

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
| `smartDspVersion` | the DSP stage changes (currently 2: rhythm, key and one-shot pitch) |

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
  becomes a key. A `DefaultValue` sampler root at unity 60 does not suppress the DSP
  pitch, because it is a mapping default rather than a measured note.

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
- The tonal pass uses the same bounded decode. Key reads at most the first 30 seconds
  of it. Pitch reads at most the first 5 seconds of active audio after the attack.
- Key profiles assume tonal, mostly diatonic material. Modal, atonal, heavily
  chromatic and percussion-led loops usually stay `unknown`; a relative major/minor
  pair with no clear margin publishes nothing rather than inventing a tonic. Drums
  whose name or immediate folder says drum/perc, and drums with an unstable chroma,
  stay `unknown`.
- Pitch runs only for a confirmed one-shot. Loops, chords, vocals, effects, drums and
  non-harmonic hits stay `unknown`; a stable segment shorter than the gate is not
  guessed.
- Commercial pack labels are not guaranteed to be the audible tonal centre of every
  loop, so the key evaluation measures agreement with the label, not an absolute
  musical truth. See `docs/key-pitch-accuracy.md`.
- Type taxonomy, the inspector and global metadata filters are later slices.
  `docs/superpowers/plans/2026-11-07-smart-catalog-foundation.md` tracks the
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
file's full path (also as its tooltip), effective Type/Structure/BPM/Key/Pitch with
their state, tags, format, rate, channels, bit depth, duration, star and any
missing/error note. Structure and Pitch are shown in the selection bar only; the
library keeps its three metadata columns.

## Rhythm DSP

`core/dsp/RhythmAnalyzer` is a pure function over decoded audio plus a bounded file
reader. `core/smart/RhythmCandidates` maps its result to smart candidates. Nothing
runs on the audio thread, and the pass uses no network, model weights or cache files.

`needsRhythmAnalysis()` gates the pass: a valid metadata BPM from any source (name,
WAV ACID, Apple Loops, ASD sidecar, REX2) suppresses it, and the extraction record is
stored as `skipped`. Decoding is the expensive step, and the library treats DSP as a
fallback rather than a second opinion, so a file with a known tempo does not pay for
it. Structure from audio is not requested on its own.

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
6. Octave preference. Tempo octaves are musically equivalent. When a half/double rival
   is comparably supported (at least 50% of the winner's quality), the DSP reports the
   octave inside 70–180 BPM, the range sample libraries actually use. The rival stays
   in the alternatives list. A half/double rival no longer makes the tempo ambiguous;
   an unrelated rival with ≥92% of the winner's quality does.
7. Structure:
   - `RhythmicLoop` when at least four onsets, at least 3.5 beats of context, ≥30%
     weighted grid coverage, ≥35% filled grid positions and grid strength ≥0.30;
   - `OneShot` when there are at most three onsets, at most four active seconds and
     peak/active-RMS ≥3 (a pad or texture stays below that);
   - `Undetermined` otherwise, including short rhythmic fragments.

Tempo is `Determined` only for a loop with no competing grid and either grid strength
≥0.55 with weighted coverage ≥0.45, or a dense grid: strength ≥0.75, coverage ≥0.30
and at least 8 beats (percussion with many ghost subdivisions). Otherwise it is
`Ambiguous`, and the smart layer stores an invalid candidate with the primary value
and its alternatives. The BPM confidence is `0.55 × grid strength + 0.45 × coverage`,
reduced by 30% when ambiguous. Both scores are heuristics, not probabilities.

A hit with a reverb tail, a two-hit stab and a sparse syncopated phrase deliberately
stay away from "loop + tempo" unless the grid evidence is there. The unit tests in
`tests/TestRhythmAnalyzer.cpp` pin silence, sustained tones, one-shots, accented bars,
eighth-note hats, out-of-range doubles, anti-phase stereo, leading silence, quiet
material, several sample rates, short files, white noise, the decoded-memory cap and
WAV reading.

### Real-library evaluation

`tests/TestRhythmRealSamples.cpp` is a hidden test (`[.][rhythm][real]`) that takes a
folder from `SONICATOR_RHYTHM_EVAL_DIR` and writes a per-file CSV to the temporary
folder. It is not part of the default suite and no library path is hard-coded.

Its reference parser is deliberately stricter than the app's: an explicit `NNNbpm`
wins; a bare number is used only when it is the single candidate in the name or in
the immediate parent folder; a label is rejected when the file's duration is not a
whole bar at that tempo (a sample number rarely lands on a bar). Only files under a
loop path are scored against a reference — a one-shot carries its pack's tempo in the
name, not a tempo of its own. One-shot paths are instead used to count published
false BPMs. Results are split by category and include structure, tempo state,
coverage, grid strength and a correct / half-double / wrong classification.

Measured on the owner's Splice library on 2026-11-09 (macOS Debug, native arm64).
The evaluation calls the analyzer directly, so it covers the DSP path itself; in the
app, loops with a usable metadata tempo never reach it.

| Measure | Result |
|---|---|
| Audio files analysed | 8 549 |
| Reference labels after the bar-fit filter | 2 893 (44 rejected) |
| Structure on labelled loop files | 1 940 `loop` (67.1%), 949 `undetermined` (32.8%), 4 `one-shot` |
| Tempo on labelled loop files | 816 determined (28.2%), 1 124 ambiguous (38.9%), 953 unknown (32.9%) |
| Published BPM within 2% of the reference | 632 / 816 (77.5%) |
| Published BPM within 1% | 627 (76.8%) |
| Median error among published | 0.082% |
| Half/double-time octave | 149 (18.3% of published) |
| Clearly wrong | 35 (4.3% of published) |
| Drums/percussion loops with `loop` structure | 556 / 642 (86.6%); 83.7% of their published BPMs correct |
| Melodic loops with `loop` structure | 548 / 926 (59.2%); 62.6% of their published BPMs correct |
| Vocal loops with `loop` structure | 177 / 301 (58.8%); 60% of their published BPMs correct |
| One-shots publishing any BPM | 9 / 1 097 (0.8%); several carry their pack's tempo in the name anyway |
| Wall time | 2 200 s, ≈0.26 s per file at a mean duration of 9.6 s |

What this says:

- The grid and the number are accurate when they are confident: almost all error
  among published values is the octave, not a wrong tempo.
- Octave ambiguity is irreducible without accents or metadata. The DSP now folds
  out-of-range doubles/triples into 70–180 BPM and leaves the rival in the
  alternatives; an in-range rival (75 vs 150, 90 vs 180) can still win on coverage.
- Melodic, vocal and pad loops are often honestly `undetermined`; publishing a beat
  for them would be a guess. Percussion and drum loops, the material this fallback is
  most useful for, reach 83.7% among published values with 2.3% clearly wrong.
- The app still prefers metadata: the same library carries BPM in names and ACID/ASD
  data, so the DSP fills gaps rather than replacing reliable values.

## Tonal DSP

`core/dsp/TonalAnalyzer` is a pure function over the bounded `readAnalysisAudio`
buffer. `core/smart/TonalCandidates` applies the per-field gates and maps the result
to `SmartCandidate` rows with `source = "dsp"`. `SmartAnalysisService` calls both in
the same pass as the rhythm stage, after one PCM decode; the DSP version is 2.

### Key

1. 8192-point Hann-windowed frames with a 1024 hop, channel powers averaged so
   anti-phase stereo is not cancelled. The first 30 seconds are used.
2. Spectral peaks are picked per frame; the strongest low/mid peak per tonal frame
   gives a circular tuning estimate. The estimate is applied to the chroma mapping.
3. A per-frame weight from spectral flatness and positive flux suppresses transients.
   A median filter over the frame chroma series (default 8 frames) removes short
   percussive spikes. Frames are whitened against their own maximum and L2-normalized
   before summing, so a loud section cannot outvote the rest of the loop.
4. The chroma is correlated with 24 keys from the Krumhansl–Kessler and Temperley
   major/minor profiles. A low-frequency bass chroma adds a small tonic prior.
5. A key is published only when the best correlation clears the floor, the chroma is
   stable over time, and the margin over the best different-tonic runner-up clears the
   tonic gate. A relative major/minor pair with a weak margin is a tonic dispute and
   publishes nothing. A confident tonic whose two modes are close publishes the bare
   tonic (`"C"`).
6. Resolved Type `kick`, `snare`, `clap`, `hat`, `percussion` or `cymbal` skips key
   analysis. A file name or immediate folder that names a percussion family (`drum`,
   `perc`, `kick`, `snare`, `hat`, `cymbal`, `crash`, `ride`, `tom`, `clap`, `break`,
   `darbuka`, `djembe`, …) also skips both fields; a higher pack name does not. A drum
   loop whose Type and path are unknown still reaches the analyzer and is rejected
   there by the percussive/consistency gate.

### Pitch

1. Active frames are found from RMS; the attack is skipped. Analysis is capped at
   5 seconds of active audio.
2. YIN runs per frame with an FFT autocorrelation difference function, an absolute
   threshold of 0.15 and parabolic interpolation. A frame keeps a pitch only above the
   clarity floor.
3. Consecutive frames within 50 cents form runs; the longest run of at least five
   frames is the stable segment. Its median is the estimate. A run whose spread
   exceeds 30 cents is rejected.
4. Harmonicity checks that the first partials of the estimate dominate 50 Hz–5 kHz,
   and that the fundamental is actually present. A missing fundamental, glide, noise
   burst or chord fails and publishes nothing.
5. Channels are analysed separately and the clearer channel wins. If the channels
   disagree by more than 50 cents at comparable confidence, the pitch is ambiguous and
   nothing is published.
6. Pitch runs only for a one-shot with no `Valid` Pitch or Root candidate, a Type
   that is not a drum, `vocal` or `fx`, and no percussion path. The exact fractional
   MIDI is stored; the resolver derives cents. The bottom bar renders `A4 +12c`.

### Storage and evaluation

Key and pitch use the existing `key_*` and `pitch_*` columns; no migration is needed.
The library keeps its Type/BPM/Key columns and the bottom bar shows Pitch. The hidden
`[.][tonal][real]` test evaluates key detection on a labelled library from
`SONICATOR_TONAL_EVAL_DIR`, splits the catalog deterministically into calibration and
report halves, and writes a per-file CSV. The measured numbers, the exact command and
the label caveat are in `docs/key-pitch-accuracy.md`. Pitch is covered by synthetic
sines, detuned notes, low notes, chords, noise, glides and anti-phase stereo; the
library carries no note labels.
