# Batch MP3 VBR, ID3, OGG — design and task list

Date: 2026-11-15. Approved by the owner in chat: ID3 comes only from the source file's existing
metadata, never from catalog overrides.

## Intent

Extend the shipped Batch Processing v1/v1.1 output set: MP3 gains VBR and ID3v2 text tags, and a new
Ogg Vorbis output carries the same text tags as Vorbis comments. Originals stay untouched; no new
network or external executable is required.

## Decisions

- **MP3 mode.** `Mp3Mode { cbr, vbr }`. CBR keeps 128/192/256/320 kbps. VBR uses LAME `V0..V9`
  quality, default `V2`. One mode matters at a time; presets store mode and both values.
- **ID3.** ID3v2 only, text frames TIT2/TPE1/TALB/TCON/TDRC/TRCK and COMM. Values are read from the
  source reader's WAV INFO fields or JUCE ID3 keys, UTF-16 for non-ASCII. No cover art, no catalog
  Genre/BPM/Key, no invented values.
- **OGG.** `Format::ogg`, VBR quality 0..10 (default 6), through JUCE's embedded libvorbis writer.
  The same text tags become TITLE/ARTIST/ALBUM/GENRE/DATE/TRACKNUMBER/COMMENT comments.
- **Validation.** MP3 and OGG are decoded back and checked for rate/channels and length within the
  codec delay/padding tolerance; exact frame equality is not promised for lossy output.
- **Policy.** Clipping, source protection, atomic publication, preview, reports and presets keep the
  existing behaviour. Lossy output does not run integer dither or the integer quantizer.

## Tasks

1. Types, capabilities, fingerprints, estimates, preset schema fields.
2. Source-tag extraction and lossy metadata policy.
3. LAME VBR + ID3v2 UTF-16 writing; OGG writer and validation.
4. Dialog controls and serialization; preset round-trip.
5. Tests: VBR MP3 with Cyrillic ID3 (raw frame check), OGG decode + comments, clipping, preset and
   dialog fields, full ctest, release/codesign.
6. Docs/notices update, merge and push.
