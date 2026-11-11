# Data credits

Generated from `services/training/datasets/manifest.json` by
`services/training/generate-credits.mjs`; do not edit by hand.

## Datasets used for the shipped model and its evaluation

| id | Title | Version | Licence | Level |
| --- | --- | --- | --- | --- |
| bbc-sfx | BBC Sound Effects | archive | RemArc licence, non-commercial use only | B |
| enst-drums | ENST-Drums | 1.0 | research user licence | B |
| esc50 | ESC-50 | 1.0 | CC BY-NC 4.0 | B |
| freesound-one-shot-percussive | Freesound One-Shot Percussive Sounds | 1.0 | per-clip Creative Commons: CC0, CC BY, CC BY-NC | B |
| fsd50k | FSD50K | 1.0 | per-clip Creative Commons: CC0, CC BY, CC BY-NC, CC Sampling+ | B |
| idmt-smt-drums | IDMT-SMT-Drums | 1.0 | CC BY-NC-ND 4.0 | B |
| medley-solos-db | Medley-solos-DB | 1.0 | research use only | B |
| medleydb | MedleyDB | 2.0 | CC BY-NC-SA 4.0 | B |
| nsynth | NSynth | 1.0.3 (full) | CC BY 4.0 | A |
| philharmonia | Philharmonia Orchestra Sound Samples | web release | CC BY-SA 3.0 | B |
| slakh2100 | Slakh2100 | 2100 | CC BY 4.0 | A |
| sonyc-ust | SONYC-UST | 2.0 | CC BY 4.0 | A |
| urbansound8k | UrbanSound8K | 1.0 | CC BY-NC 3.0 | B |

## Attribution

Licences that require attribution name the work as follows.

- BBC Sound Effects, RemArc licence, non-commercial research and education use — archive, https://sound-effects.bbcrewind.co.uk/
- ENST-Drums (Gillet and Richard, 2006), Telecom ParisTech, research use — 1.0, https://www.tsi.telecom-paristech.fr/aao/en/2010/07/12/enst-drums-an-extensive-percussive-multitrack-dataset/
- ESC-50 (Piczak, 2015), CC BY-NC 4.0 — 1.0, https://github.com/karolpiczak/ESC-50
- Freesound One-Shot Percussive Sounds (Caillon and Esling, 2020), Zenodo, per-clip Creative Commons licences; authors in licenses.json — 1.0, https://zenodo.org/search?q=Freesound%20One-Shot%20Percussive%20Sounds
- FSD50K (Fonseca et al., 2022), MTG-UPF, Zenodo, per-clip Creative Commons licences; authors in dev_clips_info_FSD50K.json and eval_clips_info_FSD50K.json — 1.0, https://zenodo.org/records/4060432
- IDMT-SMT-Drums, Fraunhofer IDMT, CC BY-NC-ND 4.0 — 1.0, https://www.idmt.fraunhofer.de/en/business_units/m2d/smt/drums.html
- Medley-solos-DB (Lostanlen et al., 2019), research use, derived from MedleyDB — 1.0, https://zenodo.org/search?q=Medley-solos-DB
- MedleyDB (Bittner et al., 2014), NYU, CC BY-NC-SA 4.0 — 2.0, https://medleydb.weebly.com/
- NSynth (Engel et al., 2017), Google Magenta, CC BY 4.0 — 1.0.3 (full), https://magenta.tensorflow.org/datasets/nsynth
- Philharmonia Orchestra Sound Samples, CC BY-SA 3.0 — web release, https://philharmonia.co.uk/resources/sound-samples/
- Slakh2100 (Manilow et al., 2019), MERL and Northwestern University, CC BY 4.0 — 2100, https://www.slakh.com/
- SONYC-UST (Bello et al., 2019), NYU, CC BY 4.0 — 2.0, https://zenodo.org/communities/sonyc/
- UrbanSound8K (Salamon et al., 2014), NYU, CC BY-NC 3.0 — 1.0, https://urbansounddataset.weebly.com/urbansound8k.html

## Not used

- **AudioCaps** (audiocaps), level C: YouTube audio, no explicit licence
- **AudioSet** (audioset), level C: YouTube audio, no explicit licence
- **Sonniss GDC Game Audio Bundle** (sonniss-gdc), level C: ML training prohibited by the licence
- **WavCaps** (wavcaps), level B: not needed; FSD50K, BBC Sound Effects and Freesound already carry its sources
