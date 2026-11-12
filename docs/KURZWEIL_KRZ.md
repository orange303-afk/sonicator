# Kurzweil K2000/K2500/K2600 banks (`.krz`, `.k25`, `.k26`)

How Sonicator reads Kurzweil object banks. Everything is big-endian (the K-series is a
Motorola 68000 platform). Only read-only access is implemented; banks are never written.

Sources of the layout (format knowledge, no code copied):

- Geoffrey Mayer, «KurzFile» notes — `*-Keymap.txt`, `*-Sample Header.txt`,
  `*-Program.txt` (mirror: <https://pdfcoffee.com/kurzfile-4-pdf-free.html>).
- acidcat / hed0rah, «krz anatomy» — <https://hed0rah.github.io/audio_files_anatomy/krz-anatomy.html>.
- Chicken Systems: `.krz`, `.k25`, `.k26` are the same format.
- Kurzweil support pages for the K2000 (`kurzweil.com/k2000`): the factory ROM samples are
  not distributed in any download; only programs and keymaps are.

## File header (32 bytes)

| Offset | Field | Meaning |
|---|---|---|
| 0 | magic | `PRAM` (object bank) or `SROM` (effects/sample-ROM container, body undocumented) |
| 4 | `osize` u32 | absolute byte offset where the PCM region begins; also the end of the object section. `osize == file size` means a bank with no sample data |
| 8 | reserved | not interpreted |
| 16 | OS version u32 | `208` = v2.08 |
| 20 | reserved | not interpreted |

`SROM` files are recognised, indexed, and refused for playback and Extract with a reason.

## Object section (from `0x20` to `osize`)

A run of blocks. Each block starts with a **negative** i32 `blocksize`; the next block starts at
`blockStart - blocksize`. An i32 `0` ends the object section.

Inside a block, objects follow each other, word-aligned:

| Offset in object | Field |
|---|---|
| 0 | object size u16 (from the hash to the end of the body) |
| 4 | hash u16: `type = hash >> 10`, `id = hash & 0x3FF` |
| 6 | body size u16 |
| 8 | `nameOfs` u16: distance from this field to the body, so **body = objectStart + 8 + nameOfs** |
| 10 | ASCII name, null-terminated, padded to a word boundary |

Types used: **36 program**, **37 keymap**, **38 sample**. All other types are skipped by their size.

## Sample object (type 38)

Body: 12-byte Sound Block, then `numHeaders` 32-byte Sound File Headers.

Sound Block: `baseId` u16 (normally 1), `numHeaders - 1` u16 at 2, `headersOfs` u16 at 4
(8 → the first header sits at body + 12), flags at 6 (**bit 0 = stereo**).

Sound File Header, offsets from the header start:

| Offset | Field | Meaning |
|---|---|---|
| 0 | root key u8 | MIDI note of the recorded pitch |
| 1 | flags u8 | **bit `0x80` inverted: set = NOT looped.** `0x70` loops, `0xF0` is one-shot |
| 2 | volume adjust i8 | × 0.5 dB |
| 3 | alt volume i8 | not used |
| 4 | max pitch u16 | not used |
| 6 | name offset u16 | not used |
| 8 | start u32 | absolute **word** offset into the PCM region |
| 12 | alt start u32 | not used |
| 16 | loop start u32 | word offset, looped samples only |
| 20 | sample end u32 | last word of the span. For a looped sample this is the **loop end**, not the audio end |
| 24 | envelope offsets | not used |
| 28 | sample period u32 | `rate = round(1e9 / period)` nanoseconds |

Audio end of a sample span: the smallest `start` of any other header that is greater than this
header's `start`, else the end of the PCM region; converted with `-1` to keep `end` inclusive.

## Keymap object (type 37)

Header 28 bytes: `defaultSampleId` u16 (0 for multisamples), `method` u16 at 2, `basePitch` u16 at 4,
`centsPerEntry` u16 at 6, `entries - 1` u16 at 8, `entrySize` u16 at 10, then eight u16 offsets
at 12, one per velocity level ppp, pp, p, mp, mf, f, ff, fff; each offset is measured from its own
field to the first entry of that level.

**`entrySize` is the stride.** Entry layout by `method` (verified on the corpus: every combination
below reproduces the stored `entrySize`):

| method | tuning | volume | sample id | sub-sample | size |
|---|---|---|---|---|---|
| 0x01 | — | — | — | u8 | 1 |
| 0x03 | — | — | u16 | u8 | 3 |
| 0x05 | — | i8 | — | u8 | 2 |
| 0x07 | — | i8 | u16 | u8 | 4 |
| 0x09 | i8 | — | — | u8 | 2 |
| 0x0B | i8 | — | u16 | u8 | 4 |
| 0x0D | i8 | i8 | — | u8 | 3 |
| 0x0F | i8 | i8 | u16 | u8 | 5 |
| 0x11 | i16 | — | — | u8 | 3 |
| 0x13 | i16 | — | u16 | u8 | 5 |
| 0x15 | i16 | i8 | — | u8 | 4 |
| 0x17 | i16 | i8 | u16 | u8 | 6 |
| 0x19 | i16 | — | — | u8 | 3 |

Fields are packed in that order from the entry start. Tuning is `semitone × 100 + cents`, so the
stored value already **is** cents. Volume is in 0.5 dB steps. A method without a sample-id field
uses the keymap's `defaultSampleId`.

**Key entry `i` maps MIDI note `i + 12`** (entry 0 = C0). Corpus evidence: with this offset the
median distance between a sample's root key and the centre of the keys mapped to it is 4.5
semitones (vs 10.0 without); 158 of 402 sample spans sit within 3 semitones of their root (vs 6).
Entries past MIDI 127 are dropped.

Velocity levels: level `L` (0 = ppp … 7 = fff) plays MIDI velocity `[L*16, L*16 + 15]`.

## Program object (type 36)

A stream of tagged segments: one tag byte, then a fixed number of data bytes keyed by
`tag & 0xF8`; `0x0F` (FX) is the exception at 7 bytes. `int16 0` ends the stream.

| Tag | Segment | Data bytes |
|---|---|---|
| 0x08 | PGM | 15 (byte 1 = synth mode, byte 2 = layer count) |
| 0x09 | LYR | 15 |
| 0x0F | FX | 7 |
| 0x18 | FUN | 3 |
| 0x10 | ASR | 7 |
| 0x14 | LFO | 7 |
| 0x20 | ENC | 15 |
| 0x40 | CAL | 31 |
| 0x50 | HOB | 15 |
| 0x68 | KDFX | 7 |
| 0x78 | KB3 | 31 |

Each layer contributes one LYR and one CAL. CAL holds the keymap links: **keymap 1 = u16 at
data offset 11, keymap 2 = u16 at data offset 7** (0 = none). Everything else in a program is VAST
and is not interpreted.

**Key range.** LYR bytes 3 and 4 look like a key range (`12,108`; `36,96`; `84,88` appear in the
corpus) but the layout is undocumented, so the reader does **not** use them: the playable range of
a layer comes from the keys the keymap actually maps to a sample. A layer whose keymap gates a
full-range map to a subrange would therefore play outside that subrange; this is accepted.

## PCM region

Signed 16-bit big-endian words from `osize` to the end of the file. Byte address of a word
`w` is `osize + 2w`.

For a **continuation part** (`<name>.kr2.krz`, `.kr3.krz`, …) the PCM starts at byte `0x24`
regardless of the `osize` written in its header (a continuation copies the first part's `osize`;
`moog1.kr2.krz` carries `6540` while its data starts at `0x24`). Only the first part carries
the objects. The PCM region of a set is the concatenation of every available part's data.

A part beyond the first is never shown as its own library row.

## Corpus findings (2026-11-13)

`/Volumes/1TBSSD/samples/_Kurzweil/Kurzweil K2000 KRZ` — 238 files:

- 234 `PRAM`, 4 `SROM` (`chorus.krz`, `flangefx.krz`, `gatedvrb.krz`, `verbhall.krz`).
- 205 banks with sample data, 33 with objects only (factory-ROM presets).
- 23 files are `.kr1.krz`/`.kr2.krz` parts of 12 sets; all pairs present.
- sample rates from `round(1e9 / period)` fall in 8 000–96 000 Hz: 44101 (255 headers),
  27201, 25000, 31301, 29800, 31244, 40016, 39101, 40000, 44601, 35726, 22505 …
- keymap methods in use: 0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F, 0x11, 0x13,
  0x15, 0x17, 0x19.

Official Kurzweil archives (`kurzweil.com/k2000`, downloaded to
`/Volumes/1TBSSD/samples/_Kurzweil/Factory`; not part of the repository) — 138 `.krz`/`.k25`
files: 137 PRAM, 1 SROM, 23 with sample data, 114 presets only.

## Extract and playback behaviour

- A bank plays through `FileInstrument`/`ExternalKitSampler`; each program is a program of the
  instrument, and a stereo sample becomes two zones panned left and right.
- Extract writes one WAV per sample header (one stereo WAV for a stereo sample object), with the
  root key and the loop in the `smpl` chunk. Two mono samples paired only by a program layer are
  written as two files.
- Banks with no sample data report that they use the K2000 factory ROM samples and cannot play.
