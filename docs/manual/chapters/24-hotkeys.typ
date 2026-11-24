#import "../style.typ": *

#chapter(24, "Hotkeys and layout",
  lead: [Every registered command has a stable identity, so a key can move without breaking a menu; menu-only actions keep their own behaviour. This chapter covers rebinding, conflicts and the three panel dividers.],
  inthis: (
    [Rebinding a command],
    [When a key is already used],
    [What keeps its own keys],
    [Panel sizes and the compact layout],
  ),
)[

#section("Rebinding")

Open *Settings → Hotkeys*. Search by command name, category or id, then click the command.

1. *Add shortcut* captures the next key combination. A modifier on its own is ignored; Escape cancels
   the capture.
2. *Apply* stores it, *Cancel* discards it.
3. If the key is already used by a command reachable from the same place, SONICATOR names that command
   and offers *Replace*. Replacing removes the key from the other command and keeps its remaining keys.
   Cancelling the conflict changes nothing.

*Remove* clears every key of the selected command; the command still works from its menu. *Reset
command* restores its built-in default. *Reset all* returns every command to defaults after a
confirmation.

A command with no keys is normal: it is reachable from a menu or a button, and some commands have no
default key at all. Changes apply immediately; a menu shows the new key the next time it opens.

#shot("/docs/manual/assets/settings-hotkeys.png", [Figure 24.1 — The Hotkeys page. The list shows each command's category, scope and effective keys.], width: 100%)

#section("Conflicts")

The keyboard router resolves a key by the focused scope and the effective binding. A conflict is only a
conflict when both commands can be reached from the same place; the same key in the library and in a
text field is not a clash.

A profile written by a newer build is preserved but not executed, so an older build cannot destroy it.
A conflicting user binding is dropped with a diagnostic instead of running two handlers.

#section("What keeps its own keys")

- Text fields keep Copy, Paste, Select all, Undo and every typed character.
- Open dialogs keep Enter and Escape.
- Tap tempo keeps Space for tapping while its dialog is open.
- The shortcut recorder itself takes the next key, whatever it is.
- Typing in search does not trigger library commands.

This is deliberate: a global binding that steals Space from a text field would make the search box
unusable.

#section("Panel layout")

Three dividers resize the workspace:

- between the folder tree and the library list — folder width;
- between the list and the Analysis panel — analysis width;
- above the preview — preview height.

Drag a divider to resize; double-click it to restore that panel's default. *Settings → General →
Workspace → Reset panel layout* restores all three and the Favorites height. It does not touch the
window size, table columns or shortcuts.

A window too small for the panels falls back to a compact layout: the panels shrink instead of
overlapping. The size you chose is kept and returns when the window grows again. The plug-in keeps its
own panel sizes, separate from the standalone application.

#section("Where preferences live")

Theme, scale, language, catalog path, window geometry, Hide empty, Show loops, Show one-shots, the
search-history click preference, the last ten searches and the shortcut overrides live in
`settings.json` in the application-data folder (chapter 2). Shortcut profiles are separate for macOS
and Windows: Command and Control are never silently converted.

]
