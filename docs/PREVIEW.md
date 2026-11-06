# Folder navigation and instrument preview

## Folder tree

The sidebar contains registered library roots and folders represented by catalogued files. Empty added roots remain visible; folders with no indexed audio are not separately discovered. Select **All samples** for the entire catalog, or a folder for its files and every descendant. Left/Right collapse/expand the selected folder. Children are materialised on expansion; paths are grouped once per unique folder. Selection and expanded branches survive scan completion.

Folder queries use the existing path index with separator-aware prefix bounds (Drums does not match Drums2); no schema migration or audio reanalysis. Results remain paged at 10,000 rows. Filename search and header sorting remain page-local, including inside a folder.

## Ordinary preview

Selecting a new file plays it. Programmatic selection restoration does not call the selection action. Reloading an unchanged selected path preserves its transport position. **Play** restarts after EOF; with **Loop** off, EOF stops permanently until another explicit Play. A changed file (size/mtime) is reloaded rather than treating its old preview as current.

**Skip Silence** is off by default. When enabled, the next start from Stop/EOF begins one frame before the first finite nonzero decoded sample in any channel. It does not trim files, apply a noise threshold or alter playback already in progress. Pause/resume retains its position. Entirely silent audio starts at zero. Detection is performed on the control thread using bounded decode blocks, not on the audio callback; scanning a long entirely silent file can take time.

## MIDI Preview

Enable **MIDI Preview** to play the selected sample from a hardware MIDI input instead of automatically playing it on selection. On first activation all currently available inputs are enabled. **Audio settings** includes input selection; use it to enable devices connected later. Disabling MIDI Preview stops voices and returns to ordinary preview (press Play or select a different file).

- 16 voices, mono/stereo audio, note 60 at original pitch; 12 semitones per octave, velocity-sensitive.
- Attack 2 ms, release 20 ms; NoteOff and sustain pedal supported. At the voice limit new notes are ignored rather than abruptly stealing an audible voice.
- Linear interpolation changes speed and pitch together; this is a preview sampler, not a formant-preserving or band-limited pitch shifter. Extreme upward transpositions may alias.
- Loop and Skip Silence are captured for each new voice. Loop edges have a short fade, finite one-shots fade toward EOF. Dense MIDI chords have a soft output safety knee above 0.9; single voices have headroom.
- MIDI note events are quantised to the next device audio block; no sample-accurate DAW scheduling is claimed. Input uses a bounded 2048-event queue; overflow/contention resets voices rather than leaving stuck notes. SysEx is ignored.
- Decoded MIDI audio is limited to 128 MiB and two channels. Oversized or multichannel files produce an explicit error, never a silent truncation; ordinary streaming preview remains available.
- Sample loading and source publication occur outside the audio callback, with output detached during publication. Render voices use preallocated state and no file/SQLite access. Existing JUCE transport/device-control locking remains; the whole application is not claimed formally lock-free.

Software-rendered tests cover actual samples and MIDI messages. Physical MIDI hardware, audible click checks and hot-plug behaviour still need owner acceptance on the real setup.
