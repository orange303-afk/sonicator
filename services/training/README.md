# Training stage

Builds `resources/ml/global_prototypes.json`, the file of learned labels that ships inside the app, and
measures what that file actually gets. Node only: no dependencies, no Python, no network calls at build
time.

## Input

A JSONL export of contributed records:

```bash
cd services/contribute-worker
node scripts/export.mjs --out out/contribute-export.jsonl
```

Each line carries `recordId`, `installId`, `featureSchema` (currently 1), 14 feature values in 0..1, an
optional `type` and optional `tags`.

## Commands

```bash
cd services/training
npm test                                              # node:test, no installs needed
node build-prototypes.mjs --in <export.jsonl>         # writes ../../resources/ml/global_prototypes.json
node evaluate.mjs --in <export.jsonl> --cv 5          # writes training-report.md
node candidates.mjs --in <export.jsonl>               # standard-tag candidates for the owner
node generate-credits.mjs                             # writes ../../docs/DATA_CREDITS.md
```

`build-prototypes.mjs` needs at least 3 installs and 10 records per label before a label ships; with
less data it writes a valid file with no labels, and the app behaves as it does without it.

`evaluate.mjs` reports two things:

- leave-one-install-out, local + global, which is the stage-2 gate (after ≥ 60% and not worse than
  before);
- `--cv N`: what the shipped prototypes alone get, with the record under test never part of the model
  that scores it. This is the number to watch when changing the assembly.

## How prototypes are built

Weighted k-means over each label's records, up to 8 centres per label. Every install carries the same
total weight inside a label, so a large library cannot fill every prototype with its own material. The
centres are stored as plain means, so every value stays in 0..1 and the app's loader, its cosine
similarity and `featureSchema 1` are unchanged.

Measured on the owner's export of 2026-11-11 (4526 records, 1 install, 13 types), five folds:

| Assembly | Shipped prototypes, 5-fold CV |
|---|---|
| Farthest-first with medoid refinement (until 2026-11-12) | 57.1% |
| Weighted k-means centres | 65.7% |

The local path (every confirmed row of the person's own library is a prototype, no clustering) reaches
80.5% on the same records, which is the ceiling the shipped file is compared against.

## Dataset feature extraction

The datasets and the app must produce the same numbers, so both use one implementation:
`core/dsp/FeatureExtractorV2`. The tool is off by default:

```bash
cmake --preset release -DSONICATOR_BUILD_TRAINING_TOOLS=ON
ninja -C ~/Library/Caches/sonicator-build/release sonicator-features
~/Library/Caches/sonicator-build/release/tools/sonicator-features/sonicator-features_artefacts/Release/sonicator-features \
    --in /Volumes/1TBSSD/Datasets/nsynth/audio --walk --out out/nsynth.jsonl
```

It writes one JSON line per file: `path`, `featureSchema` (2), `dimensions` (84), `sampleRate`,
`durationSec`, `nyquistHz` (the file's limit, not the content's band), `inBandEnergyShare` (how much of
the sound falls inside the 40–8000 Hz analysis band; near zero for a hi-hat that lives above 8 kHz) and
`features`. Analysis uses the first 10 seconds (`readAnalysisAudio`),
so a longer clip is represented by its start. Files that fail (missing, unreadable, non-finite samples)
are reported on stderr and skipped.

The layout, the 40 Hz–8 kHz analysis band and the known resolution limits are documented at the top of
`core/dsp/FeatureExtractorV2.h`. Labels are not part of the line: they come from the dataset's own
metadata, which the ingest step will read.

## Datasets and licences

`datasets/manifest.json` lists every public dataset with its licence and a tier:

- **A** CC0 / CC BY, **B** NC, SA, ND or research-only, **C** not usable (ML training prohibited, or
  audio without an explicit licence).
- Levels A and B are used; level C is never used. `validateManifest` refuses a level C entry marked as
  used.
- `docs/DATA_CREDITS.md` is generated from the registry and committed; `THIRD_PARTY_NOTICES.md` points
  at it.

A future plan adds `featureSchema 2`, a feature extraction CLI for the datasets above and `--max-tier`
for rebuilding without level B sources.

## Stage 5: gold sample, ablation and model choice

The v2 path is separate from the schema-1 prototypes above: `v2/` holds the feature groups, the shared
scaler and the two candidate models, and `train.mjs` drives them.

```bash
# 1. one read-only pass over the owner's catalog, outside the repository
~/Library/Caches/sonicator-build/release/tools/sonicator-features/sonicator-features_artefacts/Release/sonicator-features \
    --catalog "$HOME/Library/Sonicator/sonicator.sqlite" \
    --out "$HOME/Library/Application Support/Sonicator/training/gold-v2.jsonl" \
    --paths "$HOME/Library/Application Support/Sonicator/training/gold-v2.paths.jsonl"

# 2. train, ablate, evaluate and write the report
node train.mjs --public out/wave1/features.jsonl \
    --gold "$HOME/Library/Application Support/Sonicator/training/gold-v2.jsonl" \
    --out out/model-v2 --doc ../../docs/TRAINING_REPORT_V2.md
```

The gold file never carries a path (only `gold-v2.paths.jsonl` does, locally). The model candidate
stays in `out/model-v2/`; `resources/` is not touched. `train.mjs` exits 0 once it has written both
files, even when a criterion fails: the numbers are the owner's decision.

### Stage 5b: one scale for local and public evidence

A raw local cosine and a softmax probability are not comparable, so both are mapped to the precision
they actually carry (`v2/calibration.mjs`). The local map is fitted with five folds cut by
`recordId`, so two copies of the same audio never train the model that scores the other copy; the
public map is fitted on the public `val` split. The app's 0.80 acceptance threshold then means
"about 80% correct" for either source, the 0.95 penalty and 0.02 margin compare like with like, and
the report shows the whole coverage → precision curve instead of one operating point. Mode (1) is
reported at the same reading, which is why its coverage is no longer 100%.

### Wave 2 (2026-11-12): ESC-50 and UrbanSound8K

Two tier-B datasets were added: ESC-50 (2000 clips, folds 1–3/4/5 → train/val/test) and UrbanSound8K
(8732 clips, official folds 1–8/9/10). Both are NC-licensed, which the manifest allows for a free
non-commercial app. UrbanSound8K contributes **no typed labels** (its classes are events, not
instruments), so it only feeds the group/domain columns. ESC-50 contributes `clap` (40 clips) and
`vocal` (240).

Measured on the same gold sample and the same `train.mjs`:

| metric | wave 1 | wave 1 + wave 2 |
|---|---|---|
| mode (1) top-1 | 76.8% | 76.8% |
| mode (4) top-1 | 78.1% | 78.1% |
| mode (4) precision at acceptance | 87.2% | 87.6% |
| clap top-1 | 65.1% | 70.2% (218 rows) |
| cold start top-1 | 73.0% | 72.7% |

The clap gain is the only visible one; everything else moved within 0.4 pp and the criteria did not
change. SONYC-UST (14 GB) and Slakh2100-redux (104 GB) were not fetched: their labels (engine, siren,
bass, keys, guitar) do not cover the gold types that fail (`fx`, `impact`, `riser`, `synth`), so the
download would cost hours for the same tenths of a point.

### Rejected: FSD50K sound-effect classes as `fx`/`impact`

FSD50K already on disk has classes that look like sound design: `Slam`, `Thump_and_thud`, `Boom`,
`Shatter`, `Whoosh_and_swoosh_and_swish`, `Crackle`, `Crushing`, `Hiss`, `Crack`, `Buzz`, `Screech`,
`Scratching`. Mapping them to `impact` (~1000 clips) and `fx` (~2150) was tried and reverted:

| metric | before | with the mapping |
|---|---|---|
| cold start top-1 | 72.7% | 64.4% |
| mode (3) `fx` top-1 | — | 9.3% |
| mode (4) top-1 | 78.1% | 76.1% |
| percussion top-1 (4) | 82.5% | 79.8% |

The public `fx` cluster is a different family of sounds from the owner's `fx` one-shots, and the new
labels pulled multi-label clips out of `kick`/`snare`. The mapping stays out; the experiment is kept
here so it is not repeated. This also means the wave-2 datasets have no source for `riser`,
`downfilter` or `synth` — those need a labelled source the public datasets do not provide.
