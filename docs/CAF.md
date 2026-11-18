# CAF (Core Audio Format): study and implementation plan

Date: 2026-11-18. Status: study. Nothing here is implemented; the code plan is what a later
stage would follow.

CAF is Apple's Core Audio Format: a chunked container for linear PCM and compressed audio, used by
Logic Pro, Pro Tools on macOS, iOS captures and `afconvert`. The owner's backlog asks for `.caf`
support. The live catalog had no `.caf` row at the time of this study (0 of 375 828 samples), so
the work is a format addition, not a repair of an existing corpus.

## Where the format has to plug in today

- `core/scanner/FileScanner.cpp` decides what becomes a library row: its extension list is
  `wav;aif;aiff;flac;mp3;ogg;ebl;voc`. `.caf` is not there, so a CAF file is never scanned.
- `core/audio/SonicatorFormats.cpp` registers JUCE's basic formats (WAV/AIFF/FLAC) plus EBL and
  VOC. JUCE's `registerBasicFormats()` does not include CAF, and `juce::CoreAudioFormat` is not
  registered.
- Waveform, analysis, preview, the editor, drag and the plug-in all share that manager, so one
  correct reader makes the format work everywhere at once.

## Container

```
caff | version:u16be | flags:u16be | chunk*
chunk = type:4 bytes | size:i64be | payload
```

A chunk size of -1 means "to end of file" and is only valid for the last chunk, normally `data`.
Chunks that matter for reading:

| Chunk | Content |
|---|---|
| `desc` | AudioStreamBasicDescription: sample rate f64be, format ID, format flags u32be, bytes per packet, frames per packet, channels, bits per channel |
| `data` | u32be edit count, then interleaved audio |
| `pakt` | packet table for compressed data |
| `kuki` | codec magic cookie (ALAC) |
| `info` | key/UTF-8 metadata dictionary, usable for a display name |
| `chan` | channel layout |
| `free`, `uuid`, `strg` | padding, opaque and string data to skip |

For linear PCM the format ID is `lpcm` and the flags are CAF's own: bit 0 = float, bit 1 =
little-endian. Integers without the little-endian bit are big-endian; this is the opposite default
from WAV and the common source of broken CAF readers.

## Codec coverage

| Format ID | Content | Plan |
|---|---|---|
| `lpcm` | int8/16/24/32, float32/64, mono/stereo, packed | decode |
| `ulaw`, `alaw` | 8-bit companded | decode (small tables) |
| `ima4` | Apple IMA 4:1 ADPCM, 34-byte packets of 64 frames per channel | decode, verified against `afconvert` and CoreAudio |
| `alac` | Apple Lossless, cookie in `kuki` | refuse with a clear message |
| `aac `, `mp3 `, others | compressed | refuse with a clear message |

Two channel maximum: the preview, editor and analysis paths are stereo throughout, and a
multichannel file should fail honestly rather than be silently downmixed. Unpacked frames
(`bytesPerPacket` larger than the packed sample size, e.g. 24-bit in 32-bit words) are refused in
the first version unless a real fixture shows a common producer that needs them.

## Approaches

**A. Own reader (`core/caf/CafAudioFormat`, chosen).** Same shape as `core/voc/VocAudioFormat`:
a bounded pure parser (`decodeCaf`) plus an `AudioFormatReader` over the decoded frames, registered
in `SonicatorFormats`. Identical behaviour on macOS, Windows and Linux; the catalog means the same
thing on every platform, which is a project rule. Cost: one stage, comparable to VOC.

**B. `juce::CoreAudioFormat` on macOS.** About ten lines: register the format and let AudioToolbox
decode CAF, including ALAC and AAC. Rejected as the primary route: Windows and the Linux release
would index a format they cannot decode, so the same catalog would behave differently per platform
and a CAF row would open as an error on Windows. This remains a possible optional fallback for
compressed CAF on macOS only, and only if the platform split is accepted explicitly.

**C. Both.** A plus a macOS-only fallback for ALAC/AAC. Deferred: it creates two decode paths for
one extension and needs a visible "this platform can/cannot decode" rule. Follow A first and decide
later.

## Implementation plan (when the stage is scheduled)

1. `core/caf/CafAudioFormat.h/.cpp`: `struct CafAudio { sampleRate, channels, bitsPerSample,
   frames, left, right }`, `bool decodeCaf(bytes, CafAudio&, String& error)` and the format/reader
   pair. Read the whole file up to the existing 256 MiB cap and 128 M decoded-frame cap used by
   VOC; reject truncated `data`, unknown required chunks and unsupported codecs with a reason.
2. Register it in `core/audio/SonicatorFormats.cpp`; add `caf` to the scanner's extension list.
3. Extend `core/audio/ContainerValidation.cpp` with a CAF chunk walk so a file with a physically
   truncated `data` chunk is marked `invalid` instead of failing later as a generic decode error.
4. Tests in `tests/TestCafFormat.cpp`:
   - generated fixtures: 16-bit big-endian, 24-bit little-endian, float32 little-endian, µ-law,
     IMA4; every one decodes with the expected rate, channels, frame count and peak;
   - `tone.caf` committed under `tests/fixtures` next to `tone.wav`, produced with `afconvert`,
     so a real encoder's output is in the corpus;
   - a truncated `data` chunk fails with a reason; `alac` fails with "unsupported codec";
   - `FileScanner::isSupportedFormat` accepts `.caf`, `BasicAnalyzer` measures it and `AudioEngine`
     plays it.
5. Docs: an `indexed` row in `docs/manual/appendices/C-formats.typ`, a section in
   `docs/EXOTIC_FORMATS.md`, and the batch converter's research row updated (CAF becomes a
   readable source; writing CAF stays out of scope).

Elapsed work estimate: one stage, about the size of the VOC addition (parser plus reader plus
tests plus docs). Fixtures are cheap: `afconvert -f caff -d LEI16@48000` and `-d ima4` from
`tests/fixtures/tone.wav`.

## Fixtures and verification

`afconvert` on macOS can produce every variant above. During development the decoder is compared
against CoreAudio's own decode of the same file (sample values and length) rather than against
hand-computed samples; the committed fixtures then pin the result in CI on all platforms.

## Non-goals

- Writing or converting *to* CAF. The batch converter keeps its existing outputs.
- ALAC and AAC decoding in the first version.
- More than two channels.
- Metadata extraction (tempo/key) from CAF: the name-based extractor already works; CAF carries no
  standard musical metadata.
