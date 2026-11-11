# Sonicator Feature Backlog

The source of truth for roadmap items is the owner's external list:
`~/Documents/orange303/Sonicator Features.md`.

This file only tracks state, so the two lists cannot drift apart. Item numbers match the external list.
When an item is finished, record the commit or document here and append `✅` to the external line.

## Status

| № | Item | State |
|---|---|---|
| 1 | Crash reporting: log to GitHub, group similar crashes into one issue | Partly: the app writes a rolling log and a crash marker, and a report opens a prefilled issue. Automatic upload and grouping need a server. `docs/DIAGNOSTICS.md` |
| 2 | **Report** in Settings: bug, feature request, or format/bank support with a sample file | Done: **Settings → REPORT** has three buttons that open the matching prefilled issue; a sample file is attached by the user in GitHub. `docs/DIAGNOSTICS.md` |
| 3 | Integration with Sample Dicer, Loop Dicer, Drum Dicer (engine selection, Send to …) | Not started |
| 4 | Tag-cloud tab: every file a point coloured by its tag, audition on hover, click-free stop | Not started |
| 5 | Key and BPM passed to the DAW on drag | Not started |
| 6 | Plugin system for new formats and network services | Not started |
| 7 | AI agents for library organisation | Not started |
| 8 | DAW panel integration (a tab like Splice in Studio One) | Research not started |
| 9 | VST3 and AU with full drag-and-drop, Match BPM, Match Scale | Partly: VST3/AU build, host audio and MIDI, embedded dialogs; Cubase accepted. Other hosts, shared catalog, Match BPM/Scale open. `docs/plans/2026-11-11-vst3-au-plugin-spike.md` |
| 10 | Multi-format converter with settings | Not started; only selection export to WAV exists |
| 11 | Smart AI content-type detection | Stage 1: hybrid metadata + acoustic rules + adaptive prototypes. `docs/SMART_ANALYSIS.md` |
| 12 | Training on user libraries, opt-in at first run and in Settings | Done for collection: first-run consent, per-catalog watermark, background upload, preview and delete in Settings → Library, Cloudflare Worker intake in `services/contribute-worker/`. Training, aggregation and dataset publication are the next stage. `docs/PRIVACY.md`, `docs/superpowers/specs/2026-11-11-training-data-contribution-design.md` |
| 13 | Collect custom tags and sample types into later builds as built-in | Storage exists (`custom_types`, user tags); the collection step into a build does not |
| 14 | Credits in About | Done: frameworks, libraries, decoders. `ui/settings/SettingsPanel.cpp` |
| 15 | Boosty donation link | Done: Settings → About. `ui/settings/SettingsPanel.cpp` |
| 16 | Move a waveform selection while playing; wheel changes its size | Done: dragging publishes the live selection and the wheel scales it around its centre (`72f6387`) |
| 17 | Turning on MIDI preview temporarily silences playback | Done (`72f6387`) |

## Open owner decisions

- **ONNX Runtime** is linked into every build (tens of megabytes), but no model file ships and nothing
  calls `loadModel`, so the classifier branch is inactive; Type comes from DSP rules and learned
  prototypes. Keep the library for a future model, drop it, or ship a model — the owner decides.
  Recorded in `docs/DEPENDENCIES.md`.
- **Repository location:** the checkout lives in `~/Documents`, which iCloud synchronises. Builds inside
  it stall on evicted files and git operations that walk the history take minutes. Moving the checkout
  outside iCloud is recommended and not yet done.
