# VST3 and AU plugin

`plugin/` builds `SONICATOR` as a VST3 and an AudioUnit instrument. The window is the same
`MainComponent` the application uses, around the same catalog: the plugin is a browser for a library
owned by the application, not a second catalog.

## Identity

| Field | Value |
|---|---|
| Product / plug-in name | `SONICATOR` |
| Company | `Ilya Orange` |
| Bundle id | `com.sonicator.plugin` |
| Manufacturer code | `Orng` |
| Plug-in code / AU subtype | `Sncr` |
| AU type | `aumu` (instrument) |
| VST3 category | `Instrument | Synth` |

`auval -v aumu Sncr Orng` is the acceptance command.

## Build and install

For selectable application + plugin release installers and unsigned macOS quarantine handling,
see [RELEASE_INSTALLERS.md](RELEASE_INSTALLERS.md). Development build copying below is separate
from packaging and never runs unless explicitly enabled.

The targets are off by default and do not affect the application build.

```bash
cmake --preset plugin-universal
cmake --build --preset plugin-universal --target SonicatorPlugin_VST3 SonicatorPlugin_AU
```

The preset builds Release for `arm64;x86_64` with tests off, in
`~/Library/Caches/sonicator-build/plugin-universal`. Every wrapper bundle gets
`Contents/Frameworks/libonnxruntime.dylib`, an `@loader_path/../Frameworks` rpath, the VST3
`moduleinfo.json`, and an ad-hoc signature. Copying into the user's plug-in folders is opt-in:

```bash
cmake -S . -B ~/Library/Caches/sonicator-build/plugin-install -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
  -DBUILD_TESTING=OFF -DSONICATOR_BUILD_PLUGIN=ON -DSONICATOR_INSTALL_PLUGIN=ON
cmake --build ~/Library/Caches/sonicator-build/plugin-install \
  --target SonicatorPlugin_VST3 SonicatorPlugin_AU
```

With `SONICATOR_INSTALL_PLUGIN=ON` the build copies `SONICATOR.vst3` to
`~/Library/Audio/Plug-Ins/VST3` and `SONICATOR.component` to
`~/Library/Audio/Plug-Ins/Components` after signing. Without it a build never replaces an installed
plug-in behind the user's back.

`SonicatorPluginSmoke` (macOS only) opens the editor in an ordinary application, against the shared
catalog, and quits after a few seconds. `SONICATOR_PLUGIN_SELECT=<rows>` times library selections;
`SONICATOR_PLUGIN_SETTINGS=1` checks the embedded Settings panel. It is a development aid, not a
product target.

## Shared catalog and settings

The editor opens the same catalog the application uses: the path saved in
`AppSettings::defaultSettingsFile()` when the catalog was moved, otherwise
`MainComponent::defaultDatabaseFile()` (`~/Library/Sonicator/sonicator.sqlite`). It reads
`AppSettings::defaultSettingsFile()` (`~/Library/Sonicator/settings.json`). There is no copy:
tags, stars, genre, BPM, favorites and theme changed in the plugin are visible in the application and
the other way round. SQLite runs in WAL mode with a 5 s busy timeout, so short writes from both
processes coexist.

### The application owns the schema

The catalog is one file with one schema version (`core/database/CatalogSchema.h`). Only the
application migrates it, because only the application ships every writer that knows the new layout.
The plugin reads the version through a read-only connection (`inspectCatalog`) and opens the
catalog without any DDL (`SampleRepository::requireCompatibleSchema()`): no `CREATE`, no
`ALTER`, no `PRAGMA user_version`, no backup file. Consequences:

| Situation | What the plugin does |
|---|---|
| Same schema | Opens the browser as usual. |
| Older schema (old application, new plugin) | Shows "This library belongs to an older SONICATOR…" and does not touch the file. |
| Newer schema (new application, old plugin) | Shows "This library was updated by a newer SONICATOR…" and does not touch the file. |
| Missing file | Shows how to create the library in the application. |
| A file that is not a catalog, or an unreadable one | Says so and leaves the file alone. |

A window that cannot open the catalog never writes the project state: the saved folder and sample
survive a closed or failed editor. `tests/TestPlugin.cpp` covers an older catalog, a newer one, two
editors and the application at once, and the absence of migration or backup files.

The plugin is deliberately a browser. These stay in the application, where the user starts them:

- adding a root folder, **Rescan**, **New files**, **Reanalyze all**, **Smart metadata**, AutoTag;
- **Split by silence**, **Extract samples**, **Normalize selection**, **Batch Processing**;
- removing a folder from the library, catalog switching and relocation;
- **Audio/MIDI Settings**: the host owns the device.

The toolbar and the library menu show a one-line explanation instead of the disabled jobs. Audio and
MIDI come from the host: `processBlock` feeds `AudioEngine` host mode, so preview audio and the DAW
keyboard play the selected sample and no device is opened.

## Keyboard focus

The editor asks for keyboard focus on the click that lands in it (`PluginEditor::mouseDown`), and
`EDITOR_WANTS_KEYBOARD_FOCUS` is on in the wrapper build. Some Windows hosts (Fender Studio Pro /
Studio One, reported on 0.2.0) deliver the click into the plug-in window but leave the keys with the
arrangement, so typing only worked through the clipboard; taking focus on the click is what makes the
next keystroke text. A click back in the arrangement returns its shortcuts, because the plug-in never
holds a global grab.

## Project state

Each DAW project stores a versioned XML block (`SONICATOR_PLUGIN`, version 1): the last folder, the
selected sample and the editor size. Restoring a project selects the sample and shows its waveform
without starting playback. The catalog path is never stored; it is shared with the application.
Panel sizes stay in `pluginLayout` in the shared settings, so they are the same in every project.

Unknown, truncated or foreign state blocks are ignored and fall back to defaults instead of failing
in the host; a block larger than 64 KB is treated as foreign data and never parsed.
`pluginLayout`, theme and other preferences are read from the shared settings file; the plugin never
reads or writes the application's `lastFolder` / `lastSamplePath`.

A host can load another project into a running instance (`setStateInformation` while the editor is
open). The editor polls `PluginProcessor::stateGeneration()` and applies the restored folder, sample
and window size to the visible window, so what the user sees matches the project without closing it.
The first load of a window is not a user edit: it never writes an empty selection into the project.

## Dialogs

Confirmation and prompt dialogs are drawn inside the editor, not as top-level windows: hosts do not
reliably keep a plugin's modal window above their own, and a modal window that dismisses on "input
outside" closes on the first click inside the DAW. `MainComponent` implements
`dialogs::EmbeddedDialogHost`; BPM (set and tap), tag and genre prompts, and the preview's
confirmations all use it. `TapTempoDialog` keeps its own window only outside the plugin. The
Settings panel was already embedded this way.

### Interface scale

General → Interface scale works in the plugin too. It scales the entire editor component tree,
including fonts, and resizes the plugin window without changing JUCE's process-wide desktop scale
(which belongs to the DAW). Window dimensions in project state are logical, unscaled dimensions,
so reopening a project does not apply the scale twice. The shared appearance preference is saved
as before; changing it in one editor does not force another open editor to resize.

Folder/Analysis widths, Preview height and Favorites height persist in `pluginLayout` when a
divider drag finishes. Reopening the editor or creating a fresh processor restores that layout
from settings. The plugin reads Favorites height from its own layout, not the standalone app's
legacy height field. `tests/TestPlugin.cpp` (`[layout-persistence]`) changes scale and all four
dividers, then verifies the actual geometry after editor recreation and new-instance state restore.

A **new instance without project state** starts at the last user-sized editor dimensions, read
from `pluginWindowWidth` / `pluginWindowHeight` in the shared settings (logical pixels). Those
dimensions are separate from the standalone app's bounds. The editor remembers them after a
250 ms resize pause, or immediately on close. Panels still come from `pluginLayout`, so the new
window has the same available area and layout rather than clamping them into the default window.
Existing project state takes precedence over these defaults. Opening/restoring a project, or
closing an unchanged older editor, does not replace the last user-sized global default.
Tests tagged `[new-instance-layout]` cover fresh instances without any state round-trip,
processors constructed before the resize, closing before the debounce, and host state priority.

Settings has a Close button in both standalone and embedded mode. Support links are stacked.
The plugin's library context menu labels the unavailable file-processing group **Processing**.
With Loop enabled, dragging a selection outside the editor displays “Отключите луп”; live movement
and resizing of the loop inside the editor remain available. Turn Loop off to export the selection.

## Match BPM / Match Scale

Two independent toggles in the transport group, below Play/Stop: **Match BPM** and **Match Scale**.
They exist only in the plug-in window, start off in a new project, and are saved per instance in the
DAW project (plugin state v2). Old v1 projects load with both off; host tempo is never stored.

- **Match BPM** reads the project tempo in `PluginProcessor::processBlock` through the JUCE playhead
  and publishes one lock-free atomic value. A loop is stretched offline to `sourceBpm / hostBpm`
  (allowed 0.5–2.0). Pitch is unchanged. One-shots and files without an explicit loop structure are
  reported as not applicable instead of being guessed into a loop.
- **Match Scale** transposes to a manually chosen tonic (12 notes) and mode (major/minor). The
  interval is the shortest between tonics, −6…+6 semitones (tritone +6). A mode is never converted:
  A minor → C major shows `Mode mismatch; cannot match scale`; choose C minor instead. A tonal
  one-shot can be tuned to the chosen tonic from its pitch/root.
- Both toggles together apply time and pitch in one offline render, so the length is computed once.
- Rendering runs in one persistent background worker (latest request wins, 250 ms tempo debounce).
  `Rubber Band 3.3.0` R3 offline is compiled in (`cmake/RubberBand.cmake`, built-in FFT/resampler).
  Nothing on the audio thread reads files, SQLite or DSP.
- The result is an immutable PCM24 WAV at the source rate under
  `~/Library/Sonicator/exports/match/<key>/<stem>-match.wav`. It is project media: never cleaned
  automatically, never overwritten. The key covers algorithm version, catalog identity, size/mtime,
  metadata revision, frame range, format and both transforms; a partial file is never published and
  two instances that ask for the same key share one validated file.
- While a result is active the waveform shows the prepared WAV and its duration. The catalog identity
  stays the original: tags, name and Analysis still belong to the selected row, and the temporary WAV
  never becomes a row or `lastSamplePath`. Save/Normalize/edit commands are disabled with an
  explanation until Match is off.
- Preview and drag use the same file: the library row, cloud point and waveform (whole file or a
  selection cropped from the prepared WAV) all resolve through one `resolveMatchedDrag` decision. A
  pending or blocked result starts no OS drag; batch drag with Match on says `Select one sample for
  Match`. Turning both toggles off restores the existing preview/drag path unchanged.
- MIDI stays original: the prepared result is a separate transport in `AudioEngine`
  (`setMatchedTransport`/`clearMatchedTransport`), so the original sampler, REX slices and container
  remain loaded and a NoteOn plays the untouched source. A matched waveform selection never reaches
  `setMidiSelection`.
- Not in this stage: realtime warp/tempo ramps, DAW key autodetection, major↔minor conversion,
  batch matched export, hover-map matching, and REX2/sampler-bank/tracker/disc matching (these are
  refused explicitly rather than matching only their first decoded part).

## Limits

- Windows is not built; the port is tracked in `docs/WINDOWS_PORT.md`. Rubber Band is configured
  for MSVC in CMake but has not been compiled there yet.
- A plain application build (no plugin, no tests) sets `SONICATOR_ENABLE_MATCH=OFF`, so Rubber
  Band is neither fetched nor linked and the Match controls are never created.
- REX2 loops, sampler banks, tracker songs and disc entries do not use Match in v1.
- Manual Cubase/Logic acceptance is still open; see `docs/PLUGIN_MATCH_ACCEPTANCE.md`.
- Signing is ad-hoc for local use; Developer ID signing and notarization are release work.

## Tests

`tests/TestPlugin.cpp` runs in the ordinary test binary:

- state round-trip, clamping, future versions, garbage input and the 64 KB limit;
- processor stores project state, opens no device until host mode, renders silence;
- two instances keep independent state; two editors open at once keep their own project;
- editor opens repeatedly with a missing catalog and with a real one;
- an older and a newer catalog are refused without migration, and without a backup file;
- the application and the plugin keep one catalog open at the same time;
- restoring a project selection does not autoplay and does not write the application's session;
- a state restored while the editor is open updates the visible selection and window size;
- an embedded BPM/tag/genre prompt and tap tempo are drawn inside the editor;
- scanning and file processing are disabled in the embedded window with an explanation;
- Match: v1 state defaults, four flag combinations, malformed values, two independent instances;
- host tempo publication from a fake playhead, invalid/missing tempo, release invalidation;
- policy: 8 s at 120→128, Am→Cm, tritone, one-shot, mode mismatch, ratio bounds, overflow;
- offline render: exact length, pitch within 5 cents, stereo coherence, silence, cache validation,
  atomic no-clobber, failed write, changed source;
- background service: latest-wins, cancel, exceptions, debounce, no stale Ready;
- UI: controls absent in standalone, present at 880 px and 100/125%, restore without autoplay,
  prepared waveform, read-only state, Stop during preparation, original MIDI transport retained.

## Manual host acceptance

Formats: Live 12 — VST3 and AU; REAPER — VST3 and AU; Cubase 15 — VST3; Bitwig — VST3. The checklist
lives in `docs/superpowers/plans/2026-11-15-plugin-product-p1.md` (section «Приёмка в хостах»).
