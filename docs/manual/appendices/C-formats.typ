#import "../style.typ": *

#heading(level: 1)[Appendix C · Formats at a glance]

Four words are used throughout this book:

- *indexed* — scanned, measured, previewed, tagged and draggable as ordinary audio;
- *previewed* — a metadata-only row that decodes on selection into the instrument or loop preview;
- *metadata-only* — a searchable row with file identity, without decoded audio until selected;
- *not indexed* — the application does not create a row for it.

#data-table(
  header: ("Format", "Status", "Notes"),
  widths: (1.5fr, 1.2fr, 3fr),
  rows: (
    ("WAV, AIFF, FLAC", "indexed", "Measured, editable, overwriteable by the editor."),
    ("MP3, OGG Vorbis", "indexed", "Decoded; editing saves a copy, never overwrites."),
    ("VOC", "indexed", "Creative Voice, including Creative ADPCM."),
    ("CAF", "indexed", "Core Audio Format: linear PCM, mu-law, A-law and Apple IMA4; ALAC and AAC are refused; mono and stereo only."),
    ("EBL", "indexed", "E-mu Emulator X sample; drag materialises a WAV."),
    ("RX2", "previewed", "Slices mapped to MIDI; no tempo sync or stretch. `.rex`/`.rcy` are not indexed."),
    ("SF2", "previewed", "Presets, samples, kit sampler; 16-bit PCM."),
    ("EXS, NKI, SFZ", "previewed", "Zone maps over samples; bounded readers."),
    ("MOD, XM, S3M", "previewed", "Whole song and source samples; content-recognised."),
    ("XI", "previewed", "One FastTracker II instrument."),
    ("KRZ, K25, K26", "previewed", "Kurzweil banks; programs, keymaps and samples."),
    ("ISO, IMG, BIN/CUE", "metadata-only", "Only playable entries are catalogued; decode on selection."),
    ("Akai volumes", "metadata-only", "S1000/S3000 sample records; programs are not emulated."),
    ("Ableton preset and device files", "not indexed", "`.adg`, `.adv`, `.alc`, `.als`, `.amxd`; only rendered previews are indexed."),
    ("Ableton AIFC with `able` codec", "not indexed", "Only Live can decode it."),
    ("Reason `.cmb`, `.rfl`", "not indexed", "No embedded audio; `.repatch` is a metadata card."),
    ("Tracker IT, MT2, STM, MTM, 669", "not indexed", "Not claimed in this build."),
    ("MDX", "not indexed", "No decoder."),
  ),
)

#section("Editable sources")

The waveform editor can change any decodable file up to two channels, including MP3, OGG and VOC, and
a selected source from an instrument, disc image or tracker. It cannot change REX2 loops, Reason
patches, a tracker's whole-song view, an instrument with no selected source, or a file with more than
two channels.

*Overwrite* is available only for ordinary WAV/AIFF/FLAC. Everything else saves a copy, and non-WAV
sources save as WAV.
