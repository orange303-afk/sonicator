# Held-out evaluation of the shipped type model

Model: `resources/ml/global_model.json`, generated 2026-11-12T00:00:00.000Z, 28 labels on 70 of 84 features.

Test data: `data/contributions/latest/contribute.jsonl` — 8071 records from 3 install(s), of which 3148 are schema-1 with the old 14 features and cannot be scored by this model.

Nothing here trained, tuned or selected the model. The file is read exactly as the app reads it,
and the acceptance rule is the app's own: the global score carries the 0.95 penalty, a local label
wins when the global best is not more than 0.02 above it, and a type is published from 0.80.

The export is downloaded by `node tools/contribution-sync.mjs` and stays git-ignored; the numbers
below are reproducible only against the same snapshot.

## What the numbers are

- **Records** count the rows a block can score. **Top-1** is the model's first answer;
  **coverage** is the share whose first answer clears 0.80, and **precision** is how many of
  those published answers are right. The library shows both.
- **Cold start** is a person the model has never met: the shipped model alone.
- **With own confirmations** adds that person's other confirmed rows as local prototypes, one
  record out at a time, and merges them the way `AdaptiveClassifier::predictTypeWithSchema` does.
- **Own confirmations only** drops the model, to show what the acceptance threshold means on a
  raw cosine.

| class | records | meaning |
|---|---|---|
| in-vocabulary | 1391 | label is one of the model's 28 |
| missing | 32 | standard type the model cannot output |
| custom | 139 | a person's own type name |
| unknown | 0 | a name that is neither standard nor marked custom |

## Confirmed types

The person typed the sample's Type. This is the strongest truth the contributions carry, and the
only one that can judge the type field as the library stores it.

### Summary

`In-vocabulary` counts only rows whose label is one of the model's own; `all standard` adds the
standard types it cannot name, so there a vocabulary gap is a miss, as it is for a person.

**Rows the model can name**

| mode | records | top-1 | top-3 | coverage | precision at acceptance (95% CI) |
|---|---|---|---|---|---|
| cold start | 1391 | 75.1% | 85.5% | 29.5% | 92.7% (89.7%–94.8%) |
| with own confirmations | 1391 | 93.0% | 99.9% | 100.0% | 93.0% (91.5%–94.2%) |
| own confirmations only | 1391 | 93.0% | 99.9% | 100.0% | 93.0% (91.5%–94.2%) |

**All standard types**

| mode | records | top-1 | top-3 | coverage | precision at acceptance (95% CI) |
|---|---|---|---|---|---|
| cold start | 1423 | 73.4% | 83.6% | 29.1% | 91.8% (88.7%–94.1%) |
| with own confirmations | 1423 | 92.5% | 99.9% | 100.0% | 92.5% (91.0%–93.7%) |
| own confirmations only | 1423 | 92.5% | 99.9% | 100.0% | 92.5% (91.0%–93.7%) |

### Per install

`Export rows` counts everything the install sent, any schema; `records` counts the rows this
block can score. A row with no interval is a row with no answer to score.

**Cold start**

| install | versions | export rows | records | in-vocabulary | top-1 (in-vocabulary) | coverage (all standard) | precision (all standard) | local evidence |
|---|---|---|---|---|---|---|---|---|
| `10e44eea` | 0.2.0 | 4526 | 1378 | 1378 | 75.4% | 29.7% | 92.9% (90.0%–95.0%) | 100.0% |
| `4fbda754` | 0.2.0, 0.3.0 | 3512 | 176 | 13 | 38.5% | 11.1% | 0.0% (0.0%–43.4%) | 100.0% |
| `3e6e5a11` | 0.2.0 | 33 | 8 | 0 | — | — | — | 100.0% |

**With own confirmations**

| install | versions | export rows | records | in-vocabulary | top-1 (in-vocabulary) | coverage (all standard) | precision (all standard) | local evidence |
|---|---|---|---|---|---|---|---|---|
| `10e44eea` | 0.2.0 | 4526 | 1378 | 1378 | 93.2% | 100.0% | 93.2% (91.7%–94.4%) | 100.0% |
| `4fbda754` | 0.2.0, 0.3.0 | 3512 | 176 | 13 | 69.2% | 100.0% | 71.1% (56.6%–82.3%) | 100.0% |
| `3e6e5a11` | 0.2.0 | 33 | 8 | 0 | — | — | — | 100.0% |

**Own confirmations only**

| install | versions | export rows | records | in-vocabulary | top-1 (in-vocabulary) | coverage (all standard) | precision (all standard) | local evidence |
|---|---|---|---|---|---|---|---|---|
| `10e44eea` | 0.2.0 | 4526 | 1378 | 1378 | 93.2% | 100.0% | 93.2% (91.7%–94.4%) | 100.0% |
| `4fbda754` | 0.2.0, 0.3.0 | 3512 | 176 | 13 | 69.2% | 100.0% | 71.1% (56.6%–82.3%) | 100.0% |
| `3e6e5a11` | 0.2.0 | 33 | 8 | 0 | — | — | — | 100.0% |

### The two scales at the acceptance threshold

Rows with a local prototype that is not a copy of the same audio: **1562** of 1562. Copies skipped: **816** prototypes whose vector equals the row's own.

Median best local cosine: **0.986**.

| evidence | records | top-1 | coverage | precision |
|---|---|---|---|---|
| model alone, cold start | 1391 | 75.1% | 29.5% | 92.7% |
| the two merged, as the app does | 1391 | 93.0% | 100.0% | 93.0% |
| local cosine alone | 1391 | 93.0% | 100.0% | 93.0% |

The local label wins **1562** of 1562 rows.
Of those wins, **1562** were published, and **1449** were right (92.8%).

The median best cosine is already above the 0.80 threshold, so the threshold cannot gate a local answer there: it decides nothing. The global score is mapped to the precision it carries, the local cosine is a similarity, so the merge compares a calibrated number with a raw one and the raw one wins almost every time.

### Per type, cold start

| type | records | accepted | precision | top-1 | top-3 | mean score | most common accepted mistake |
|---|---|---|---|---|---|---|---|
| `kick` | 720 | 347 (48.2%) | 99.1% | 94.4% | 97.9% | 0.750 | snare ×2 |
| `snare` | 648 | 62 (9.6%) | 58.1% | 54.8% | 73.0% | 0.490 | clap ×22 |
| `vocal` | 13 | 1 (7.7%) | 0.0% | 38.5% | 46.2% | 0.505 | clap ×1 |
| `hat` | 10 | — | — | 40.0% | 50.0% | 0.337 | — |

### What the acceptance threshold decides, cold start

The threshold is a trade: a lower one publishes more answers and is right less often. Nothing
here changes it; the table is what a decision would be made on.

| threshold | coverage, labels the model can name | precision there | coverage, all standard | precision there |
|---|---|---|---|---|
| 0.50 | 64.7% | 86.4% | 64.1% | 85.3% |
| 0.60 | 57.0% | 88.7% | 56.4% | 87.7% |
| 0.70 | 46.1% | 90.2% | 45.5% | 89.2% |
| 0.80 | 29.5% | 92.7% | 29.1% | 91.8% |
| 0.90 | 12.7% | 96.0% | 12.4% | 96.0% |
| 0.95 | 4.2% | 94.8% | 4.1% | 94.8% |

### Types the model cannot name

These labels sit outside the model's vocabulary, so every answer it gives them is one of its
own types. This is the vocabulary gap, not a ranking mistake.

| type | records | installs | accepted | what the model answers (cold start) |
|---|---|---|---|---|
| `fx` | 32 | 1 | 4 (12.5%) | percussion ×8, vocal ×6, keys ×4 |

### Custom types

A custom type cannot come from the shipped model at all. Locally, however, the person's own
confirmed rows become prototypes for that name, which is why the local columns can be non-zero.

| type | records | installs | cold start answer | local top-1 | local coverage |
|---|---|---|---|---|---|
| `fill` | 100 | 1 | vocal ×39, percussion ×33 | 97.0% | 100.0% |
| `drum loop` | 31 | 1 | percussion ×25, tamb ×4 | 90.3% | 100.0% |
| `top loop` | 8 | 1 | clap ×5, percussion ×2 | 100.0% | 100.0% |

### Does the global score mean what it says?

The similarity map turns a probability into the precision the model had at that probability on
the public validation split. Measuring it on contributed audio tests whether that promise holds
outside the public datasets.

| accepted score | records | mean score | measured precision |
|---|---|---|---|
| 0.80–0.85 | 59 | 0.825 | 89.8% |
| 0.85–0.90 | 174 | 0.878 | 90.2% |
| 0.90–0.95 | 119 | 0.926 | 96.6% |
| 0.95–1.00 | 58 | 0.950 | 94.8% |

### The confident mistakes, cold start

| true label | answered as | records |
|---|---|---|
| `snare` | `clap` | 22 |
| `snare` | `kick` | 4 |
| `kick` | `snare` | 2 |
| `kick` | `rim` | 1 |
| `vocal` | `clap` | 1 |

## Tag-derived labels

A row without a Type whose tags hold exactly one shipped type name (Kick, Snare, Hat, ...) is a
second, weaker test set: the person did name the sound, but not in the Type field. A row whose
tags hold several type names, or none, stays out. Read this block as evidence about the sound,
not about what the Type column would say.

Rows taken from tags: **3157**; rows left out for ambiguous or absent type tags:
**204**.

### Summary

`In-vocabulary` counts only rows whose label is one of the model's own; `all standard` adds the
standard types it cannot name, so there a vocabulary gap is a miss, as it is for a person.

**Rows the model can name**

| mode | records | top-1 | top-3 | coverage | precision at acceptance (95% CI) |
|---|---|---|---|---|---|
| cold start | 2738 | 64.2% | 82.0% | 16.1% | 89.3% (86.1%–91.9%) |
| with own confirmations | 2738 | 7.9% | 18.6% | 96.2% | 7.6% (6.7%–8.7%) |
| own confirmations only | 2738 | 0.0% | 0.0% | 91.0% | 0.0% (0.0%–0.2%) |

**All standard types**

| mode | records | top-1 | top-3 | coverage | precision at acceptance (95% CI) |
|---|---|---|---|---|---|
| cold start | 3157 | 55.7% | 71.1% | 15.0% | 83.3% (79.6%–86.4%) |
| with own confirmations | 3157 | 10.5% | 23.5% | 96.6% | 10.3% (9.3%–11.4%) |
| own confirmations only | 3157 | 3.7% | 7.4% | 92.1% | 4.0% (3.3%–4.7%) |

### Per install

`Export rows` counts everything the install sent, any schema; `records` counts the rows this
block can score. A row with no interval is a row with no answer to score.

**Cold start**

| install | versions | export rows | records | in-vocabulary | top-1 (in-vocabulary) | coverage (all standard) | precision (all standard) | local evidence |
|---|---|---|---|---|---|---|---|---|
| `4fbda754` | 0.2.0, 0.3.0 | 3512 | 3155 | 2736 | 64.2% | 15.0% | 83.3% (79.6%–86.4%) | 100.0% |
| `3e6e5a11` | 0.2.0 | 33 | 2 | 2 | 0.0% | 0.0% | — | 100.0% |

**With own confirmations**

| install | versions | export rows | records | in-vocabulary | top-1 (in-vocabulary) | coverage (all standard) | precision (all standard) | local evidence |
|---|---|---|---|---|---|---|---|---|
| `4fbda754` | 0.2.0, 0.3.0 | 3512 | 3155 | 2736 | 7.9% | 96.6% | 10.3% (9.3%–11.4%) | 100.0% |
| `3e6e5a11` | 0.2.0 | 33 | 2 | 2 | 0.0% | 0.0% | — | 100.0% |

**Own confirmations only**

| install | versions | export rows | records | in-vocabulary | top-1 (in-vocabulary) | coverage (all standard) | precision (all standard) | local evidence |
|---|---|---|---|---|---|---|---|---|
| `4fbda754` | 0.2.0, 0.3.0 | 3512 | 3155 | 2736 | 0.0% | 92.2% | 4.0% (3.3%–4.7%) | 100.0% |
| `3e6e5a11` | 0.2.0 | 33 | 2 | 2 | 0.0% | 0.0% | — | 100.0% |

### The two scales at the acceptance threshold

Rows with a local prototype that is not a copy of the same audio: **3157** of 3157. Copies skipped: **0** prototypes whose vector equals the row's own.

Median best local cosine: **0.875**.

| evidence | records | top-1 | coverage | precision |
|---|---|---|---|---|
| model alone, cold start | 2738 | 64.2% | 16.1% | 89.3% |
| the two merged, as the app does | 2738 | 7.9% | 96.2% | 7.6% |
| local cosine alone | 2738 | 0.0% | 91.0% | 0.0% |

The local label wins **2914** of 3157 rows.
Of those wins, **2821** were published, and **113** were right (4.0%).

The median best cosine is already above the 0.80 threshold, so the threshold cannot gate a local answer there: it decides nothing. The global score is mapped to the precision it carries, the local cosine is a similarity, so the merge compares a calibrated number with a raw one and the raw one wins almost every time.

### Per type, cold start

| type | records | accepted | precision | top-1 | top-3 | mean score | most common accepted mistake |
|---|---|---|---|---|---|---|---|
| `snare` | 920 | 54 (5.9%) | 92.6% | 55.7% | 77.5% | 0.450 | clap ×3 |
| `hat` | 692 | 45 (6.5%) | 46.7% | 67.6% | 88.3% | 0.523 | tamb ×20 |
| `clap` | 453 | 147 (32.5%) | 99.3% | 89.2% | 94.7% | 0.705 | snare ×1 |
| `kick` | 284 | 168 (59.2%) | 100.0% | 97.2% | 98.9% | 0.789 | — |
| `crash` | 81 | 2 (2.5%) | 50.0% | 35.8% | 56.8% | 0.453 | hat ×1 |
| `ride` | 74 | 9 (12.2%) | 0.0% | 14.9% | 44.6% | 0.559 | hat ×5 |
| `percussion` | 73 | 3 (4.1%) | 0.0% | 16.4% | 43.8% | 0.391 | hat ×1 |
| `tamb` | 51 | 5 (9.8%) | 100.0% | 31.4% | 49.0% | 0.529 | — |
| `bass` | 42 | 1 (2.4%) | 100.0% | 38.1% | 97.6% | 0.441 | — |
| `snap` | 26 | 5 (19.2%) | 20.0% | 30.8% | 65.4% | 0.577 | clap ×4 |
| `shaker` | 21 | 1 (4.8%) | 0.0% | 9.5% | 33.3% | 0.452 | tamb ×1 |
| `rim` | 15 | — | — | 0.0% | 20.0% | 0.411 | — |
| `tom` | 6 | — | — | 50.0% | 100.0% | 0.477 | — |

### What the acceptance threshold decides, cold start

The threshold is a trade: a lower one publishes more answers and is right less often. Nothing
here changes it; the table is what a decision would be made on.

| threshold | coverage, labels the model can name | precision there | coverage, all standard | precision there |
|---|---|---|---|---|
| 0.50 | 51.8% | 80.5% | 49.0% | 73.8% |
| 0.60 | 42.8% | 83.6% | 40.1% | 77.5% |
| 0.70 | 32.2% | 86.7% | 29.9% | 80.9% |
| 0.80 | 16.1% | 89.3% | 15.0% | 83.3% |
| 0.90 | 4.6% | 85.0% | 4.4% | 78.3% |
| 0.95 | 0.9% | 76.9% | 0.9% | 71.4% |

### Types the model cannot name

These labels sit outside the model's vocabulary, so every answer it gives them is one of its
own types. This is the vocabulary gap, not a ranking mistake.

| type | records | installs | accepted | what the model answers (cold start) |
|---|---|---|---|---|
| `fx` | 319 | 1 | 17 (5.3%) | vocal ×63, clap ×43, brass ×31 |
| `downfilter` | 75 | 1 | 9 (12.0%) | crash ×25, clap ×21, brass ×13 |
| `synth` | 25 | 1 | 6 (24.0%) | vocal ×14, brass ×6, keys ×2 |

### Custom types

No row in this set uses a custom type name.

### Does the global score mean what it says?

The similarity map turns a probability into the precision the model had at that probability on
the public validation split. Measuring it on contributed audio tests whether that promise holds
outside the public datasets.

| accepted score | records | mean score | measured precision |
|---|---|---|---|
| 0.80–0.85 | 105 | 0.818 | 85.7% |
| 0.85–0.90 | 208 | 0.878 | 93.8% |
| 0.90–0.95 | 101 | 0.924 | 87.1% |
| 0.95–1.00 | 26 | 0.950 | 76.9% |

### The confident mistakes, cold start

| true label | answered as | records |
|---|---|---|
| `hat` | `tamb` | 20 |
| `ride` | `hat` | 5 |
| `snap` | `clap` | 4 |
| `hat` | `ride` | 3 |
| `ride` | `crash` | 3 |
| `snare` | `clap` | 3 |
| `clap` | `snare` | 1 |
| `crash` | `hat` | 1 |
| `hat` | `kick` | 1 |
| `percussion` | `hat` | 1 |
| `percussion` | `rim` | 1 |
| `percussion` | `snare` | 1 |

## Reading the numbers

- The cold start is the only number that speaks for a new person. A per-install row with few
  records carries a wide interval; read the interval, not the point.
- The gold sample that chose and calibrated this model is the owner's catalog, 4526 confirmed rows
  in `docs/TRAINING_REPORT_V2.md`. The install whose `export rows` add up to that number is that
  catalog: its rows are an in-sample fit, and only the other installs measure generalization.
- Local prototypes come from contributed rows, so a wrong local answer can be a wrong older
  confirmation, not a model failure. Copies of the same audio are skipped as prototypes, so the
  local numbers do not count a duplicate of the tested sound as its own second opinion.

## Reproducing

```bash
cd services/training && node evaluate-shipped.mjs --in "../../data/contributions/latest/contribute.jsonl" --model "../../resources/ml/global_model.json" --doc "../../docs/AUTODETECT_HELDOUT.md"
```
