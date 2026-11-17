#import "../style.typ": *

#chapter(13, "Split, Extract and Normalize",
  lead: [Three batch actions for material that is already inside a file: cut a long recording at silence, pull the samples out of an instrument, or level a selection of files.],
  inthis: (
    [Split one or more files by silence],
    [Extract embedded samples from instruments, images and trackers],
    [Normalize a selection of library rows],
    [What is written, what is left alone, and the destructive options],
  ),
)[

#section("Split by silence")

*Split by silence…* is in the row context menu for one or more selected files. The dialog shows the
count, three parameters and two output options:

#data-table(
  header: ("Control", "Range", "Meaning"),
  widths: (1.4fr, 1.4fr, 2.6fr),
  rows: (
    ("Silence threshold", "−120…−6 dBFS", "A frame counts as silence when every channel is below it."),
    ("Minimum silence", "10…10 000 ms", "Shorter gaps do not split; they stay inside a part."),
    ("Minimum part", "10…10 000 ms", "Parts shorter than this are merged rather than emitted."),
  ),
)

Two checkboxes matter:

- *Save next to each original file* writes the parts beside the source. Clearing it enables a folder
  field; *Browse…* chooses an existing folder.
- *Move originals to Trash after a successful split* is off by default. When on, the source is moved
  to the system Trash only after all parts were written successfully. Leave it off until you have
  listened to the result.

*Normalize each part to −0.1 dBFS* applies one peak coefficient to every part, so a quiet tail does
not produce a quiet file. It is off by default.

The parts are ordinary WAV files with numbered names based on the source stem. The source is never
edited in place. If the source produces fewer than two parts, the result is the whole file and the
dialog says so. Cancelling keeps the parts already written and removes the unfinished temporary.

#shot("/docs/manual/assets/split.png", [Figure 13.1 — The Split dialog with its defaults. The folder line shows where the parts will go; *Browse…* changes it.])

#section("Extract samples")

*Extract samples…* is offered for containers that hold audio inside: SoundFont 2, EXS24, Kontakt
(NKI), SFZ, REX2, tracker modules, Kurzweil banks, E-mu EBL banks and disc images. The dialog lists
the number of sources, the destination and three options:

- *Save next to each original file* or a chosen folder.
- *Normalize each sample to −0.1 dBFS*.
- *As heard in song (first note)* for tracker and bank sources whose samples are tuned by the song or
  program; it tunes the extracted samples to the first note rather than the raw reference rate.

Extraction writes WAV files, never edits the container, and shows progress per file. Problems with
one sample — an unsupported variant, a missing referenced file, an ambiguous name — are listed in the
details instead of aborting the whole run. *Show details* reveals the log.

#shot("/docs/manual/assets/extract.png", [Figure 13.2 — Extract samples from one bank. The options are the same for a tracker module or a disc image; only the source list changes.])

The chapter on the specific container explains what can be extracted and what is metadata-only
(chapters 15–19).

#section("Normalize selected")

Select one or more ordinary rows and choose *Normalize selected…* (Shift+Cmd+N on macOS,
Shift+Ctrl+N on Windows). Every WAV, AIFF or FLAC file in the selection is rewritten in place to a
peak of −0.1 dBFS.

This is the one library action that changes originals, so it asks before writing unless *Settings →
General → Editing → Always overwrite original files* is on. Files held open in the waveform editor
with unsaved edits are skipped and named in the report; the editor's buffer is never overwritten
behind your back. Compressed formats are not rewritten in place.

#warning[
  Normalize selected is not the same as the editor's *Normalize* command. The editor changes a buffer
  and waits for a save; this command writes files. If a DAW project references the file, its bytes
  change and the project may need a reload.
]

]
