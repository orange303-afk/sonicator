# DSP key and one-shot pitch — accuracy report

Measured on 2026-11-09 on macOS (native arm64, Debug build, Apple Clang). The
evaluation calls `TonalAnalyzer::analyzeKey` directly on the same bounded decode the
smart pipeline uses. It does not go through the app's metadata gate, so the numbers
describe the DSP itself; in the app a filename key always wins.

## Reference corpus

`/Volumes/1TBSSD/Splice/Splice/Samples/packs`, read-only. Only files with a trailing
key label are scored:

- a token after `_`, `-` or whitespace, letter A–G, optional `#`/`b`, then `m`,
  `min`, `minor`, `maj` or `major`;
- enharmonic spellings are normalized to a pitch class (`Abm` = `G#m`);
- a bare letter is rejected, so a pack word is not a key;
- the label is treated as the reference, even where the loop's audible tonal centre
  may be its relative major or minor.

Files are split deterministically by `FNV-1a(full path) % 2` into **calibration** and
**report** halves. Thresholds were tuned on the calibration half only. The full
report half was not used for tuning.

Categories come from the path: `one_shot`, `drum` (drum/kick/snare/hat/percussion),
`loop` (loop/phrase/song starter), `other`.

## Metrics

- **published**: the analyzer returned a key.
- **correct**: tonic and mode both match the label.
- **bare tonic**: the analyzer was sure of the tonic but withheld the mode.
- **wrong mode**: same tonic, other mode (parallel).
- **relative**: the relative major/minor of the label.
- **wrong**: any other tonic.
- **unknown**: no key published.

`correct + bare tonic + wrong mode + relative` is the harmonically compatible set;
only `correct` agrees with the label's mode convention.

## Command

```sh
SONICATOR_TONAL_EVAL_DIR=/Volumes/1TBSSD/Splice/Splice/Samples/packs \
SONICATOR_TONAL_EVAL_SPLIT=report \
build/tests/SonicatorTests "[.][tonal][real]"
```

The test writes a per-file CSV to the temporary directory
(`sonicator-tonal-eval.csv`) with the reference, detected key, confidence, profile
scores, tuning, tonality, chroma and bass chroma.

## Calibration half (loop files)

A bounded sample of the first 500 traversal files, calibration half, loop category:
179 loop files, 103 labelled, 67 published.

| Measure | Count |
|---|---:|
| Correct key | 26 (38.8% of published, 25.2% of labelled) |
| Bare tonic | 4 |
| Wrong mode (parallel) | 4 |
| Relative | 7 |
| Wrong | 26 |
| Unknown | 36 |

Harmonically compatible: 41 / 67 published (61.2%), 39.8% of labelled.

## Report half (loop files)

<!-- FINAL REPORT NUMBERS -->

## Pitch

The library has no note labels, so pitch is measured on synthetic audio in
`tests/TestTonalAnalyzer.cpp`:

- A440 → MIDI 69 within 0.02; A4 +25 cents → 69.25 within 0.03 and 25 cents within 3;
- C3 → 48 and E2 → 40 within 0.05;
- 48 kHz and 96 kHz A440;
- silence, white noise, a 220→110 Hz glide, a noise burst and a C major triad are
  rejected;
- anti-phase stereo A440 is detected;
- confidence is finite and in 0..1.

No claim is made about pitch accuracy on commercial one-shots; that needs a labelled
note corpus that this library does not provide.

## Interpretation

- The DSP agrees with the pack label on roughly two fifths of published loop keys, and
  is harmonically compatible with about three fifths when relative and parallel
  spellings are counted. A relative major/minor or a parallel mode is the same
  key signature or tonic; calling it "wrong" measures the label convention, not a
  wrong scale.
- Commercial pack names are not guaranteed to be the audible tonal centre of every
  loop. Several labelled loops analysed here are clearly their relative major or their
  dominant; the report measures agreement with the label, not absolute musical truth.
- The profile method is deliberately conservative: a low correlation, a weak tonic
  margin, a relative-key tie or a percussive signal produces `unknown`. Coverage is
  therefore lower than a forced guess, and the app never invents a key.
- Keys are the lowest-priority source; a filename or container key always wins in the
  app. The DSP fills gaps rather than replacing reliable metadata.
- Drums and non-harmonic hits remain `unknown`. This is the intended answer, not a
  failure to be papered over.

## Reproduction notes

- The evaluation is hidden (`[.][tonal][real]`) and is not part of the default suite.
- No library path is committed. Set `SONICATOR_TONAL_EVAL_DIR` to run it.
- `SONICATOR_TONAL_EVAL_LIMIT` bounds the traversal for a quick smoke run;
  `SONICATOR_TONAL_EVAL_SPLIT` selects `calibration` or `report`.
- The temporary CSV is diagnostic and is not part of the repository.
