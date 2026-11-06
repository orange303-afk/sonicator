# Settings and About — design

Stage after library maintenance. Scope: persisted theme, interface scale, language placeholder,
catalog location, and the owner's About/support block.

## Goals

- Settings survive restarts and live in a fixed application-data location, not in the catalog.
- Theme: system (follows the OS), dark, light, applied at startup and immediately on change.
- Interface scale: 100/110/125/150%, applied at startup and on change.
- Language value stored for future translations; only English exists and the UI says so.
- Catalog location: copy the current catalog to a new file and switch to it, or open an existing
  SONICATOR catalog. Keep the previous file. Refuse during indexing.
- About: exact owner text with clickable Gumroad, Bandcamp and Ko-fi links.
- Audio-device selection remains reachable, now from Settings.

## Non-goals

- No translations, no per-component scale, no catalog merge, no automatic deletion of old files.
- No theme editor or custom accents.

## Decisions

- `core/settings/AppSettings` stores JSON in `Sonicator/settings.json` via a temporary file and
  replace. Unknown/invalid values fall back to defaults; scale is clamped to 75–200 (UI offers
  100/110/125/150).
- The theme is a mutable palette with dark and light values. System mode reads
  `Desktop::isDarkModeActive` and a `DarkModeSettingListener` keeps it live. Components with
  constructor-set colours implement `lookAndFeelChanged`; custom paint reads the palette directly.
  The silver wordmark is replaced by typeset text on light backgrounds.
- Scale uses `Desktop::setGlobalScaleFactor`; `app/Main.cpp` applies the saved value before the
  window appears.
- Catalog switching builds new connections and runs `createTables` before touching the current
  repositories. The old file is kept after a copy. Open mode requires `SampleRepository::isCatalog`
  (both `samples` and `roots` tables plus a nonzero `user_version`), so a foreign SQLite file is
  never altered. Switching is refused while indexing.
- The saved catalog path is cleared and the default used when the saved file is missing or invalid
  at startup; the event is logged.
- Settings are saved on change (theme/scale/language) and after a successful catalog switch.
- Language is disabled in the UI because no second translation exists.

## Verification

Settings JSON round-trip and invalid values; theme/scale callbacks; all four support URLs; dialog
lifecycle; live switch/copy with persisted path; rejection of foreign and missing catalogs without
modifying them; light and dark rendering. Debug and Release suites plus CTest.
