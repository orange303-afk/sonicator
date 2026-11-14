# Command inventory

Sonicator runs actions from buttons, menu items and keys. Every action that is registered here has
a stable id and can be rebound in **Settings → Hotkeys**. `ui/commands/CommandRegistry` is the
single catalogue: a menu item, a button and a key all resolve to the same id, so a renamed label
never breaks a binding and a menu hint can never describe a key that no longer works.

Ids are `scope.action` in lower camel case and are never translated. `scope` is one of `app`,
`library`, `folders`, `waveform`, `discBrowser`, `settings`.

## Status in this stage

The registry currently holds every action with a single, parameter-free entry point:

- all waveform editor commands, the library `Normalize selected`, and the transport keys;
- `Settings`, focus search, add last tag and the layout resets.

Commands that live only inside a parameterised context-menu branch – choosing a tag, a type or a
BPM value, splitting, extracting, folder management, starring and page navigation – are listed in
the second half of this document and are **not yet rebindable**. They keep working exactly as
before; moving them into the registry is a separate change because each one needs its menu logic
extracted first. An action is never shown in Hotkeys until its handler is shared.

## How a command runs

1. A component registers the command with a `CommandSpec` (name, description, category, scope,
   platform defaults) plus two callbacks: `state()` and `invoke()`.
2. `state()` is evaluated when the menu is built **and again inside `invoke()`**. A command that
   became unavailable while a menu was open does not run.
3. Menus render their item from the spec; the shortcut column reads the effective binding.
4. The keyboard router resolves a key by focused scope and effective binding. Text fields, open
   dialogs and the shortcut recorder keep the key to themselves.
5. A newer shortcut schema is preserved but not executed, and a conflicting user binding is
   dropped with a diagnostic instead of running two handlers.

## Registered commands

### Waveform editor

Popup ids 1–17 are the contract with the preview's context menu and are unchanged.

| Id | Popup id | Name | Default |
|---|---|---|---|
| `waveform.undo` | 1 | Undo | ⌘Z |
| `waveform.redo` | 2 | Redo | ⇧⌘Z |
| `waveform.deleteRegion` | 3 | Delete region | Backspace / Forward Delete |
| `waveform.crop` | 4 | Crop to selection | C |
| `waveform.silence` | 5 | Silence region | S |
| `waveform.reverse` | 6 | Reverse | R |
| `waveform.fadeIn` | 7 | Fade in | I |
| `waveform.fadeOut` | 8 | Fade out | O |
| `waveform.normalize` | 9 | Normalize (−0.1 dBFS) | N |
| `waveform.gain` | 10 | Gain… | G |
| `waveform.toMono` | 11 | Convert to mono | M |
| `waveform.removeDc` | 12 | Remove DC offset | D |
| `waveform.trimSilence` | 13 | Trim silence | T |
| `waveform.zoomToSelection` | 14 | Zoom to selection | Z |
| `waveform.zoomToFit` | 15 | Zoom to fit | ⌘0 |
| `waveform.save` | 16 | Save… | ⌘S |

Waveform commands are available only while the preview holds editable audio; otherwise they are
disabled with the reason "No editable audio in the preview".

### Library and application

| Id | Name | Scope | Default |
|---|---|---|---|
| `library.normalizeSelected` | Normalize selected… | library | ⇧⌘N |
| `library.addLastTag` | Add last tag | library | — |
| `app.settings` | Settings | app | ⌘, |
| `app.focusSearch` | Focus search | app | ⌘F |
| `layout.reset` | Reset panel layout | app | — |
| `layout.reset.folders` | Reset folder panel width | app | — |
| `layout.reset.analysis` | Reset analysis panel width | app | — |
| `layout.reset.preview` | Reset preview height | app | — |

`library.addLastTag` applies the last tag the user assigned in this catalog, never a prediction.
It is unavailable when the selection is empty or no tag has been applied yet.

`app.settings` opens the last used section. The section ids `general`, `audioMidi`, `hotkeys`,
`plugins` and `about` are stored in the settings file; a page that is unavailable (in the DAW
plugin) falls back to General.

### Preview transport

| Id | Name | Default | Notes |
|---|---|---|---|
| `preview.playPause` | Play / pause | Space | |
| `preview.stop` | Stop | — | |
| `preview.seekBack` | Seek back one second | ← | |
| `preview.seekForward` | Seek forward one second | → | |
| `preview.clearSelection` | Clear selection / stop | Escape | Instrument mode keeps its pin |

## Menu-only commands (not yet rebindable)

These keep their current behaviour and are not listed in Hotkeys. Moving one into the registry
means extracting its menu branch into a single handler first.

| Area | Actions |
|---|---|
| Library row | Star/unstar, Copy path, Show in Finder, Analyze selected, AutoTag selected, Reset tags, Split by silence, Extract samples, Ignore, Show all copies |
| Tags | Tag ▸, New tag, Manage tags (colour/rename/delete), Remove tag |
| Metadata | Type ▸ (standard, custom, reset), BPM ▸ (set, tap, clear) |
| Library toolbar | New files, Rescan, Reanalyze all, Smart metadata, Add folder, Tags filter, Starred, Hide empty, Clear filters, page size, previous/next page |
| Folder tree | Open in Finder, Favorite, Ignore, Rename, Scan, Smart metadata, AutoTag, Tag ▸, Reset tags, Remove from library |
| Settings | Move/Open/Relocate/Backup catalog, contribution preview/delete, report links |

## Deliberate exclusions

Not commands, not rebindable; each is owned by the focused control:

- Typing in any text field: Copy, Paste, Select all, Undo, Delete, Space, letters.
- Combo boxes, page-size selector, tag filter menus and other value pickers.
- Mouse gestures: waveform selection, drag to DAW, scroll/zoom, context menus, panel dividers.
- Local dialog buttons: OK / Cancel / Close, and Enter / Escape inside them.
- Tap BPM's Space (tap), Return (apply) and Escape (cancel) inside its own dialog.
- Tracker note input through MIDI.
- The operating system's file chooser and any native menu.

## Adding a command

1. Pick a stable id from the scope above; never rename an id that has shipped.
2. Register it with a name, description, category, scope and platform defaults. Do not invent a
   default that conflicts with another command in an overlapping scope.
3. Route the existing button/menu item through `registry.invoke(id)` instead of calling the
   handler directly, so the availability check is shared.
4. Give destructive commands `destructive = true` and keep their confirmation.
5. Add a test that the command appears in the inventory and that its handler runs once.
