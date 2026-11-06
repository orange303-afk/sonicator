# Waveform Selection and DAW Drag Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Behave like Finder/Explorer drag and drop: dragging library rows carries the original file(s), dragging a waveform selection carries an exported WAV of exactly that range, and nothing is moved or deleted from the library.

**Architecture:** Library rows start a standard external file drag of existing originals through a testable helper. Pure geometry helpers drive a selection state inside `WaveformDisplay`. A core `AudioExporter` writes a chunked, `*.part`-safe WAV. A `SelectionExporter` worker prepares the selection file in the background with generation-based cancellation, and `PreviewPanel` wires selection, status, Clear, drag, and preview-grade range audition together.

**Tech Stack:** C++20, JUCE 9.0.5 (`juce_audio_formats`, `juce_gui_basics`), Catch2 3.7.1, CMake/Ninja.

**Spec:** `docs/superpowers/specs/2026-11-06-waveform-selection-drag-design.md`

## Global Constraints

- Never write to, rename, or delete the original audio file; export is a new file. Library row drags always pass `canMoveFiles = false` and never alter the selection.
- Library row drags carry selected rows' original full paths in current order, existing files only; missing files are skipped, and no drag starts when none remain.
- Export format is WAV only. Preserve source sample rate and channel count.
  Bit depth: 16-bit source → 16-bit PCM; 24/32-bit integer → 24-bit PCM; float source → 32-bit float; compressed source → 24-bit PCM. No gain change, normalization, fades or time-stretch.
- Export destination is `<userApplicationDataDirectory>/Sonicator/exports`; tests inject a temporary folder instead.
- Read and write in bounded chunks; never allocate a buffer for the whole selected range. Write `destination + ".part"` and rename only after a successful finalize. Delete the partial file on every failure path.
- Exported files are never deleted automatically in this stage.
- Refuse more than 64 channels with an explicit error; never silently downmix or truncate.
- Keep the existing focus policy: every preview button/checkbox sets `setWantsKeyboardFocus(false)` and `setMouseClickGrabsKeyboardFocus(false)`.
- Selection audition is preview-grade (30 Hz timer), not sample-accurate.
- Debug and Release suites must pass; the Release macOS bundle stays ad-hoc signed.
- C++20; follow existing naming, `juce::` types, and test style.

## Review Focus

- Multi-row drags must contain every selected existing original in order, with missing rows skipped and no accidental move.
- Unusual channel layouts (3–64 channels, and >64) must export with the right count or fail explicitly, never downmix.
- Degenerate ranges (`NaN`, negative, zero-length, reversed, past EOF) must produce a clear error and no `.part` file, never a crash.
- Long ranges must stay chunked and cancelling quickly, so the UI remains responsive.
- Hostile/odd source names (`../`, `/`, `:`, very long, empty stem) must stay inside the exports folder and stay unique.
- Selection audition must not fight Loop/EOF/Skip Silence, and a click near the end must stop or wrap predictably.

---

### Task 1: Selection geometry and waveform interaction

**Files:**
- Create: `ui/waveform/WaveformSelection.h`
- Modify: `ui/waveform/WaveformDisplay.h`
- Modify: `ui/waveform/WaveformDisplay.cpp`
- Test: `tests/TestSelection.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `namespace sonicator::selection`
    - `struct Range { double start = 0, end = 0; bool valid() const; double length() const; };`
    - `double timeAtX(double x, int width, double totalLength);`
    - `float xAtTime(double time, int width, double totalLength);`
    - `Range orderedRange(double a, double b, double totalLength);`
    - `bool exceedsDragThreshold(int dx, int dy);`
    - `bool contains(const Range&, double time);`
    - `juce::String formatRange(const Range&);` → `"0.250–1.100 s"`
  - `WaveformDisplay` gains:
    - `void setSelection(selection::Range);`
    - `selection::Range selection() const;`
    - `void clearSelection();`
    - `double totalLength() const;`
    - `std::function<void(selection::Range)> onSelectionChanged;`
    - `std::function<void()> onExternalDragRequested;`

- [ ] **Step 1: Write the failing geometry and selection-state tests**

Create `tests/TestSelection.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "ui/waveform/WaveformDisplay.h"
#include "ui/waveform/WaveformSelection.h"
#include "TestHelpers.h"
using namespace sonicator;
TEST_CASE("Waveform geometry maps pixels to time and back", "[selection][geometry]")
{
    REQUIRE(selection::timeAtX(6, 106, 10.0) == Catch::Approx(0.0));
    REQUIRE(selection::timeAtX(106, 106, 10.0) == Catch::Approx(10.0));
    REQUIRE(selection::timeAtX(-100, 106, 10.0) == Catch::Approx(0.0));
    REQUIRE(selection::timeAtX(1000, 106, 10.0) == Catch::Approx(10.0));
    REQUIRE(selection::xAtTime(5.0, 106, 10.0) == Catch::Approx(56.0).margin(0.01));
    REQUIRE(selection::timeAtX(selection::xAtTime(6.25, 106, 10.0), 106, 10.0)
            == Catch::Approx(6.25).margin(0.001));
}
TEST_CASE("Waveform selection orders and clamps its endpoints", "[selection][geometry]")
{
    auto r = selection::orderedRange(0.8, 0.2, 1.0);
    REQUIRE(r.start == Catch::Approx(0.2)); REQUIRE(r.end == Catch::Approx(0.8));
    REQUIRE(selection::orderedRange(-1.0, 0.4, 1.0).start == Catch::Approx(0.0));
    REQUIRE(selection::orderedRange(0.4, 9.0, 1.0).end == Catch::Approx(1.0));
    REQUIRE_FALSE(selection::orderedRange(0.5, 0.5, 1.0).valid());
    REQUIRE(selection::contains(r, 0.5)); REQUIRE_FALSE(selection::contains(r, 0.9));
}
TEST_CASE("Only drags beyond three pixels create a selection", "[selection][geometry]")
{
    REQUIRE_FALSE(selection::exceedsDragThreshold(3, 0));
    REQUIRE_FALSE(selection::exceedsDragThreshold(0, -3));
    REQUIRE(selection::exceedsDragThreshold(4, 0));
    REQUIRE(selection::exceedsDragThreshold(0, 4));
}
TEST_CASE("Selection range label uses seconds with millisecond precision", "[selection][label]")
{
    REQUIRE(selection::formatRange({0.25, 1.1}) == juce::String::fromUTF8("0.250–1.100 s"));
}
TEST_CASE("Setting a file or clearing notifies once and resets the selection", "[selection][state]")
{
    TestDirectory t; WaveformDisplay waveform(t.dir.getChildFile("cache"));
    int changes = 0; selection::Range seen;
    waveform.onSelectionChanged = [&](selection::Range r) { ++changes; seen = r; };
    waveform.setSelection({0.4, 0.2});
    REQUIRE(waveform.selection().start == Catch::Approx(0.2));
    REQUIRE(waveform.selection().end == Catch::Approx(0.4));
    REQUIRE(changes == 1);
    waveform.clearSelection();
    REQUIRE_FALSE(waveform.selection().valid());
    REQUIRE(changes == 2);
    waveform.clearSelection();
    REQUIRE(changes == 2);
    waveform.setSelection({0.1, 0.2});
    waveform.setFile(t.wave());
    REQUIRE_FALSE(waveform.selection().valid());
    REQUIRE(changes == 4);
    REQUIRE_FALSE(seen.valid());
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build --parallel 6 --target SonicatorTests`
Expected: FAIL to compile — `ui/waveform/WaveformSelection.h` not found and `setSelection`/`onSelectionChanged` missing.

- [ ] **Step 3: Implement `ui/waveform/WaveformSelection.h`**

Header-only helpers with waveform inset `6.0` and midpoint threshold: `exceedsDragThreshold` returns `dx*dx + dy*dy > 9`. `timeAtX` uses the inset and `juce::jlimit`; `xAtTime` is the inverse clamped to `[6, width-6]`. `orderedRange` clamps both endpoints to `[0, totalLength]` and returns the smaller/larger pair. `formatRange` uses `juce::String(value, 3)`, an en dash (`juce::String::fromUTF8("–")`), and `" s"`.

- [ ] **Step 4: Run the geometry tests to verify they pass**

Run: `build/tests/SonicatorTests "[selection][geometry]" "[selection][label]"`
Expected: PASS (three geometry/label cases).

- [ ] **Step 5: Implement selection state and mouse interaction in `WaveformDisplay`**

Add the members and callbacks from the Interfaces block. Behaviour:

- `setSelection(r)` normalizes with `orderedRange`, stores it even if invalid, and calls `onSelectionChanged` when the value changed.
- `clearSelection()` notifies only when a valid selection existed; `setFile()` resets position and calls `clearSelection()`.
- `mouseDown`: store the pixel origin and anchor time. Record whether the anchor is inside a valid selection.
- `mouseDrag`: if the press was inside a valid selection and the pointer moved beyond `exceedsDragThreshold`, call `onExternalDragRequested()` once and ignore the rest of that gesture. Otherwise, once the threshold is exceeded, update the selection to `orderedRange(anchorTime, timeAtX(...))`.
- `mouseUp`: if no drag happened, call `onSeek(anchorTime)` and `clearSelection()`; otherwise leave the finalized selection in place.
- `paint`: after the waveform, fill the selection with `theme::accent.withAlpha(0.16f)`, draw start/end lines with `0.85f` alpha, and draw `selection::formatRange` in an 11 px label clamped into the waveform.

- [ ] **Step 6: Run the selection-state tests**

Run: `build/tests/SonicatorTests "[selection]"`
Expected: PASS (all selection cases).

- [ ] **Step 7: Commit**

```bash
git add ui/waveform/WaveformSelection.h ui/waveform/WaveformDisplay.h ui/waveform/WaveformDisplay.cpp tests/TestSelection.cpp tests/CMakeLists.txt
git commit -m "feat: add waveform range selection and geometry helpers"
```

---

### Task 2: `AudioExporter` chunked WAV export

**Files:**
- Create: `core/export/AudioExporter.h`
- Create: `core/export/AudioExporter.cpp`
- Modify: `CMakeLists.txt` (add sources to `SonicatorCore`)
- Modify: `tests/CMakeLists.txt` (add `TestExport.cpp`)
- Test: `tests/TestExport.cpp`

**Interfaces:**
- Consumes: `CancelCheck` from `core/scanner/FileScanner.h`.
- Produces:
  - `struct sonicator::ExportResult { bool success = false; juce::File file; juce::String error; };`
  - `class sonicator::AudioExporter`
    - `static constexpr juce::int64 exportChunkFrames = 65536;`
    - `static constexpr int maxChannels = 64;`
    - `static juce::File defaultExportFolder();`
    - `static juce::String sanitizeStem(const juce::String& sourceFileNameWithoutExtension);`
    - `static juce::String rangeSuffix(double startSeconds, double endSeconds);`
    - `ExportResult exportRange(const juce::File& source, double start, double end, const juce::File& destination, CancelCheck cancel = {}) const;`
    - `ExportResult exportSelection(const juce::File& source, double start, double end, const juce::File& folder, CancelCheck cancel = {}) const;`

- [ ] **Step 1: Write the failing export tests**

Create `tests/TestExport.cpp` with a local helper that writes real WAVs at a chosen bit depth:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/export/AudioExporter.h"
#include "core/scanner/FileScanner.h"
#include "TestHelpers.h"
#include <cmath>
using namespace sonicator;
static juce::File writeWav(const juce::File& file, int channels, int bits, bool floating, int frames)
{
    file.deleteFile();
    auto stream = file.createOutputStream();
    juce::WavAudioFormat wav;
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate(44100.0)
        .withNumChannels(channels).withBitsPerSample(bits)
        .withSampleFormat(floating ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                   : juce::AudioFormatWriterOptions::SampleFormat::integral);
    auto writer = wav.createWriterFor(stream, options);
    if (writer == nullptr) return {};
    juce::AudioBuffer<float> buffer(channels, frames);
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < frames; ++i)
            buffer.setSample(c, i, (c % 2 == 0 ? 1.0f : -1.0f) * 0.5f
                * std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * (float) i / 44100.0f));
    writer->writeFromAudioSampleBuffer(buffer, 0, frames);
    return file;
}
```

Test cases:

1. `"Export preserves rate, channels, bit depth, frame count and samples"` — stereo 16-bit 2 s source; export `0.2..0.5`; decode result with `juce::AudioFormatManager`; require `sampleRate == 44100`, `numChannels == 2`, `bitsPerSample == 16`, `lengthInSamples == 13230`; compare result sample 0 and last sample with source frames 8820 and 13229 (`Catch::Approx(...).margin(1.0/32768.0)`). Capture `size`, `getLastModificationTime`, and `FileScanner::computeFileHash` before/after and require unchanged.
2. `"Export clamps ranges to the file and rejects empty or missing sources"` — `-1..0.1` produces 4410 frames; `0.5..0.5`, `0.7..0.2`, and `NAN..0.2` fail with a non-empty error; missing file fails; no `out.wav.part` remains in any case.
3. `"Export maps integer, float and compressed sources to the documented WAV formats"` — 24-bit integer source → 24-bit PCM; 32-bit float source → 32 bits and `usesFloatingPointData`; `SONICATOR_FIXTURE_DIR "/tone.flac"` → 24-bit PCM and not float. Also assert the exported sample rate and channel count match each source.
4. `"Export refuses more than 64 channels without a partial file"` — `t.wave("wide.wav", 440, 65, 100)`; export fails; error contains `"64"`; neither destination nor `.part` exists.
5. `"Export names are sanitized, bounded and unique"` — `sanitizeStem("../../Kick 01:*?")` has no path separator and starts with `Kick_01`; an empty stem becomes `audio`; a 200-character stem is at most 48 characters; `rangeSuffix(0.25, 1.1) == "0250-1100"`; two `exportSelection` calls on `Kick 01.wav` produce two existing files whose names both start `Kick_01_0250-1100_` and differ, and whose parent is the supplied folder.
6. `"Long exports run in bounded chunks and cancel cleanly"` — 10 s mono source, export `0..10` with a callback that counts calls and returns `false`; require `checks == (441000 + AudioExporter::exportChunkFrames - 1) / AudioExporter::exportChunkFrames`; then a second export with a callback returning `true` fails with `"cancelled"` (case-insensitive) and leaves no destination or `.part`.

- [ ] **Step 2: Run the export tests to verify they fail**

Run: `cmake --build build --parallel 6 --target SonicatorTests`
Expected: FAIL to compile — `core/export/AudioExporter.h` not found.

- [ ] **Step 3: Implement `AudioExporter`**

`exportRange` algorithm:

1. Require `std::isfinite(start) && std::isfinite(end)`, `source.existsAsFile()`, and create the destination's parent directory.
2. Open with `juce::AudioFormatManager::registerBasicFormats()` and `createReaderFor(source)`; require a reader, `sampleRate > 0`, `numChannels` in `1..maxChannels`, and `lengthInSamples > 0`.
3. Compute `startFrame = llround(start * sampleRate)` and `endFrame = llround(end * sampleRate)`, clamp to `[0, lengthInSamples]`, and require `endFrame > startFrame`.
4. Choose output format: float source → 32-bit float; otherwise compressed (`getFormatName()` contains `"FLAC"` or `"MP3"`) → 24-bit PCM; otherwise `bitsPerSample <= 16` → 16, else 24.
5. Require `!destination.existsAsFile()`. Delete any stale `destination + ".part"`.
6. Create the part stream and a `juce::WavAudioFormat` writer with
   `AudioFormatWriterOptions{}.withSampleRate(...).withNumChannels(...).withBitsPerSample(...).withSampleFormat(...)`.
7. Loop `exportChunkFrames` at a time: check `cancel` at the top of every iteration (return a cancelled error if true), allocate/reuse one `AudioBuffer<float>(channels, chunk)`, `reader->read(buffer.getArrayOfWritePointers(), channels, frame, n)`, then `writer->writeFromFloatArrays(buffer.getArrayOfReadPointers(), channels, n)`. Fail on either false return.
8. Reset the writer to finalize, require the part exists and is non-empty, then `part.moveFileTo(destination)`.
9. Any failure path (including exceptions) deletes the part file and returns `{false, {}, error}`. Success returns `{true, destination, {}}`.

`exportSelection` creates `folder`, builds
`sanitizeStem(source.getFileNameWithoutExtension()) + "_" + rangeSuffix(start, end) + "_" + suffix + ".wav"`,
where `suffix` is 6 lowercase hex characters from `juce::Uuid`, regenerated while the destination exists, then calls `exportRange`.

`sanitizeStem`: keep Unicode letters/digits plus `-` and `_`, replace every other character with `_`, collapse repeated `_`, trim leading/trailing `.`, `_`, and spaces, fall back to `"audio"`, and truncate to 48 characters.

`rangeSuffix`: `juce::String((int) std::llround(seconds * 1000.0)).paddedLeft('0', 4)` for both endpoints, joined with `"-"`.

`defaultExportFolder`: `juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Sonicator/exports")`.

- [ ] **Step 4: Run the export tests**

Run: `build/tests/SonicatorTests "[export]"`
Expected: PASS (all six cases).

- [ ] **Step 5: Run the current full suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: 1/1 passed.

- [ ] **Step 6: Commit**

```bash
git add core/export/AudioExporter.h core/export/AudioExporter.cpp CMakeLists.txt tests/CMakeLists.txt tests/TestExport.cpp
git commit -m "feat: export waveform ranges to chunked WAV files"
```

---

### Task 3: Background selection preparation

**Files:**
- Create: `core/export/SelectionExporter.h`
- Create: `core/export/SelectionExporter.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/TestExport.cpp`

**Interfaces:**
- Consumes: `AudioExporter::exportSelection`, `ExportResult`.
- Produces:
  - `class sonicator::SelectionExporter`
    - `enum class Status { idle, preparing, ready, failed };`
    - `struct State { Status status = Status::idle; juce::File file; juce::String error; double start = 0, end = 0; juce::uint64 generation = 0; bool isReady() const; };`
    - `using Callback = std::function<void(const State&)>;`
    - `SelectionExporter(juce::File exportFolder, Callback callback);`
    - `~SelectionExporter();`
    - `void prepare(const juce::File& source, double start, double end);`
    - `void cancel();`
    - `State state() const;`

- [ ] **Step 1: Write the failing worker tests**

Append to `tests/TestExport.cpp` a pump helper and three cases:

```cpp
static bool pumpUntil(const std::function<bool()>& condition, int timeoutMs = 5000)
{
    const auto start = juce::Time::getMillisecondCounter();
    while (! condition())
    {
        if (juce::Time::getMillisecondCounter() - start > (juce::uint32) timeoutMs) return false;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    }
    return true;
}
```

1. `"Selection export prepares a ready WAV on the message thread"` — create `SelectionExporter(folder, callback)`; `prepare(2 s source, 0.2, 0.5)`; record `last` in the callback; `REQUIRE(pumpUntil([&]{ return last.isReady(); }))`; require `last.file.existsAsFile()`, `last.generation == 1`, `last.start == 0.2`, `last.end == 0.5`, and the file decodes to `13230` frames.
2. `"A newer selection supersedes an older preparation"` — call `prepare(source, 0.1, 0.2)` then immediately `prepare(source, 0.6, 0.9)`; pump until `last.isReady()` and `last.generation == 2`; require `last.start == 0.6` and `last.end == 0.9`; pump briefly and require no callback with generation 1 arrived after generation 2 (`seenOldAfterNew` stays false).
3. `"A failed preparation exposes an error and no file"` — `prepare(missing, 0, 1)`; pump until `last.status == Status::failed`; require a non-empty error and `last.file == juce::File()`.

- [ ] **Step 2: Run the worker tests to verify they fail**

Run: `cmake --build build --parallel 6 --target SonicatorTests`
Expected: FAIL to compile — `core/export/SelectionExporter.h` not found.

- [ ] **Step 3: Implement `SelectionExporter`**

Use one `std::thread` per preparation; `prepare` joins the previous worker first (its cancel check makes that short), then under a mutex stores the request, increments `generation`, sets `preparing`, and starts the worker. The worker snapshots its request and generation, calls `AudioExporter::exportSelection` with a cancel lambda `generation != myGeneration`, then publishes `ready`/`failed` only when `myGeneration` is still current. Publish the state snapshot via `juce::MessageManager::callAsync` capturing a copy of the callback and state — never `this` inside the async lambda. `cancel()` increments the generation and sets `idle`. The destructor increments the generation, then joins.

- [ ] **Step 4: Run the worker tests**

Run: `build/tests/SonicatorTests "[export]"`
Expected: PASS (six exporter cases plus three worker cases).

- [ ] **Step 5: Commit**

```bash
git add core/export/SelectionExporter.h core/export/SelectionExporter.cpp CMakeLists.txt tests/TestExport.cpp
git commit -m "feat: prepare selection exports in the background"
```

---

### Task 4: PreviewPanel wiring — status, Clear, Escape, external drag

**Files:**
- Modify: `ui/preview/PreviewPanel.h`
- Modify: `ui/preview/PreviewPanel.cpp`
- Test: `tests/TestSelection.cpp`

**Interfaces:**
- Consumes: `SelectionExporter`, `AudioExporter::defaultExportFolder()`, `WaveformDisplay` callbacks.
- Produces:
  - `PreviewPanel(AudioEngine&, const juce::File& cacheFolder, const juce::File& exportFolder = AudioExporter::defaultExportFolder());`
  - `void selectRange(double start, double end);`
  - `selection::Range selectedRange() const;`
  - `void clearSelection();`
  - `bool isSelectionReady() const;`
  - `juce::File preparedSelection() const;`
  - `SelectionExporter::State selectionExportState() const;`
  - `bool waitForSelectionPreparation(int timeoutMs);`
  - `double waveformLength() const;`

- [ ] **Step 1: Write the failing PreviewPanel selection tests**

Append to `tests/TestSelection.cpp` (include `"core/export/SelectionExporter.h"` and `"ui/preview/PreviewPanel.h"`):

1. `"Loading a new file clears the selection and hides Clear"` — `AudioEngine engine(false); PreviewPanel preview(engine, cache, exports);` load two versions of a file; `preview.selectRange(0.2, 0.5)`; require valid range and Clear visible (`findChildWithID("clearSelection")`); load the second file; require invalid range, Clear hidden, status empty.
2. `"Selecting a range prepares a WAV and shows Ready"` — load a 2 s source; `engine.stop()`; `selectRange(0.2, 0.5)`; `REQUIRE(preview.waitForSelectionPreparation(5000))`; require `preview.isSelectionReady()`, the prepared file exists and decodes to 13230 frames, status text contains `"Ready"`, and Clear is visible. Then `preview.clearSelection()`; require invalid range, hidden Clear, empty status.
3. `"Escape clears the selection and stops playback"` — select a range, `preview.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey))`; require invalid range and `! engine.isPlaying()`.
4. `"Clear does not take keyboard focus"` — find `clearSelection`; require `! getWantsKeyboardFocus()` and `! getMouseClickGrabsKeyboardFocus()`.

The worker is polled rather than callback-driven: `waitForSelectionPreparation` waits on the worker and refreshes the label; `timerCallback` calls the same refresh every 30 Hz tick.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build --parallel 6 --target SonicatorTests`
Expected: FAIL to compile — `PreviewPanel` has no `selectRange`/`isSelectionReady` and no third constructor argument.

- [ ] **Step 3: Implement the wiring**

- Add `SelectionExporter exporter; juce::TextButton clearSelection { "Clear" }; juce::Label selectionStatus;` and the public methods above.
- Construct `exporter(exportFolder)`; no callback is posted from the worker.
- In the constructor, add `clearSelection` and `selectionStatus` as visible children; give them the IDs `"clearSelection"` and `"selectionStatus"`; hide `clearSelection` initially; set `clearSelection` text `"Clear"`, tooltip `"Clear the waveform selection"`, and the focus policy.
- Wire `waveform.onSelectionChanged` to `selectionChanged(range)`, and `waveform.onExternalDragRequested` to `beginExternalDrag()`.
- `selectRange` clamps with `selection::orderedRange(start, end, engine.length())` then calls `waveform.setSelection`.
- `selectionChanged(valid)`:
  - valid: show Clear, set status `"Preparing selection…"`, call `exporter.prepare(loadedFile, start, end)` (an empty `loadedFile` produces a failed state, not a crash).
  - invalid: hide Clear, clear status text, call `exporter.cancel()`.
- `refreshSelectionStatus()`: read `exporter.state()`; ignore when no current selection; `preparing` → `"Preparing selection…"`; `ready` → `"Ready — drag the selection into a DAW"`; `failed` → the export error. `timerCallback` calls it every tick, and `waitForSelectionPreparation(timeoutMs)` waits on the worker then refreshes.
- `beginExternalDrag()`: if the exporter state is not ready or the file is missing, set the status to `Preparing selection…`/error and return without dragging. Otherwise:
  ```cpp
  juce::StringArray files { state.file.getFullPathName() };
  juce::DragAndDropContainer::performExternalDragDropOfFiles(files, false, &waveform);
  ```
  Never call `prepare`/export here.
- `keyPressed` Escape: clear the selection when valid, then `engine.stop()` and return true.
- Layout: in `resized`, reserve a final 26 px row under the transport buttons; put `clearSelection` on the right at 58 px and `selectionStatus` across the rest.

- [ ] **Step 4: Run the selection tests**

Run: `build/tests/SonicatorTests "[selection]"`
Expected: PASS (all cases, including the asynchronous preparation case).

- [ ] **Step 5: Run the full suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: 1/1 passed, with the preview focus test updated from 6 to 7 buttons.

- [ ] **Step 6: Commit**

```bash
git add ui/preview/PreviewPanel.h ui/preview/PreviewPanel.cpp tests/TestSelection.cpp tests/TestHelpers.h tests/TestUI.cpp
git commit -m "feat: wire selection preparation, Clear and external drag into preview"
```

---

### Task 5: Preview audition of the selected range

**Files:**
- Create: `core/audio/SelectionPlayback.h`
- Modify: `ui/preview/PreviewPanel.cpp`
- Modify: `tests/TestSelection.cpp`

**Interfaces:**
- Consumes: `PreviewPanel::selectedRange`, `AudioEngine`.
- Produces:
  - `enum class sonicator::SelectionBoundary { none, restart, stop };`
  - `SelectionBoundary sonicator::selectionBoundary(double position, double start, double end, bool looping);`
  - `PreviewPanel::updateSelectionPlayback()` — the boundary check the 30 Hz timer calls; tests pump transport blocks and call it directly because JUCE 9 has modal loops disabled.

- [ ] **Step 1: Write the failing boundary and timer tests**

Append to `tests/TestSelection.cpp`:

```cpp
TEST_CASE("Selection boundary stops or restarts at the range end", "[selection][audition]")
{
    using sonicator::SelectionBoundary;
    REQUIRE(selectionBoundary(0.5, 0.2, 0.8, false) == SelectionBoundary::none);
    REQUIRE(selectionBoundary(0.8, 0.2, 0.8, false) == SelectionBoundary::stop);
    REQUIRE(selectionBoundary(0.9, 0.2, 0.8, true) == SelectionBoundary::restart);
    REQUIRE(selectionBoundary(0.9, 0.8, 0.8, false) == SelectionBoundary::none);
}
TEST_CASE("Play with a selection starts at its beginning and stops at its end", "[selection][audition]")
{
    TestDirectory t; AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"), t.dir.getChildFile("exports"));
    auto file = t.wave("audition.wav", 440, 1, 44100 * 2);
    REQUIRE(preview.loadFile(file)); engine.stop();
    engine.transport().prepareToPlay(256, 44100);
    preview.selectRange(0.2, 0.35);
    engine.seek(1.5);
    REQUIRE(preview.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    REQUIRE(engine.position() == Catch::Approx(0.2).margin(0.01));
    juce::AudioBuffer<float> buffer(2, 128);
    bool stopped = false;
    for (int i = 0; i < 400 && ! stopped; ++i)
    {
        engine.transport().getNextAudioBlock({&buffer, 0, 128});
        preview.updateSelectionPlayback();
        stopped = ! engine.isPlaying();
    }
    REQUIRE(stopped);
    REQUIRE(engine.position() >= 0.35);
    REQUIRE(engine.position() <= 0.35 + 128.0 / 44100.0 + 0.05);
    engine.transport().releaseResources();
}
TEST_CASE("Loop wraps selection playback back to its start", "[selection][audition]")
{
    TestDirectory t; AudioEngine engine(false);
    PreviewPanel preview(engine, t.dir.getChildFile("cache"), t.dir.getChildFile("exports"));
    REQUIRE(preview.loadFile(t.wave("loop.wav", 440, 1, 44100 * 2))); engine.stop();
    engine.transport().prepareToPlay(256, 44100); engine.setLooping(true);
    preview.selectRange(0.2, 0.35);
    REQUIRE(preview.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    juce::AudioBuffer<float> buffer(2, 128);
    bool reachedEnd = false, wrapped = false;
    for (int i = 0; i < 500 && ! wrapped; ++i)
    {
        engine.transport().getNextAudioBlock({&buffer, 0, 128});
        preview.updateSelectionPlayback();
        const auto p = engine.position();
        if (p >= 0.34) reachedEnd = true;
        if (reachedEnd && p < 0.25) wrapped = true;
    }
    REQUIRE(reachedEnd); REQUIRE(wrapped); REQUIRE(engine.isPlaying());
    engine.transport().releaseResources();
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build --parallel 6 --target SonicatorTests`
Expected: FAIL to compile — `SelectionPlayback.h`/`selectionBoundary` missing and selection Play starts at the manually seeked position.

- [ ] **Step 3: Implement `SelectionPlayback.h` and timer logic**

`selectionBoundary` requires `end > start`, finite values, and a finite position ≥ end; returns `restart` when `looping`, otherwise `stop`. Otherwise `none`.

In `PreviewPanel::togglePlayback`, when starting playback with a valid selection, seek to `range.start` before `engine.play()`. Implement `updateSelectionPlayback()` to apply the boundary when a valid selection exists and `engine.isPlaying()`: `restart` → `engine.seek(range.start)`; `stop` → `engine.pause()`. Call it from `timerCallback` before updating the waveform; the tests call it after each transport block.

- [ ] **Step 4: Run the audition tests**

Run: `build/tests/SonicatorTests "[selection][audition]"`
Expected: PASS (three cases).

- [ ] **Step 5: Run the full suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: 1/1 passed.

- [ ] **Step 6: Commit**

```bash
git add core/audio/SelectionPlayback.h ui/preview/PreviewPanel.cpp tests/TestSelection.cpp
git commit -m "feat: audition the waveform selection with preview-grade boundaries"
```

---

### Task 6: Library row drag of original files

**Files:**
- Create: `core/export/ExternalFileDrag.h`
- Create: `core/export/ExternalFileDrag.cpp`
- Modify: `ui/library/FileListPanel.h`
- Modify: `ui/library/FileListPanel.cpp`
- Modify: `ui/MainComponent.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Test: `tests/TestLibraryDrag.cpp`

**Interfaces:**
- Consumes: `SampleRecord`, `juce::DragAndDropContainer`.
- Produces:
  - `class sonicator::ExternalFileDrag`
    - `using DragFunction = std::function<bool(const juce::StringArray&, bool, juce::Component*)>;`
    - `static juce::StringArray existingPaths(const juce::StringArray& candidates);`
    - `static juce::StringArray selectedPaths(const std::vector<SampleRecord>& visible, const std::vector<int>& selectedRows);`
    - `static bool exceedsThreshold(int dx, int dy);`
    - `static bool start(const juce::StringArray& candidates, juce::Component* source, DragFunction drag = {});`
  - `FileListPanel` gains:
    - `void selectRows(const std::vector<int>& rows);`
    - `juce::StringArray dragPaths() const;`
    - `int prepareRowDrag(int row, bool addToSelection = false);`
    - `void setExternalDragHandler(ExternalFileDrag::DragFunction);`
    - `std::function<void(juce::String)> onDragMessage;`

- [ ] **Step 1: Write the failing library-drag tests**

Create `tests/TestLibraryDrag.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "core/export/ExternalFileDrag.h"
#include "ui/library/FileListPanel.h"
#include "TestHelpers.h"
using namespace sonicator;
TEST_CASE("External file drag keeps only existing paths in order", "[drag][library]")
{
    TestDirectory t; auto a = t.wave("a.wav"), b = t.wave("b.wav");
    juce::StringArray candidates { b.getFullPathName(),
        t.dir.getChildFile("missing.wav").getFullPathName(), a.getFullPathName() };
    const auto files = ExternalFileDrag::existingPaths(candidates);
    REQUIRE(files.size() == 2);
    REQUIRE(files[0] == b.getFullPathName()); REQUIRE(files[1] == a.getFullPathName());
}
TEST_CASE("Selected library rows map to existing originals in order", "[drag][library]")
{
    TestDirectory t; auto a = t.wave("a.wav"), b = t.wave("b.wav");
    SampleRecord ra, rm, rb; ra.id = 1; ra.filePath = a.getFullPathName(); ra.fileName = "a.wav";
    rm.id = 2; rm.filePath = t.dir.getChildFile("gone.wav").getFullPathName(); rm.fileName = "gone.wav";
    rb.id = 3; rb.filePath = b.getFullPathName(); rb.fileName = "b.wav";
    const std::vector<SampleRecord> rows { ra, rm, rb };
    auto paths = ExternalFileDrag::selectedPaths(rows, { 0, 1, 2 });
    REQUIRE(paths.size() == 2);
    REQUIRE(paths[0] == a.getFullPathName()); REQUIRE(paths[1] == b.getFullPathName());
    paths = ExternalFileDrag::selectedPaths(rows, { 2, 0, 99 });
    REQUIRE(paths.size() == 2);
    REQUIRE(paths[0] == a.getFullPathName()); REQUIRE(paths[1] == b.getFullPathName());
}
TEST_CASE("Row drag starts only with existing files and never moves them", "[drag][library]")
{
    TestDirectory t; auto a = t.wave("a.wav");
    juce::Component sourceComponent;
    bool called = false, canMove = true; juce::Component* source = nullptr;
    auto drag = [&](const juce::StringArray& files, bool move, juce::Component* c)
    {
        called = true; canMove = move; source = c;
        return files.size() == 1 && files[0] == a.getFullPathName();
    };
    REQUIRE_FALSE(ExternalFileDrag::start({}, &sourceComponent, drag));
    REQUIRE_FALSE(called);
    REQUIRE_FALSE(ExternalFileDrag::start({ t.dir.getChildFile("gone.wav").getFullPathName() },
                                          &sourceComponent, drag));
    REQUIRE_FALSE(called);
    REQUIRE(ExternalFileDrag::start({ a.getFullPathName() }, &sourceComponent, drag));
    REQUIRE(called); REQUIRE_FALSE(canMove); REQUIRE(source == &sourceComponent);
}
TEST_CASE("Row drag threshold is five pixels", "[drag][library]")
{
    REQUIRE_FALSE(ExternalFileDrag::exceedsThreshold(5, 0));
    REQUIRE(ExternalFileDrag::exceedsThreshold(6, 0));
    REQUIRE(ExternalFileDrag::exceedsThreshold(0, -6));
}
TEST_CASE("FileListPanel prepares unselected rows and exposes multi-selected originals", "[drag][library]")
{
    TestDirectory t; auto a = t.wave("a.wav"), c = t.wave("c.wav");
    SampleRecord ra, rb, rm; ra.id = 1; ra.filePath = a.getFullPathName(); ra.fileName = "a.wav";
    rb.id = 2; rb.filePath = c.getFullPathName(); rb.fileName = "c.wav";
    rm.id = 3; rm.filePath = t.dir.getChildFile("gone.wav").getFullPathName(); rm.fileName = "b.wav";
    FileListPanel list; list.setBounds(0, 0, 800, 400); list.resized();
    list.setSamples({ ra, rm, rb });
    list.selectRows({ 0, 2 });
    const auto paths = list.dragPaths();
    REQUIRE(paths.size() == 2);
    REQUIRE(paths[0] == a.getFullPathName()); REQUIRE(paths[1] == c.getFullPathName());
    REQUIRE(list.prepareRowDrag(1) == 1);
    REQUIRE(list.getSelectedRow() == 1);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build --parallel 6 --target SonicatorTests`
Expected: FAIL to compile — `core/export/ExternalFileDrag.h` not found and `FileListPanel` lacks the new methods.

- [ ] **Step 3: Implement `ExternalFileDrag`**

- `existingPaths`: keep candidates in order whose `juce::File(path).existsAsFile()` is true.
- `selectedPaths`: iterate `selectedRows` in ascending order (skip duplicates/out-of-range), and keep each `visible[row].filePath` that exists as a file. Missing records are skipped, not errors.
- `exceedsThreshold`: `dx*dx + dy*dy > 25`.
- `start`: filter with `existingPaths`; if empty return false without calling `drag`; otherwise call the injected handler or `juce::DragAndDropContainer::performExternalDragDropOfFiles(files, false, source)` and return its result. `canMoveFiles` is always `false`.

- [ ] **Step 4: Implement row drag in `FileListPanel`**

- Constructor: `table.setMultipleSelectionEnabled(true); table.addMouseListener(this, true);`.
- Destructor: `table.removeMouseListener(this);` before `setModel(nullptr)`.
- `selectRows`: deselect all, then for each in-bounds row call `table.selectRow(row, true, false)`.
- `dragPaths`: collect `table.getSelectedRows()` into a vector and return `ExternalFileDrag::selectedPaths(visible, rows)`.
- `prepareRowDrag(row, addToSelection)`: if the row is valid and not already selected, `table.selectRow(row, true, ! addToSelection)`; return `row`.
- Mouse listener:
  - `mouseDown`: convert to table coordinates with `e.getEventRelativeTo(&table)`; `pressedRow = prepareRowDrag(table.getRowContainingPosition(pos.x, pos.y), e.mods.isCommandDown() || e.mods.isShiftDown());` store the origin and arm only when `pressedRow >= 0`.
  - `mouseDrag`: on the first move past `ExternalFileDrag::exceedsThreshold`, call `dragPaths()`. If empty, call `onDragMessage("Selected files are missing or unavailable")`; otherwise call `ExternalFileDrag::start(paths, &table, externalDragHandler)`. Do not change the selection.
  - `mouseUp`: disarm.
- `setExternalDragHandler` stores the test/platform seam; the default is `{}` (the real OS drag).

- [ ] **Step 5: Surface drag failures in the app status**

In `MainComponent`'s constructor wire `library.onDragMessage = [this](const juce::String& message) { status.setText(message, juce::dontSendNotification); };`.

- [ ] **Step 6: Run the library-drag tests**

Run: `build/tests/SonicatorTests "[drag][library]"`
Expected: PASS (five cases).

- [ ] **Step 7: Run the full suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: 1/1 passed; existing library selection tests still pass with multi-selection enabled.

- [ ] **Step 8: Commit**

```bash
git add core/export/ExternalFileDrag.h core/export/ExternalFileDrag.cpp ui/library/FileListPanel.h ui/library/FileListPanel.cpp ui/MainComponent.cpp CMakeLists.txt tests/CMakeLists.txt tests/TestLibraryDrag.cpp
git commit -m "feat: drag library rows as original files"
```

---

### Task 7: UI snapshot and documentation

**Files:**
- Modify: `README.md`
- Modify: `docs/PREVIEW.md`
- Modify: `docs/TESTING.md`
- Modify: `tests/TestSelection.cpp`

**Interfaces:**
- Consumes: everything above.
- Produces: user-facing documentation and a rendered snapshot check with a visible selection.

- [ ] **Step 1: Add the snapshot test**

In `tests/TestSelection.cpp` add a case that loads a 2 s file, stops playback, selects `0.2..0.6`, pumps until `preview.waveformLength() > 0` (thumbnail loaded), sets bounds `0,0,900,156`, and requires `createComponentSnapshot(getLocalBounds()).isValid()`. When `SONICATOR_SELECTION_SCREENSHOT` is set, write the PNG there with `juce::PNGImageFormat` and require the write succeeds.

- [ ] **Step 2: Run the snapshot test**

Run: `cmake --build build --parallel 6 --target SonicatorTests && SONICATOR_SELECTION_SCREENSHOT=/tmp/sonicator-selection.png build/tests/SonicatorTests "[selection][snapshot]"`
Expected: PASS; inspect `/tmp/sonicator-selection.png` and confirm the accent range, boundary lines, and `0.200–0.600 s` label are visible.

- [ ] **Step 3: Document the feature**

`README.md`:
- Use step 3: add “drag one or more library rows to Finder, the desktop, a messenger or a DAW; originals are copied by the target and never moved or deleted”.
- Use step 3: add “drag across the waveform to select a range; click seeks and clears”.
- Use step 4: add **Clear** and **Escape** behaviour.
- New short “Selection export” subsection under Data: destination folder, WAV/rate/channel/bit-depth rules, `*.part` safety, exported files are not deleted automatically, so the folder grows until manually cleaned; original files are never modified.

`docs/PREVIEW.md`:
- Add a “Dragging original files” section covering single/multi-selection, existing files only, `canMoveFiles = false`, and the fact that the target decides copy vs import.
- Add a “Waveform selection and export” section covering the 3 px threshold, drag-inside-selection → external drag, Preparing/Ready/error states, Clear/Escape, preview-grade boundary behaviour (one timer tick), and the fact that only WAV is exported with no fades/normalization/stretch.

`docs/TESTING.md`:
- Add a dated stage section with the actual test/assertion counts, the new coverage list, Debug/Release result, and the explicit limitation that external drag into a DAW/Finder is manual on macOS and unverified on Windows.

- [ ] **Step 4: Run the full suite and check docs**

Run: `ctest --test-dir build --output-on-failure`
Expected: 1/1 passed.

- [ ] **Step 5: Commit**

```bash
git add README.md docs/PREVIEW.md docs/TESTING.md tests/TestSelection.cpp
git commit -m "docs: document waveform selection, export and DAW drag"
```

---

### Task 8: Debug/Release verification, graph update

**Files:**
- Modify: `docs/TESTING.md` (final numbers only)

**Interfaces:**
- Consumes: all completed tasks.
- Produces: verified Debug and Release builds and an updated knowledge graph.

- [ ] **Step 1: Build and run Debug**

Run: `cmake --build build --parallel 6 && ctest --test-dir build --output-on-failure`
Expected: build succeeds; `1/1` passed.

- [ ] **Step 2: Build and run Release**

Run: `cmake --build build-release --parallel 6 && ctest --test-dir build-release --output-on-failure`
Expected: build succeeds; `1/1` passed.

- [ ] **Step 3: Verify the Release bundle signature**

Run: `codesign --verify --verbose build-release/Sonicator_artefacts/Release/SONICATOR.app`
Expected: `valid on disk` and `satisfies its Designated Requirement`.

- [ ] **Step 4: Manual drag check**

Launch the Debug app, select a file, and drag a single row and then multiple selected rows into Finder, the desktop, a messenger and a DAW track. Confirm the target receives every selected existing original, copies/imports them, and the library originals stay on disk. Then drag a short waveform range, wait for `Ready`, and drop it on the same targets; confirm the dropped WAV is playable and contains only the chosen interval. Record the result here; if a target is unavailable, record exactly which part was not verified. This step is not automatable in the test harness.

- [ ] **Step 5: Update the graph**

Run: `graphify update .`
Expected: completes without error and updates `graphify-out/`.

- [ ] **Step 6: Record final counts and commit**

Update `docs/TESTING.md` with the final case/assertion counts and any manual limitations, then:

```bash
git add docs/TESTING.md graphify-out
git commit -m "docs: record waveform selection stage verification"
```

---

## Self-Review

- **Spec coverage:** library row drag and original paths → Task 6; selection model/interaction → Task 1; exporter, naming, bit depth, `.part`, channel refusal → Task 2; background generation/cancel → Task 3; status/Clear/Escape/external selection drag → Task 4; audition → Task 5; snapshots/docs → Task 7; Debug/Release/manual/graph → Task 8.
- **Type consistency:** `selection::Range` is used by `WaveformDisplay` and `PreviewPanel`; `SelectionExporter::State` is produced once and consumed only by `PreviewPanel`; `AudioExporter::exportSelection` is the only export entry point used by the worker; `ExternalFileDrag` is the only path builder for library row drags.
- **Review Focus coverage:** multi-row drag/missing files → Task 6 tests; channel limits → Task 2 test 4; degenerate ranges → Task 2 test 2; long/cancel → Task 2 test 6 and Task 3 test 2; hostile names → Task 2 test 5; boundary/Loop interaction → Task 5 tests.
- **No automatic cleanup** is implemented deliberately; documentation states the disk-growth cost.
