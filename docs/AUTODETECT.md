# Type detection: what the shipped model does on real libraries

Date: 2026-11-25. Measured with `services/training/evaluate-shipped.mjs`; the tables are generated in
`docs/AUTODETECT_HELDOUT.md`. The local-domain fix landed on 2026-11-26; the section on it records the
measured result on the same export.

## What is measured

`resources/ml/global_model.json` is the schema-2 softmax model trained on the public wave
(`docs/TRAINING_REPORT_V2.md`). For every analysed sample the app computes the model's ranking and the
best cosine against the person's own confirmed rows, then merges the two
(`AdaptiveClassifier::predictTypeWithSchema`).

The held-out evaluation reads the contribution export as the test set and applies the app's own rule:
0.95 penalty on the global score, a local label wins when the global best is not more than 0.02 above
it, and a type is published from 0.80. Nothing is trained, tuned or selected there.

Two truth sources, because the export has two:

- **confirmed types** — the person set the Type field: 1562 records from 3 installs. One install is the
  owner's catalog (4526 export rows), which supplied the gold sample that chose and calibrated this
  model, so its numbers are an in-sample fit.
- **tag-derived labels** — a record with no Type whose tags hold exactly one shipped type name: 3157
  records, 3155 of them from one install that typed almost nothing. This is the only independent set
  of a second person's library, and it is about the sound rather than about the Type column.

## What the model gets

On the independent set (3157 tag-derived records, one install):

| metric | value |
|---|---|
| top-1, labels the model can name (2738 records) | 64.2% |
| top-1, all standard types (3157 records) | 55.7% |
| coverage at the 0.80 threshold | 15.0% |
| precision of the published answers | 83.3% (95% CI 79.6–86.4%) |

Per type, cold start: kick 97.2% (284 records), clap 89.2% (453), snare 55.7% (920), hat 67.6% (692),
bass 38.1% (42, but top-3 97.6%), crash 35.8% (81), tamb 31.4% (51), snap 30.8% (26), percussion 16.4%
(73), ride 14.9% (74), shaker 9.5% (21), rim 0% (15).

The owner's own confirmed types (in-sample, 1378 records): kick 94.4% top-1 and 99.1% of published
answers right; snare 54.8% top-1, and only 9.6% of snares are published at all, 58.1% of those right.
His 22 confident snare mistakes are `clap`.

## The local-domain fix (2026-11-26)

The first defect above is fixed in `AdaptiveClassifier`: a schema-2 query answered while the shipped
model is loaded now uses that model alone. Local prototypes still answer when no model is shipped or
the query is schema 1; a manual Type on a file was never touched by this path. No model file, no
feature schema and no retraining changed.

The same export, measured again with `evaluate-shipped.mjs`:

| independent set (3157 tag-derived rows) | top-1 | coverage | precision |
|---|---|---|---|
| local cosine merged (previous rule) | 10.5% | 96.6% | 10.3% |
| shipped model alone (current rule) | **55.7%** | 15.0% | **83.3%** |

On the owner's own typed rows the "with own confirmations" number falls from 92.5% to 73.4% top-1,
because that set supplied the prototypes that were outvoting the model: it is an in-sample fit, and
the independent set is the one that measures a second person. The full comparison, per install and
per label, is `docs/AUTODETECT_HELDOUT.md`.

## Five things the data says

1. **The local path, not the model, is what breaks type accuracy for a second person.** The app's
   merge compares a calibrated global score with a raw 84-dimension cosine. The cosine is above the
   0.80 threshold for almost every pair of samples, so a local label wins 2914 of 3157 rows; on the
   2821 it publishes, 113 are right (4.0%). Top-1 falls from 55.7% to 10.5% and precision from 83.3%
   to 10.3%. The prototypes there are the same person's 176 typed rows, mostly `Fill` and `Drum Loop`
   — the model would have answered `snare`, `hat` or `kick` correctly.
2. **The vocabulary gap is real and countable.** `fx`, `riser`, `impact`, `downfilter`, `synth`,
   `drop` and `foley` cannot come out of the model at all. 419 of the 3155 independent rows are such
   types; the model answers `fx` with `vocal`, `clap` or `brass`, `downfilter` with `crash` or `clap`.
3. **The percussion families are confused.** On the independent set the confident mistakes are
   `hat → tamb` (20), `ride → hat` (5), `snap → clap` (4), `hat → ride` (3), `ride → crash` (3),
   `snare → clap` (3). Snare is rarely published (5.9% coverage) even where it is first (55.7%).
4. **The 0.80 threshold publishes a small, precise slice.** On the independent set 15.0% of samples
   get a type at 83.3% precision; at 0.50 it would be 52% at 80%. This is a product decision, not a
   tuning mistake, but it is the number that decides how "smart" the app feels.
5. **Duplicate content is everywhere.** 52% of the owner's scored rows share an exact feature vector
   with another row: the same audio under two names. The local path counts such a copy as a second
   opinion, which inflates every local measurement; the evaluator skips copies, the app does not.

## What to do, in order

1. **Stop comparing a cosine with a calibrated probability.** Done on 2026-11-26: the model answers
   schema-2 queries on its own when it is loaded, and the independent set measured 55.7% top-1 and
   83.3% precision instead of 10.5% and 10.3%. What remains from this list is the vocabulary and the
   percussion families below, both of which need labelled data rather than a merge rule.
2. **Give the model the missing vocabulary, or stop promising it.** A real `fx` cluster is not in the
   public wave; the FSD50K mapping was tried and reverted (`docs/TRAINING_REPORT_V2.md`). Until a
   labelled source exists, the honest behaviour is the current one: publish nothing for those types
   and keep them a manual tag.
3. **Find labelled sources for the hi-hat, tambourine and ride family**, which is where the confident
   mistakes live. The one-shot percussive dataset does not carry those distinctions, and neither does
   FSD50K mapped by keyword.
4. **Retrain only on a wider base.** One install produced almost all the schema-2 types, and that
   install is the gold sample. Nothing should move into `resources/` from this export alone.

## Reproducing

```bash
node tools/contribution-sync.mjs            # writes data/contributions/latest (git-ignored)
cd services/training
node evaluate-shipped.mjs --out out/heldout --doc ../../docs/AUTODETECT_HELDOUT.md
```

The tool writes `out/heldout/summary.json` (every table and interval) and the report above. It exits 0
when both are written: the numbers are the owner's decision, not a gate.
