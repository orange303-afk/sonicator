#import "../style.typ": *

#chapter(16, "Instrument files",
  lead: [EXS24, Kontakt NKI and text SFZ instruments are read as zone maps over samples on disk. The browser shows the samples, auditions them and exports them, without pretending to be the original sampler.],
  inthis: (
    [How an instrument is read],
    [The sample selector and MIDI mapping],
    [Missing and ambiguous samples],
    [What the bounded readers ignore],
  ),
)[

#section("A shared shape")

An instrument is a map: a key range and velocity range point at a sample, with a root key, tuning and
sometimes a loop. SONICATOR reads that map, resolves the sample references to files, and gives you the
same surface for all three formats: a sample selector, the waveform of the selected source, ordinary
playback, MIDI Preview through the map, and a WAV export.

The instrument file is indexed as a metadata-only row so it appears in the library. The samples it
references may live anywhere the reader can resolve them.

#section("EXS24")

EXS files are read as Apple's little-endian 84-byte block format: instrument count, zone key and
velocity, root key, fine tune, sample start and end, loop points, and sample file references. Real
libraries use several zone payload sizes; the reader handles 96, 104, 108 and 136 bytes and reads the
reference from the correct tail position for each.

Sample references use block order, because some writers leave every block header index at zero. The
block title is the file name; the stored path is treated as a hint, since HFS absolute paths from
another machine are usually stale. Resolution tries the exact path, the stored relative path, suffixes
below the instrument folder and its parents, then a bounded basename search below the instrument
folder. Two basename matches produce an explicit *Ambiguous sample name* error; the reader never
guesses.

EXS often stores a stereo sample as separate left and right blocks with no reliable pair link. Those
are listed as two sources. Big-endian EXS blocks are reported as unsupported.

#section("Kontakt NKI")

NKI is read only when it contains the zlib-compressed `K2_Container` or `NiSS_Program` XML. The
supported subset maps key and velocity ranges, root key, zone tune, sample start and end, and the
`file_ex2` or `file` sample reference, including Kontakt's path escaping.

NI file containers, binary containers, encrypted content, scripts, groups, effects, crossfades,
reverse zones and NKI loop points are outside the subset. A zone with loop data plays one-shot and
says so. No compatibility beyond this XML subset is claimed.

#section("SFZ")

SFZ is a text format. SONICATOR reads the playback-relevant part: samples, key and velocity ranges,
root key, tuning, volume, pan and loops. Filters, envelopes, LFOs, effects and MIDI rules are ignored,
in the same spirit as the VAST part of a Kurzweil program. Files are never written.

Opcodes outside this set are ignored. The instrument file is never written.

#section("Zones, one-shots and releases")

A zone with a single key that is clearly a drum map — an EXS one-shot zone or a single-key NKI drum
zone — ignores NoteOff and rings out to the end of the sample. That is what makes a kick a kick. Wide
sustained zones still release on NoteOff.

The preview header shows the selected sample's range as `MIDI 36–47`; the transport line reports
`voices N · MIDI <note> ok` or `no zone`. A note outside the map stays silent by design, and the line
tells you why.

#section("Limits")

- Instruments are decoded with a 256 MiB cap and a 4096-source cap; exceeding either is reported.
- A missing sample does not abort the instrument: the resolved zones still play and the missing ones
  are named.
- EXS and NKI share the SF2 sample selector, audition, MIDI Preview and WAV export. SF2 stereo headers
  collapse to one source; EXS stereo blocks often cannot.
- Export preserves the source format where it is known: 16-bit stays 16-bit, 24/32-bit integer becomes
  24-bit PCM, float becomes 32-bit float.
- The originals and their samples are never modified.

]
