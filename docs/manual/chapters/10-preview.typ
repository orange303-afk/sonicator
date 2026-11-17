#import "../style.typ": *

#chapter(10, "Preview",
  lead: [Select a row and it plays. The preview band owns the transport, the toggles and the waveform; everything else in this part builds on it.],
  inthis: (
    [Auto Preview, Play, Pause, Stop and Loop],
    [Skip Silence and the volume control],
    [Keyboard control while the library has focus],
    [MIDI Preview and its settings],
  ),
)[

The preview plays the original file. No export, no resampling and no copy is made for ordinary
playback; the decoded stream goes to the selected output device.

#block(above: 12pt, below: 12pt, breakable: false)[
  #block(stroke: 0.6pt + rule, radius: 2pt, clip: true)[
    #image("/docs/manual/assets/preview-wave.png", width: 100%)
  ]
  #v(6pt)
  #block(stroke: 0.6pt + rule, radius: 2pt, clip: true)[
    #image("/docs/manual/assets/preview-controls.png", width: 62%)
  ]
  #v(4pt)
  #set text(size: 8.5pt, fill: muted)
  #set par(leading: 0.6em)
  Figure 10.1 — The preview band: the waveform and file name above, the transport, time and toggles below.
]

#section("Auto Preview and the transport")

*Auto Preview* is on by default: selecting a new row starts playback. Turning it off makes selection
silent; explicit *Play* still works in both modes. The toggle is saved in `settings.json` and restored
on the next launch.

*Play* starts from the current position; after the end it restarts, unless *Loop* is off and the end
was reached — then it stays stopped until you press Play again. *Pause* keeps the position. *Stop*
returns to the beginning.

*Loop* repeats the file, or the waveform selection when one exists. For an ordinary file the loop is
sample-accurate and lives in the transport; there is no timer tick between repeats. Loading a new file
clears the selection, and Auto Preview then plays the whole file.

#section("Skip Silence")

*Skip Silence* is off by default. When on, the next start from Stop or end-of-file begins one frame
before the first non-zero sample in any channel. It does not trim the file, change the threshold,
or apply a noise gate; pause and resume keep the current position.

Detection happens on the control thread in bounded blocks. On a long, entirely silent file it can
take a moment before playback starts; an all-silent file starts at zero.

#section("Volume and time")

The volume slider controls the preview output only. The time block shows position and length; the
format line under it names the container, sample rate and channels. Preview uses the first two
channels of a multichannel file — it is not a surround downmix.

#section("Keyboard")

While the library has focus:

- #keys("Space") plays or pauses.
- #keys("Left") and #keys("Right") seek one second back or forward.
- #keys("Escape") stops and clears the waveform selection, unless an editing control owns the key.

Typing in the search field or a dialog keeps its own keys; Space types a space there. The transport
buttons never steal keyboard focus from the table, so arrow navigation continues after a click.

#section("MIDI Preview")

*MIDI Preview* lets a hardware MIDI input play the selected sample chromatically. Note 60 sounds at
the file's original pitch; each semitone is a semitone, velocity maps to level. On first activation
every available input is enabled. Inputs connected later are enabled in *Settings → Audio & MIDI*.

For ordinary files the preview uses 16 voices with a 2 ms attack and a 20 ms release, and NoteOff
ends the note. At the voice limit new notes are ignored instead of stealing an audible voice. A
waveform selection bounds the note: it starts at the selection start and stops at its end, or loops
inside it while *Loop* is on.

MIDI Preview has an additional 1.2× gain (about +1.58 dB) before the master gain, so a MIDI audition
sits above the ordinary stream. It applies to MIDI voices only, not to Play or to exported audio.

Instrument files use a larger voice pool and a longer release (chapter 16). Turning MIDI Preview off
clears sounding voices but leaves ordinary playback running.

#tip[
  If notes play but the pitch is wrong, check the file's root note in the selection bar. A sampler
  root of C3 shifts the mapping by an octave; that is the instrument's mapping, not a bug.
]

]
