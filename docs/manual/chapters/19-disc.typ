#import "../style.typ": *

#chapter(19, "Disc images",
  lead: [A CD image or an Akai volume is catalogued without being unpacked: only the playable entries appear, each with a synthetic path, and each decodes on demand.],
  inthis: (
    [Which images are read],
    [What appears in the library],
    [The image browser and Ignore],
    [What happens when an image changes or disappears],
  ),
)[

#section("Which images are read")

A scan walks the folder and probes each image. Supported:

- ISO-9660 Level 1/2 images, with Joliet names preferred when valid.
- `.bin`/`.img` with a sibling `.cue` using `FILE … BINARY`, MODE1/2048, MODE1/2352, MODE2/2352 XA
  Form 1 and AUDIO tracks. A multi-data-track cue offers a track choice.
- Raw Akai S1000/S3000 volumes.
- Plain IMG/BIN that matches neither ISO nor the Akai layout is reported as `unknown`, not guessed.
- MDX is not supported.

Only entries this build can open are catalogued: WAV, AIFF, FLAC, MP3 and OGG inside an ISO, and Akai
sample records. Programs, unsupported records and text files stay visible in the disc browser but do
not clutter the library. A `.bin` with a sibling `.cue` is represented by the cue, so its contents are
indexed once.

#section("Rows and synthetic paths")

Each playable entry becomes a metadata-only row whose synthetic path is
`<image>::<internal path>`. The name and the internal folder are searchable like any other sample.
Tags, stars, genres and notes work normally. Duration, rate and measured features stay empty until
the row is selected, because indexing never decodes PCM.

Selecting such a row decodes it through the bounded disc reader and loads the ordinary preview
transport, waveform and loop. Nothing is extracted to disk; there is no fake file path and no
temporary audio.

#section("The image browser")

Press Enter or double-click an image row to open the disc browser on that entry's folder. It lists
the whole image, including entries that are not in the library. *Ignore* on the image hides its
contents; removing the folder deletes its disc rows and index state.

Across a rescan, an image is re-walked only when its identity changes: size and modification time,
plus the data file for a `.cue`. Unchanged images are skipped.

#section("Changes, missing images and export")

A changed image is rebuilt. Paths that survive keep their tags and stars; entries that disappeared are
marked missing. A missing image marks its contents missing and keeps tags and stars; an explicit
*Remove* deletes the rows.

Dragging a selection from the waveform exports a temporary WAV. Disc rows themselves are never OS file
paths and cannot be dragged as originals. *Extract samples…* writes the embedded audio as normal WAV
files, with the same options as any other container (chapter 13).

#note[
  Because indexing decodes nothing, an image with a damaged sector may still list rows that fail on
  selection. The failure is reported with the reason; the image is never rewritten.
]

]
