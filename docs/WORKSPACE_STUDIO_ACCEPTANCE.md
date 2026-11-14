# Workspace in the Studio layer — acceptance report

Date: 2026-11-14. Branch: `stage-workspace-studio` (based on `fa49d44`).
Spec: `docs/superpowers/specs/2026-11-14-workspace-studio-design.md`; mockup:
`docs/superpowers/specs/assets/2026-11-14-workspace-studio-mockup.html`.

## What shipped

- **Primitives** (`ui/shell/StudioControls.*`): `IconButton`, `Chip`, `CheckChip`, `StatTile`,
  `SectionCaption`, plus `studio::setIcon`/`setCircle` for controls that keep their JUCE type.
  They all derive from `juce::TextButton`/`ToggleButton`, so the button text stays the accessible
  name and every test that finds a control by its text keeps working. `ActionIcons` gained
  `chevronLeft` and `more`.
- **Header**: 72 px on the 4/8/12 grid; the wordmark column is measured from a token; search has
  the magnifier inside the field and the clear control only while there is text; Settings is a
  30 px text button on the right (the gear icon of the first pass was replaced at the owner's
  request — the word is easier to find than the symbol).
- **Toolbar and filters**: `LIBRARY` is a chip with a chevron; page controls are 32×32 icon
  buttons; `Tags`, `Starred`, `Clear filters` are chips and `Hide empty` is a check chip with a
  visible tick. Chip widths are measured from their text (minimum 64 px), so the row fits an
  880 px window — with the previous fixed 64 + 76 + 186 + 92 the row was 436 px wide in a 416 px
  pane and clipped `Clear filters`.
- **Table**: 36 px rows, header font without letter tracking (so `FORMAT` fits its column), and a
  2 px accent stripe on the selected row. Columns, widths, sorting and cells are unchanged.
- **Analysis**: the padded-spaces `Label` is now `AnalysisCard`, a grid of value + caption tiles
  (two columns from 210 px), a meta line and notes that both wrap with their own `TextLayout` and
  are drawn in the muted token; the card owns the path tooltip, and tiles and notes never take the
  mouse. Content and height are set together, so a batch selection from launch no longer clips. Genre
  keeps its source next to the value; duplicates, missing files, errors, disc-image lines and the
  batch-selection note are all still shown. A restored session now fills the card as well.
- **Preview**: 36 px accent Play disc whose glyph follows the transport state, icon Stop, a time
  block with the format line under it, chip toggles, and the selection row under the wave. The
  whole column fits the 132 px of content at the default 156 px panel height, without changing the
  saved panel sizes.
- **Dialogs**: `ui/shell/DialogSurface` (panel, 20 px margins, title/footer areas, control, slider
  and check rows) now lays out Split, Extract, AutoTag and Tap tempo. `AlertWindow` uses the same
  dialog radius. Sliders keep their 90 px value box and check rows keep the tick. Text fields sit
  on the window background so their edge shows. The tap-tempo idle value is an em dash stored as a
  code point, not a mojibake literal.

## Verification actually run

| Check | Result |
|---|---|
| `ctest --preset debug` | 1/1 passed, 33.3 s |
| Full suite (`SonicatorTests`) | 1069 cases / 14 648 assertions passed |
| `cmake --build --preset release --target Sonicator` | `SONICATOR.app` linked and ad-hoc signed |
| Screenshots from real components | 14 PNGs, both themes, `1400x900` and `1120x720`, plus the Split dialog |
| Real application at 100% | Launched the Release build: header, chips, table, analysis, preview render as the components do |
| Real application at 125% | Set Interface scale to 125%, relaunched, captured: nothing clipped, table columns still complete, then restored the saved settings |
| `graphify update .` | graph rebuilt (13 415 nodes) |

Screenshot command:

```bash
SONICATOR_SCREENSHOT_DIR=$PWD/build-shots $HOME/Library/Caches/sonicator-build/debug/tests/SonicatorTests \
  "The workspace renders its Studio parts for acceptance"
```

Files: `library-{dark,light}-{1400x900,1120x720}.png`, `analysis-{...}.png`, `preview-{...}.png`,
`dialog-{dark,light}-520x430.png`.

## Not verified

- **Windows**: no machine or CI run in this session.
- **A real DAW host**: the plugin bundle was not rebuilt or driven in this stage; the workspace
  layout is shared, but `MainComponent` inside a host was not looked at.
- The live 100% and 125% captures were taken on the owner's desktop and are deliberately not
  committed; the committed record is the generated set above plus this description.

## Known limits

- Table columns still stretch to fit and truncate long file names; that is the existing behaviour
  and the stage did not change columns.
- With nothing selected the preview's selection row is empty (Clear and Drag appear with a
  selection).
- Pre-existing: `?`/`??` smart-metadata markers, and the Debug-build assertions from non-ASCII
  literals in `MainComponent.cpp`/`PreviewPanel.cpp`.
- The Analysis card's place inside the panel is a fixed 14 px scrollbar allowance; at very large
  interface scales the viewport scrolls rather than shrinking the tiles.
- Deferred polish from the branch review: `LIBRARY` is still a 40 px pill rather than a 30 px
  chip, the gear has no tooltip, dialog fields rely on the LookAndFeel's own editor colours, the
  bank/tracker selector is 18 px high, and `SectionCaption`/`ActionIcon::more` are unused.

## Follow-up after the first review (same branch)

- **Analysis hero**: Key and BPM are shown large (26 px values, 11 px captions) in the free space
  above SMART METADATA, and hide themselves while the smart metadata is still unknown (`?`, `??`,
  `n/a`, `--`) instead of showing markers at 26 px.
- **Tags**: analysis chips grew to 26 px with a 12.5 px label and a 9 px dot.
- **Star click**: a left click on the star column toggles the favorite in place (`onStarClicked`),
  and the row drag no longer starts from that column.
- **Region edges**: dragging a selection edge resizes the region whether or not the Loop chip is
  armed (moving the whole region still needs the armed loop, because a plain drag inside is the DAW
  file drag). The resize cursor appears on the edges in both cases.
