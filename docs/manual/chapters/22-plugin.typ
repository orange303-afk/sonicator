#import "../style.typ": *

#chapter(22, "The SONICATOR plugin",
  lead: [The same browser inside the DAW: one shared catalog, a read-only window, and two toggles that fit a loop to the project tempo or scale.],
  inthis: (
    [What the VST3 and AU versions are],
    [The shared catalog and the read-only rule],
    [Project state and window size],
    [Match BPM and Match Scale],
    [What the plugin refuses to do],
  ),
)[

#section("Identity")

The plug-in is called *SONICATOR*, published as VST3 and AU by *Ilya Orange* (bundle id
`com.sonicator.plugin`). The AU is an instrument (`aumu`, subtype `Sncr`, manufacturer `Orng`); the
VST3 category is Instrument | Synth. On macOS it installs to `/Library/Audio/Plug-Ins/VST3` and
`/Library/Audio/Plug-Ins/Components`; on Windows the VST3 goes under Common Files.

It is a browser, not a sampler instrument. The instrument identity exists because hosts need a
category; audio comes from the host and the selected sample is auditioned through the host's device.


#section("One catalog, read-only")

The editor opens the catalog saved in the shared settings, or the default `sonicator.sqlite`. Tags,
stars, genres, BPM, favourites and theme changed in the plug-in are visible in the application and the
other way round. SQLite runs in WAL mode with a 5 s busy timeout, so short writes from both processes
coexist.

The application owns the schema. The plug-in opens the catalog with a read-only connection: no
`CREATE`, no `ALTER`, no migration, no backup file. If the catalog belongs to a different schema
version, the window explains which side is newer and leaves the file alone:

- an older schema: *This library belongs to an older SONICATOR…*;
- a newer schema: *This library was updated by a newer SONICATOR…*;
- a missing or invalid file: how to create the library in the application.

Every job that writes audio or changes the catalog stays in the application: adding roots, scans,
smart analysis, AutoTag, Split, Extract, Normalize selected, Batch Processing, folder removal, catalog
switching and relocation. The toolbar and the library menu show a one-line explanation instead of a
disabled button.

#plate("/docs/manual/assets/plugin.png", [Figure 22.1 — The plugin window at 1400×820. Match BPM and Match Scale sit under the transport; the library is shared with the standalone application.])

#section("Project state")

Each DAW project stores a versioned XML block: the last folder, the selected sample and the editor
size. Opening a project selects the sample and shows its waveform without starting playback. Panel
sizes live in the shared settings, not in the project, so they are the same in every project.

Unknown, truncated or foreign state blocks are ignored and fall back to defaults. A block larger than
64 KB is treated as foreign data. A window that cannot open the catalog never writes project state, so
a saved selection survives a closed or failed editor.

Dialogs are drawn inside the editor rather than as top-level windows, because hosts do not reliably
keep a plug-in's window above their own. The editor size is stored in the project, so a project
opens at the size it was saved with; panel sizes are shared through the settings file.

#section("Match BPM and Match Scale")

Two toggles below the transport exist only in the plug-in. Both start off in a new project and are
saved per instance.

- *Match BPM* reads the project tempo from the host and stretches a loop offline to
  `sourceBpm / hostBpm` (allowed 0.5–2.0). Pitch is unchanged. One-shots and files without an explicit
  loop structure are reported as not applicable instead of being guessed into a loop.
- *Match Scale* transposes to a chosen tonic (12 notes) and mode (major/minor). The interval is the
  shortest between tonics, −6…+6 semitones. A mode is never converted: A minor to C major reports
  *Mode mismatch; cannot match scale*; choose C minor instead. A tonal one-shot can be tuned to the
  chosen tonic from its pitch or root.

Both together apply time and pitch in one offline render. Rendering runs on one background worker
(latest request wins, 250 ms tempo debounce) with Rubber Band 3.3.0 R3. Nothing on the audio thread
reads files, SQLite or DSP.

The result is an immutable PCM24 WAV at the source rate under `exports/match/`.
It is project media: never cleaned automatically, never overwritten. A partial file is never published,
and two instances asking for the same key share one validated file. The catalog row stays the
original: tags, name and Analysis still describe the source; the matched WAV never becomes a row. Save,
Normalize and edit commands are disabled while a matched result is active.

Preview and drag use the matched file: the library row, a cloud point and a waveform selection all
resolve the same way. A pending or blocked result starts no OS drag. MIDI stays original: notes play
the untouched sampler, REX slices and containers. Turning both toggles off restores the ordinary path.

#section("Limits")

- REX2 loops, sampler banks, tracker songs and disc entries do not use Match in v1; the plug-in refuses
  them explicitly rather than matching only the first decoded part.
- Real-time warp, tempo ramps, DAW key autodetection, major↔minor conversion and batch matched export
  are not implemented.
- Host acceptance in Live 12, REAPER, Cubase 15 and Bitwig is still manual work. The AU acceptance
  command is `auval -v aumu Sncr Orng`.
- Windows plug-in builds are not part of this edition's verified set.


]