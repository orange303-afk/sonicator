# DSP key and one-shot pitch — design

Status: awaiting approval, 2026-11-09.

## Intent

Fill the two metadata gaps the Phase 2 spike measured:

- Musical **Key** exists in file names for only ~3.2% of WAVs. Loops without a key label
  currently stay `unknown`.
- **Pitch** is not extracted from audio at all. One-shots that carry no sampler root or
  pitch metadata cannot be searched or transposed by note.

Both values come from classical DSP over the PCM the smart pass already has to decode.
No network, no model weights, no cache files, no new training data. Drums and
non-harmonic hits stay honestly `unknown` instead of being guessed.

The reference corpus is the owner's Splice library at
`/Volumes/1TBSSD/Splice/Splice/Samples/packs`; key labels are the trailing token in the
file name (`..._Cm.wav`, `..._F#min.wav`, `..._Ebmaj.wav`). Pitch accuracy is measured
on synthetic notes because the library carries no note labels.

## Goals

- One PCM decode per file, shared by the rhythm and tonal analyses.
- Key: chromagram from the decoded frames, tuning estimate, percussion suppression,
  24 major/minor profiles from Krumhansl–Kessler and Temperley, confidence from the
  margin over the runner-up.
- Pitch: YIN on the most stable segment of a one-shot, output MIDI + cents.
- Per-file gate: Key only when no usable Key metadata exists; Pitch only for one-shots
  with no usable Pitch or Root metadata.
- Confident tonic without a mode is published as a bare tonic (`"C"`); a dispute about
  the tonic publishes nothing.
- Store through the existing `key_*` and `pitch_*` smart fields. No schema migration.
- Show Pitch in the bottom selection bar, not as a new library column.
- A hidden evaluation test and a measured accuracy report.

## Non-goals

- No chord recognition, key change tracking, or modulation timeline.
- No pitch detection for loops, chords, vocals or polyphonic material.
- No replacement of metadata: the DSP remains the lowest-priority source.
- No ML, no external service, no model download.
- No change to measured Phase 1 features, tags, stars or overrides.
- No new library column and no new inspector in this slice.

## Context found

- `core/smart/SmartAnalysisService::collect` runs metadata extractors, then a rhythm
  DSP fallback only when no usable metadata BPM exists. `RhythmAnalyzer::analyzeFile`
  opens and decodes the file itself.
- `SmartResult` already stores `keyTonic/keyMode/keyState/keySource` and
  `hasPitch/pitchMidi/pitchCents/pitchState/pitchSource`; schema v7 has the columns.
- `SmartDisplay` (the read model for the library and bottom bar) exposes Type,
  Structure, BPM and Key, but not Pitch. `smartDisplays()` does not select the pitch
  columns, and the bottom bar has no Pitch field.
- `resolveKey` already treats a bare tonic as mode `unknown`, and `keyDisplayName`
  renders it as `"C"`. `resolvePitch` accepts a fractional MIDI value and derives
  cents from it.
- `KeyNames` has key parsing/formatting but no MIDI-note formatter.
- `smartDspVersion` is 1 (rhythm only). The DSP version is stored per result and a
  mismatch makes the sample a candidate for the next incremental pass without
  re-hashing or re-running measured features.
- The real library has 8,550 decodable files; a strict trailing-key parser found 1,839
  labelled files across 12 roots, with `m/min/minor` and `maj/major` suffixes and
  enharmonic flats (`Abm`, `Bbm`, `Ebm`, `Dbm`, `Gbm`).

## Approaches considered

### A. Shared decoded PCM, two independent analyzers (chosen)

A new bounded reader decodes the prefix once into `juce::AudioBuffer`. The existing
`RhythmAnalyzer::analyze(buffer, rate)` and the new `TonalAnalyzer` consume that same
buffer. Each analyzer keeps its own FFT geometry.

- Satisfies "one decode per file" exactly, which is the dominant cost.
- Leaves the tuned rhythm algorithm untouched; its existing tests remain the safety net.
- Two FFT passes per file (roughly 0.05–0.1 s), far below decode cost.
- Easy to test each analyzer in isolation.

### B. Shared spectral frame pass

Refactor the rhythm envelope builder to expose magnitude spectra and feed both onset
flux and chroma from one FFT pass.

- Literal single spectral pass, but ties the key analysis to the rhythm frame size
  (~11.6 ms, 1024/2048 FFT). That resolution is poor for tuning and low-note chroma;
  key would need a larger FFT anyway.
- High regression risk in the carefully tuned rhythm code for a small saving.
- Rejected for this slice.

### C. Independent decode per analyzer

Simplest code, but violates the one-decode requirement and doubles disk/decode work on
8k+ files. Rejected.

## Key analysis

`core/dsp/TonalAnalyzer` is a pure function over decoded audio plus `CancelCheck`.

1. **Frames.** 8192-point Hann-windowed FFT, hop 1024 samples (about 23 ms at 44.1 kHz),
   channel powers averaged (anti-phase stereo is not cancelled). Analysis is capped at
   30 s or the decoded prefix, whichever is shorter; the cap is recorded in the
   explanation.
2. **Tuning estimate.** Spectral peaks in 60 Hz–3 kHz across frames are compared with
   equal-tempered semitone centres. A circular histogram of the fractional-semitone
   deviation gives the tuning offset (documented, typically within ±50 cents). Chroma
   mapping uses that offset; the estimate is stored in the analysis diagnostics.
3. **Percussion suppression.** Two stages:
   - per-frame tonal weight from spectral flatness and positive spectral flux: a
     broadband transient or noise frame contributes little;
   - a median filter over each chroma bin along time (about 0.4 s) removes short
     percussive spikes while sustained harmonic content survives.
4. **Chroma.** Magnitudes between 55 Hz and 5 kHz are folded into 12 pitch classes with
   a Gaussian semitone kernel (σ ≈ 0.5 semitone), then normalized. A tonality score is
   the share of energy in stable, low-flatness frames over the active frames.
5. **Profiles.** 24 keys from two published profile sets:
   - Krumhansl–Kessler major/minor;
   - Temperley major/minor.
   Each key score is the mean of the Pearson correlations of the normalized chroma
   with both profile rotations. Both individual correlations are kept in diagnostics.
6. **Decision.**
   - If tonality is below the percussion/noise gate, or the best correlation is below
     the absolute floor, publish nothing (drums, noise, silence, very short material).
   - Let `best` be the winning key and `runnerUp` the best key with a **different
     tonic**. If the tonic margin is below `minTonicMargin`, publish nothing
     ("dispute about the tonic — nothing").
   - If the same tonic has a competing mode within `modeMargin`, publish the bare
     tonic (`"C"`, mode empty).
   - Otherwise publish the full key (`"C# minor"`).
   - Confidence is a documented heuristic from the best-vs-runner-up margin and the
     absolute correlation, not a probability. It is stored in the candidate's
     `auxValue`.
7. **Type and drum gates.**
   - Key is skipped for resolved Types `kick`, `snare`, `clap`, `hat`, `percussion`,
     `cymbal`. A drum loop whose Type is unknown still reaches the analyzer and is
     rejected there by the percussive gate.
   - Pitch is skipped for the same drum Types plus `vocal` and `fx` (the non-goals rule
     out vocals and non-harmonic effects). `pad`/`synth`/polyphonic one-shots are not
     skipped by Type; the stability and harmonicity checks reject them.
   - A sequence that is dominated by transients, has high flatness and low tonality is
     percussive. Either gate means `unknown`, never a key or a note.

Thresholds live in `TonalOptions` and are calibrated on a deterministic half of the
real library; the other half is reported untouched. Calibration is **precision-first**,
following the rhythm slice: a wrong published key costs more than an `unknown`, so the
margins are set to keep clearly wrong publications low, and the report states both
coverage and accuracy among published rather than hiding the unknown share.

## Pitch analysis

1. Use the same decoded buffer. Find the active region from frame RMS (peak-relative
   −45 dBFS) and skip the first 20 ms of attack.
2. YIN per frame:
   - window ≥ 2.2 periods of the lowest expected f0 (50 Hz), power-of-two, capped at
     16,384 samples; hop 256 samples;
   - cumulative-mean normalized difference, absolute threshold 0.15, parabolic
     interpolation;
   - clarity = 1 − minimum difference.
3. **Stable segment.** Keep frames with clarity ≥ 0.5 and sufficient level. Group
   consecutive frames whose pitch stays within 50 cents into runs; take the longest run
   of at least 4 frames (about 46 ms). The median pitch of the run is the estimate.
4. **Harmonicity.** Check that spectral energy at the first 5–8 integer partials of the
   estimate dominates the energy in 50 Hz–5 kHz. A glide (kick), noise (snare) or
   inharmonic hit fails either stability or harmonicity and publishes nothing.
5. **Output.** Exact fractional MIDI (e.g. `69.12`), cents = `(midi − round(midi)) × 100`.
   Confidence from clarity × stability × harmonicity is written to the explanation;
   the candidate's `auxValue` stays 0 because the resolver treats it as extra cents.
6. **Gate.** Only one-shots with no valid Pitch and no valid RootNote. A
   `DefaultValue` root (unity/base note 60 written by `smpl`/`inst`/ACID) is a sampler
   mapping default, not a measured pitch, so it does not block the DSP. Structure comes
   from metadata first; when it is unknown and no metadata BPM exists, the rhythm pass
   determines it. A loop or an unknown structure publishes no pitch.
7. **Channels.** YIN runs per channel and keeps the channel with the higher clarity. If
   the channels disagree by more than 50 cents at comparable confidence, the pitch is
   ambiguous and nothing is published. A mono sum is never used, so an anti-phase
   stereo one-shot is not cancelled.

## Smart integration

- `core/smart/TonalCandidates` maps the two analyses to `SmartCandidate` rows with
  `source = "dsp"`, explanation, limitation and confidence.
- `SmartAnalysisService::collect`:
  1. run metadata extractors as today;
  2. decide `needRhythm = !needsRhythmAnalysis(metadata)`,
     `needKey = !hasValidKeyMetadata`,
     `needPitch = !hasValidPitchOrRootMetadata && structureMayBeOneShot`;
     a `DefaultValue` root does not count as pitched metadata;
  3. if any DSP need exists, decode once with the shared bounded reader;
  4. run `RhythmAnalyzer::analyze` when `needRhythm` or when structure is needed for the
     pitch gate; run `TonalAnalyzer::analyzeKey` when `needKey`; run
     `TonalAnalyzer::analyzePitch` when the resolved structure is `one-shot` and
     `needPitch`;
  5. publish a `dsp` extraction record with `skipped`, `ok`, `unsupported` or `error`,
     exactly like the rhythm pass does today.
- `smartDspVersion` becomes 2. The incremental pass re-runs the DSP stage for existing
  rows without touching measured features or schema.

## Storage and UI

- No migration. Key uses the existing `key_*` columns; pitch uses the existing
  `pitch_*` columns.
- `SmartDisplay` gains `hasPitch`, `pitchMidi`, `pitchCents`, `pitchState`,
  `pitchOverride`.
- `SampleRepository::smartDisplays` selects the pitch columns and the `pitch` override.
- The bottom selection bar appends `Pitch: A4 +12c` (or `?`, `n/a`, `manual: …`,
  `(conflict)`, `(ambiguous)`, `(stale)`), matching the existing Key field's markers.
- `KeyNames` gains `midiNoteName(int)` and `formatCents(double)` helpers.
- The library keeps its Type/BPM/Key columns; Pitch is deliberately bottom-bar only.

## Evaluation

- `tests/TestTonalRealSamples.cpp`, hidden `[.][tonal][real]`, reads
  `SONICATOR_TONAL_EVAL_DIR` (optional `SONICATOR_TONAL_EVAL_LIMIT`). No library path is
  committed.
- Reference parser: a trailing key token after a delimiter, with `m/min/minor` and
  `maj/major` suffixes; enharmonic spellings normalized to pitch classes. Bare letters
  are rejected unless explicitly suffixed, so pack words do not become keys.
- Categories: loop, one-shot, drum/percussion, other; files without a key label are
  not scored for accuracy but are counted for false publications.
- Metrics: files, labelled, published, correct tonic+mode, correct tonic only,
  relative/parallel confusions, wrong, unknown, coverage, wall time. CSV per file.
- Deterministic split: tune on a stable hash half, report the other half. The report
  will state both and will not claim more than measured.
- Pitch accuracy is measured by `tests/TestTonalAnalyzer.cpp` on synthetic sines,
  detuned sines and chords; the real library has no note labels.

## Tests first

1. `TestTonalAnalyzer.cpp` — key on triads in all 12 major and 12 minor keys, scales,
   tuning offsets, detuning, silence, noise, click trains, drum-like patterns,
   anti-phase stereo, 44.1/48/96 kHz, short files; pitch on A440, detuned notes, low
   notes, noise, kick sweep, snare-like hits, chords, sample rates.
2. `TestTonalCandidates.cpp` — gates, structure, bare tonic, no candidate on tonic
   dispute, drum Type, candidate fields, confidence bounds.
3. `TestAnalysisAudio.cpp` — decode bounds, truncation flag, cancellation, unsupported
   file.
4. `TestSmartDatabase.cpp` / `TestUI.cpp` — pitch survives publication and appears in
   `SmartDisplay` and the bottom bar text.
5. `TestTonalRealSamples.cpp` — hidden evaluation.

## Files

- Create `core/dsp/AnalysisAudio.h/.cpp`
- Create `core/dsp/TonalAnalyzer.h/.cpp`
- Create `core/smart/TonalCandidates.h/.cpp`
- Modify `core/smart/SmartAnalysisService.cpp`
- Modify `core/smart/SmartTypes.h` (SmartDisplay pitch fields, DSP version)
- Modify `core/smart/KeyNames.h/.cpp` (note formatting)
- Modify `core/database/SampleRepository.cpp` (`smartDisplays`)
- Modify `ui/MainComponent.cpp` (bottom bar)
- Modify `CMakeLists.txt`, `tests/CMakeLists.txt`
- Tests: `TestTonalAnalyzer.cpp`, `TestTonalCandidates.cpp`, `TestAnalysisAudio.cpp`,
  `TestTonalRealSamples.cpp`, plus pitch display assertions
- Docs: this design, an execution plan, `docs/SMART_ANALYSIS.md`,
  `docs/key-pitch-accuracy.md`

## Risks and limits

- Key profiles assume tonal, mostly diatonic material. Modal, atonal, heavily
  chromatic and percussion-led loops will often be `unknown`; that is the intended
  answer, not a failure.
- Chroma cannot distinguish a relative major from its relative minor without a clear
  profile margin; the bare-tonic rule avoids inventing a mode.
- A one-shot with a short, noisy or gliding pitch stays unknown rather than guessed.
- The evaluation library's labels are commercial pack metadata and not guaranteed
  musically exact; the report measures agreement with those labels.
- macOS Debug is the verified platform; Windows and Intel macOS are separate checks.

## Calls made without asking (correct me if wrong)

- "Confident tonic without a mode — `C`" means a bare tonic string, which the existing
  resolver already stores as mode `unknown`.
- "One decode" means one PCM decode; the rhythm and tonal analyzers keep separate FFT
  passes (Approach A).
- Pitch is gated on a confirmed one-shot. If structure is unknown and metadata already
  has a BPM, the file gets no pitch rather than a guess.
- A `DefaultValue` sampler root does not suppress pitch; only a `Valid` Pitch or Root
  candidate does. Unity 60 is a mapping default, not a measured note.
- Pitch runs per channel and picks the clearer one; a mono sum is never used.
- Key/Pitch Type gates: drums are skipped for both; `vocal`/`fx` are skipped for pitch.
- Calibration is precision-first, not coverage-first.
- The DSP stays the lowest-priority source, so a filename key always wins.
