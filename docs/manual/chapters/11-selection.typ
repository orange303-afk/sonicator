#import "../style.typ": *

#chapter(11, "Selection and dragging",
  lead: [A range on the waveform becomes a new WAV, ready to drag into a DAW. Library rows drag as files; a selection drags as exactly that range.],
  inthis: (
    [Select a range and adjust it while it loops],
    [What the prepared WAV contains],
    [Drag a row, several rows, or a selection],
    [Where exports live and why they are never deleted automatically],
  ),
)[

#section("Selecting a range")

Drag across the waveform by more than about 3 pixels to select a time range. The endpoints clamp to
the file and may be dragged in either direction. A plain click seeks and clears the selection. While
a selection exists, the overlay draws a translucent range, start and end lines and a label such as
`0.250–1.100 s`.

With *Loop* on, the selection is also the loop range. Dragging inside it moves the range; dragging
within a few pixels of either boundary resizes it. Both edit live while audio plays, and the playhead
keeps its relative position inside the range, so the material under it changes as you move the window.
Cmd-drag (Ctrl-drag on Windows) inside the selection starts an external file drag instead.

The mouse wheel resizes the selection around its centre. Escape clears it when the preview has
keyboard focus; the *Clear* button does the same without a key.

#shot("/docs/manual/assets/selection.png", [Figure 11.1 — A prepared selection. The label names the range; the status line says the WAV is ready to drag.], width: 100%)

#section("What gets prepared")

While a selection exists, a background worker writes a WAV of exactly that range. The status reads
*Preparing selection…*, then *Ready — drag the selection into a DAW*, or shows an explicit error. A
live drag or wheel edit postpones the export until the gesture has been quiet for about 300 ms, so
you do not get one file per pixel. Dragging before it is ready is refused with that status.

The export preserves the source sample rate and channel count. 16-bit sources stay 16-bit PCM,
24/32-bit integer sources become 24-bit PCM, float sources become 32-bit float, and compressed sources
become 24-bit PCM. There is no gain change, normalization, fade or time-stretch. Files with more than
64 channels are refused rather than downmixed.

Selections live under `Sonicator/exports/`:

- macOS: `~/Library/Sonicator/exports/`
- Windows: `%APPDATA%\Sonicator\exports\`

The application never deletes them. A DAW project or a sent message may still reference a file, so
the folder grows until you remove files yourself. Chapter 12 adds editing that can replace the
original, but a selection export never does.

#section("Dragging rows")

Select one or more rows and drag them out of the window. The operating system receives file URLs
with `canMoveFiles = false`: Finder and the desktop copy, messengers attach, DAWs import onto a track.
The library original is never moved, renamed or deleted, and the application needs no per-DAW
integration.

- A multi-row drag carries every selected existing original in table order; missing files are
  skipped. If none remain, no drag starts and the status explains why.
- The row under the mouse is selected before the drag if it was not already part of the selection.
- A drag starts after 5 pixels, so a click does not accidentally start an OS drag. Dragging does not
  alter the selection.
- Only external drag is supported: no drag between application panes and no MIDI drag.

On macOS the drag carries real file references, so a DAW sees files, not text. On Windows the same
JUCE file-URL path is used, but external drag has been verified on macOS only; check it on your
Windows setup before relying on it in a session.

#section("Dragging a selection")

Dragging from inside a prepared waveform selection carries the exported WAV, not the original. That
is the point of the feature: a four-bar slice can leave the library without creating a permanent edit.
The same WAV can also be dragged to Finder or attached to a message.

If the selection is still preparing, the drag is refused with a status rather than starting a partial
file. The last prepared WAV is kept under `exports/` even after you move the selection, so a drag
always has a complete file to carry.

]
