# FastTracker II instruments (`.xi`)

An XI file is one FastTracker II instrument: a fixed header, one 40-byte sample header and a block
of delta-encoded PCM. It is not a song, so Sonicator opens it as a tracker document with a single
source slot, plays it through the existing sample path and extracts the PCM. Files are read-only.

Sources of the layout (format knowledge, no code copied):

- FastTracker II XI instrument layout, as documented in the tracker community (XI.TXT / the
  ModdingWiki XI page) and verified byte-for-byte against the library corpus.
- The 40-byte sample header and delta PCM encoding are the same shape as XM, which Sonicator already
  reads in `core/tracker/TrackerXmReader.cpp`.

## Layout

| Offset | Size | Field |
|---|---:|---|
| 0 | 21 | `Extended Instrument: ` |
| 21 | 22 | instrument name (null padded) |
| 43 | 1 | `0x1A` marker |
| 44 | 20 | tracker name |
| 64 | 2 | version, little-endian (`0x0102` in the corpus) |
| 66 | 96 | 48 volume-envelope points (not interpreted) |
| 162 | 96 | 48 panning-envelope points (not interpreted) |
| 258 | 8 | envelope point counts, sustain and loop points |
| 266 | 6 | envelope/vibrato types and depths |
| 272 | 4 | volume fadeout |
| 276 | 22 | reserved |
| 298 | 40 | sample header |
| 338 | — | delta PCM |

The sample header is the XM 40-byte header: u32 length in bytes, u32 loop start, u32 loop length,
volume, signed finetune, type, panning, signed relative note, reserved, 22-byte sample name.

Sample `type` bits used:

| Bit | Meaning |
|---|---|
| `0x10` | 16-bit sample (otherwise 8-bit) |
| `0x03` | loop kind: 0 none, 1 forward, 2 ping-pong |
| `0x20` | stereo; rejected (XI instruments in the library are mono) |

PCM is signed delta encoded exactly like XM: 8-bit deltas are added to a signed 8-bit accumulator,
16-bit deltas are little-endian signed 16-bit values added to a signed 16-bit accumulator. The
`TrackerPcmEncoding::deltaInt8` / `deltaInt16LE` decoder in `TrackerDocument` does this.

## Musical values

- Published `sampleRate` is the rate at MIDI note 60:
  `8363 * 2^((relativeNote + finetune/128) / 12)`, matching the XM reader.
- `rootKey` is `60 - relativeNote`: the note at which the sample plays at the 8363 Hz tracker
  reference. This is the value used when extracting, so a relative-note sample is written at its
  natural rate rather than transposed by the note-60 convention.
- Loop bounds are converted from bytes to frames; an out-of-range loop is disabled with a warning.
- Envelopes, fadeout, vibrato and panning are not interpreted. XI playback is the raw source,
  chromatically, with the loop.

## Playback and Extract

- `TrackerDocument::open` dispatches on the 21-byte magic, builds `TrackerFormat::xi` and one
  `TrackerSampleSlot`.
- `AudioEngine::previewTracker` recognises XI before calling libxmp: a standalone instrument has no
  song, so it sets tracker mode, selects slot 0 and marks `trackerHasSong()` false. The preview
  shows the source waveform and drag button, hides the whole-song progress bar and does not offer a
  "Whole song" choice.
- `EmbeddedSampleSet` lists the slot; `read` returns the decoded PCM and `playbackRatio` uses the
  root key to recover the natural rate. Extract therefore writes the PCM as an ordinary WAV like a
  MOD/XM/S3M sample (no `smpl` chunk; that remains a separate task).

## Not implemented

- Envelopes, panning, vibrato and fadeout.
- Stereo XI samples.
- More than one instrument: the format itself carries one.
- Writing `.xi`.

## Correlation with the library (2026-11-13)

`SONICATOR_XI_DIR=/Volumes/1TBSSD/samples` (read-only):

- 2 621 `.xi` files found, **2 615 open and decode** (99.77 %), 113 341 944 frames.
- 6 rejected, all explained:
  - `SMAN3.XI` declares more sample data than its 8 KiB file contains;
  - `131.XI`, `1.XI` and `LARGE.XI` declare a zero-length sample;
  - `10.XI` (Kohrah/ACIDSND/8BIT) is raw headerless PCM renamed `.XI`;
  - `!MD_2.XI` is an empty file.
- Both 8- and 16-bit samples, forward and ping-pong loops, and relative notes occur throughout the
  corpus; the reader decodes all of them with no unexplained rejection.

## Tests

```sh
cmake --build --preset release --target SonicatorTests
~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[xi]"
```

Synthetic instruments cover 8-bit delta PCM with a forward loop, 16-bit delta PCM with a ping-pong
loop and relative note, root-key/rate derivation, truncation, zero length, preview as a one-slot
instrument, the hidden whole-song mode, `EmbeddedSampleSet` extraction and the natural-rate
`playbackRatio`. Optional corpus run:

```sh
SONICATOR_XI_DIR="/Volumes/1TBSSD/samples" \
  ~/Library/Caches/sonicator-build/release/tests/SonicatorTests "[.external][xi-corpus]"
```

## Integration

- `TrackerFormat::xi` is a fourth reader next to MOD/XM/S3M and shares `TrackerDocument`,
  `TrackerSampleSlot`, the lazy PCM cache and `SampleVoicePlayer`.
- `.xi` is a metadata-only catalogue format; `LegacyFormatProbe` confirms the magic and the sample
  header extent before the catalogue accepts it.
- `EmbeddedSampleSet`/Extract treat it as a tracker source (one PCM entry), but not as a song.
