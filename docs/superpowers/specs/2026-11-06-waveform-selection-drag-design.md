# Drag and drop samples and selections to Finder, messengers and DAWs

Next stage after page sizes. Goal: behave like Finder/Explorer drag and drop. Dragging a library
row carries the **original** file(s); dragging a waveform selection carries a newly exported WAV of
exactly that range. The drop target decides what to do: copy to a folder/desktop, attach to a
message, or import onto a DAW track. Nothing is moved or deleted from the library.

## Steps

- [ ] 1. External drag from library rows: single and multi-selection, original paths, existing files only
- [ ] 2. Selection model and waveform interaction (drag to select, click to seek/clear, overlay, clear action)
- [ ] 3. `AudioExporter` service: chunked WAV export of a time range with naming, partial-file safety and unit tests
- [ ] 4. Background selection preparation with generation/cancellation and ready/error state
- [ ] 5. External drag of the prepared selection WAV, started from inside the selected range
- [ ] 6. Preview audition of the selected range (play from start, stop/loop at end, clear on new file)
- [ ] 7. UI polish: range label, Preparing/Ready feedback, Clear button, Escape interaction
- [ ] 8. Tests, snapshots, docs, Debug/Release, manual drags to Finder/desktop/messenger/DAW, graphify update

## Intent

The browser should feel like dragging files out of Finder:

- drag one or more rows → the target receives the original audio file(s);
- drag a selected waveform range → the target receives a WAV containing only that range;
- desktop/Finder copies, messengers attach, DAWs import onto a track — all through standard file
  URLs, with no per-application integration;
- the library originals are never moved, renamed or deleted.

This is also the foundation for later Split Samples and export workflows.

## Scope

- External file drag only. No internal drag between app panes and no drag of MIDI, multiple ranges
  or multiple selections.
- Selection inside one file only; export as WAV; no time-stretch, pitch-shift, normalization, fades
  or crossfades.
- No destructive editing and no writing to the original audio path.
- No automatic cleanup of exported selection files in this stage.

## Design

### Library row drag (original files)

- A mouse listener on the table tracks mouse-down/drag. After a 5 px threshold on a row, start
  `juce::DragAndDropContainer::performExternalDragDropOfFiles(paths, false, sourceComponent)`.
- `paths` are the selected rows' original full paths, in current order, existing files only. If the
  row under the mouse is not selected, select it first.
- Multi-selection drags all selected existing files. A missing file is skipped; if none remain, no
  drag starts and the status explains why.
- `canMoveFiles = false` is essential: the OS must copy or import, never offer/drive a move of the
  library original. The app itself never deletes or renames it.
- The source component is the table/list panel; the OS supplies the drag image and cursor. Drop
  targets (Finder, desktop, Messenger, DAW) behave exactly as for a Finder drag because they
  receive standard file URLs.
- Dragging does not alter the selection; a normal click still seeks/previews.

### Waveform selection (new WAV)

- `WaveformDisplay` stores a `Range { double start, end; }` in seconds, clamped to the thumbnail
  length, plus the existing playhead.
- Mouse down records the anchor. A drag of more than ~3 px on the waveform creates a selection;
  mouse up finalizes it. A plain click seeks and clears the selection. Dragging **inside an
  existing selection** starts the external drag of the prepared WAV instead of editing it.
- New file or `setFile` clears the selection. Escape clears it when the preview has focus.
- Overlay: translucent accent rectangle, start/end lines, compact `0.250–1.100 s` label.
- A small `Clear` button appears while a selection exists; it follows the existing no-keyboard-focus
  policy for transport buttons.
- Pure pixel↔time helpers keep the geometry unit-testable independently of mouse events.

### Export service

- `core/export/AudioExporter`:
  - `ExportResult exportRange(const juce::File& source, double start, double end, const juce::File& destination) const`.
  - Opens the source through `AudioFormatManager`, clamps frames to the reader length, requires at
    least one frame, and reads/writes in bounded chunks (for example 64k frames) so a long range
    does not allocate the whole buffer.
  - WAV with the source sample rate and channel count. Bit depth: 16-bit source → 16-bit PCM,
    24/32-bit integer → 24-bit PCM, float source → 32-bit float, compressed source → 24-bit PCM.
    No gain change.
  - Writes to `destination + ".part"` and renames only on success; failures delete the partial
    file and return an error. The source is opened read-only.
  - If the channel count is unsupported by `WavAudioFormat`, fail with a clear error rather than
    silently downmixing.
- Destination folder: `Sonicator/exports/` under the application-data location (macOS
  `~/Library/Sonicator/exports`, Windows `%APPDATA%\Sonicator\exports`).
- File name: sanitized source stem + range in milliseconds + a short unique suffix, e.g.
  `Kick_01_0250-1100_a1b2c3.wav`. Sanitize separators/reserved characters and cap length; a source
  name can never escape the exports folder.
- Files are **not** deleted automatically: a DAW project or a message may reference them. Document
  disk growth; a "Clear exports" action can follow with Settings.

### Preparation and dragging the selection

- On selection change, a background worker exports the range to the exports folder. Only one job at
  a time; a new selection increments a generation and cancels/ignores the older result. Until ready
  show `Preparing selection…`; on failure show the error and disable dragging.
- Dragging from inside the selection uses
  `performExternalDragDropOfFiles({readyFile}, false, this)`. No export happens inside the mouse
  event, so the drag starts immediately. If the file is not ready, do not start a drag; show a
  short status.
- The worker stops on PreviewPanel destruction; results return through a SafePointer.
- The exported WAV is a new file; the dropped copy/import is handled by the target, just like
  dragging from Finder.

### Audition

- Play with a selection seeks to its start and plays. A 30 Hz check in `PreviewPanel` stops at the
  end (or seeks back to the start when Loop is on). Accept one timer tick of tolerance; this is
  preview, not sample-accurate editing.
- Auto Preview on row change still plays the whole file; a new file clears the selection first.
- The time label keeps showing playhead/length; the range is drawn on the waveform.

## Tests

- Helper for external row drag: selected existing files are returned in order, missing files are
  skipped, an unselected row selects itself, and a non-empty drag is started only when paths exist.
- Export a 0.2–0.5 s range from a synthetic WAV and decode the result: sample rate, channels, bit
  depth, frame count and samples match; the original size/hash/mtime are unchanged.
- Mono/stereo sources; a compressed fixture exports successfully.
- Clamp start/end, reject zero-length/missing/corrupt sources, leave no `.part` file on failure.
- File-name sanitization and uniqueness in the exports folder.
- Long-range export uses bounded chunks; worker generation/cancel handling.
- Waveform geometry: pixel→time, ordering, clamping, 3 px threshold, clear on click/new file.
- Preview: clear on load, play from start, stop/loop at end (timer-driven test).
- UI snapshots with a visible selection.
- Manual, because system drag is not automatable: drag single and multiple rows to the desktop,
  Finder folder, a messenger, and a DAW track; drag a selection to the same targets; verify the
  originals remain and the dropped WAV matches the range.

## Acceptance criteria

- Dragging rows behaves like Finder: desktop/Finder copies, messengers attach, DAWs import; the
  originals stay in the library and on disk.
- Multiple selected rows arrive as multiple files where the target supports it.
- Dragging a selection produces a playable WAV containing exactly the chosen interval, saved by the
  target wherever it was dropped; the exports copy stays for DAW references.
- The UI stays responsive while preparing longer ranges; a drag starts promptly once ready.
- Debug and Release suites pass; the Release bundle remains ad-hoc signed.

## Risks / notes

- The OS/target decides copy vs import; this app never moves or deletes library files because
  `canMoveFiles` is false.
- Bit depth and channel count may still be rejected by an unusual host; the export error is
  explicit and the original is untouched.
- External file drag is verified on macOS only in this stage; Windows uses the same JUCE API and
  must be checked separately.
- Exported selections accumulate deliberately; a cleanup action is a follow-up.
- Selection playback is preview-grade (timer boundary), not a sample-accurate transport feature.
- The selection/export machinery is intentionally reusable for Split Samples later.
