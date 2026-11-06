# Folder navigation and instrument preview

## Folder tree

The sidebar contains registered library roots and folders represented by catalogued files. Empty added roots remain visible; folders with no indexed audio are not separately discovered. Select **All samples** for the entire catalog, or a folder for its files and every descendant. Left/Right collapse/expand the selected folder. Children are materialised on expansion; paths are grouped once per unique folder. Selection and expanded branches survive scan completion.

Folder queries use the existing path index with separator-aware prefix bounds (Drums does not match Drums2); no schema migration or audio reanalysis. Results remain paged at 10,000 rows. Filename search and header sorting remain page-local, including inside a folder.

## Ordinary preview

**Auto Preview** is on by default: selecting a new file plays it, including when MIDI Preview is enabled. Turning it off makes selection silent; explicit **Play** remains available in both modes. Programmatic selection restoration does not call the selection action. Reloading an unchanged selected path preserves its transport position. **Play** restarts after EOF; with **Loop** off, EOF stops permanently until another explicit Play. A changed file (size/mtime) is reloaded rather than treating its old preview as current.

Buttons and checkboxes do not take keyboard focus on click; nonmodal toolbar and preview actions restore focus to the file list so Up/Down continue navigating. Search and the folder tree retain their own keyboard behaviour when explicitly focused; modal dialogs/choosers must retain focus while open. The header uses a larger tightly cropped wordmark without a separate decorative icon; the OS app icon is unchanged.

**Skip Silence** is off by default. When enabled, the next start from Stop/EOF begins one frame before the first finite nonzero decoded sample in any channel. It does not trim files, apply a noise threshold or alter playback already in progress. Pause/resume retains its position. Entirely silent audio starts at zero. Detection is performed on the control thread using bounded decode blocks, not on the audio callback; scanning a long entirely silent file can take time.

## Dragging original files

Select one or more library rows and drag them out of the window. The OS receives standard file URLs with `canMoveFiles = false`: Finder and the desktop copy, messengers attach, and DAWs import onto a track. The library original is never moved, renamed or deleted, and the app never needs per-target integration.

- A multi-row drag carries every selected existing original in current table order; missing files are skipped. If none remain, no drag starts and the status explains why.
- The row under the mouse is selected before the drag when it was not already selected. A normal click still seeks/previews.
- A drag starts after 5 px, so a click does not accidentally start an OS drag. Dragging does not alter the selection.
- Only external file drag is supported in this stage: no internal drag between app panes, no MIDI drag, and no multi-range drag.

## Waveform selection and export

Drag across the waveform by more than about 3 px to select a time range. Endpoints are clamped to the file and finalized in either direction. A plain click seeks and clears the selection; dragging from inside an existing selection starts the external file drag instead of editing it. A new file, `setFile`, or **Escape** clears the selection when the preview has keyboard focus. A **Clear** button appears while a selection exists and follows the same no-keyboard-focus policy as the transport buttons.

The overlay draws a translucent accent range, start/end lines and a compact `0.250–1.100 s` label. While a selection exists, a background worker exports it. The status reads **Preparing selection…**, then **Ready — drag the selection into a DAW**, or shows the explicit export error. Dragging before the file is ready is refused with that status; no export runs inside the mouse event, so the OS drag starts immediately once ready.

**Play** with a selection seeks to its start. The 30 Hz preview stops at the end, or seeks back to the start when **Loop** is on. This is preview accuracy (one timer tick), not sample-accurate editing. **Auto Preview** on row change still plays the whole file; loading a new file clears the selection first.

Selection export is WAV only and never modifies the source. It preserves the source sample rate and channel count; 16-bit sources stay 16-bit PCM, 24/32-bit integer sources become 24-bit PCM, float sources become 32-bit float, and compressed sources become 24-bit PCM. There is no gain change, normalization, fade or time-stretch. Export writes `destination + ".part"` in bounded chunks and renames only after success, so a failed or cancelled export leaves no partial file.

Exported selections live under `Sonicator/exports/` and are **not** deleted automatically: a DAW project or a message may still reference them. The folder grows until you remove files manually; a cleanup action is a later Settings feature. More than 64 channels is refused explicitly rather than downmixed.

External drag into DAWs/messengers is verified on macOS in this stage. Windows uses the same JUCE file-URL path but still needs separate verification.

## MIDI Preview

Enable **MIDI Preview** to play the selected sample from a hardware MIDI input. **Auto Preview** independently controls automatic audition on selection; ordinary preview and MIDI voices are mixed when both are active. On first activation all currently available inputs are enabled. **Audio settings** includes input selection; use it to enable devices connected later. Changing MIDI Preview stops current playback/voices; press Play or select a different file to audition again.

- 16 voices, mono/stereo audio, note 60 at original pitch; 12 semitones per octave, velocity-sensitive.
- Attack 2 ms, release 20 ms; NoteOff and sustain pedal supported. At the voice limit new notes are ignored rather than abruptly stealing an audible voice.
- Linear interpolation changes speed and pitch together; this is a preview sampler, not a formant-preserving or band-limited pitch shifter. Extreme upward transpositions may alias.
- Loop and Skip Silence are captured for each new voice. Loop edges have a short fade, finite one-shots fade toward EOF. Dense MIDI chords have a soft output safety knee above 0.9; single voices have headroom.
- MIDI note events are quantised to the next device audio block; no sample-accurate DAW scheduling is claimed. Input uses a bounded 2048-event queue; overflow/contention resets voices rather than leaving stuck notes. SysEx is ignored.
- Decoded MIDI audio is limited to 128 MiB and two channels. Oversized or multichannel files produce an explicit error, never a silent truncation; ordinary streaming preview remains available.
- Sample loading and source publication occur outside the audio callback, with output detached during publication. Render voices use preallocated state and no file/SQLite access. Existing JUCE transport/device-control locking remains; the whole application is not claimed formally lock-free.

Software-rendered tests cover actual samples and MIDI messages. Physical MIDI hardware, audible click checks and hot-plug behaviour still need owner acceptance on the real setup.
