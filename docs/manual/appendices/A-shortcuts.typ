#import "../style.typ": *

#heading(level: 1)[Appendix A · Keyboard shortcuts]

These are the built-in defaults. Every command in the first three tables can be rebound in
*Settings → Hotkeys*; the values below are what a fresh profile starts with. Windows uses Ctrl where
macOS uses Cmd.

#section("Waveform editor")

#data-table(
  header: ("Command", "macOS", "Windows"),
  widths: (2.2fr, 1fr, 1fr),
  rows: (
    ("Undo", "Cmd+Z", "Ctrl+Z"),
    ("Redo", "Shift+Cmd+Z", "Shift+Ctrl+Z"),
    ("Delete region", "Backspace / Forward Delete", "Backspace / Forward Delete"),
    ("Crop to selection", "C", "C"),
    ("Silence region", "S", "S"),
    ("Reverse", "R", "R"),
    ("Fade in", "I", "I"),
    ("Fade out", "O", "O"),
    ("Normalize (−0.1 dBFS)", "N", "N"),
    ("Gain…", "G", "G"),
    ("Convert to mono", "M", "M"),
    ("Remove DC offset", "D", "D"),
    ("Trim silence", "T", "T"),
    ("Zoom to selection", "Z", "Z"),
    ("Zoom to fit", "Cmd+0", "Ctrl+0"),
    ("Save…", "Cmd+S", "Ctrl+S"),
  ),
)

#section("Library and application")

#data-table(
  header: ("Command", "macOS", "Windows"),
  widths: (2.4fr, 1fr, 1fr),
  rows: (
    ("Normalize selected…", "Shift+Cmd+N", "Shift+Ctrl+N"),
    ("Settings", "Cmd+,", "Ctrl+,"),
    ("Focus search", "Cmd+F", "Ctrl+F"),
    ("Assign last used tag", "⇧⌘T", "⇧⌘T"),
    ("Settings — Hotkeys", "—", "—"),
    ("Reset panel layout", "—", "—"),
    ("Reset folder panel width", "—", "—"),
    ("Reset Analysis panel width", "—", "—"),
    ("Reset preview height", "—", "—"),
  ),
)

An em dash means the command has no default key; it is still reachable from its menu or button.

#section("Preview transport")

#data-table(
  header: ("Command", "macOS", "Windows"),
  widths: (2.2fr, 1fr, 1fr),
  rows: (
    ("Play / pause", "Space", "Space"),
    ("Stop", "—", "—"),
    ("Seek back one second", "Left", "Left"),
    ("Seek forward one second", "Right", "Right"),
    ("Clear selection / stop", "Escape", "Escape"),
  ),
)

#section("Not rebindable")

These actions live in menus and context menus without a registered command: star/unstar, Copy, Show in
Finder/Explorer, Analyze selected, AutoTag selected, Reset tags, Split, Extract, Ignore, Show all
copies, tag management, Type/BPM/Genre submenus, the scan buttons, the folder actions, page navigation
and the Settings actions. Typing, open dialogs and value pickers keep their own keys, as do mouse
gestures.
