# Disc images: capabilities and limits

Read-only browser for `.iso`, `.img` and `.bin` assets. Recognition is by content; the extension
never selects a parser. Originals are opened read-only, never mounted, copied, renamed or
repacked.

## Supported now

| Capability | Status |
|---|---|
| ISO-9660 Level 1/2 listing and file extents | yes (2048-byte logical blocks) |
| Joliet names (UCS-2BE) | yes, preferred over the primary tree when valid |
| ISO version suffix (`;1`) | stripped for display, kept in the raw name; identity is the record offset |
| Multi-extent, extended attributes, interleaved files, Rock Ridge names/links | visible with an explicit unsupported capability |
| UDF | reported unsupported, never read as ISO |
| WAV/AIFF/FLAC/MP3 inside ISO (mono/stereo) | decoded from the entry stream; no temporary files or fake paths |
| Raw Akai S1000/S3000 catalogue | yes: validated partitions → volumes → records |
| Akai program records | metadata-only containers; keygroups, filters, envelopes are not emulated |
| Akai sample PCM | yes for the verified variant: 150-byte header + signed 16-bit little-endian; any other header length is refused, not guessed |
| Stereo pairs | Akai and ISO L/R records with the same base name and equal size merge into one stereo entry; physical mono WAVs with L/R markers play and export as one stereo file |
| BIN/CUE | yes for `FILE ... BINARY`, MODE1/2048, MODE1/2352, MODE2/2352 (XA Form 1), AUDIO tracks (metadata-only), INDEX 00/01, PREGAP/POSTGAP; a sibling `.cue` is found for a BIN, and a multi-data-track cue offers a track choice |
| MDX | unsupported; separate decoder/licence gate |

Plain IMG/BIN content that matches neither ISO nor the raw Akai layout is reported as `unknown`,
not guessed.

## BIN/CUE details

- A `.cue` row opens directly; a `.bin`/`.img` row looks for a case-insensitive `.cue` with the same
  stem in its own directory. Two matching cue files are reported as ambiguous.
- FILE references must be relative to the cue directory, must not escape it, and symlinks, URLs,
  absolute paths, `..`, special files and non-BINARY FILEs are refused. A missing FILE is a local
  malformed error.
- Times are `mm:ss:ff` with `ss < 60` and `ff < 75`; track numbers are unique; track starts must
  increase. `INDEX 00` is stored but the track starts at `INDEX 01`. A synthetic `PREGAP` is
  recorded and never added to the stored file offset.
- User data: MODE1/2048 at offset 0; MODE1/2352 at offset 16 with sync/mode validation;
  MODE2/2352 only XA Form 1 at offset 24 with matching subheader copies. Mode 2 Form 2 is refused
  per sector, not read as data.
- One data track opens automatically. With several data tracks the browser lists them by number
  and mode; opening one re-resolves the cue with that track selected. AUDIO tracks are listed only
  by their data-track sibling context and are not extracted.
- Not implemented: BIN without CUE and raw 2352 layout guessing; mixed 2048/2352 tracks inside one
  FILE; FILE WAVE/MP3; subchannel data; EDC/ECC verification.

## Akai details verified against a real image

- 8192-byte blocks; the partition table is read at `0x4500`.
- Each validated partition has its own volume table at `+0x00ca` and FAT at `+0x070a`.
- Volume and file start blocks are **relative to their partition**.
- This image stores FAT values big-endian while volume/file fields are little-endian; the byte
  order is detected from the three reserved `0x0040` markers, not assumed.
- Zero-record padding and reserved/free/end FAT markers are checked; cycles, early EOF and
  out-of-image chains reject only the affected sample.

Real read-only check (`Best Service - Real Mega Rhythm AKAI - Single Sounds.iso`, 629,764,096
bytes): 13 partitions, 60 volumes, 253 program records, 2,681 sample records; size, mtime and
SHA-256 unchanged.

PCM evidence for this library (read-only analysis of the image, no copied corpus):

- `fileSize - frames*2` is exactly **150** for every checked sample (300 of 300), with the frame
  count taken from the header at `0x1a` and the rate at `0x8a` (44,100 Hz throughout).
- Interpreting the data as **signed 16-bit little-endian** gives near-perfect adjacent-sample
  correlation (0.99, single-digit wrap jumps per sample). The unsigned-centred and big-endian
  interpretations produce hundreds to thousands of full-scale discontinuities.
- 20 real samples from the first volume decode to finite PCM with a nonzero peak; L/R records
  (`CO1HANDB1 .L` / `.R`) merge into one stereo entry and decode to two channels.
- The 2,681 sample records become 1,911 listed sample entries after verified L/R pairing; the
  record count itself is unchanged.
- Other header lengths (for example the documented 190-byte S3000 variant) are **refused** with an
  explicit message. No independent third-party decoder was used; this is whole-library structural
  and statistical evidence, not a bit-for-bit comparison with another tool.

## Stereo pairs

- Recognised markers (case-insensitive, before the extension): `-L/-R`, ` L/ R`, `.L/.R`, `_L/_R`.
- Disc entries (Akai and ISO) merge only when one left and one right share the same base and the
  declared sizes are equal; a lone marker or a size mismatch stays separate.
- Physical WAV files: selecting either side resolves the complementary mono file in the same
  folder, requires equal sample rate and frame count, and produces one stereo snapshot for Play,
  MIDI and WAV export (L is always the left channel). Both catalog rows remain; the SQL identity,
  tags and history are untouched. Collapsing them into one row is a separate catalog decision.
- A stereo pair is refused when either file is not mono, the lengths/rates differ, the source is
  already stereo, or the 128 MiB decoded limit is exceeded; the ordinary single-file path is used
  and the reason is shown.

## Export and playback

Selected PCM is one immutable snapshot used by the waveform, Play and chromatic MIDI. Export is
the source frames only: no transpose, gain, velocity, pan, envelope, normalisation or resampling.
Integer 8/16-bit sources export as 16-bit, 24-bit as 24-bit, float32 as float32 WAV; lossy
decoders (MP3) declare 24-bit decoded precision; integer above 24 bits is refused rather than
truncated.

## Not established

- BIN/CUE layout parsing, pregap/INDEX handling and raw 2352 user-data extraction.
- Akai PCM numeric equivalence to an independent decoder.
- MDX catalogue or playback.
- Mid-session unplug/restore of a disc browser.
- Remote/network images (always refused).
