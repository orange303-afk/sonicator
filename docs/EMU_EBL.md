# E-mu Emulator X samples (`.ebl`)

Sonicator reads `.ebl` sample files from E-mu Emulator X / Proteus X banks as ordinary library
audio: they are scanned, analysed, previewed, shown as waveforms, played through MIDI preview,
exported and dragged to a DAW. The format is documented here from the independent MIT-licensed
[samplerdisc](https://github.com/bmxcode/samplerdisc) specification; no samplerdisc code is
compiled into or called by Sonicator, and Python is not part of the build or of the default tests.

## What a bank is

A bank is a folder, not a disc image:

```
Proteus 1.exb/
    Proteus 1.exb        bank definition: presets and zones (not read)
    SamplePool/
        Proteus 1SL001.ebl
        ...
```

Each `.ebl` holds one sample. The `.exb` definition describes playable instruments and is a
separate task; Sonicator indexes the samples, not the presets.

## Layout

Big-endian outer IFF headers, little-endian data fields, signed 16-bit little-endian PCM.

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | `FORM` |
| 0x04 | 4 | file size − 8, big-endian |
| 0x08 | 8 | `E5B0TOC2` |
| 0x10 | 4 | 78, big-endian |
| 0x14 | 4 | `E5S1`, first section |
| 0x18 | 4 | section size |
| 0x1C | 4 | absolute offset of the second section, big-endian |

The header before the second section is variable-width, so its offset is read from `0x1C` and
never assumed. The second section is 14 bytes; a 176-byte data-description block follows it.

| Offset in block | Size | Field |
|---|---|---|
| 0 | 64 | sample name, UTF-16LE, space/NUL padded |
| 68 | 4 | V2, audio anchor: `audio_start = block + V2 − 4` |
| 72 | 4 | V3, per-channel byte length (`V3 − V2`) |
| 100 | 4 | sample rate |
| 108 | 4 | V12: channel byte at `(V12 >> 16) & 0xFF` |
| 112 | 64 | comment, UTF-16LE |

Rules the decoder follows, all verified against the real banks below:

- **Channels** are stereo only when the channel byte equals `0x03`. `0x01` and the less common
  `0x02` are mono; testing for "not mono" would misclassify the `0x02` files as stereo.
- **Audio start** is `block + V2 − 4`, so the pad before the PCM may be 4 or 8 bytes and is not
  silently assumed.
- **Audio end** is the `EXLZ` loop-trailer offset when the trailer carries a complete `MARK`,
  otherwise the end of the file. Reading to EOF regardless is one frame long on every looped file.
  The length is rounded down to whole 16-bit samples.
- **Stereo** is stored non-interleaved (`LLLL…RRRR`), each block `V3 − V2` bytes. The left block
  starts at `audio_end − 2·(V3 − V2)`: the start is anchored from the **end**, because the front
  anchor is two bytes late on some banks. A truncated file whose from-the-end start would land
  inside the description block falls back to splitting the front-anchored span in half, losing the
  tail rather than the head.
- **Loop**: at `MARK + 8` and `MARK + 12`, little-endian frames, end exclusive. A loop failing
  `0 ≤ start < end ≤ frames` is dropped. The drag-to-DAW WAV writes it into the RIFF `smpl` chunk
  with an inclusive end.
- **Refusals** are explicit, never crashes: not a `FORM`, no `E5B0TOC2`, first section not `E5S1`,
  second section not where the header says, description block past EOF, zero sample rate, no audio,
  or more than 128 MiB decoded. Every read is bounds-checked.
- `.ebl` files are opened read-only and never modified.

## In the application

- `EblAudioFormat` registers in the shared `registerSonicatorFormats()` alongside JUCE's basic
  formats, so every reader path already used for WAV/AIFF (scan analysis, smart analysis, waveform,
  preview, MIDI, selection export, Split) gains `.ebl` without format-specific code.
- Catalog rows use the sample name from the header (`org nod C +36`), not the bank's sequence name
  (`Orbit 8080SL289.ebl`); an empty header keeps the file name. Search matches the header name.
- Dragging an `.ebl` row to a DAW (or Finder) materialises a 16-bit WAV in
  `~/Library/Application Support/Sonicator/exports/drag`: PCM, rate and channels are unchanged, and
  a loop is written to `smpl`. A failed conversion is reported in the status line and does not drop
  the rest of the selection.
- The source `.ebl` stays untouched; the drag cache is regenerated when the source changes.

## Verified against the owner's banks

All 18 banks under `E-MU Sound Sets` (`SONICATOR_EBL_DIR`):

| Measure | Result |
|---|---|
| `.ebl` files | 13 156 |
| Refused | **0** |
| Mono / stereo | 12 873 / 283 |
| Looped | 6 296 |
| Distinct sample rates | 1 429 |

Reference check against `uvx samplerdisc extract-banks` (independent decoder, same inputs,
`SONICATOR_EBL_REFERENCE`):

| Measure | Result |
|---|---|
| Compared | 13 026 (99.0 % of files) |
| PCM mismatches | **0** |
| Ambiguous duplicate header names within a bank | 130 |
| Unmatched names | 0 |

The 130 ambiguous files are samples whose header names repeat inside one bank; samplerdisc
disambiguates output files with a `_2` suffix, so which output belongs to which input cannot be
established from the names alone. Their PCM is covered by the decoder rule tests and by the rest of
the bank.

## Reproducing the checks

The corpus and reference tests are opt-in (hidden tags) and write nothing into the library:

```bash
SONICATOR_EBL_DIR="/Volumes/1TBSSD/samples/E-MU Sound Sets" \
  ~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[.ebl-corpus]"

uvx samplerdisc extract-banks "/Volumes/1TBSSD/samples/E-MU Sound Sets" /tmp/sonicator-ebl-ref
SONICATOR_EBL_DIR="/Volumes/1TBSSD/samples/E-MU Sound Sets" \
SONICATOR_EBL_REFERENCE=/tmp/sonicator-ebl-ref \
  ~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[.ebl-reference]"
```

The default test suite covers the decoder rules with synthetic files (pads, mono sub-types, the
two-byte stereo anchor, a truncated stereo fallback, loop bounds, every refusal reason, and a loop
over every truncation of a valid file) plus the format, catalog and drag paths.

## Credit

Format knowledge comes from [samplerdisc](https://github.com/bmxcode/samplerdisc) by bmxcode,
MIT licence. Its `docs/formats/emu-ebl.md` and `sample/emu_ebl.py` were the specification and the
reference; the C++ decoder here is an independent implementation of the documented rules. No
samplerdisc source code is copied, linked, or executed by the application.
