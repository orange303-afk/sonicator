# Session restore and Hide empty default — design

Status: implemented 2026-11-08.

## Hide empty

`AppSettings::hideEmpty` defaults to `true`. A settings file without the key, or with a non-boolean
value, loads with the filter enabled. An explicit `false` still disables it, and every toggle or
Clear filters action persists the resulting state as before.

## Last folder and selected file

`AppSettings` stores the last folder and the last selected sample path in the global settings file.
They are restored on the next launch when they still make sense:

- The folder must exist on disk and contain catalogued rows in the current catalog.
- The folder tree opens its ancestors and selects the folder.
- The selected path must resolve to a catalog row inside the restored folder; the row is selected
  without firing the row action, and the details/tag strip are refreshed.
- The preview loads the file and waveform with autoplay disabled, so reopening the app does not
  unexpectedly audition audio.

Changes are debounced and written by the normal settings writer. Session persistence runs only in
the standalone application; tests opt in with `flushSessionState()` so a test never overwrites the
user's real settings file.

## Verification

- Settings round-trip and missing/malformed Hide empty.
- Folder tree opens/selects a nested path.
- Row selection by id does not fire the selection action.
- Preview loads a file without playback.
- A full MainComponent restart restores folder, tree selection, row and status, then persists a new
  selection.
