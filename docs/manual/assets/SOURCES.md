# Manual plate provenance

Every PNG in this folder is rendered by `tests/TestManualShots.cpp`
(`[.manual-shots]`) from the real components, against a temporary synthetic catalog of 313
short WAV files, twelve tags, stars, metadata overrides, one duplicate pair, one missing file and
one decode-error row. No image is a mockup and nothing comes from the owner's library.

Regenerate:

```sh
cmake --build --preset debug --target SonicatorTests
SONICATOR_MANUAL_SHOT_DIR=$PWD/docs/manual/assets \
  ~/Library/Caches/sonicator-build/debug/tests/SonicatorTests "[.manual-shots]"
```

| Plate | Chapter | Rendered from | State |
|---|---|---|---|
| `workspace.png` | 3 | `MainComponent`, 1400×900 at 2× | Desktop library, 313 rows, no selection |
| `folder-tree.png` | 3, 4 | folder tree component | Library root open, `Drums/Kicks` selected |
| `table.png` | 5 | library table | First row selected, all columns visible |
| `search.png` | 6 | header, toolbar and table | Query `type:kick key:Am`, one match |
| `tags.png` | 7 | toolbar, table and Analysis panel | Tag filter `Kick`, two rows selected |
| `analysis-card.png` | 3, 9 | Analysis card | `Kick_808_Am_120.wav`, measured features |
| `analysis-hero.png` | 3, 9 | Key/BPM hero | Same file, manual key and BPM |
| `preview-wave.png` | 10 | Preview waveform | Waveform loaded, transport at zero |
| `preview-controls.png` | 10 | Preview transport | Time, play/stop, volume, toggles |
| `selection.png` | 11 | Preview band | Range 0.250–0.400 s, export ready |
| `cloud.png` | 14 | full window, 2× | Cloud tab, 241 tagged files, overview |
| `settings-general.png` | 23 | `SettingsPanel` | General page, dark theme |
| `settings-hotkeys.png` | 24 | `SettingsPanel` | Hotkeys page with the real command registry |
| `plugin.png` | 22 | `PluginEditor` | 1400×820, dark settings, Match controls visible |
| `split.png` | 13 | `SplitDialog` | Defaults, save next to originals |
| `extract.png` | 13 | `ExtractDialog` | Defaults, one source |
| `autotag.png` | 8 | `AutoTagDialog` | 64 new proposals for untagged rows, folder column hidden |
| `batch.png` | 21 | `BatchProcessingDialog` | Four-step cleanup chain, WAV output, eight sources |

The library lives in a temporary directory. Split, Extract and Settings display documented or
stable paths instead of the temp folder, because those dialogs only need a path for display; no
audio is read from the shown path.
