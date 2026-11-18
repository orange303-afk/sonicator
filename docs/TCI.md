# Slate Trigger `.tci`: feasibility note

Date: 2026-11-18. Status: deferred by the owner. This is a research note, not a plan that is
being executed, and nothing here is implemented.

## What the format is

`.tci` is Slate Digital's Trigger Compressed Instrument: a self-contained drum instrument for the
Trigger 2 plug-in. One file carries every velocity layer and round robin of one articulation,
encoded at 44.1 kHz, plus a small parameter set. Instrument files are written by Slate's Trigger
Instrument Editor and by vendors (GGD, MixWave and others ship TCI versions of their libraries).
There is no published specification.

Nothing on this machine uses the format: no `.tci` file was found on the mounted volumes and the
live catalog (375 828 rows on 2026-11-18) has none. Any work on it is blocked until at least one
real file is available.

## Prior art

A clean-room, MIT-licensed decoder exists: `github.com/AidanScroggins/tci2wav` (October 2026,
Python and browser JavaScript). Its `WRITEUP.md` documents the container and the bitstream and
reports verification by exact correlation against DAW renders. The summary below is from that
writeup; it has not been independently verified here.

### Container (V2)

```
magic(64) + audio_len:u32le + 0:u32 + fmt(24) + blob + footer
```

`audio_len` sits at offset 64 and bounds the blob. The blob is the concatenation of per-wave
bitstreams in wave order; wave `i` owns `ceil(wd{i}comp1 / 8)` bytes. The footer is an 8-byte
header plus a zlib stream whose payload is `VC2!` followed by one XML element
(`<trigger_instrument ...>`) with attributes such as `data_count` and, per wave, `wd{i}comp1`
(used bits), `wd{i}frames` (play length), `wd{i}stereo`, `wd{i}vol` and `wd{i}samples`. That footer
is enough to list the waves, their lengths and channel layout without decoding audio; the
instrument name and layer grouping come from the same XML where present.

### Audio codec

Not WAV and not a standard codec: a custom bitstream.

- Every integer sample, raw or residual, is sign-magnitude, not two's complement.
- V1 tails are `[k:8][201 samples of k bits]` blocks, MSB-first, no padding, `k = 1..24`.
- V2 heads (the transient) have several encodings: raw 24-bit big-endian samples, one attack
  block with a side bit count, multi-block heads, or a leading zero pad. The decoder finds them by
  scanning for a long V1 chain and fitting the block structure; several fits can match, so the
  writeup ranks them by ground-truth correlation, sibling-wave correlation and exact length.
- Stereo waves are one joint stream of `2 * frames` samples, L/R alternating by global index. The
  tail decodes exactly; the first ~93–100 frames (about 2 ms) come from a table path that is not
  decoded, so that tool exports them silent and marks the wave as partial.
- The older standalone V1 files and the Trigger Instrument Editor variant (magic `TRIGGER `,
  `COMPRESSED INSTRUMENT` tag, 128-byte header, waves chained by gap records) are both documented
  in the same writeup.

## What support would mean for Sonicator

A `.tci` is not a one-shot audio file; it is an instrument, like SF2 or EXS24. Supporting it would
mean:

1. a container row in the catalog (one row per file, metadata-only until opened);
2. a zone map built from the velocity layers × round robins, played through the existing
   `ExternalKitSampler` / `FileInstrument` paths;
3. a decoder that is honest per wave: a wave whose transient cannot be decoded is either silent
   and flagged, or the file is refused as a whole — never guessed silently;
4. velocity grouping by attack peak (the only signal available) rather than by metadata, with the
   assumption stated in the UI.

## Effort and risks

- **Large.** This is a research-grade decoder port, not a normal reader: the mono solver is a
  fitting procedure with several heuristics, and the stereo head is still unsolved upstream. A
  faithful port needs its own fixture corpus and a ground-truth render for every wave.
- **Corpus required.** None of this can be validated without real `.tci` files, ideally the same
  instrument exported by both the Trigger Instrument Editor and a vendor.
- **Partial fidelity.** Accepting "first ~2 ms of stereo waves silent" is a product decision, not a
  technical one. For a drum replacer the transient is the most audible part, so partial stereo
  requires explicit owner approval.
- **Licensing.** The decoder is MIT, which is compatible with this project's AGPL-3.0-or-later, and
  would need an entry in `THIRD_PARTY_NOTICES.md` and a clean-room note. TCI itself is proprietary;
  reading user-owned files is the same position as the existing Kontakt monolith and E-mu work,
  but the format has no public documentation and may change without notice.
- **No encoder.** Sonicator would be a reader only; `.tci` export is out of scope.

## Decision

Deferred. Revisit when the owner supplies at least one `.tci` (a free vendor instrument is enough)
and decides whether a partial stereo decode is acceptable. Until then the backlog entry stays a
study item and no code is added.

## References

- `github.com/AidanScroggins/tci2wav` — MIT decoder, `WRITEUP.md`, Python and web implementations.
- Steven Slate Drums, "How to use the Trigger Instrument Editor" — what a TCI contains and how it
  is produced.
