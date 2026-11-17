# SONICATOR User Manual Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce a 70–95 page, print-quality English user manual for SONICATOR as an A4 PDF built with Typst, with every screenshot rendered from the real UI components.

**Architecture:** A hidden Catch2 test seeds a temporary catalog with synthetic samples, tags, stars and metadata, then snapshots the real `MainComponent` and dialogs to `docs/manual/assets/`. Typst sources in `docs/manual/` assemble the book; a PyMuPDF script verifies structure, fonts and links. Prose is written per part, compiled early and often.

**Tech Stack:** C++20/JUCE 9 test binary (Catch2 3.7.1), Typst 0.15.1, Manrope from `resources/fonts`, Python 3 + PyMuPDF for PDF verification.

**Spec:** `docs/superpowers/specs/2026-11-17-user-manual-design.md`

## Global Constraints

- English only, British spelling ("behaviour", "colour", "analysed"); UI labels keep their exact casing and are **bold**.
- Audience: first-time user, macOS or Windows, no command line. Keys are words in caps: **Cmd**, **Shift**, **Opt**, **Ctrl**; differences written "Cmd+S (macOS) / Ctrl+S (Windows)".
- Target release: `0.1.0`, built from the commit recorded on the colophon.
- Body Manrope 10.5 pt on A4, 148 mm measure; accent `#c2611c`; dark plate `#17181a`.
- Screenshots only from real components (`createComponentSnapshot`), never mockups, never the owner's catalog. Detail plates are crops at 100%; overview plates are 2× renders on landscape pages 257 mm wide. No image scaled below 70% of its rendered size.
- Every factual claim traces to `README.md`, a `docs/*.md` file or the source. Code wins over prose; contradictions are fixed in the manual and reported.
- Never describe Windows-only behaviour as verified; say what was actually checked.
- Destructive actions always name their confirmation: Remove from library, Reset tags, Reanalyze all, Overwrite, Replace output.
- Sources and the built PDF are committed under `docs/manual/`.

## Review Focus

These are the failure modes a reader will actually hit; each has a test or a named verification step in the owning task.

1. **A search example that matches nothing** in a real catalog because of a typo in a mask, `ext:`, `type:`, `key:` or `note:` — Task 5 runs every example through `parseSearchTerms` and `queryIds` on the seeded catalog.
2. **A shortcut that does nothing on one platform** because the manual invented it or mixed platforms — Task 13 checks every key against `docs/COMMANDS.md` and the registry defaults; the reading guide states the platform split.
3. **Wording that sounds like it deletes audio** where the app only removes catalog rows or writes a copy — Task 8 and Task 14 check each against the confirmation strings in the source (`Remove from library`, `Reset tags`, `Overwrite`, `Save copy`, `Replace output`).
4. **A screenshot whose state the reader cannot reproduce** because the seeded data or the open dialog does not match the documented steps — Task 2–4 capture only states reachable from a documented action, and `assets/SOURCES.md` records the action for each plate.
5. **A step list that hides a required confirmation** (Reanalyze all, Reset tags, Replace output, Overwrite) — Task 8/14 copy-edit pass walks every numbered step and marks where a dialog appears.

---

## File Structure

- `tests/TestManualShots.cpp` — hidden `[.manual-shots]` test: seeds the catalog, captures every plate. Grows by one capture block per task.
- `tests/TestManualClaims.cpp` — non-hidden tests that pin the manual's search examples and command ids against the real code.
- `tests/CMakeLists.txt` — registers both files on `SonicatorTests`.
- `docs/manual/style.typ` — page geometry, type scale, colours, key caps, callouts, figures, tables, running feet.
- `docs/manual/manual.typ` — front matter, part dividers, chapter includes, outline.
- `docs/manual/chapters/NN-slug.typ` — one file per chapter, 25 files.
- `docs/manual/appendices/A-shortcuts.typ` … `E-glossary.typ`.
- `docs/manual/assets/*.png` — generated plates; `docs/manual/assets/SOURCES.md` — plate → capture block.
- `docs/manual/verify.py` — PyMuPDF structural checks.
- `docs/manual/README.md` — build and regenerate commands.
- `docs/manual/sonicator-manual.pdf` — the deliverable.

---

### Task 1: Screenshot harness and the workspace plate

**Files:**
- Create: `tests/TestManualShots.cpp`
- Modify: `tests/CMakeLists.txt` (add `target_sources(SonicatorTests PRIVATE TestManualShots.cpp)` after the `TestStudioRendering.cpp` line)

**Interfaces:**
- Consumes: `TestDirectory` and `pumpUntil` from `tests/TestHelpers.h`; `MainComponent`, `AppSettings`, `ThemeMode`.
- Produces: `namespace manual { juce::File seedLibrary(const juce::File& dir); void capture(juce::Component&, int scale, const juce::String& name); }` used by Tasks 2–4.

- [ ] **Step 1: Write the failing capture test**

```cpp
TEST_CASE("Manual plates render from the real components", "[.manual-shots]")
{
    const auto out = juce::SystemStats::getEnvironmentVariable("SONICATOR_MANUAL_SHOT_DIR", {});
    if (out.isEmpty()) return;
    const juce::File folder(out);
    folder.createDirectory();
    TestDirectory t;
    theme::apply(ThemeMode::Dark);
    AppSettings settings;
    settings.theme = ThemeMode::Dark;
    MainComponent app(t.dir.getChildFile("manual.sqlite"), false, settings,
                      t.dir.getChildFile("manual-settings.json"));
    app.setSize(1400, 900);
    app.resized();
    manual::capture(app, 2, "workspace.png");
    theme::apply(ThemeMode::System);
}
```

- [ ] **Step 2: Run it and confirm the failure**

Run:
```sh
cmake --build --preset debug --target SonicatorTests
SONICATOR_MANUAL_SHOT_DIR=$PWD/docs/manual/assets \
  ~/Library/Caches/sonicator-build/debug/tests/SonicatorTests "[.manual-shots]"
```
Expected: link error for `manual::capture`, or no PNG written.

- [ ] **Step 3: Implement `manual::capture`**

`capture` takes `createComponentSnapshot(component.getLocalBounds(), false, (float) scale)` and writes a PNG through `juce::PNGImageFormat`; it `REQUIRE`s the image is valid and at least 1000 px wide, and prints the output path.

- [ ] **Step 4: Run again and check the image**

Run the command from Step 2. Expected: `docs/manual/assets/workspace.png`, 2800×1800, dark UI, one empty table.

- [ ] **Step 5: Commit**

```bash
git add tests/TestManualShots.cpp tests/CMakeLists.txt
git commit -m "test: add the manual screenshot harness"
```

### Task 2: Seeded library and the list plates

**Files:**
- Modify: `tests/TestManualShots.cpp`
- Create: `docs/manual/assets/SOURCES.md`

**Interfaces:**
- Consumes: `capture`, `seedLibrary` from Task 1.
- Produces: `manual::seedLibrary` returns a populated catalog path; `docs/manual/assets/{workspace-map,table,search,tags,analysis,preview}.png`.

- [ ] **Step 1: Seed the catalog and assert its shape**

Extend `seedLibrary` to create five folders under the temp root — `Drums`, `Bass`, `Pads`, `Loops`, `Vocals` — and about 40 mono 16-bit WAVs named plausibly (`Kick_808_Am_120.wav`, `Snare_Tight_01.wav`, `Hat_Closed_16.wav`, `Bass_Sub_Cmin_140.wav`, `Pad_Warm_Cmin.wav`, `Loop_Dub_Chords_120.wav`, `Vox_Chop_Ah.wav`, …) with durations of 0.4–4 s. For each: `repo.upsert(BasicAnalyzer().analyze(file))`, then `repo.addRoot` for the library root.

Add tags through the repository and assign them: `Kick`, `Snare`, `Hat`, `Bass`, `Pad`, `Loop`, `Vocal`, `Dub` (colours from the built-in palette), star six rows, and set smart overrides so `type:kick`, `key:Am`, `genre:house` and `note:C4` have matches.

Add two duplicate rows with identical SHA-256 and size and call `repo.refreshDuplicates()`; add one row with `missing = true` and one `error` row.

Assert in the test: `repo.count() == 40`, at least 8 tags, at least 2 duplicate members.

- [ ] **Step 2: Run the test and check the counts pass**

Run the Task 1 command. Expected: PASS, and the empty-table plate is replaced by a populated one.

- [ ] **Step 3: Capture the four list plates**

In the same test, after the base workspace shot:
- `workspace.png` — full window at 2× for the landscape plate;
- `table.png` — crop of the table with the duplicate `×2` row and the selection bar visible, 100%;
- `search.png` — `app.searchLibrary("type:kick key:Am")`, wait with `pumpUntil` for the row count to settle, crop header + table;
- `tags.png` — select three rows, call `app.setTagFilters({kickTagId}, {}, false)`, crop the toolbar chips and tag strip;
- `folder-tree.png` — crop of the sidebar with Favorites expanded, 100%;
- `analysis.png` — select a pad row and snapshot the Analysis card with padding;
- `preview.png` — select a row, snapshot the preview band;
- `selection.png` — set a waveform range through the preview panel, wait for the prepared-export status, snapshot the preview band showing the range and its label.

Use `findDescendantWithID(app, "library")`, `"analysisDetails"`, `"preview"` for the crops. If the range cannot be set without mouse events, expose the smallest test hook on the preview panel (a `setSelectionForTest(start, end)` that calls the same path as a drag) rather than simulating a drag.

- [ ] **Step 4: Run and inspect each PNG**

Open the six files. Expected: no blank panels, readable labels at 100% crops, duplicate badge visible in `table.png`, `search.png` shows a filtered result, `analysis.png` shows Key/BPM.

- [ ] **Step 5: Record provenance**

Create `docs/manual/assets/SOURCES.md` with a table: file, capture block, documented action, chapter.

- [ ] **Step 6: Commit**

```bash
git add tests/TestManualShots.cpp docs/manual/assets
git commit -m "test: seed the manual library and capture list plates"
```

### Task 3: Dialog plates

**Files:**
- Modify: `tests/TestManualShots.cpp`
- Modify: `docs/manual/assets/SOURCES.md`

**Interfaces:**
- Consumes: the seeded catalog from Task 2.
- Produces: `tag-editor.png`, `autotag.png`, `split.png`, `extract.png`, `batch.png`.

- [ ] **Step 1: Capture AutoTag**

Seed the catalog, construct `AutoTagService service(repo)`, `service.startSelected(allIds)`, `REQUIRE(service.waitUntilFinished(60000))`, `AutoTagResult result; REQUIRE(service.takeResult(result))`, then `AutoTagDialog dialog(result, service, []{});` set 980×720, snapshot at 2×.

- [ ] **Step 2: Capture Split and Extract**

`SplitDialog split({ firstPadFile }, AppSettings{}, [](SplitOptions) { return true; });` at 520×430; `ExtractDialog extract({ firstSf2OrWav }, AppSettings{}, [](ExtractOptions) { return true; });` at its default size after `resized()`.

- [ ] **Step 3: Capture Batch**

```cpp
batch::Request request;
request.sources.kind = batch::SourceSelection::Kind::rows;
request.sources.rows = firstSixIds;
request.destination.nextToSources = true;
request.recipe.steps = { batch::RemoveDc{}, batch::TrimSilence{ -60.0, 5.0 },
                         batch::Normalize{ -0.1 }, batch::FadeOut{ 2.0, false } };
request.recipe.outputs = { batch::OutputPreset{} };
auto prepared = batch::prepareBatch(repo, request, {});
REQUIRE(prepared.ok);
batch::BatchProcessingDialog dialog(prepared.batch, t.dir.getChildFile("presets.json"));
dialog.setSize(980, 720);
dialog.resized();
```

`PreflightResult` carries the prepared batch in its `batch` field; `REQUIRE(prepared.ok)` before opening the dialog.

- [ ] **Step 4: Capture the tag editor or fall back**

If the app has a tag-management surface that can be opened headlessly (start from `MainComponent::setTagFilters` and the tag strip), capture it as `tag-editor.png`. If it is a native menu only, skip this plate and note in `SOURCES.md` that chapter 7 uses the `tags.png` crop.

- [ ] **Step 5: Run and inspect**

Run the Task 1 command. Expected: all five/six PNGs render with real content, no clipped footer buttons, no empty tables.

- [ ] **Step 6: Commit**

```bash
git add tests/TestManualShots.cpp docs/manual/assets
git commit -m "test: capture the manual dialog plates"
```

### Task 4: Cloud, Settings and plugin plates

**Files:**
- Modify: `tests/TestManualShots.cpp`
- Modify: `docs/manual/assets/SOURCES.md`

**Interfaces:**
- Consumes: seeded catalog; `MainComponent::showCloudView(bool)`; `SettingsPanel::selectSection(SettingsSection)`; `plugin::PluginProcessor`.
- Produces: `cloud.png`, `cloud-zoom.png`, `settings-general.png`, `settings-hotkeys.png`, `plugin.png`.

- [ ] **Step 1: Capture the cloud**

`app.showCloudView(true)`, find `"cloudView"`, `pumpUntil` for `hasLayout()`, run 200 frames, snapshot at 2×; then `zoomBy(3.0f, centre)` and snapshot a crop for `cloud-zoom.png`.

- [ ] **Step 2: Capture Settings**

Build `SettingsPanel panel(settings, catalogPath, false)` with `SonicatorLookAndFeel`, size 820×640, `selectSection(general)` → `settings-general.png`, `selectSection(hotkeys)` → `settings-hotkeys.png`.

- [ ] **Step 3: Capture the plugin window**

```cpp
plugin::PluginProcessor processor(catalogPath, settingsPath);
std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
editor->setSize(plugin::PluginState::defaultWidth, plugin::PluginState::defaultHeight);
manual::capture(*editor, 2, "plugin.png");
```

- [ ] **Step 4: Run and inspect all five plates**

Expected: cloud has visible islands and labels, Settings pages are filled, plugin shows the browser with its read-only toolbar.

- [ ] **Step 5: Commit**

```bash
git add tests/TestManualShots.cpp docs/manual/assets
git commit -m "test: capture cloud, settings and plugin plates"
```

### Task 5: Search-example test

**Files:**
- Create: `tests/TestManualClaims.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `parseSearchTerms`, `LibraryFilter`, `SampleRepository`, seeded WAVs.
- Produces: a test that fails if a documented example stops parsing or matching.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("Manual search examples parse and match a real catalog", "[manual][search]")
{
    TestDirectory t;
    // About 8 named WAVs: Kick_808_Am_120.wav, Snare_Tight_01.wav, Pad_Warm_Cmin.wav,
    // Loop_Dub_Chords_120.wav, Vox_Chop_Ah.wav, Hat_Closed_16.wav, Bass_Sub_Cmin_140.wav,
    // Rimshot_02.wav
    // ... seed repository with BasicAnalyzer and smart overrides for Kick / Am / house / C4 ...

    struct Example { const char* query; size_t minimum; };
    const Example examples[] {
        { "kick", 1 }, { "ext:wav", 8 }, { "type:kick", 1 },
        { "key:Am", 1 }, { "note:C4", 1 }, { "genre:house", 1 }, { "\"bass drum\"", 0 }
    };
    for (const auto& e : examples)
    {
        LibraryFilter filter;
        filter.text = e.query;
        const auto ids = repo.queryIds(filter);
        INFO("example: " << e.query);
        CHECK(ids.size() >= e.minimum);
        CHECK(! parseSearchTerms(e.query).empty());
    }
    // Masks and alternation are parse-only here: the fixture has no RX2/SF2 rows.
    for (const char* query : { "*.rx2", "*.rx2|*.sf2", "kick_*", "*.rex/rx2/sf2" })
        CHECK(! parseSearchTerms(query).empty());
}
```

- [ ] **Step 2: Run and watch it fail**

Run: `~/Library/Caches/sonicator-build/debug/tests/SonicatorTests "[manual][search]"`.
Expected: FAIL — the file is not in the binary yet.

- [ ] **Step 3: Register the file and make the fixture**

Add the source to CMake and seed the eight files with the metadata the examples need: `SmartOverride o; o.textValue = "Kick"; repo.setSmartOverride(id, SmartField::Type, o);` and the same for `SmartField::Key` (`"Am"`), `SmartField::Genre` (`"house"`) and `SmartField::RootNote` (text `"C4"`), following the field semantics in `core/smart/SmartTypes.h`.

- [ ] **Step 4: Run to pass**

Same command. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add tests/TestManualClaims.cpp tests/CMakeLists.txt
git commit -m "test: pin the manual search examples to the real parser"
```

### Task 6: Typst scaffold, cover and outline

**Files:**
- Create: `docs/manual/style.typ`, `docs/manual/manual.typ`, `docs/manual/chapters/01-what-it-is.typ` (stub), `docs/manual/verify.py`, `docs/manual/README.md`

**Interfaces:**
- Produces: `#let body(...)`, `#let note(...)`, `#let tip(...)`, `#let warning(...)`, `#let keys(...)`, `#let figure(path, caption)`, `#let plate(path, caption)`; chapter files call these.

- [ ] **Step 1: Write `style.typ`**

Page A4, margins `(top: 22mm, bottom: 24mm, inside: 22mm, outside: 26mm)`, `set text(font: ("Manrope", "DejaVu Sans"), size: 10.5pt)`; running feet with `context` page numbering; heading styles for part, chapter, H2, H3; key-cap box using a rounded rect; callouts with a 2 pt left rule; figure/plate components; table style with hairlines and 9 pt.

- [ ] **Step 2: Write `manual.typ` with cover, colophon, reading guide, TOC**

Cover: dark page, `/resources/branding/icon-glossy-chevron-1024.png` at 38 mm (absolute path from the Typst root), `/resources/branding/logo-silver.png` as the wordmark, "User Manual", subtitle, "Version 0.1.0 · <commit>". Colophon: licence AGPL-3.0-or-later, copyright, build command. Reading guide: platform keys, screenshot note, code/monospace note. Then `#outline()` with chapter depth.

- [ ] **Step 3: Compile the stub book**

```sh
typst compile --font-path resources/fonts --root . docs/manual/manual.typ \
  docs/manual/sonicator-manual.pdf
```
Expected: a PDF with cover, colophon, guide, TOC and one stub chapter; no warnings.

- [ ] **Step 4: Write `verify.py` and run it**

Checks: page count ≥ 6, outline has ≥ 1 entry, Manrope appears in `doc.get_page_fonts`, no `□` in extracted text, every page has text or an image. Run:
```sh
python3 docs/manual/verify.py docs/manual/sonicator-manual.pdf
```
Expected: `OK` and the page count.

- [ ] **Step 5: Commit**

```bash
git add docs/manual
git commit -m "docs: scaffold the manual build with Typst"
```

### Task 7: Part I — chapters 1–3

**Files:**
- Create: `docs/manual/chapters/01-what-it-is.typ`, `02-installing.typ`, `03-workspace.typ`
- Modify: `docs/manual/manual.typ` (includes)

**Interfaces:**
- Consumes: `body`, `note`, `tip`, `warning`, `keys`, `figure`, `plate`.
- Produces: the voice and chapter template every later chapter copies.

- [ ] **Step 1: Write the chapter template and chapter 1 from the spec**

Chapter 1 uses `README.md` "Use" and "Data"; it must state: catalog is an index; originals are opened read-only; three optional network actions (update check, opt-in contribution, REPORT); what the app does not do. Include the "files ↔ catalog" diagram in `style.typ`.

- [ ] **Step 2: Write chapters 2 and 3**

Chapter 2: `dist/SONICATOR-0.1.0-macOS-universal.pkg`, `Windows/*.msi`, unsigned status, Gatekeeper/SmartScreen, `.sha256`, first launch, data locations from README "Data". Chapter 3: header/search, toolbar, folder tree, table, Analysis, Preview, List/Cloud, panel resizing; figures `workspace-map.png`, `table.png`.

- [ ] **Step 3: Compile and read the pages**

```sh
typst compile --font-path resources/fonts --root . docs/manual/manual.typ docs/manual/sonicator-manual.pdf
python3 docs/manual/verify.py docs/manual/sonicator-manual.pdf
```
Expected: compile clean; page count ≥ 12; every figure appears under its caption on the same spread.

- [ ] **Step 4: Check the five Review Focus items for these chapters**

Search examples none yet; keys match `docs/COMMANDS.md`; "Remove from library" wording matches the confirmation in `ui/library`; each plate's state is reachable by the steps in the text; no step hides a dialog.

- [ ] **Step 5: Commit**

```bash
git add docs/manual
git commit -m "docs: write manual part I"
```

### Task 8: Part II — chapters 4–9 (checkpoint 1)

**Files:**
- Create: `chapters/04-folders.typ` … `09-metadata.typ`
- Modify: `manual.typ`

**Interfaces:**
- Consumes: seeded plates `search.png`, `tags.png`, `analysis.png`, `autotag.png`.
- Produces: first checkpoint for the owner.

- [ ] **Step 1: Write chapters 4–6 from `README.md`, `docs/TAGGING.md` folder section, `docs/PREVIEW.md` folder tree**

Chapter 4 covers roots/scans/cancel/New files/Reanalyze/Analyze selected/missing/Remove from library/Reason and Ableton packs. Chapter 5: columns, sorting, pages, Hide empty, duplicates, errors, selection bar. Chapter 6: the full search syntax with the examples pinned by Task 5.

- [ ] **Step 2: Write chapters 7–9 from `docs/TAGGING.md` and `docs/SMART_ANALYSIS.md`**

Tags chapter follows the actual order in the UI; AutoTag chapter includes the class table (compressed to the common classes plus a note), review dialog and skip reasons; metadata chapter covers Type/BPM/Key/Genre/Structure, overrides, Tap BPM, provenance and unknowns.

- [ ] **Step 3: Compile, verify and read**

Run the Task 7 commands. Expected: page count ≥ 30; figures 4–7 present; AutoTag table does not split a row across pages.

- [ ] **Step 4: Send checkpoint 1**

Give the owner the PDF pages for Part II and ask only for corrections of fact or tone. Stop and wait for the answer before starting Part III.

- [ ] **Step 5: Commit**

```bash
git add docs/manual
git commit -m "docs: write manual part II"
```

### Task 9: Part III — chapters 10–14

**Files:**
- Create: `chapters/10-preview.typ` … `14-cloud.typ`
- Modify: `manual.typ`

**Interfaces:**
- Consumes: `preview.png`, `cloud.png`, `cloud-zoom.png`.
- Produces: playback, editing, export and cloud chapters.

- [ ] **Step 1: Write chapters 10–12 from `docs/PREVIEW.md` and the waveform-editor spec**

Preview toggles and keys, figure `preview.png`; selection gestures including Loop edge drag, figures `selection.png` and a crop of the waveform editor commands (if the context menu cannot be captured headlessly, use a `selection.png` crop and say the commands are also in the right-click menu); prepared WAV and exports folder; editor commands table from `docs/COMMANDS.md`; Overwrite vs Save copy with the exact prompt; what happens to tags/analysis.

- [ ] **Step 2: Write chapters 13–14**

Split/Extract/Normalize with each dialog's outcome; Cloud map: what is drawn, hover audio, camera, drag, filter independence, the no-position caption.

- [ ] **Step 3: Compile, verify, inspect**

Run the Task 7 commands; check the editor command table keys against `docs/COMMANDS.md`; confirm the cloud plate's zoom state matches the caption.

- [ ] **Step 4: Commit**

```bash
git add docs/manual
git commit -m "docs: write manual part III"
```

### Task 10: Part IV — chapters 15–19

**Files:**
- Create: `chapters/15-rex-sf2.typ` … `19-disc-images.typ`
- Modify: `manual.typ`

**Interfaces:**
- Consumes: `docs/PREVIEW.md`, `docs/SFZ.md`, `docs/XI.md`, `docs/EMU_EBL.md`, `docs/KURZWEIL_KRZ.md`, `docs/VOC.md`, `docs/DISC_IMAGES.md`.

- [ ] **Step 1: Write chapters 15–17** (REX2/SF2; EXS24/NKI/SFZ; trackers/XI) with one "what works / what does not" table per family.

- [ ] **Step 2: Write chapters 18–19** (EBL/KRZ/Akai/VOC; disc images) including the synthetic path format and image change behaviour.

- [ ] **Step 3: Compile, verify, inspect**

Run the Task 7 commands. Expected: page count ≥ 55; every format chapter and the later Appendix C use the same four status words: indexed, previewed, metadata-only, not indexed.

- [ ] **Step 4: Commit**

```bash
git add docs/manual
git commit -m "docs: write manual part IV"
```

### Task 11: Part V — chapters 20–22 (checkpoint 2)

**Files:**
- Create: `chapters/20-dragging.typ`, `21-batch.typ`, `22-plugin.typ`
- Modify: `manual.typ`

**Interfaces:**
- Consumes: `batch.png`, `plugin.png`, `docs/BATCH_PROCESSING.md`, `docs/PLUGIN.md`, `docs/PLUGIN_MATCH_ACCEPTANCE.md`.

- [ ] **Step 1: Write chapter 20** (drag semantics, selection WAV formats, keeping references alive) from `docs/PREVIEW.md` and README "Data".

- [ ] **Step 2: Write chapters 21–22** (batch chain table, presets, outputs, naming, split, safety, report; plugin shared catalog, read-only, project state, Match BPM/Scale, host status).

- [ ] **Step 3: Compile, verify, inspect**

Run the Task 7 commands. Expected: landscape batch plate is upright and readable; the chain table lists every step with its range.

- [ ] **Step 4: Send checkpoint 2** with pages for Parts III–V; wait for the owner's answer.

- [ ] **Step 5: Commit**

```bash
git add docs/manual
git commit -m "docs: write manual part V"
```

### Task 12: Part VI — chapters 23–25

**Files:**
- Create: `chapters/23-settings.typ`, `24-hotkeys-layout.typ`, `25-troubleshooting.typ`
- Modify: `manual.typ`

**Interfaces:**
- Consumes: `docs/SHORTCUTS.md`, `docs/DIAGNOSTICS.md`, `docs/PRIVACY.md`, `core/updates`, `core/contribute`, Settings sections.

- [ ] **Step 1: Write chapter 23** including the update check, the Contribute toggle (off by default, what is sent, what is never sent) and REPORT, each with its exact label.

- [ ] **Step 2: Write chapters 24–25**: rebinding and conflicts; logs, `crash.txt`, common situations, backup/restore.

- [ ] **Step 3: Compile, verify, inspect**

Run the Task 7 commands. Expected: page count ≥ 75; Settings plate labels match the text.

- [ ] **Step 4: Commit**

```bash
git add docs/manual
git commit -m "docs: write manual part VI"
```

### Task 13: Appendices A–E

**Files:**
- Create: `appendices/A-shortcuts.typ` … `E-glossary.typ`
- Modify: `manual.typ`

**Interfaces:**
- Consumes: `docs/COMMANDS.md` (defaults), search syntax from chapter 6, format status from chapters 15–19, data locations from README.
- Produces: appendix tables.

- [ ] **Step 1: Write Appendix A** with macOS and Windows columns; include only registered commands from `docs/COMMANDS.md` and the menu-only actions without keys.

- [ ] **Step 2: Write Appendix B** as a two-column table: example → what it matches, reusing the eight tested examples plus masks.

- [ ] **Step 3: Write Appendices C–E**: format status table (indexed / preview / metadata-only / not indexed), files SONICATOR writes, glossary of terms used in the book.

- [ ] **Step 4: Check every key and command id**

Run:
```sh
grep -o '`[a-z]*\.[a-zA-Z.]*`' docs/manual/appendices/A-shortcuts.typ | sort -u
```
Expected: every id appears in `docs/COMMANDS.md`; any id that does not is removed or corrected.

- [ ] **Step 5: Compile and verify**

Run the Task 7 commands. Expected: page count ≥ 85, outline lists all appendices.

- [ ] **Step 6: Commit**

```bash
git add docs/manual
git commit -m "docs: add the manual appendices"
```

### Task 14: Full verification pass

**Files:**
- Modify: any chapter found wrong; `docs/manual/verify.py` if a check is missing.

**Interfaces:**
- Produces: a verified PDF and a claim table.

- [ ] **Step 1: Rebuild from scratch**

```sh
rm -f docs/manual/sonicator-manual.pdf
typst compile --font-path resources/fonts --root . docs/manual/manual.typ docs/manual/sonicator-manual.pdf
```
Expected: no warnings, PDF written.

- [ ] **Step 2: Run the structural check**

`python3 docs/manual/verify.py docs/manual/sonicator-manual.pdf`
Expected: ≥ 70 pages, all chapters in the outline, Manrope embedded, no `□`, every link target present.

- [ ] **Step 3: Render every page and inspect**

```sh
python3 - <<'EOF'
import pymupdf
doc = pymupdf.open("docs/manual/sonicator-manual.pdf")
for i, page in enumerate(doc):
    page.get_pixmap(dpi=110).save(f"/var/tmp/manual-page-{i+1:03}.png")
print(len(doc), "pages")
EOF
```
Inspect for overflow, clipped images, orphan headings, split tables, wrong running feet.

- [ ] **Step 4: Accuracy table**

For each chapter, list its factual claims and the source (`README.md` line, doc heading, source file). Fix any contradiction; report the fixed ones to the owner.

- [ ] **Step 5: Copy-edit pass**

Walk each numbered step: does it name the dialog that appears? Are UI labels exact? Are platform keys split? Any "TBD", placeholder or duplicated sentence removed.

- [ ] **Step 6: Commit**

```bash
git add docs/manual
git commit -m "docs: verify the manual build"
```

### Task 15: Repository integration and handover

**Files:**
- Modify: `AGENTS.md` (one line under Feature Specifications)
- Create/modify: `docs/manual/README.md`

**Interfaces:**
- Consumes: everything above.

- [ ] **Step 1: Write `docs/manual/README.md`** with the two commands (capture plates, compile PDF) and the verification command.

- [ ] **Step 2: Add the `AGENTS.md` line**

"User manual (English, PDF): built with Typst from `docs/manual/`, screenshots generated by `tests/TestManualShots.cpp`; rebuild with `typst compile --font-path resources/fonts --root . docs/manual/manual.typ docs/manual/sonicator-manual.pdf`."

- [ ] **Step 3: Update the graph and check the tree**

```sh
graphify update .
git status --short
```
Expected: only `docs/manual`, `tests/TestManualShots.cpp`, `tests/TestManualClaims.cpp`, `tests/CMakeLists.txt`, `AGENTS.md` and the plan/spec.

- [ ] **Step 4: Final commit and handover**

```bash
git add AGENTS.md docs/manual tests
git commit -m "docs: add the SONICATOR user manual"
```

Give the owner the PDF path, the page count, the verification output and the list of calls made in the final pass.

---

## Self-review

**Spec coverage:** Part structure, visual system, screenshot rules, build and verification, deliverables, risks — all mapped to Tasks 1–15. The five Review Focus items have a test (Task 5), a command (Task 13), or a named checklist step (Tasks 7–9, 14).

**Placeholders:** no "TBD" or "handle edge cases"; every step names a file, a command or exact copy. Two implementation details are deliberately discovered at execution time and flagged: the `PreflightResult` field name (Task 3) and the tag-management surface (Task 3, Step 4) — both have a stated fallback.

**Type consistency:** helper names `manual::capture`, `manual::seedLibrary` are defined once and reused; asset names are used identically in Tasks 2–12 and the chapters.
