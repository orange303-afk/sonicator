# Settings, Hotkeys and panel layout — acceptance report

Date: 2026-11-14. Branch: `stage-settings-hotkeys-layout` (based on `2730ae0`).

## What shipped

- Settings is a navigation column plus one page per section: General, Audio & MIDI, Hotkeys,
  Plugins (honest "coming later"), About. Each page keeps its own scroll position; the last
  section is stored; the Audio page embeds the device selector instead of opening a second window.
- A command registry now backs the waveform editor, transport keys, `Settings`, focus search,
  add last tag and the layout resets. Menus and the Hotkeys page read the same effective bindings.
- Settings → Hotkeys: search, capture with Escape cancel, several keys per command, remove, reset
  one, reset all with confirmation, conflict detection with an explicit Replace.
- Three dividers resize Folders, Analysis and Preview. Sizes persist per profile (standalone and
  DAW plugin separately), survive window shrinking, and reset per panel or all at once.
- Studio visual layer: spacing/typography tokens, a vector icon set, colour-id driven buttons and
  left-aligned navigation. Applied to **Settings and Hotkeys only**. The workspace still uses the
  previous styling: the icon set is not used by any button yet, and the library table, toolbar,
  Analysis panel, preview transport and dialogs have not been restyled. Finishing that pass is a
  separate stage.

## Verification actually run

| Check | Result |
|---|---|
| `cmake --build --preset debug --target SonicatorTests` | builds |
| `ctest --preset debug` | 1/1 passed, 34 s |
| Full suite, three randomised orders | 1028 cases / ~14 270 assertions passed each time |
| `cmake --build --preset release --target Sonicator` | `SONICATOR.app` linked and ad-hoc signed |
| Plugin spike VST3, separate tree, `SONICATOR_INSTALL_PLUGIN_SPIKE=OFF` | bundle built; user plug-in folder unchanged (installed copy still dated 2026-11-11) |
| Rendered snapshots (dark) | workspace, Settings → General, Settings → Hotkeys written from the real components |

## Not verified

- **Windows**: no machine or CI run in this session. The layout model, settings codec and shortcut
  profiles are platform-independent, but nothing was executed there.
- **A real DAW host** (Cubase/Logic/Ableton/REAPER): the plugin bundle builds; no host was driven.
  Embedded-mode behaviour is covered only by headless tests with a fake host engine.
- **Interface scale 125/150% and a small physical display**: not driven interactively. The layout
  math and minimum sizes are covered by unit tests and a compact fallback, not by a person looking
  at a 150% screen.
- **Physical keyboard on macOS with a Russian layout**: the router matches JUCE key codes; no
  manual pass was made with a second layout.

## Known limits in this stage

- Parameterised context-menu commands (tag/type/BPM pickers, split, extract, folder management,
  starring, page navigation) remain menu-only and are not rebindable; `docs/COMMANDS.md` lists
  them explicitly. They were not moved because their menu branches must be extracted into shared
  handlers first.
- `Save copy` and `Save and overwrite` are still reached through the existing Save dialog rather
  than as separate registry commands.
- Pre-existing JUCE debug assertions from non-ASCII literals in `MainComponent.cpp` and
  `PreviewPanel.cpp` still print in Debug builds. They predate this branch and do not affect tests.
- The Plugins page is a placeholder; no SDK or loader is included.

## How to check it by hand

1. Open Settings; switch every page; close and reopen: the last page returns.
2. Hotkeys: rebind `Normalize` to `Alt+K`, reopen the waveform menu — it shows `Alt+K`; press `N`
   — nothing happens; press `Alt+K` — it normalizes.
3. Remove every key of `Delete region`; press Backspace in the waveform — the region is kept.
4. Open the Audio & MIDI page: the selector is inside the page, and switching away and back does
   not reopen the device.
5. Drag all three dividers, resize the window smaller and larger, restart: your sizes return.
   Double-click each divider; then Settings → General → Reset panel layout.
6. During indexing, open Settings: catalog buttons stay disabled and re-enable when indexing ends.
7. In the plugin (host editor), Settings shows General/Hotkeys/About only, and no key reaches the
   library behind the overlay.
