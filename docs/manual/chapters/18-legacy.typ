#import "../style.typ": *

#chapter(18, "Legacy sampler libraries",
  lead: [E-mu EBL, Kurzweil banks, Akai volumes and Creative VOC files come from samplers older than most of the samples in your library. They are read-only and honest about what cannot be recovered.],
  inthis: (
    [E-mu Emulator X banks as ordinary audio],
    [Kurzweil banks as instruments],
    [Akai volumes and sample records],
    [Creative Voice files as ordinary audio],
  ),
)[

#section("E-mu Emulator X: .ebl")

An EBL bank is a folder, not a disc image. Each `.ebl` file is one sample, and SONICATOR registers its
reader alongside the ordinary formats: scanning, analysis, waveform, preview, MIDI preview, selection
export and Split all see `.ebl` as normal audio.

Catalog rows use the sample name from the header (`org nod C +36`), not the bank's sequence name, when
the header carries one; an empty header keeps the file name. Search matches the header name. A loop
stored in the sample is preserved.

Dragging an `.ebl` row to a DAW materialises a 16-bit WAV in `exports/drag`, with PCM, rate and
channels unchanged and the loop written to the `smpl` chunk. The drag cache is keyed by the source
path, so dragging the same sample twice reuses one file and a project that referenced it keeps
working after a re-render.

#section("Kurzweil: .krz, .k25, .k26")

Kurzweil K2000/K2500/K2600 banks are big-endian object banks. The reader understands the bank header,
programs, keymaps and sample objects. A bank plays through the instrument sampler: each program is a
program of the instrument, and a stereo sample becomes two zones panned left and right.

*Extract samples…* writes one WAV per sample header — one stereo WAV for a stereo object — with the
root key and loop in the `smpl` chunk. Two mono samples paired only by a program layer are written as
two files.

A bank with no sample data says that it uses the K2000 factory ROM samples and cannot play. The bank
itself is never written.

#section("Akai volumes")

Akai sample records live inside disc images (chapter 19). A raw Akai volume is read as partitions →
volumes → records. Two sample variants are decoded: the S1000 150-byte header and the S3000 192-byte
header, both signed 16-bit little-endian. The length must match the declared bytes exactly; any other
header is refused rather than guessed. The volume family resolves the header size: a record that fits
only the other family is treated as damaged.

Akai program records appear as metadata-only containers: keygroups, filters and envelopes are not
emulated. A stereo pair with equal base name and size merges into one entry.

#section("Creative Voice: .voc")

Creative Voice is the DOS-era PCM and ADPCM container used by Sound Blaster software. It is read
through the shared audio format manager, so a `.voc` appears in the library, draws its waveform,
plays, is analyzed and drags into a DAW exactly like a WAV.

The decoder covers the block types of the format, stereo extension blocks, the new-format block and
Creative ADPCM with the Sound Blaster Pro predictor rules. Unsupported blocks are reported with the
reason; the file is never modified.

]
