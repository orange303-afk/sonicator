# SONICATOR user manual — design

Date: 2026-11-17. Status: approved-for-planning pending owner review.
Target: the current `main` (manual states the version and commit it was built from).

## Intent

Write a detailed, task-oriented manual in English for a musician or producer who wants to bring a
messy sample folder or drive into order, find material quickly, and get it into a DAW. It is not a
developer reference and not a feature list: every chapter ends with something the reader can see
in the application.

Why now: the application is feature-complete enough to be used by someone who did not build it
(library, tags, smart metadata, waveform editor, cloud map, batch processing, DAW plugin), while
all existing prose is internal — `README.md` is a dense technical summary and `docs/` records
stages. A new user has nothing to read.

## Audience

- Primary: a first-time user with 1,000–400,000 samples, on macOS or Windows.
- Secondary: an existing user looking up one function ("how do I move my catalog to a new Mac?").

Assumed knowledge: can install an application and drag files into a DAW. No audio-engineering
theory, no command line, no git.

## Goals

1. A reader who follows Part I–II ends with an indexed, tagged, searchable library.
2. A reader who follows Part III–V can audition, trim, export, convert and use the plugin without
   reading the source.
3. Every UI label, menu path and key in the manual exists in the application exactly as written.
4. Honest about limits: unverified platforms, metadata that stays unknown, formats that are
   metadata-only, destructive operations that need a confirmation.
5. Print-quality PDF: A4, embedded fonts, working contents and links, no clipped screenshots.

## Non-goals

- No build-from-source, architecture, schema or API documentation (that stays in `docs/`).
- No marketing copy, no "AI" framing, no promises beyond what the current build does.
- No tutorial project that requires the owner's sample library.
- No translated edition; English only. The file layout leaves room for one later.

## Anti-goals

- A PDF that is a concatenation of `README.md` and stage notes.
- Screenshots of a mockup, or a screenshot of the owner's real catalog with personal paths.
- A chapter that says "various options are available" without naming them.
- A manual that describes Windows as verified when only macOS has been run.
- A sentence that says the application never uses the network, when three optional actions do.

## Constraints

- **Content source of truth**: `README.md`, `docs/*.md`, specs in `docs/superpowers/specs/`, and
  the UI code. Where a doc and the code disagree, the code wins and the difference is reported.
- **Language**: British English, matching the repository ("behaviour", "colour", "analysed").
  UI labels keep their exact casing and are written in **bold**; file paths, search syntax and
  code use a monospace face.
- **Platform**: the book is dual-platform. Keys are spelled as words in key caps — **Cmd**,
  **Shift**, **Opt**, **Ctrl** — because Manrope has no `⌘`, `⇧` or `⌥` glyphs and no fallback
  font is installed. Where the platforms differ the text reads "Cmd+S (macOS) / Ctrl+S
  (Windows)"; Appendix A has one column per platform. Screenshots are macOS; "How to read this
  manual" says so once. Anything not verified on Windows is marked as such where it matters.
- **Layout tool**: Typst 0.15, already installed, with the project's Manrope files in
  `resources/fonts`. Pandoc, LaTeX, WeasyPrint and Poppler are not installed, and ReportLab would
  mean hand-placing every line.
- **Screenshots**: rendered offscreen from the real JUCE components, never drawn by hand.
- **Repository**: sources, assets and the built PDF live in `docs/manual/` and are committed.

## Approaches considered

1. **Typst (chosen).** One text source, a real type system, page references, running heads, PDF
   outline and links, images, and a fast local build. The project already ships Manrope. Can be
   diffed and reviewed like code.
2. **HTML + headless Chromium print.** Good CSS control, but no installed renderer, no page-number
   cross-references without extra tooling, and the output is harder to keep consistent across
   machines.
3. **LaTeX.** Best print typography, but no TeX installation, a heavy toolchain for contributors,
   and slow iteration.
4. **Python ReportLab.** Precise but imperative: every heading, table and page break is code, and
   the document becomes unmaintainable at 60+ pages.

Typst is the only option that is already present, keeps the source reviewable, and gives page
references and a PDF outline without extra tools.

## Structure

Front matter: cover; colophon (version, commit, licence, build command); "How to read this
manual"; table of contents.

### Part I — Getting started

1. **What SONICATOR is.** A local browser over a catalog that indexes files; originals are never
   moved, renamed or deleted by browsing; no account is needed, and indexing, search and playback
   work offline. Three optional network actions exist and are stated up front rather than hidden:
   the update check, the opt-in anonymous contribution to the classifier dataset, and REPORT,
   which opens a pre-filled GitHub issue in the browser. The catalog is a database, not a copy of
   the audio. What the application deliberately does not do.
2. **Installing and first launch.** The artefacts in `dist/` as they exist: the macOS universal
   `.pkg` and the Windows x64 `.msi`, both unsigned and not notarised, with the Gatekeeper and
   SmartScreen steps a first-time user will actually meet, and the `.sha256` check; first window;
   theme and interface scale; where the catalog, settings, logs and exports live; adding the
   first folder.
3. **The workspace.** Header and search; LIBRARY toolbar; folder tree and Favorites; table;
   Analysis card; Preview band; List / Cloud switch; selection bar; resizing panels.

### Part II — Building the library

4. **Folders and scanning.** Add folder, registered roots, nested folders, Pause / Resume /
   Cancel, Rescan, New files, Reanalyze all, Analyze selected, missing roots and error rows,
   Remove from library; Reason Packs and Ableton Packs registration.
5. **The library table.** Columns, whole-catalog sorting, page sizes including All, Hide empty,
   duplicates, missing files, decode errors, multi-selection, the row menu.
6. **Search.** Words and all-token behaviour, masks, `ext:`, `type:`, `key:`, `note:`,
   alternation, Enter/Escape, the ten remembered searches.
7. **Tags.** Creating, renaming, recolouring, deleting; assigning to a selection; recent tags;
   adding the last tag; Tag folder; tag filter; Starred; Reset tags; favorite folders.
8. **AutoTag.** Rule-based suggestions from filenames and stored metadata; the class table;
   review, exclusion and application; what it refuses to guess.
9. **Metadata and corrections.** Type, BPM, Key, Genre and Structure; provenance; manual
   overrides; Tap BPM; the learned prototype; why some values stay unknown.

### Part III — Listening and shaping

10. **Preview.** Auto Preview, transport, seek keys, Loop, Skip Silence, volume, MIDI Preview.
11. **Selection and dragging.** Waveform selection, editing the range while it loops, the prepared
    WAV, dragging rows versus dragging a selection, the exports folder and why it grows.
12. **The waveform editor.** Undo/redo, all edit commands and their keys, zoom, the unsaved-changes
    prompt, Overwrite versus Save copy, what happens to tags and analysis.
13. **Split, Extract and Normalize.** Split by silence; extracting embedded samples from
    containers and images; normalising a selection of library rows.
14. **The cloud map.** What is drawn, hover audition, zoom/pan/Back/Overview, dragging a point,
    why the list filter does not apply, and which files have no position.

### Part IV — Special sources

15. **REX2 and SoundFonts.** RX2 slices, Gate and Loop; SF2 presets, samples and the kit
    sampler.
16. **Instrument files.** EXS24, NKI and SFZ: zones, key and velocity ranges, root key, missing
    samples, and what the bounded readers deliberately ignore.
17. **Tracker modules and XI.** MOD / XM / S3M songs and FastTracker II instruments: Whole song
    versus a source sample, chromatic MIDI audition, export, and the unsupported variants.
18. **Legacy sampler libraries.** E-mu Emulator X `.ebl` banks, Kurzweil `.krz` / `.k25` / `.k26`
    banks, Akai volumes and Creative `.voc` files: what is readable, what is metadata-only, and
    what is never decoded.
19. **Disc images.** ISO / IMG / BIN / CUE, synthetic paths, the image browser, Ignore, what
    happens when an image changes or disappears.

### Part V — Into the DAW

20. **Dragging and exporting.** File URLs versus copies, selection WAVs, formats and rates,
    keeping a DAW project's references alive.
21. **Batch Processing.** Entry points, the operation chain, presets, outputs and formats, naming
    and conflict policies, Split inside a chain, preview, safety, the report, and the limits of
    v1.
22. **The SONICATOR plugin.** Shared catalog and settings; read-only behaviour inside the host;
    project state and window size; disabled scanning and processing; Match BPM and Match Scale;
    host acceptance notes.

### Part VI — Keeping it healthy

23. **Settings.** General (theme, scale, language, workspace, catalog), Audio/MIDI, Hotkeys,
    About; backup; Move / Open / Relocate; switching catalogs; the update check; the
    **Contribute anonymous feedback** toggle — off by default, what a contribution contains and
    what it never contains; REPORT.
24. **Hotkeys and layout.** Rebinding, conflicts and Replace, what keeps its own keys, resetting
    commands, panel dividers and reset.
25. **When something goes wrong.** The log and `crash.txt`; REPORT and what it sends; common
    situations (missing roots, silent files, decode errors, unsupported containers); backup and
    restore; privacy.

### Appendices

- A. Default keyboard shortcuts.
- B. Search syntax cheat sheet.
- C. Formats at a glance: indexed / previewed / metadata-only / not indexed.
- D. Files and folders SONICATOR writes.
- E. Glossary.

Each chapter opens with "In this chapter" (three to five bullets), states the outcome at the end
("You now have…"), and cross-references the next relevant chapter. Warnings appear where an
action is destructive or irreversible.

**Length target: 70–95 pages**, including front matter and appendices. The draft is reviewed in
three checkpoints — Part I, Part II, then the rest — so a wrong voice is caught before 200 pages
of prose exist.

## Visual design

- **Page**: A4, margins 22 mm top / 24 mm bottom / 22 mm inner / 26 mm outer; single column,
  148 mm measure; body Manrope Regular 10.5 pt, leading 0.70 em; colour `#1a1b1d` on white.
- **Type scale**: part divider 40 pt; chapter number 64 pt at 18% accent; H1 26 pt SemiBold;
  H2 15 pt SemiBold; H3 11 pt SemiBold caps with letter spacing; caption 8.5 pt muted.
- **Palette**: accent `#c2611c` (light-theme accent, prints well), dark plate `#17181a`, muted
  `#63666a`, rule `#d4d7db`, tint `#f3f4f6`. The orange is used for chapter numbers, rules,
  links, and the tip callout only — never as a text colour on tint.
- **Running feet**: page number on the outer edge, "SONICATOR · User manual" on the inner edge,
  hairline above; suppressed on part dividers and the cover.
- **Chapter opener**: number on the left in accent tint, title, a two-line lead, and the
  "In this chapter" list; the body starts on the same page to keep the book compact.
- **Callouts**: Note (grey rule), Tip (accent rule), Warning (red rule), Shortcut (keys as small
  rounded boxes). Tinted background at 4%, left rule 2 pt.
- **Screenshots**: placed at the full measure or a tight crop; 1 px `#d4d7db` border; captions
  numbered per chapter ("Figure 7.2 — The tag filter…"). No arrows or numbers drawn over the UI
  unless a caption cannot refer to a visible label.
- **Diagrams**: two simple Typst drawings only — "files on disk ↔ catalog database ↔ views" and
  "the batch chain". No stock art, no icons beyond the project's own branding.
- **Cover**: dark plate, the glossy silver app icon at 38 mm, wordmark, title "User Manual",
  subtitle "From a folder of samples to a working library", version and commit line, thin accent
  rule. Back cover: one-paragraph summary, licence, repository address.

## Screenshots

All shots come from real components in a headless render. A new test file
`tests/TestManualShots.cpp`, tagged `[.manual-shots]` and driven by
`SONICATOR_MANUAL_SHOT_DIR`, seeds a temporary catalog with about 40 synthetic WAV files that
have plausible names (`Kick_808_Am_120.wav`, `Pad_Warm_Cmin.wav`, `Snare_Tight_01.wav`), a folder
structure, tags, stars and metadata overrides. Nothing from the owner's library is used.

Planned plates:

| # | Chapter | Shot |
|---|---|---|
| 1 | 3 | Workspace, dark, 1400×900, populated list |
| 2 | 3 | Folder tree and Favorites, cropped |
| 3 | 5 | Table with duplicates, Hide empty and the selection bar |
| 4 | 6 | Search with `type:kick key:Am` and results |
| 5 | 7 | Tag submenu and tag filter |
| 6 | 8 | AutoTag review dialog |
| 7 | 9 | Analysis card with Key/BPM hero |
| 8 | 10 | Preview transport, loop and selection |
| 9 | 11 | Waveform selection with the "Ready — drag" state |
| 10 | 12 | Waveform editor context menu |
| 11 | 13 | Split by silence dialog |
| 12 | 14 | Cloud map, overview and one zoomed plate |
| 13 | 21 | Batch Processing dialog with a chain and outputs |
| 14 | 22 | The plugin window rendered from `PluginEditor` (no invented host chrome) |
| 15 | 23 | Settings General and Hotkeys |

If a dialog cannot be seeded headlessly without private state, that chapter uses a workspace crop
instead; the manual never ships a fabricated image. Light-theme variants are used only for the
cover-adjacent parts and where the dark shot hides a state.

**Legibility rule.** A screenshot is never scaled below 70% of its rendered size. Detail plates
are tight crops placed at 100%, so UI text prints at 7–9 pt. Four overview plates — the workspace
map, the cloud, the batch dialog and the plugin — are rendered at 2× scale and placed on a
landscape A4 page 257 mm wide, which keeps UI text at about 7 pt. A full-window shot is never
shrunk into the 148 mm text measure; that would print labels at 1.5 pt.

## Deliverables

```text
docs/manual/
  sonicator-manual.pdf        built PDF, committed
  manual.typ                  front matter, part dividers, includes
  style.typ                   page, type, callouts, figures, tables
  chapters/01-what-it-is.typ  … 23-when-something-goes-wrong.typ
  appendices/*.typ
  assets/*.png                generated screenshots
  README.md                   how to rebuild, how to regenerate shots
```

`AGENTS.md` gains one line under feature specifications pointing at the manual and its source
command. The manual states the application version (`0.1.0`) and the commit it documents.

## Build and verification

Build:

```sh
typst compile --font-path resources/fonts --root . \
  docs/manual/manual.typ docs/manual/sonicator-manual.pdf
```

Verification, all required before the work is called done:

1. **Clean compile**: no Typst warnings, no missing font or glyph warnings.
2. **Document sanity** with PyMuPDF: page count, PDF outline present with all chapters, internal
   and external links working, fonts embedded, no `□` replacement glyphs in extracted text.
3. **Visual pass**: render every page at 110 dpi and inspect for overflow, clipped screenshots,
   orphan headings, a figure separated from its caption, and a table split badly across pages.
4. **Accuracy pass**: a claim table mapping each chapter's factual statements to the README,
   a `docs/` file or a source file; any contradiction is fixed in the manual and reported.
5. **Language pass**: consistency of UI labels, British spelling, no leftover notes or TODOs.
6. **Screenshot provenance**: every PNG in `assets/` is regenerated by the documented command;
   no image is cropped from a mockup or from the owner's catalog.
7. **Repository**: `graphify update .` after the test file is added; `git status` shows only the
   manual, the test and the `AGENTS.md` line.

## Risks and mitigations

- **Screenshot seeding is the long pole.** Mitigation: build the screenshot test first, before
  chapter prose; if a dialog resists a headless seed, drop that plate rather than fake it.
- **The application changes while the manual is written.** Mitigation: write against one commit,
  state it on the colophon; verification re-reads the UI strings from the source at the end.
- **Feature drift between README and code.** Mitigation: accuracy pass with the claim table; code
  wins.
- **Typst glyph coverage** for `⌘`, `⇧`, arrows and box drawing. Mitigation: Manrope with DejaVu
  Sans fallback, checked in the extracted text and the visual pass.
- **Manual becomes a second README.** Mitigation: the chapter template (outcome, steps, result)
  and the anti-goals above.

## Left to the builder

- The exact wording of the prose, examples and tips.
- The crop of each screenshot and whether a plate needs a light-theme variant.
- Typst component names and file split, as long as the layout rules above hold.
- Whether the appendix tables are generated from `docs/COMMANDS.md` by hand or by a small script.
