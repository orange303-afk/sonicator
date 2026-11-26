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

The editor works on the audio the preview holds: any decodable file up to two channels, including
MP3, OGG and VOC, and a selected source from an instrument, disc image or tracker. A file with more
than two channels is not editable and says so. Only ordinary WAV, AIFF and FLAC can be overwritten in
place; everything else saves a copy.

REX2 loops, Reason patches, a tracker's *whole song* view and an instrument with no selected source
are not editable. Their preview has no single editable buffer.

#section("Waveform or spectrum")

The tabs above the card switch between the amplitude waveform and a spectrogram of the same visible
range. Frequency is logarithmic from 20 Hz to Nyquist with guides at 100 Hz, 1 kHz and 10 kHz; the
*Log* / *Linear* pill changes the axis, and levels run from −90 dB up to the loudest bin in view.
Selection, zoom, playhead, loop drag and dragging a range into a DAW work the same in both views.
*Snap* in the card's toggle row (or the `Z` command) makes a dragged range edge land on the nearest
zero crossing of the source, so an exported fragment or a loop starts and ends at silence instead of
on a step and does not click; the magnet reaches five milliseconds either side of the pointer, and a
range set by the application stays exact. A long range is analysed with a coarser time step and
labelled *overview*. The view and the scale are remembered between launches.

#plate("/docs/manual/assets/preview-spectrum.png", [Figure 12.1 — The same preview card in spectrum mode. The Waveform / Spectrum tabs and the Log / Linear pill sit in the title row; the time range, selection and playhead are shared with the waveform.])

#section("Selecting inside the spectrum")

The strip in the card's top-left corner switches the selection tool: *Time* keeps the ordinary
waveform range, *Rect* drags a time × frequency box, *Lasso* closes a freehand polygon, and *Wand*
flood-fills around the clicked point within its tolerance — click the `dB` pill to cycle
3 / 6 / 12 / 24 dB. Shift adds to the selection, Alt subtracts, a plain drag replaces it; *Clear*,
Escape and the context menu empty it. Zoom, pan, Loop and the playhead keep working while a 2D
selection exists. The time range and the spectral mask are separate: Play, Loop and MIDI follow the
time range, while the mask decides what is exported and dragged.

The selected time and frequencies are prepared as audio: the range is analysed, every bin outside
the mask is removed, and the result has the length of the selection with silence around it, so it
keeps its place when dropped into a DAW. The overlay is drawn exactly on the highlighted time and
frequency band, and each tool has its own cursor. To drag the fragment out, grab the highlighted
area itself — the cursor turns into a hand over it — or press Play to audition it: the card keeps
showing the original spectrum and the playhead follows the selection. Loop repeats the fragment,
and MIDI Preview plays it too, so a key sounds the selected band and time only; clearing the
selection returns the ordinary sample. Shift and Alt keep drawing on top of the selection, and the
wand always fills on click. Selections longer than a minute are refused with a plain message
instead of exhausting memory.

#plate("/docs/manual/assets/preview-spectral-selection.png", [Figure 12.2 — A rectangular spectral selection: only this time and frequency band will be exported, dragged or auditioned.])

#section("The spectral cache")

`Spectral analysis (N)…` in the sample context menu (under *Processing*) and in the folder menu
precomputes a whole-file spectrogram for the selected files, so their preview opens at once; the
status line shows progress and the run can be cancelled. Settings → Library has *Cache spectrograms*,
the current size and a *Clear cache* button. A cache entry belongs to the file's path, size and
modification time, so editing or replacing a file simply rebuilds its picture.

#section("Commands")

Operations act on the waveform selection; without a selection, on the whole file. Delete, Crop and
Silence require a selection. Trim silence always works on the file edges.

#data-table(
  header: ("Command", "Default", "What it does"),
  widths: (1.5fr, 1fr, 2.6fr),
  rows: (
    ("Undo / Redo", "Cmd+Z / Shift+Cmd+Z", "Steps through the edit buffer."),
    ("Delete region", "Backspace / Forward Delete", "Removes the selected frames; the tail closes up."),
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
    ("Waveform / spectrum view", "V", "Draws peaks or a spectrogram of the same range."),
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
the container. Copies of non-WAV/AIFF/FLAC
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
