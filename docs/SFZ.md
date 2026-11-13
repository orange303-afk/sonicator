# SFZ instruments (`.sfz`)

How Sonicator reads text SFZ instruments. Only the playback-relevant part of the format is
interpreted: samples, key and velocity ranges, root key, tuning, volume, pan and loops. Filters,
envelopes, LFOs, effects and MIDI rules are ignored, in the same spirit as the VAST part of a
Kurzweil program. Files are never written.

Sources of the layout (format knowledge, no code copied):

- SFZ format reference — <https://sfzformat.com/opcodes/> (opcode names and value ranges).
- Chicken Systems Translator output found in the library: the concrete section order and the
  unquoted sample paths with spaces were verified against those files, not assumed.

## What is read

A file is a sequence of sections followed by `opcode=value` pairs. Sections used:

| Section | Meaning in Sonicator |
|---|---|
| `<control>` | File defaults (`default_path`) |
| `<master>` / `<global>` | Defaults inherited by every following group |
| `<group>` | Defaults inherited by every following region |
| `<region>` | One playable zone |

Other sections (`<curve>`, `<effect>`, `<midi>`) are skipped. Section names and opcode names are
case-insensitive. `//` comments are removed outside quotes. Values may be quoted; Chicken Systems
files also write unquoted sample paths with spaces (`sample=..\..\One Shots\Snare_01.wav`), so a
space ends a value only when the next token is another `opcode=value` pair.

Inheritance is master → group → region. A new `<master>`/`<global>` replaces the master defaults and
clears the current group; a new `<group>` clears only the group; every `<region>` starts from a copy
of master plus group. `$name=value` definitions are substituted into later values, including
`${name}`.

## Opcodes mapped to `ExternalZone`

| Opcode | Mapping |
|---|---|
| `sample` | Resolved against the file's folder and `default_path` |
| `lokey`, `hikey` | `keyLow`, `keyHigh` (note names such as `C#3` are accepted) |
| `key` | Sets the root key; also sets both keys when `lokey`/`hikey` are absent |
| `pitch_keycenter` | `rootKey` |
| `lovel`, `hivel` | `velocityLow`, `velocityHigh` |
| `tune`, `transpose`, `finetune` | `tuningCents` = tune + 100 × transpose + finetune |
| `volume` | `attenuationDb` = −volume (SFZ volume is dB gain) |
| `pan` | `pan` = pan / 100, clamped to −1…1 |
| `offset` | `start`, in frames |
| `end` | Last frame played, **inclusive**; internal `end` is `end + 1` |
| `loop_start`, `loop_end` | Frames, `loop_end` inclusive |
| `loopmode` / `loop_mode` | `one_shot`, `loop_continuous`, `loop_sustain` |

A loop is enabled when `loopmode` is `loop_continuous`/`loop_sustain`, or when `loopmode` is absent
and loop points are present. `one_shot` sets `oneShot` so NoteOff does not cut the tail. Missing
`end`/loop bounds fall back to the decoded sample length. Out-of-range values are clamped; a zone
whose `offset` starts at or past the end of its sample is dropped with a warning naming the sample.

## Sample resolution

`sample` is normalised (`\` → `/`) and tried:

1. as an absolute path, when `default_path` is absolute;
2. under the instrument folder and each of its parents (up to six levels), with `default_path`
   appended and without it — this covers the common library layout where `Programs/…/Kit.sfz`
   refers to a sibling `Multisamples/…/Samples` folder;
3. otherwise a bounded basename search (depth 4, 20 000 entries) from the same folders, accepting
   only one match per folder. Two matches are an explicit ambiguity, never a guess.

A missing sample is listed in `FileInstrument::warnings` by its stored name and the instrument is
marked `partial`; the remaining zones still play. If no zone can be built, loading fails with the
warning list as its error, so a broken instrument never opens as silence.

## Not implemented

- `#include`, `$`-macro path fragments beyond plain substitution, and built-in `*sine` samples.
- Filters, envelopes, LFOs, effects and MIDI CC bindings (no audible claim).
- Extract: SFZ points at external WAVs, so there is nothing embedded to export.
- Writing `.sfz`.

## Correlation with the library (2026-11-13)

`SONICATOR_SFZ_DIR=/Volumes/1TBSSD/samples` (read-only):

- 4 105 `.sfz` files found, **4 100 load** (99.88 %), 116 497 zones.
- 5 rejected, all for one honest reason: the referenced WAV in this library is a shortened copy,
  so the region's `offset` starts at or past the end of the audio. The files are
  `16 Violins Sim Bow.sfz`, `Extrinsic.sfz`, `Pipe Organ.sfz`, `Voice Organ 1.sfz` and
  `Voice Organ 2.sfz` (Virtuoso 2000 / Proteus 2000 / Mo' Phatt sets).
- The `Samplephonics - Dub Techno Progressions` pack (5 files) loads 5/5; the whole `e-mu`
  subtree loads 3 487/3 492.

The unit tests cover two-region maps with velocity bands and inherited master/group values,
nested `default_path` with backslashes, `key` as a root, loop and one-shot modes, missing samples,
partial kits, preview, catalogue rows and the sampler. Run them with:

```sh
cmake --build --preset release --target SonicatorTests
~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[sfz]"
```

The corpus check is opt-in:

```sh
SONICATOR_SFZ_DIR="/Volumes/1TBSSD/samples" \
  ~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[.external][sfz-corpus]"
```

## Integration

- `FileInstrumentFormat::sfz` dispatches to `core/sfz/SfzInstrument.cpp`; the model is the same
  `FileInstrument` used by EXS24, NKI and Kurzweil, so `ExternalKitSampler`, MIDI preview, the
  waveform and source list work unchanged.
- `.sfz` is a metadata-only catalogue format; `AssetAnalyzer` records `SFZ` after checking the text
  for a section marker, so a renamed non-instrument gets an invalid row with a reason.
- No Extract entry: the samples are external files and already appear in the library on their own.
