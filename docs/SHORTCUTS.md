# Shortcuts and panel layout

## Rebinding a key

1. Open **Settings → Hotkeys**.
2. Search by command name, category or id, then click the command.
3. **Add shortcut** captures the next key combination. A modifier on its own is ignored.
   **Escape** cancels the capture.
4. **Apply** stores it. **Cancel** discards it.
5. If the key is already used by a command that can be reached from the same place, Sonicator
   names that command and offers **Replace**. Replacing removes the key from the other command
   and keeps its remaining keys. Cancelling the conflict changes nothing.

**Remove** clears every key of the selected command; the command still works from its menu.
**Reset command** restores the built-in default. **Reset all** returns every command to its
defaults after a confirmation.

A command with no keys is normal: it is reachable from its menu or button. Some commands have no
default at all.

Changes apply immediately, without a restart. The menu shows the new key the next time it opens.

## What keeps its own keys

Text fields keep Copy, Paste, Select all, Undo and every typed character. Open dialogs keep
Enter/Escape. Tap BPM keeps Space for tapping while its dialog is open. The shortcut recorder
itself takes the next key, whatever it is.

## Panel sizes

Three dividers resize the workspace:

- between the folder tree and the library list — folder width;
- between the library list and the Analysis panel — analysis width;
- above the preview — preview height.

Drag a divider to resize. Double-click it to restore that panel's default. **Settings → General →
Workspace → Reset panel layout** restores all three and the Favorites height; it does not touch
the window size, table columns or shortcuts.

A window too small for the panels falls back to a compact layout: the panels shrink instead of
overlapping. The size you chose is kept, and it returns when the window grows again. The DAW
plugin keeps its own panel sizes, separate from the standalone application.

## Where the data lives

Settings are stored in `Sonicator/settings.json`:

```
shortcuts.mac / shortcuts.windows   overrides per platform, keyed by stable command id
layoutStandalone / layoutPlugin     panel sizes in logical pixels
settingsSection                     the last Settings page
```

A missing id means "use the built-in defaults"; an empty list means "no key". macOS and Windows
profiles are separate: Command and Control are never silently converted. A profile written by a
newer build is preserved but not applied, so an older build cannot destroy it.
