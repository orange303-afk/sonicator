#import "../style.typ": *

#chapter(12, "The waveform editor",
  lead: [Small repairs without leaving the browser: trim, fade, normalize, reverse, convert to mono. Edits live in memory until you save; nothing is written by accident.],
  inthis: (
    [Which files can be edited],
    [The edit commands and their keys],
    [Undo, zoom and the edit buffer],
    [Overwrite, Save copy and unsaved changes],
    [What happens to tags, analysis and loop points],
  ),
)[

#section("Which files can be edited")

The editor works on the audio the preview holds: ordinary WAV, AIFF and FLAC files, and a selected
source from an instrument, disc image or tracker. It holds at most two channels; a file with more
than two channels is not editable and says so.

REX2 loops, Reason patches, a tracker's *whole song* view and an instrument with no selected source
are not editable. Their preview has no single editable buffer.

#section("Commands")

Operations act on the waveform selection; without a selection, on the whole file. Delete, Crop and
Silence require a selection. Trim silence always works on the file edges.

#data-table(
  header: ("Command", "Default", "What it does"),
  widths: (1.5fr, 1fr, 2.6fr),
  rows: (
    ("Undo / Redo", "Cmd+Z / Shift+Cmd+Z", "Steps through the edit buffer."),
    ("Delete region", "Backspace / Delete", "Removes the selected frames; the tail closes up."),
    ("Crop to selection", "C", "Keeps only the selection."),
    ("Silence region", "S", "Writes zeros over the selection; length unchanged."),
    ("Reverse", "R", "Reverses the frames in the range."),
    ("Fade in / Fade out", "I / O", "Linear amplitude ramp over the range."),
    ("Normalize", "N", "One peak coefficient to −0.1 dBFS; silence is left alone."),
    ("Gain…", "G", "Applies a gain in dB, −24 to +24; clipping is reported, not hidden."),
    ("Convert to mono", "M", "Mixes to one channel."),
    ("Remove DC offset", "D", "Subtracts the per-channel mean."),
    ("Trim silence", "T", "Removes leading and trailing near-silence."),
    ("Zoom to selection / fit", "Z / Cmd+0", "Changes the view, not the audio."),
    ("Save…", "Cmd+S", "Opens the save dialog described below."),
  ),
)

The same commands live in the waveform context menu, which also shows the effective key. Every
command is rebindable in *Settings → Hotkeys* (chapter 24); the table lists the macOS defaults, and
Windows uses Ctrl instead of Cmd.

#section("The edit buffer")

A file is decoded into memory when you edit it. Commands change that buffer, undo and redo step
through the changes, and the waveform and playback both read the buffer. The original file on disk is
untouched until you save.

A bullet next to the file name marks unsaved edits. If you select another row, open an image or close
the application with that bullet showing, a dialog asks what to do:

#block(fill: tint, inset: 12pt, radius: 3pt, above: 8pt, below: 12pt)[
  #set text(size: 9.5pt)
  *“Clap_15.wav” has unsaved edits.* \
  #keys("Save…") #h(6pt) #keys("Discard") #h(6pt) #keys("Cancel")
]

*Save…* opens the save dialog; after a successful save the move continues. *Discard* drops the buffer.
*Cancel* returns to the edited file and stops the move.

#section("Saving")

#block(fill: tint, inset: 12pt, radius: 3pt, above: 8pt, below: 12pt)[
  #set text(size: 9.5pt)
  *Save changes to “Clap_15.wav”?* \
  #keys("Overwrite") #h(6pt) #keys("Save copy") #h(6pt) #keys("Cancel")
]

*Overwrite* is available for ordinary WAV, AIFF and FLAC files. The new audio is written to a
temporary file in the same folder, in the same format, rate, bit depth and channel count (mono after
*Convert to mono*). The result is validated, then atomically replaces the original. If the path is a
symbolic link, the real target is replaced and the link stays a link. The file is reanalyzed in the
background and its catalog row keeps its id, tags and star.

*Save copy* writes `<stem> edit.wav` next to the original — or `edit 2`, `edit 3` if taken — and never
overwrites an existing file. For a source from an image, instrument or tracker the copy goes next to
the container, or to the exports folder if that folder is not writable. Copies of non-WAV/AIFF/FLAC
sources are written as WAV. The copy is added to the catalog and inherits the original's user tags and
star; the preview switches to the copy.

*Cancel* changes nothing.

#tip[
  *Settings → General → Editing → Always overwrite original files* skips the prompt for WAV, AIFF and
  FLAC and makes *Save* replace the file directly. Other formats still save as a copy. Leave it off
  until you are comfortable with the editor.
]

#section("What is preserved")

WAV sampler chunks (loop points, root key), cue markers, BWF/`bext` and ACID metadata are carried when
the format supports them. If an edit moves or removes frames, loop and cue points are remapped: points
inside a deleted range are dropped, points after it move, and a reverse mirrors them. A loop whose
start ends up at or after its end is removed.

Save copy does not inherit machine-learning feedback, only tags and the star. That keeps near-identical
copies out of the training set.

]
