# Silence Split Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split one file or a selected batch at silence into separate WAV parts, with a themed dialog for threshold and output folder, and one post-run prompt to move the originals to the Trash.

**Architecture:** A streaming 1 ms-grain detector in `core/split` finds parts between silence runs. A background `SplitService` writes each part through the existing validated `AudioExporter`, never touching the source. `MainComponent` hosts the dialog, polls progress, indexes the new files and shows the deletion prompt only for files whose split was complete.

**Tech Stack:** C++20, JUCE 9.0.5 audio formats, bundled SQLite 3.47.2, Catch2 3.7.1.

**Spec:** `docs/superpowers/specs/2026-11-09-ignore-and-split-design.md` (Part 2)

## Global Constraints

- Defaults: −60 dBFS peak, minimum silence 250 ms, minimum part 100 ms, 5 ms edge padding and 5 ms fade.
- Parts are always WAV, preserving source sample rate, channel count and the existing bit-depth policy: 16-bit stays 16-bit, wider integer becomes 24-bit, float stays 32-bit float, compressed becomes 24-bit.
- Names: `<stem>_01.wav`, `<stem>_02.wav`, … zero-padded to at least 2 digits; existing files are never overwritten.
- Originals are read-only until the user confirms `Move originals to Trash`; deletion is `juce::File::moveToTrash()` only, never permanent.
- A file is deletable only when every detected part was written and validated, no fragment was dropped, and the batch was not cancelled.
- Split and library jobs (indexing, smart analysis, tagging, AutoTag) never run at the same time.
- No new third-party dependency and no FFmpeg.
- Full suite must stay green: `ctest --test-dir build --output-on-failure`.

## Review Focus

- A 139 s stereo file: detection must stream, not load it whole, and cancellation must leave completed parts and remove the `.part`.
- A file with no interior silence: report it, leave the source untouched, never offer it for deletion.
- A source folder that is not writable: fail that file only, keep the batch going, and never offer deletion.
- Two batches into the same folder: existing `_01` names must not be overwritten.
- A file whose short 50 ms fragment is dropped by `minPartMs`: parts may exist, but the original must not be offered for deletion.
- Cancelling mid-batch: nothing already written is deleted, but no original is offered for deletion.

---

### Task 1: Fade support in the range exporter

**Files:**
- Modify: `core/export/AudioExporter.h`
- Modify: `core/export/AudioExporter.cpp`
- Modify: `tests/TestExport.cpp`

**Interfaces:**
- Consumes: existing `exportRange`.
- Produces:
  ```cpp
  ExportResult exportRange(const juce::File& source, double start, double end,
                           const juce::File& destination, CancelCheck cancel = {},
                           double fadeSeconds = 0) const;
  ```

- [ ] **Step 1: Write the failing test**

Add to `tests/TestExport.cpp`:

```cpp
TEST_CASE("Range export applies an optional linear fade at both ends", "[export][fade]")
{
    TestDirectory t;
    auto source = t.dir.getChildFile("steady.wav");
    source.replaceWithText("");
    {
        std::unique_ptr<juce::OutputStream> stream(source.createOutputStream());
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}
            .withSampleRate(44100.0).withNumChannels(1).withBitsPerSample(16));
        juce::AudioBuffer<float> constant(1, 44100);
        constant.clear();
        for (int i = 0; i < constant.getNumSamples(); ++i) constant.setSample(0, i, 0.5f);
        writer->writeFromAudioSampleBuffer(constant, 0, constant.getNumSamples());
    }
    auto out = t.dir.getChildFile("faded.wav");
    AudioExporter exporter;
    REQUIRE(exporter.exportRange(source, 0.0, 1.0, out, {}, 0.01).success);
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(out));
    REQUIRE(reader != nullptr);
    juce::AudioBuffer<float> first(1, 1), mid(1, 1), last(1, 1);
    reader->read(&first.getWritePointer(0), 1, 0, 1, true, true);
    reader->read(&mid.getWritePointer(0), 1, 22050, 1, true, true);
    reader->read(&last.getWritePointer(0), 1, reader->lengthInSamples - 1, 1, true, true);
    REQUIRE(std::abs(first.getSample(0, 0)) < 0.01f);
    REQUIRE(std::abs(last.getSample(0, 0)) < 0.01f);
    REQUIRE(std::abs(mid.getSample(0, 0)) > 0.45f);
}
```

- [ ] **Step 2: Run to confirm failure**

Run: `cmake --build build --target SonicatorTests --parallel 6`
Expected: compile error — `exportRange` takes five arguments.

- [ ] **Step 3: Implement the fade**

Add the trailing `double fadeSeconds = 0` parameter. In the chunk loop, compute `const auto totalFrames = endFrame - startFrame; const auto fadeFrames = juce::jlimit<juce::int64>(0, totalFrames / 2, (juce::int64) std::llround(fadeSeconds * rate));`. For each sample index `i` in the chunk, with `n = frame - startFrame + i`, multiply every channel by:

```cpp
float gain = 1.0f;
if (fadeFrames > 0)
{
    if (n < fadeFrames) gain = (float) n / (float) fadeFrames;
    else if (n >= totalFrames - fadeFrames) gain = (float) (totalFrames - 1 - n) / (float) fadeFrames;
}
```

Apply after `reader->read` and before `writeFromFloatArrays`. Leave `exportSelection`, `exportRexSelection` and `exportSampleSelection` unchanged.

- [ ] **Step 4: Run the tests**

Run: `build/tests/SonicatorTests "[export]" --reporter compact`
Expected: PASS, including the existing export tests.

- [ ] **Step 5: Commit**

```bash
git add core/export/AudioExporter.h core/export/AudioExporter.cpp tests/TestExport.cpp
git commit -m "feat: add optional edge fade to range export"
```

---

### Task 2: Streaming silence detector

**Files:**
- Create: `core/split/SilenceDetector.h`
- Create: `core/split/SilenceDetector.cpp`
- Create: `tests/TestSilenceSplit.cpp`
- Modify: `CMakeLists.txt` (`SonicatorCore` sources)
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  struct SilenceOptions
  {
      double thresholdDb = -60.0;
      double minSilenceMs = 250.0;
      double minPartMs = 100.0;
      double edgePaddingMs = 5.0;
  };
  struct SplitPart { double startSeconds = 0, endSeconds = 0; };
  struct SplitDetection { std::vector<SplitPart> parts; int discardedShortParts = 0; };
  SplitDetection detectSplitParts(juce::AudioFormatReader&, const SilenceOptions&, CancelCheck);
  ```

- [ ] **Step 1: Write the failing tests**

Create `tests/TestSilenceSplit.cpp` with a helper that writes a sequence of tone and silence segments:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/split/SilenceDetector.h"
#include "TestHelpers.h"
using namespace sonicator;
namespace
{
juce::File segments(const juce::File& file,
                    const std::vector<std::pair<int, bool>>& toneOrSilence,
                    double rate = 44100.0)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}
        .withSampleRate(rate).withNumChannels(1).withBitsPerSample(16));
    int total = 0;
    for (const auto& [frames, tone] : toneOrSilence) total += frames;
    juce::AudioBuffer<float> buffer(1, total);
    buffer.clear();
    int pos = 0;
    for (const auto& [frames, tone] : toneOrSilence)
    {
        if (tone)
            for (int n = 0; n < frames; ++n, ++pos)
                buffer.setSample(0, pos, 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi
                    * 220.0f * (float) pos / 44100.0f));
        else
            pos += frames;
    }
    writer->writeFromAudioSampleBuffer(buffer, 0, total);
    writer.reset();
    return file;
}
juce::AudioFormatManager& formats()
{
    static juce::AudioFormatManager manager;
    static const bool registered = [] { manager.registerBasicFormats(); return true; }();
    juce::ignoreUnused(registered);
    return manager;
}
}

TEST_CASE("Detector splits tone bursts at two-second silence", "[split][detector]")
{
    TestDirectory t;
    auto file = segments(t.dir.getChildFile("a.wav"),
        { { 44100, true }, { 88200, false }, { 44100, true },
          { 88200, false }, { 44100, true }, { 88200, false } });
    std::unique_ptr<juce::AudioFormatReader> reader(formats().createReaderFor(file));
    const auto result = detectSplitParts(*reader, {}, {});
    REQUIRE(result.parts.size() == 3);
    REQUIRE(result.parts[0].startSeconds == Catch::Approx(0.0).margin(0.01));
    REQUIRE(result.parts[0].endSeconds == Catch::Approx(1.0).margin(0.01));
    REQUIRE(result.parts[1].startSeconds == Catch::Approx(3.0).margin(0.01));
    REQUIRE(result.parts[2].endSeconds == Catch::Approx(7.0).margin(0.01));
    REQUIRE(result.discardedShortParts == 0);
}

TEST_CASE("Detector ignores short gaps and trims file edges", "[split][detector]")
{
    TestDirectory t;
    // 0.5 s lead, 1 s tone, 100 ms gap (below 250 ms), 1 s tone, 0.5 s tail
    auto file = segments(t.dir.getChildFile("b.wav"),
        { { 22050, false }, { 44100, true }, { 4410, false }, { 44100, true }, { 22050, false } });
    std::unique_ptr<juce::AudioFormatReader> reader(formats().createReaderFor(file));
    const auto result = detectSplitParts(*reader, {}, {});
    REQUIRE(result.parts.size() == 1);
    REQUIRE(result.parts[0].startSeconds == Catch::Approx(0.5).margin(0.01));
    REQUIRE(result.parts[0].endSeconds == Catch::Approx(2.6).margin(0.01));
}

TEST_CASE("Detector counts a dropped short fragment", "[split][detector]")
{
    TestDirectory t;
    // 1 s tone, 0.5 s gap, 50 ms blip, 0.5 s gap, 1 s tone
    auto file = segments(t.dir.getChildFile("c.wav"),
        { { 44100, true }, { 22050, false }, { 2205, true }, { 22050, false }, { 44100, true } });
    std::unique_ptr<juce::AudioFormatReader> reader(formats().createReaderFor(file));
    const auto result = detectSplitParts(*reader, {}, {});
    REQUIRE(result.parts.size() == 2);
    REQUIRE(result.discardedShortParts == 1);
}

TEST_CASE("Detector stops on cancellation", "[split][detector]")
{
    TestDirectory t;
    auto file = segments(t.dir.getChildFile("d.wav"),
        { { 44100, true }, { 44100, false }, { 44100, true } });
    std::unique_ptr<juce::AudioFormatReader> reader(formats().createReaderFor(file));
    const auto result = detectSplitParts(*reader, {}, [] { return true; });
    REQUIRE(result.parts.empty());
}
```

- [ ] **Step 2: Run to confirm failure**

Run: `cmake --build build --target SonicatorTests --parallel 6`
Expected: compile error — the split header does not exist.

- [ ] **Step 3: Implement the detector**

Add `core/split/SilenceDetector.cpp` to the `SonicatorCore` source list in `CMakeLists.txt` and `TestSilenceSplit.cpp` to `tests/CMakeLists.txt`.

Algorithm in `SilenceDetector.cpp`:

1. `rate = reader.sampleRate`; `grainFrames = max(1, (juce::int64) llround(rate * 0.001))`; `threshold = std::pow(10.0, options.thresholdDb / 20.0)`; `total = reader.lengthInSamples`; `minSilenceFrames = round(rate * options.minSilenceMs / 1000)`, `minPartFrames = round(rate * options.minPartMs / 1000)`, `padFrames = round(rate * options.edgePaddingMs / 1000)`.
2. Read in blocks of 65536 frames into a `juce::AudioBuffer<float>` sized `min(65536, remaining)`. Walk grains inside the block; a grain may straddle a block boundary, so keep `grainStart` as an absolute frame and process a grain only once `grainStart + grainFrames <= blockEnd`.
3. `peak = max over channels and frames of abs(sample)`.
4. Maintain `silenceStart = -1`. On `peak <= threshold`, set `silenceStart = grainStart` if unset. On a louder grain with `silenceStart >= 0`, if `grainStart - silenceStart >= minSilenceFrames` push gap `{silenceStart, grainStart}`, then reset `silenceStart = -1`.
5. At EOF, if `silenceStart >= 0` and `total - silenceStart >= minSilenceFrames` push gap `{silenceStart, total}`.
6. Build candidates between gaps: `cursor = 0`; for each gap `[a,b]`, if `a > cursor` push `{cursor,a}`, then `cursor = b`; after the loop push `{cursor,total}` if `cursor < total`.
7. Extend each candidate by `padFrames` into the adjacent gap: `start = max(previous gap start or 0, candidateStart - pad)`, `end = min(next gap end or total, candidateEnd + pad)`. Never let `start` fall below the previous part's end.
8. Drop candidates shorter than `minPartFrames`, incrementing `discardedShortParts`.
9. Return parts in seconds (`frame / rate`). Check `cancel` once per block and return the parts found so far.

- [ ] **Step 4: Run the tests**

Run: `build/tests/SonicatorTests "[split][detector]" --reporter compact`
Expected: PASS with boundaries within the 10 ms margins.

- [ ] **Step 5: Commit**

```bash
git add core/split/SilenceDetector.h core/split/SilenceDetector.cpp tests/TestSilenceSplit.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add streaming silence detector for splitting"
```

---

### Task 3: Background split service

**Files:**
- Create: `core/split/SplitService.h`
- Create: `core/split/SplitService.cpp`
- Modify: `CMakeLists.txt` (`SonicatorCore`)
- Modify: `tests/TestSilenceSplit.cpp`

**Interfaces:**
- Consumes: Task 1 `exportRange(..., fadeSeconds)`, Task 2 detector.
- Produces:
  ```cpp
  struct SplitOptions { SilenceOptions silence; juce::File outputFolder; bool sameFolder = true; };
  enum class SplitFileState { split, noSplit, skipped, failed, cancelled };
  struct SplitFileResult
  {
      juce::File source;
      std::vector<juce::File> parts;
      juce::String error;
      SplitFileState state = SplitFileState::skipped;
      int detectedParts = 0;
      int discardedShortParts = 0;
      bool deletable() const;
  };
  struct SplitProgress
  {
      bool running = false, cancelled = false;
      int total = 0, processed = 0, succeeded = 0, failed = 0, skipped = 0;
      juce::String current, message;
  };
  class SplitService final : private juce::Thread
  {
  public:
      explicit SplitService() : Thread("Silence split") {}
      ~SplitService() override;
      bool start(const std::vector<juce::File>&, const SplitOptions&);
      void cancel();
      SplitProgress progress() const;
      std::vector<SplitFileResult> results() const;
      bool waitUntilFinished(int timeoutMs);
  };
  ```

- [ ] **Step 1: Write the failing tests**

Add to `tests/TestSilenceSplit.cpp`:

```cpp
TEST_CASE("Split service writes named parts, keeps originals and never overwrites", "[split][service]")
{
    TestDirectory t;
    auto source = segments(t.dir.getChildFile("loop.wav"),
        { { 44100, true }, { 44100, false }, { 44100, true },
          { 44100, false }, { 44100, true } });
    t.dir.getChildFile("loop_01.wav").replaceWithText("occupied");
    SplitService service;
    SplitOptions options;
    REQUIRE(service.start({ source }, options));
    REQUIRE(service.waitUntilFinished(20000));
    auto results = service.results();
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].state == SplitFileState::split);
    REQUIRE(results[0].deletable());
    REQUIRE(results[0].parts.size() == 3);
    REQUIRE(source.existsAsFile());
    REQUIRE(t.dir.getChildFile("loop_01.wav").loadFileAsString() == "occupied");
    REQUIRE(t.dir.getChildFile("loop_01-1.wav").existsAsFile());
    REQUIRE(t.dir.getChildFile("loop_02.wav").existsAsFile());
    REQUIRE(t.dir.getChildFile("loop_03.wav").existsAsFile());
}

TEST_CASE("A bad file does not stop the batch and dropped fragments block deletion", "[split][service]")
{
    TestDirectory t;
    auto good = segments(t.dir.getChildFile("good.wav"),
        { { 44100, true }, { 44100, false }, { 44100, true } });
    auto blip = segments(t.dir.getChildFile("blip.wav"),
        { { 44100, true }, { 22050, false }, { 2205, true },
          { 22050, false }, { 44100, true } });
    auto missing = t.dir.getChildFile("missing.wav");
    SplitService service;
    REQUIRE(service.start({ good, missing, blip }, SplitOptions{}));
    REQUIRE(service.waitUntilFinished(20000));
    auto results = service.results();
    REQUIRE(results.size() == 3);
    REQUIRE(results[0].state == SplitFileState::split);
    REQUIRE(results[0].deletable());
    REQUIRE(results[1].state == SplitFileState::skipped);
    REQUIRE_FALSE(results[1].deletable());
    REQUIRE(results[2].discardedShortParts == 1);
    REQUIRE_FALSE(results[2].deletable());
}

TEST_CASE("No-silence sources and unusable output folders are reported, not deleted", "[split][service]")
{
    TestDirectory t;
    auto continuous = segments(t.dir.getChildFile("continuous.wav"), { { 132300, true } });
    SplitService service;
    REQUIRE(service.start({ continuous }, SplitOptions{}));
    REQUIRE(service.waitUntilFinished(20000));
    auto results = service.results();
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].state == SplitFileState::noSplit);
    REQUIRE(results[0].parts.empty());
    REQUIRE(continuous.existsAsFile());

    auto source = segments(t.dir.getChildFile("tone.wav"),
        { { 44100, true }, { 44100, false }, { 44100, true } });
    SplitOptions bad; bad.sameFolder = false; bad.outputFolder = t.dir.getChildFile("not-a-folder.wav");
    bad.outputFolder.replaceWithText("x");
    SplitService second;
    REQUIRE(second.start({ source }, bad));
    REQUIRE(second.waitUntilFinished(20000));
    results = second.results();
    REQUIRE(results[0].state == SplitFileState::failed);
    REQUIRE_FALSE(results[0].deletable());
    REQUIRE(source.existsAsFile());
}

TEST_CASE("Only a complete split is deletable", "[split][service]")
{
    SplitFileResult r;
    r.state = SplitFileState::split;
    r.detectedParts = 2; r.parts = { juce::File("/tmp/a_01.wav"), juce::File("/tmp/a_02.wav") };
    REQUIRE(r.deletable());
    r.discardedShortParts = 1;
    REQUIRE_FALSE(r.deletable());
    r.discardedShortParts = 0; r.state = SplitFileState::cancelled;
    REQUIRE_FALSE(r.deletable());
    r.state = SplitFileState::failed;
    REQUIRE_FALSE(r.deletable());
    r.state = SplitFileState::noSplit;
    REQUIRE_FALSE(r.deletable());
}
```

- [ ] **Step 2: Run to confirm failure**

Run: `cmake --build build --target SonicatorTests --parallel 6`
Expected: compile error — `SplitService` does not exist.

- [ ] **Step 3: Implement the service**

Add `core/split/SplitService.cpp` to `SonicatorCore`.

- `start` refuses when `isThreadRunning()`, waits for a previous thread, stores `files` and `options`, resets `progress`, `results`, and the cancel flag, then `startThread()`.
- `run()` for each source, checking `cancelled` before each file:
  1. `FileScanner::isSupportedFormat(file) && ! FileScanner::isMetadataOnlyFormat(file)` else `skipped`.
  2. Output folder = `sameFolder ? file.getParentDirectory() : options.outputFolder`; `createDirectory()` failure → `failed`.
  3. Open a reader with `AudioFormatManager`; `detectSplitParts(*reader, options.silence, cancelCheck)`.
  4. `< 2` parts → `noSplit`; error text "No silence found".
  5. `width = max(2, (int) juce::String(parts.size()).length())`; for each part `i` compute a free destination with the helper below; call `AudioExporter::exportRange(file, start, end, destination, cancelCheck, 0.005)`.
  6. Any failed export → `state = failed`, keep already written parts, stop this file.
  7. Otherwise `state = split`, `succeeded++`, record `discardedShortParts`.
- If cancelled before or during a file, set `state = cancelled` and stop after that file. `progress.cancelled = cancelled`.
- `results()` returns the vector under a mutex; `waitUntilFinished` uses `waitForThreadToExit`.

Free helpers in `SplitService.cpp` (test through the service):

```cpp
juce::String splitStem(const juce::String& fileNameWithoutExtension);
juce::File uniquePartFile(const juce::File& folder, const juce::String& stem, int index, int width);
```

`splitStem` replaces `< > : " / \\ | ? *` and control characters with `_`, trims trailing spaces and dots, caps at 120 characters and falls back to `audio`. `uniquePartFile` builds `<stem>_<padded index>.wav`; if it exists, tries `<stem>_<padded index>-1.wav` … `-99.wav` and returns `{}` when none is free.

- [ ] **Step 4: Run the tests**

Run: `build/tests/SonicatorTests "[split][service]" --reporter compact`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add core/split/SplitService.h core/split/SplitService.cpp tests/TestSilenceSplit.cpp CMakeLists.txt
git commit -m "feat: add background silence split service"
```

---

### Task 4: Remove catalog rows for trashed originals

**Files:**
- Modify: `core/database/SampleRepository.h`
- Modify: `core/database/SampleRepository.cpp`
- Modify: `tests/TestIgnore.cpp`

**Interfaces:**
- Produces: `void SampleRepository::removeByPaths(const juce::StringArray& paths);`

- [ ] **Step 1: Write the failing test**

Add to `tests/TestIgnore.cpp`:

```cpp
TEST_CASE("removeByPaths drops rows, tag links and exact ignore entries", "[database][remove]")
{
    TestDirectory t; Database db(t.dir.getChildFile("remove.sqlite"));
    SampleRepository repo(db); repo.createTables();
    SampleRecord r; r.filePath = "/lib/gone.wav"; r.fileName = "gone.wav";
    const auto id = repo.upsert(r);
    const auto tag = repo.createTag("Kick", 0xff00ff00);
    repo.setSampleTag(id, tag, true);
    repo.ignorePath("/lib/gone.wav", false);
    repo.removeByPaths({ "/lib/gone.wav" });
    REQUIRE_FALSE(repo.findByPath("/lib/gone.wav").has_value());
    REQUIRE_FALSE(repo.hasExactIgnore("/lib/gone.wav"));
    REQUIRE(repo.tagsForSample(id).empty());
}
```

- [ ] **Step 2: Run to confirm failure**

Run: `cmake --build build --target SonicatorTests --parallel 6`
Expected: compile error — `removeByPaths` does not exist.

- [ ] **Step 3: Implement it**

Inside one `BEGIN IMMEDIATE` transaction, for each path run `DELETE FROM samples WHERE path=?` and `DELETE FROM ignored_paths WHERE path=? AND is_folder=0`. Foreign keys cascade tag links, duplicates and smart rows. Do not touch folder ignore entries.

- [ ] **Step 4: Run the test**

Run: `build/tests/SonicatorTests "[database][remove]" --reporter compact`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add core/database/SampleRepository.h core/database/SampleRepository.cpp tests/TestIgnore.cpp
git commit -m "feat: remove catalog rows for deleted originals"
```

---

### Task 5: Index several output folders in one run

**Files:**
- Modify: `core/indexing/IndexingService.h`
- Modify: `core/indexing/IndexingService.cpp`
- Modify: `tests/TestIndexing.cpp`

**Interfaces:**
- Produces: `bool IndexingService::startFolders(const juce::StringArray& folders, bool force = false);` — discovers the folders, never registers them as roots.

- [ ] **Step 1: Write the failing test**

Add to `tests/TestIndexing.cpp`:

```cpp
TEST_CASE("startFolders indexes several folders without registering roots", "[indexing][split]")
{
    TestDirectory t;
    t.wave("a/one.wav"); t.wave("b/two.wav");
    Database db(t.dir.getChildFile("folders.sqlite")); SampleRepository repo(db); repo.createTables();
    IndexingService index(repo);
    REQUIRE(index.startFolders({ t.dir.getChildFile("a").getFullPathName(),
                                 t.dir.getChildFile("b").getFullPathName() }));
    REQUIRE(index.waitUntilFinished(20000));
    REQUIRE(repo.count() == 2);
    REQUIRE(repo.roots().isEmpty());
}
```

- [ ] **Step 2: Run to confirm failure**

Run: `cmake --build build --target SonicatorTests --parallel 6`
Expected: compile error — `startFolders` does not exist.

- [ ] **Step 3: Implement it**

```cpp
bool IndexingService::startFolders(const juce::StringArray& roots, bool force)
{
    return beginScan(roots, force, ScanMode::Full, false);
}
```

Make `startSubtree` delegate to `startFolders({ folder }, force)`.

- [ ] **Step 4: Run the test**

Run: `build/tests/SonicatorTests "[indexing][split]" --reporter compact`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add core/indexing/IndexingService.h core/indexing/IndexingService.cpp tests/TestIndexing.cpp
git commit -m "feat: scan several output folders in one indexing run"
```

---

### Task 6: Persist split dialog defaults

**Files:**
- Modify: `core/settings/AppSettings.h`
- Modify: `core/settings/AppSettings.cpp`
- Modify: `tests/TestSettings.cpp`

**Interfaces:**
- Produces: `double splitThresholdDb = -60.0; int splitMinSilenceMs = 250; int splitMinPartMs = 100; bool splitSameFolder = true;`

- [ ] **Step 1: Write the failing test**

Add to `tests/TestSettings.cpp`:

```cpp
TEST_CASE("Split defaults persist and clamp to the supported ranges", "[settings][split]")
{
    TestDirectory t; SettingsStore store(t.dir.getChildFile("settings.json"));
    auto loaded = store.load();
    REQUIRE(loaded.splitThresholdDb == -60.0);
    REQUIRE(loaded.splitMinSilenceMs == 250);
    REQUIRE(loaded.splitMinPartMs == 100);
    REQUIRE(loaded.splitSameFolder);
    store.update([](AppSettings& s)
    {
        s.splitThresholdDb = -45.0; s.splitMinSilenceMs = 800;
        s.splitMinPartMs = 200; s.splitSameFolder = false;
    });
    loaded = store.load();
    REQUIRE(loaded.splitThresholdDb == -45.0);
    REQUIRE(loaded.splitMinSilenceMs == 800);
    REQUIRE(loaded.splitMinPartMs == 200);
    REQUIRE_FALSE(loaded.splitSameFolder);
}
```

- [ ] **Step 2: Run to confirm failure**

Run: `cmake --build build --target SonicatorTests --parallel 6`
Expected: compile error — the fields do not exist.

- [ ] **Step 3: Implement the fields**

Add the four fields. In `load()`, read them with `object->getProperty` and clamp: threshold to `[-80, -20]`, silence to `[50, 2000]`, part to `[20, 2000]`; `splitSameFolder` uses the existing `readBool` helper. In `save()`, write `splitThreshold`, `splitMinSilence`, `splitMinPart`, `splitSameFolder`.

- [ ] **Step 4: Run the test**

Run: `build/tests/SonicatorTests "[settings][split]" --reporter compact`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add core/settings/AppSettings.h core/settings/AppSettings.cpp tests/TestSettings.cpp
git commit -m "feat: persist split dialog defaults"
```

---

### Task 7: Split dialog

**Files:**
- Create: `ui/split/SplitDialog.h`
- Create: `ui/split/SplitDialog.cpp`
- Modify: `CMakeLists.txt` (`SonicatorUI`)
- Modify: `tests/TestUI.cpp`

**Interfaces:**
- Produces:
  ```cpp
  class SplitDialog final : public juce::Component
  {
  public:
      SplitDialog(std::vector<juce::File> sources, const AppSettings&,
                  std::function<void(SplitOptions)> onAccept);
      void paint(juce::Graphics&) override;
      void resized() override;
      SplitOptions currentOptions() const;
  private:
      std::vector<juce::File> sources;
      SplitOptions options;
      juce::Slider threshold, minSilence, minPart;
      juce::ToggleButton sameFolder { "Save next to each original file" };
      juce::TextEditor folder;
      juce::TextButton browse { "Browse..." }, accept { "Split" }, cancel { "Cancel" };
      std::function<void(SplitOptions)> acceptCallback;
  };
  ```

- [ ] **Step 1: Write the failing test**

Add to `tests/TestUI.cpp`:

```cpp
TEST_CASE("Split dialog exposes the persisted defaults and same-folder mode", "[ui][split]")
{
    AppSettings settings;
    settings.splitThresholdDb = -55.0;
    settings.splitMinSilenceMs = 400;
    int accepted = 0;
    SplitDialog dialog({ juce::File("/tmp/a.wav") }, settings, [&](SplitOptions) { ++accepted; });
    const auto options = dialog.currentOptions();
    REQUIRE(options.silence.thresholdDb == -55.0);
    REQUIRE(options.silence.minSilenceMs == 400.0);
    REQUIRE(options.sameFolder);
    REQUIRE(dialog.findChildWithID("splitThreshold") != nullptr);
    REQUIRE(dialog.findChildWithID("splitSameFolder") != nullptr);
}
```

- [ ] **Step 2: Run to confirm failure**

Run: `cmake --build build --target SonicatorTests --parallel 6`
Expected: compile error — `SplitDialog` does not exist.

- [ ] **Step 3: Implement the dialog**

Add `ui/split/SplitDialog.cpp` to `SonicatorUI`. Build a vertical layout with:

- A title label `Split <N> file(s) by silence`.
- Threshold slider −80…−20, step 1, suffix ` dBFS`, component ID `splitThreshold`.
- Minimum silence slider 50…2000, step 10, suffix ` ms`, ID `splitMinSilence`.
- Minimum part slider 20…2000, step 10, suffix ` ms`, ID `splitMinPart`.
- `sameFolder` toggle, ID `splitSameFolder`, default from settings. When on, disable the folder editor and Browse; when off, enable them and prefill the first source's parent folder.
- Folder editor + `browse` button using `juce::FileChooser` async, `folder` as the selected folder.
- `accept` and `cancel` buttons. `accept` validates `sameFolder || juce::File(folder.getText()).isDirectory()` and calls `acceptCallback(currentOptions())`; otherwise shows an inline error label.

`currentOptions()` copies the slider values into `options.silence` and `options.outputFolder`, and `options.sameFolder`.

- [ ] **Step 4: Run the test**

Run: `build/tests/SonicatorTests "[ui][split]" --reporter compact`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add ui/split/SplitDialog.h ui/split/SplitDialog.cpp CMakeLists.txt tests/TestUI.cpp
git commit -m "feat: add the split settings dialog"
```

---

### Task 8: Main window wiring, progress and Trash prompt

**Files:**
- Modify: `ui/MainComponent.h`
- Modify: `ui/MainComponent.cpp`

**Interfaces:**
- Consumes: Tasks 1–7.
- Produces: private members `std::unique_ptr<SplitService> splitter; juce::Component::SafePointer<juce::DialogWindow> splitDialog; juce::StringArray pendingSplitScan;` and methods `void startSplit(const std::vector<juce::File>&); void finishSplit(); void requestSplitScan(const juce::StringArray& folders);`.

- [ ] **Step 1: Add the context-menu entry**

In `showSampleContextMenu`, compute `splitCandidates`: selected paths that exist as files, are supported, are not metadata-only and are not ignored. Add:

```cpp
menu.addItem(9008, splitCandidates.size() > 1
    ? "Split by silence (" + juce::String(splitCandidates.size()) + ")..."
    : juce::String("Split by silence..."), ! splitCandidates.empty());
```

In the callback, `result == 9008` calls `startSplit(splitCandidates)`.

- [ ] **Step 2: Open the dialog and start the service**

`startSplit(files)`:

- Return with a status message when `splitter->progress().running` or any library job is running.
- Create `new SplitDialog(files, appSettings, [safe = SafePointer<MainComponent>(this)](SplitOptions options) { if (safe == nullptr) return; if (safe->splitDialog != nullptr) safe->splitDialog->exitModalState(0); safe->splitter->start(files, options); /* persist defaults */ });`. The dialog closes itself from accept/cancel exactly like `AutoTagDialog::closeDialog()` (`findParentComponentOfClass<juce::DialogWindow>()->exitModalState(0)`), so the callback may simply start the service.
- Persist `options.silence` and `options.sameFolder` through `settingsStore.update`.
- Host it with `juce::DialogWindow::LaunchOptions` exactly like `showAutoTagDialog` (owned content, `theme::background`, no native title bar, resizable), storing `splitDialog = options.launchAsync();` and assigning `&look`.

- [ ] **Step 3: Poll progress and finish**

In `timerCallback`, before the tagging/AutoTag/indexing status branches:

```cpp
const auto sp = splitter ? splitter->progress() : SplitProgress{};
if (sp.running)
{
    status.setText("Splitting " + juce::String(sp.processed) + "/" + juce::String(sp.total)
        + (sp.current.isNotEmpty() ? "  |  " + sp.current : ""), juce::dontSendNotification);
    status.setTooltip(status.getText());
}
```

Track `splitWasRunning`; when it transitions to false, call `finishSplit()` once.

Enable/disable buttons: while split runs, `cancel.setEnabled(true)`, `pause.setEnabled(false)`, and `addFolder/rescan/reanalyze/newFiles/smartButton` stay disabled. In `cancel.onClick`, cancel the splitter first.

Guard `requestScan`, `scanRoots`, smart start, tagging start and AutoTag start with `if (splitter && splitter->progress().running) { status.setText("Wait for Split to finish.", juce::dontSendNotification); return; }`. Guard `startSplit` symmetrically against running library jobs.

- [ ] **Step 4: Finish: summary, scan and deletion prompt**

`finishSplit()`:

1. `const auto results = splitter->results();` count `split`, `failed`, `skipped`, total parts.
2. Build `juce::StringArray folders` from every result's parts' parent folders (deduplicated); call `requestSplitScan(folders)`.
3. Show the summary in the status label.
4. Collect `deletable` results. If none, return. Otherwise `dialogs::showAlert(*this, "Split complete", message, { { "Move originals to Trash", 1 }, { "Keep originals", 0 } }, callback, juce::MessageBoxIconType::QuestionIcon)` where `message` says how many files and parts, that originals go to the Trash, and names files that were skipped (`results` with `failed/skipped`). On choice 1, for each deletable source call `moveToTrash()`; also trash same-stem `.asd` and `.pk` sidecars when present; collect paths that moved; call `writeRepo->removeByPaths(moved)`, `writeRepo->refreshDuplicates()`, `reloadLibrary()`, refresh both trees. On a failed move, keep that path and append it to the status message.

`requestSplitScan(folders)` stores `pendingSplitScan` and tries to start `indexer->startFolders(folders)` when `indexer` is idle; otherwise the existing 30 Hz timer starts it as soon as `indexer->progress().running` becomes false. Clear `pendingSplitScan`, cancel `splitter`, drop `splitDialog` and discard pending results in `switchCatalog`.

- [ ] **Step 5: Build and run the suite**

Run: `cmake --build build --parallel 6 && ctest --test-dir build --output-on-failure`
Expected: build succeeds, all tests pass.

- [ ] **Step 6: Manual acceptance**

Copy two files from `/Volumes/1TBSSD/samples/!__OLDSCHOOL/AMG 160dB The Drum and Bass Interface` into a temporary folder and import that folder. Select both, run `Split by silence...` with defaults. Verify: each file yields several `_01…` parts; parts appear in the library after the automatic scan; choosing `Keep originals` leaves the sources untouched; choosing `Move originals to Trash` removes the sources and their rows; the Trash contains the originals; a second run into the same folder does not overwrite the first parts.

- [ ] **Step 7: Commit**

```bash
git add ui/MainComponent.h ui/MainComponent.cpp
git commit -m "feat: wire silence split into the library UI"
```
