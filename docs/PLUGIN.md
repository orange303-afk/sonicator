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

The plugin is deliberately a browser. These stay in the application, where the user starts them:

- adding a root folder, **Rescan**, **New files**, **Reanalyze all**, **Smart metadata**, AutoTag;
- **Split by silence**, **Extract samples**, **Normalize selection**, **Batch Processing**;
- removing a folder from the library, catalog switching and relocation;
- **Audio/MIDI Settings**: the host owns the device.

The toolbar and the library menu show a one-line explanation instead of the disabled jobs. Audio and
MIDI come from the host: `processBlock` feeds `AudioEngine` host mode, so preview audio and the DAW
keyboard play the selected sample and no device is opened.

## Project state

Each DAW project stores a versioned XML block (`SONICATOR_PLUGIN`, version 1): the last folder, the
selected sample and the editor size. Restoring a project selects the sample and shows its waveform
without starting playback. The catalog path is never stored; it is shared with the application.
Panel sizes stay in `pluginLayout` in the shared settings, so they are the same in every project.

Unknown, truncated or foreign state blocks are ignored and fall back to defaults instead of failing
in the host. `pluginLayout`, theme and other preferences are read from the shared settings file; the
plugin never reads or writes the application's `lastFolder` / `lastSamplePath`.

## Limits

- Match BPM and Match Scale are P2: both need time-stretch / pitch-shift and a decision about which
  host tempo/key information is available. They are not part of this build.
- Windows is not built; the port is tracked in `docs/WINDOWS_PORT.md`.
- Signing is ad-hoc for local use; Developer ID signing and notarization are release work.

## Tests

`tests/TestPlugin.cpp` runs in the ordinary test binary:

- state round-trip, clamping, future versions and garbage input;
- processor stores project state, opens no device until host mode, renders silence;
- two instances keep independent state;
- editor opens repeatedly with a missing catalog and with a real one;
- restoring a project selection does not autoplay and does not write the application's session;
- scanning and file processing are disabled in the embedded window with an explanation.

## Manual host acceptance

Formats: Live 12 — VST3 and AU; REAPER — VST3 and AU; Cubase 15 — VST3; Bitwig — VST3. The checklist
lives in `docs/superpowers/plans/2026-11-15-plugin-product-p1.md` (section «Приёмка в хостах»).
