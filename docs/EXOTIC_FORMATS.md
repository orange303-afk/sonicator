# Exotic formats in the sample library: inventory and feasibility

Date: 2026-11-13. Source: a full walk of `/Volumes/1TBSSD/samples` (≈1.1M files) with extension
and magic-signature sampling; counts and sizes are from that walk. This document is a study, not a
plan: nothing here is implemented.

Formats Sonicator already reads: WAV/AIFF/FLAC/MP3/OGG, Kurzweil `.krz/.k25/.k26`, E-mu `.ebl`,
SF2, EXS24, NKI (external samples), REX2, MOD/XM/S3M, disc images (ISO/BIN/CUE), Reason `.repatch`.

## Inventory (what is actually in the library)

| Extension | Files | Size | What it is | Audio inside |
|---|---:|---:|---|---|
| `.xpm` | 2 695 | 2.35 GB | Akai MPC program (XML, `<SampleName>` / `<SampleFile>` per pad) | no, references WAVs by name |
| `.xi` | 2 621 | 186 MB | FastTracker II instrument | **yes, PCM** |
| `.voc` | 2 090 | 199 MB | Creative Voice File (DOS) | **yes, PCM + Creative ADPCM** |
| `.sfz` | 4 105 | 20 MB | SFZ text instrument (`<region> sample=…`) | references WAVs |
| `.akp` | 2 394 | 7.6 MB | Akai S5000/S6000 program (RIFF `APRG`) | references samples |
| `.sxt` | 481 | 2.3 MB | Reason NN-XT patch (`FORM/PTCH`, `REFS` paths) | references WAVs |
| `.smp` | 589 | 649 MB | several unrelated old formats (`SOUND SAMPLE`, raw 8-bit) | sometimes |
| `.snd` | 187 | 3.8 MB | headerless 8-bit DOS samples | yes, no header |
| `.iff` | 18 | 1.2 MB | Amiga IFF `8SVX`/`16SV` | **yes, PCM with loops** |
| `.exb` | 18 | 278 MB | E-mu bank (E5B0, samples + presets) | **yes, PCM** |
| `.spitfire` | 25 | 2.1 GB | Spitfire Audio LABS container (own header + offset table) | probably raw PCM |
| `.nkx` | 25 | 34.9 GB | Kontakt monolith (samples inside) | **yes, PCM** |
| `.nkr` | 25 | 90 MB | Kontakt resource container | samples for `.nkx` |
| `.nkm` | 82 | 13 MB | Kontakt multis | references instruments |
| `.pgm` | 13 | 0.1 MB | MPC1000 program | references WAVs |
| `.mdl` | 80 | 7.5 MB | DigiTracker module | **yes, PCM** |
| `.669` / `.dsm` | 14 | 0.8 MB | tracker modules | **yes, PCM** |

Also present and **not** samples: `.ens`/`.ism` (Reaktor, 6 GB), `.nabs`/`.glo` (Absynth),
`.nfm8`/`.ksd`/`.nmsv`/`.fxp`/`.vstpreset` (synth presets), `.syx` (MIDI dumps), `.cmf`/`.rol`
(Ad Lib music), `.com` (DOS executables), `.dat` (µTorrent part files), `.asd` (Ableton
analysis sidecars). The `_rx7`, `_sp12`, `_linn`-style "extensions" from the raw filename scan are
artefacts of folder names, not formats.

## Candidates, by value and effort

### 1. SFZ — 4 105 instruments, cheapest real coverage

*Status 2026-11-13: implemented — see `docs/SFZ.md` (4 100/4 105 load).*

`<region> sample=… lokey=… hikey=… lovel=… hivel=… pitch_keycenter=… loop_start=… end=…` maps
directly onto `ExternalZone`. It is the same shape of work as the existing NKI reader: parse text,
resolve relative paths (backslashes and `..\..` are common), decode WAVs into `SampleAudio`,
build zones. Reuses `FileInstrument`, `ExternalKitSampler`, preview, catalogue, drag.
Risks: `<master>`/`<group>` inheritance, `$`-variables, `default_path`, missing samples. Effort:
one stage, comparable to NKI. **Recommended next format.**

### 2. XI + the remaining tracker family — 2 700 files of PCM

FastTracker II `.xi` carries one instrument with keymap, loop and 8/16-bit PCM. The project has
its own MOD/XM/S3M readers, so `.xi` needs a fourth reader but shares `TrackerDocument`,
`TrackerSongRenderer` (libxmp renders modules only, XI is a standalone instrument and would play
through the existing sample path instead). `.mdl`, `.669`, `.dsm` are modules; libxmp already
renders such formats, so they are a matter of probing and feeding the same renderer.
Effort: `.xi` medium (one reader), the other modules low. **Recommended second.**

### 3. VOC — 2 090 audio files

`Creative Voice File` blocks: 8-bit, 16-bit and Creative 2/3/4-bit ADPCM, plus silence and repeat
blocks. A reader is a few hundred lines; the sample becomes a normal audio source through the same
format-manager registration as EBL. Effort: one stage, small. **Cheap win, vintage material.**

### 4. Sampler programs that point at WAVs the library already has

`.xpm` (MPC), `.akp` (Akai S5k/S6k), `.sxt` (NN-XT), `.pgm` (MPC1000). Each is a program that
names samples; the WAVs are already in the library, so the work is mapping + path resolution +
catalogue integration, not decoding.
- `.xpm` — XML, `<SampleName>`/`<SampleFile>` per pad; the file also embeds a large JSON pad
  blob, which explains its size. Mapping name → WAV is fuzzy (names repeat across packs).
- `.akp` — RIFF `APRG` with documented chunks; Akai sample names inside a S5000 set.
- `.sxt` — `FORM/PTCH` with an `REFS` table of folder/file names; simplest of the four.
- `.pgm` — MPC1000, small, same idea.
Effort: medium per format, mostly resolution rules. Value: kits and multi-samples, not one-shots.

### 5. E-mu `.exb` — 18 files, 278 MB of PCM

The bank that wraps the `.ebl` samples (and the presets/zones). The EBL reader and its
`docs/format` knowledge already exist; `samplerdisc` does not read `.exb`, so it is a
reverse-engineering job of moderate size. Worth it only for those 18 banks.

### 6. Amiga IFF `8SVX`/`16SV` — 18 files

`FORM/8SVX` + `VHDR` (rate, loop start/end, octaves) + `BODY`. Tiny reader, real loops, but the
material is 1.2 MB in total. Do it only as a side effect of a general "classic formats" pass.

### 7. Content sniffing for damaged names

The library has ~100 audio files whose extension was truncated by a broken archive
(`.wa_`, `.ex_`, `.mi_`, `.vb_`, `.st_`, `.hl_`, `.dl_`). RIFF/FORM magic would recover them
without a per-format reader. Small, generic, improves any future format work.

### 8. Kontakt monolith (`.nkx`/`.nkr`) — 35 GB, research only

The single largest pool of samples in the library, and the least suitable for a first pass: the
container is undocumented, NKI support today only resolves *external* samples, and the format
carries Kontakt-specific zone/script data. A reader would be a stage of its own with a real risk
of never being correct. Treat as a study item, not a plan.

### 9. Spitfire LABS `.spitfire` — 2.1 GB

Header `Spitfire` + a table of sample records with offsets; likely raw PCM after the table.
Proprietary, 25 files. Investigate only if the user wants that specific library.

### Not worth a reader

`.smp`/`.snd`/`.sam` (several headerless old formats, ambiguous 8-bit, low value), `.ens`/`.ism`
(Reaktor ensembles, not samples), `.cmf`/`.rol`/`.com` (DOS music and executables), `.nfm8`/
`.ksd`/`.nmsv`/`.fxp`/`.vstpreset`/`.nabs`/`.glo`/`.kpe` (synth presets), `.syx` (MIDI dumps),
`.dat` (torrent parts).

## Suggested order

1. **SFZ** — biggest real coverage for the least new machinery.
2. **VOC** — small reader, 2 090 playable files, no preset logic.
3. **XI**, then `.mdl`/`.669`/`.dsm` — completes the tracker story.
4. **NN-XT `.sxt`**, then MPC `.xpm` / Akai `.akp` / MPC1000 `.pgm` — kits from existing WAVs.
5. **IFF 8SVX/16SV** and content sniffing — small polish items.
6. **`.exb`**, **Spitfire**, **Kontakt monolith** — only on request, after the above.
