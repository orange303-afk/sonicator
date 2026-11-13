# Creative Voice files (`.voc`)

Creative Voice File is the DOS-era PCM and ADPCM container used by Sound Blaster software. Sonicator
reads it through the shared `AudioFormatManager`, so a `.voc` appears in the library, draws its
waveform, plays, is analysed and can be dragged into a DAW exactly like a WAV. Files are read-only.

Sources of the layout (format knowledge, no code copied):

- Creative VOC.TXT reproduced in the libsndfile documentation (`src/voc.c`): header, all block
  types, block 8 (stereo attribute extension) and block 9 (new format), plus the creative ADPCM
  compression numbers.
- The exact Sound Blaster Pro ADPCM predictor/step rule was checked against the reference decoder
  in FFmpeg (`libavcodec/adpcm.c`, `adpcm_sbpro_expand_nibble`); the implementation here is
  independent and is tested with hand-computed nibble sequences.

## Container

| Offset | Field |
|---|---|
| 0 | `Creative Voice File` (19 bytes; the trailing `0x1A` byte is optional in practice) |
| 20 | u16 little-endian offset of the first block (normally 26) |
| 22 | version |
| 24 | version checksum, not validated |

Blocks start at that offset: one type byte, a three-byte little-endian length, then the payload.
Type `0` is the terminator and has no length. Every block is bounds-checked against the file;
truncation is an error, never silent padding.

## Blocks

| Type | Handling |
|---|---|
| 1 | Sound data: rate time constant, compression type, PCM |
| 2 | Continuation of the previous sound data (same format and ADPCM state) |
| 3 | Silence: `length - 1` frames, own rate; inserted as zeros |
| 4, 5 | Marker and ASCII text: skipped |
| 6, 7 | Repeat start with count, end repeat: the segment is duplicated `count - 1` times |
| 8 | Attribute extension for the following block 1: time constant, pack method, mono/stereo mode |
| 9 | New format: u32 rate, bits, channels, u16 format, four reserved bytes, PCM |

Compression numbers (both block 1 and block 9) are `0` unsigned 8-bit PCM, `1` Creative 4-bit
ADPCM, `2` Creative 2.6-bit (3-bit) ADPCM, `3` Creative 2-bit ADPCM; block 9 also uses `4` for
signed 16-bit PCM. Format `6`/`7` (A-law/u-law) and block 1's Multi-DAC `5` are rejected with a
reason rather than played as noise. Block 8 with a four-byte payload is exactly what the library
contains; it changes the following block 1 to stereo and is not itself audio.

### Rates and channels

- Block 1 rate = `round(1e6 / (256 - rateByte))`, block 3 uses the same formula, block 9 carries the
  rate directly.
- A file may concatenate clips recorded at different rates and channel counts (common in DOS
  compilations). The decoded part is resampled linearly to the next block's rate and up/down-mixed
  between mono and stereo, so one continuous stream is produced. Repeat indices follow the
  conversion.
- 16-bit PCM is signed little-endian; 8-bit PCM is unsigned with 128 = silence.

### Creative ADPCM

Each new sound block starts with one raw 8-bit sample per channel: `predictor = 128 * (byte - 128)`.
After that each code is decoded as:

```
sign    = code & (1 << (width - 1))
delta   = code & ((1 << (width - 1)) - 1)
diff    = delta << (7 + step + shift)
predictor = clamp(predictor ± diff, -16384, 16256)
if delta >= 2*width - 3 and step < 3: ++step
else if delta == 0 and step > 0: --step
```

4-bit codes take the high and low nibble of each byte (two samples), 2.6-bit codes take 3 + 3 + 2
bits (three samples, the last with `width = 2`), and 2-bit codes take four values per byte
(`shift = 2`). Stereo 4-bit and 2-bit interleave the two channel states; stereo 2.6-bit is rejected
because the reference layout is not interleaved. A continuation block keeps the state.

## Not implemented

- Block 1 compression 5 (undocumented Multi-DAC) and A-law/u-law block 9 formats: rejected with a
  reason.
- Endless repeats (`count = 0xFFFF`): rejected instead of looping forever.
- Writing `.voc`.

## Correlation with the library (2026-11-13)

`SONICATOR_VOC_DIR=/Volumes/1TBSSD/samples` (read-only):

- 2 090 `.voc` files found, **2 072 decode** (99.14 %), 323 740 732 frames.
- The `VOCSQ-Z` set (723 files) decodes with its block-1 ADPCM clips, block-8 stereo extensions,
  block-3 silences and block-9 stereo data.
- 18 rejected, all explained:
  - 6 are not VOC at all: five headerless raw dumps and one DOS `MZ` executable renamed `.VOC`
    (`BABY.VOC`, `ED&CLINT.VOC`, `HUNTCOOL.VOC`, `ROBOBERY.VOC`, `SIREN.VOC`, `SLMONKEY.VOC`);
  - 4 declare more block data than the file contains (`DWHO3`, `LETERIP`, `REVBART`, `SUKDIK`);
  - 3 use block-1 Multi-DAC compression 5 (`BUGSDEAD`, `CLINT`, `DHARRY`);
  - 3 contain endless repeats (`FLYKNIFE`, both `NONSTOP` copies);
  - 2 start an ADPCM continuation without a preceding sound block (`FTBL-COS`, `INCA-WAR`).

## Tests

```sh
cmake --build --preset release --target SonicatorTests
~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[voc]"
```

Synthetic files cover block 1 PCM, block 9 16-bit and stereo PCM, block 8 stereo, all three ADPCM
widths, silence, repeat, the format registration and preview, and the rejection paths (bad
signature, truncation, A-law, invalid rate, no audio). Optional corpus run:

```sh
SONICATOR_VOC_DIR="/Volumes/1TBSSD/samples" \
  ~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[.external][voc-corpus]"
```

## Integration

`registerSonicatorFormats` adds `VocAudioFormat` next to JUCE's basic formats and the E-mu EBL
reader, so every path that calls it (scan, waveform, transport, MIDI preview, analysis, export,
drag) sees VOC as ordinary audio. The format manager can only return a reader or `nullptr`; the
decoder exposes its exact reason through `decodeVoc`, which the tests and any future catalogue
diagnostic use.
