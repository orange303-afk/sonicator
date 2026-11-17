#import "../style.typ": *

#chapter(15, "REX2 and SoundFonts",
  lead: [Two container formats that behave unlike ordinary audio: a REX2 loop becomes a sliced instrument, and a SoundFont becomes a bank of presets and samples.],
  inthis: (
    [What a REX2 row can do],
    [Slices, Gate and Loop],
    [SoundFont presets, samples and the kit sampler],
    [What neither reader claims to do],
  ),
)[

#section("REX2")

A `.rx2` file is indexed as a metadata-only row: it is searchable by name, format and mask, and it
carries file identity and readable duration, but no measured descriptors. Legacy `.rex` and `.rcy`
files are not indexed.

Selecting a row decodes the DWOP audio in memory and draws the waveform with slice markers. Visible
slices are mapped to consecutive MIDI notes starting at 36. The preview names the mapped range and the
last received MIDI note; a note outside the range is marked as such.

- *Gate* stops a slice on NoteOff; off means the slice plays to its end.
- Notes outside the mapped range wrap into the slice set, so any key auditions something.
- *Play* auditions the whole decoded loop; *Loop* repeats it. Ordinary file playback is not used for
  RX2.
- At most 128 slices are mapped. There is no tempo sync or time-stretch.

A waveform selection exports a normal 24-bit WAV at the source rate and channel count, rendered from
the decoded PCM kept for the preview — not through the generic audio reader, which cannot open `.rx2`.
That WAV drags into a DAW like any other.

#section("SoundFont 2")

Selecting an `.sf2` loads its first drum preset, or the first preset when the bank has no drum kit,
into the MIDI kit sampler and enables MIDI Preview. The preset menu switches between every preset,
melodic instruments included. A second selector lists the unique audio sources used by the current
preset; repeated zones over one source are one row, and a stereo pair is one row.

The sampler supports key and velocity ranges, root key, tuning, loop, attenuation, pan and
exclusive-class choke. It is not a full synthesis engine: no modulators, filters, envelopes or NRPN.

Audition and export use the original PCM at its sample rate and channels, with no MIDI transpose,
velocity gain, pan, envelope or normalisation. The waveform and export bounds use each sample's real
start and end. Export writes 16-bit PCM, matching the bank data; the parser reads 16-bit PCM without
the optional `sm24` chunk, so 24-bit precision is not claimed.

#note[
  The bank is never modified. A missing or malformed zone is reported with its reason rather than
  read past the PCM.
]

]
