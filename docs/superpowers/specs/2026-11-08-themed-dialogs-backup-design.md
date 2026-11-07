# Themed dialogs and Settings backup — design

Status: implemented 2026-11-08.

## Intent

Move database backup out of the main footer into Settings, name it **Backup database**, and make
dialog windows use the SONICATOR theme instead of JUCE's default alert and title-bar styling.

## Backup database

- The footer no longer has a Backup button.
- `Settings > Library` has a `Backup database` button. It calls the same SQLite backup path used
  before, writes the copy beside the catalog, and reports the saved path or the error in the
  Settings database status line.
- Original audio files are not included. The operation remains safe while the catalog is open.

## Dialog styling

- A small `ui/shell/ThemedDialogs` helper creates modal `AlertWindow`s with the owner's
  `SonicatorLookAndFeel`: theme colours, fonts, button height and themed buttons.
- Settings, Relocate, Audio/MIDI and AutoTag windows use a non-native title bar. The
  LookAndFeel draws the title bar in the theme surface colour and the current theme's title text.
  Close controls are plain cross icons with a subtle hover surface, not coloured traffic-light
  discs. The Settings footer close button uses the same cross language.
- The OS file chooser remains native; it is a system dialog, not an in-app window.
- Confirmation, prompt, warning and info alerts use the helper; the prompt variant keeps the tag
  name editor and the same OK/Cancel keys.

## Constraints

- No new schema or migration.
- macOS Debug remains the verified platform; Windows still needs separate acceptance.
- The settings panel grows to fit the new Library row; its backup button must remain inside the
  visible content bounds.

## Verification

- Settings backup writes one consistent copy and the dialog is non-native and themed.
- Confirmation alerts use the app LookAndFeel.
- Existing settings, relocation, tag, AutoTag and preview tests stay green.
