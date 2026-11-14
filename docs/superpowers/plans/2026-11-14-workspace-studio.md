# Workspace in the Studio layer — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the main window — header, toolbar, filters, table, Analysis, Preview and dialogs — the same Studio visual layer that Settings and Hotkeys already use, without changing behaviour, data or keyboard paths.

**Architecture:** A small set of Studio primitives (`ui/shell/StudioControls.*`) derives from the existing JUCE button types, so every test that finds a control by its text and every accessibility name keeps working while the LookAndFeel paints icons, pills and ticks from a property. `MainComponent`, `FileListPanel`, `PreviewPanel` and the four dialogs are then rebuilt from those primitives plus the existing `studio::` tokens; the Analysis label becomes a real `AnalysisCard` component that receives ready-formatted values.

**Tech Stack:** C++20, JUCE 9.0.5, Catch2 3.7.1, Ninja + CMake presets.

**Spec:** `docs/superpowers/specs/2026-11-14-workspace-studio-design.md` (approved; mockup next to it in `docs/superpowers/specs/assets/2026-11-14-workspace-studio-mockup.html`).

## Global Constraints

- Only new or changed numbers come from `studio::` (`ui/shell/DesignTokens.h`); pre-existing literals in untouched code stay.
- Colours come from `theme::` only. The accent stays `theme::accent`; no teal anywhere in the app.
- Every restyled control keeps its `getButtonText()` and its `getToggleState()` semantics: tests look controls up by text, and Play's text is the transport state indicator.
- No behaviour change: same actions, same menu items, same settings persistence, same columns, sort order, filters, keyboard bindings and session state.
- The preview waveform (`WaveformDisplay`) is not touched.
- Never read a file on the preview timer; the format line is filled only where the file is already opened.
- Tests use temporary catalogs and settings files only (`TestDirectory`, `AppSettings::setTestSettingsDirectory`).
- Build and run: `cmake --build --preset debug --target SonicatorTests` then `$HOME/Library/Caches/sonicator-build/debug/SonicatorTests "<tags>"`; full suite `ctest --preset debug`.
- Commits follow the existing style: `feat(ui):`, `fix(ui):`, `docs:`.

## Review Focus

1. A catalog entry with a long file name or a long path: the Analysis card must wrap the notes and the meta line instead of clipping, and the path must stay available as the tooltip.
2. A batch selection of thousands of rows: only a note is shown, no stale statistics, and the table keeps one accent stripe on the selected block's first row rather than none.
3. The minimum window (880×580) and 125% scale: chips, page controls and the search field must not clip, and the filter row still shows at least Tags, Starred and Hide empty.
4. Missing, errored and ignored samples: the card keeps `File is missing`, the error text, `Inside image …` and the duplicate-copy lines that the old label printed.
5. A disc image, a SoundFont and a tracker module in Preview: the format line shows what the panel already knows, the transport still ends with Play/Pause text in the right state, and no file is opened from the timer.
6. Light theme: the accent Play disc keeps a readable glyph, and every icon button keeps a visible keyboard-focus ring.

---

### Task 1: Studio primitives

**Files:**
- Modify: `ui/shell/DesignTokens.h`
- Modify: `ui/shell/ActionIcons.h`, `ui/shell/ActionIcons.cpp`
- Modify: `ui/shell/Theme.h`
- Create: `ui/shell/StudioControls.h`, `ui/shell/StudioControls.cpp`
- Test: `tests/TestStudioControls.cpp` (create), `tests/TestStudioTheme.cpp` (extend icon loop)
- Modify: `CMakeLists.txt:137` (add `ui/shell/StudioControls.cpp` to `SonicatorUI`)
- Modify: `tests/CMakeLists.txt` (register both test files)

**Interfaces:**
- Consumes: `ActionIcon`, `drawActionIcon`, `studio::` tokens, `theme::`.
- Produces (exact, later tasks rely on these):

```cpp
namespace sonicator::studio {
inline constexpr int iconButtonSize = 32;
inline constexpr int circleButtonSize = 36;
inline constexpr int chipHeight = 30;
inline constexpr int chipSmallHeight = 26;
inline constexpr int chipRadius = 15;
inline constexpr int headerHeight = 72;
inline constexpr int brandingWidth = 298;
inline constexpr int tableRowHeight = 36;
juce::Font statValueFont();    // 17 bold
juce::Font statCaptionFont();  // studio::captionFont()

class IconButton final : public juce::TextButton {
public:
    IconButton(ActionIcon, const juce::String& accessibleName);
    ActionIcon icon() const noexcept;
    void setIcon(ActionIcon);
};
class Chip final : public juce::TextButton {
public:
    explicit Chip(const juce::String& text, bool small = false);
    void setTrailingIcon(ActionIcon);   // e.g. chevronDown on LIBRARY
};
class CheckChip final : public juce::ToggleButton {
public:
    explicit CheckChip(const juce::String& text, bool small = false);
};
struct Stat { juce::String value, caption; };
class StatTile final : public juce::Component {
public:
    void setStat(const Stat&);
    const Stat& stat() const noexcept;
    static int preferredHeight();
    void paint(juce::Graphics&) override;
};
class SectionCaption final : public juce::Label {
public:
    explicit SectionCaption(const juce::String& text);
};

// For controls that keep their own JUCE type (the preview's Play is a plain TextButton because its
// text is the transport state): set the same properties the primitives set.
void setIcon(juce::Button&, ActionIcon);
void setCircle(juce::Button&, bool round);
}
```

Button properties the LookAndFeel reads: `studioIcon` (int `ActionIcon`), `studioTrailingIcon` (int), `studioCircle` (bool), `studioChip` (int: 1 normal, 2 small), `studioCheckChip` (bool).

- [ ] **Step 1: Write the failing tests** in `tests/TestStudioControls.cpp`

```cpp
TEST_CASE("Icon buttons draw their icon instead of their text", "[ui][studio]") {
    SonicatorLookAndFeel look;
    IconButton a(ActionIcon::settings, "Settings"), b(ActionIcon::settings, "Play");
    a.setLookAndFeel(&look); b.setLookAndFeel(&look);
    a.setSize(32, 32); b.setSize(32, 32);
    REQUIRE(a.getButtonText() == "Settings");
    REQUIRE(a.icon() == ActionIcon::settings);
    REQUIRE(a.getProperties()["studioIcon"] == (int) ActionIcon::settings);
    REQUIRE(a.createComponentSnapshot(a.getLocalBounds())
            == b.createComponentSnapshot(b.getLocalBounds()));
}
TEST_CASE("Chips and check chips toggle and keep their labels", "[ui][studio]") {
    Chip c("Starred"); REQUIRE(c.getButtonText() == "Starred");
    REQUIRE((int) c.getProperties()["studioChip"] == 1);
    c.triggerClick(); REQUIRE(c.getToggleState());
    CheckChip ch("Hide empty"); REQUIRE(ch.getButtonText() == "Hide empty");
    REQUIRE((bool) ch.getProperties()["studioCheckChip"]);
    ch.triggerClick(); REQUIRE(ch.getToggleState());
}
TEST_CASE("Stat tiles carry a value and a caption", "[ui][studio]") {
    StatTile t; t.setStat({ "2.184 s", "Length" });
    REQUIRE(t.stat().value == "2.184 s");
    REQUIRE(t.stat().caption == "Length");
    REQUIRE(StatTile::preferredHeight() >= 24);
}
```

Extend the icon array in `tests/TestStudioTheme.cpp` from 13 to 15 entries with `chevronLeft` and `more`.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build --preset debug --target SonicatorTests`
Expected: compile error, `StudioControls.h` not found.

- [ ] **Step 3: Implement the tokens and icons**

Add the constants and two fonts to `DesignTokens.h`; add `chevronLeft` (mirror of `chevronRight`) and `more` (three 0.09-radius filled dots at y 0.28/0.5/0.72) to `ActionIcons`. `StatTile::preferredHeight()` returns `studio::spacing + (int) statValueFont().getHeight() + (int) statCaptionFont().getHeight()`.

- [ ] **Step 4: Implement the primitives**

`IconButton` sets `studioIcon`, the accessible name as button text, `setWantsKeyboardFocus(false)` and `setMouseClickGrabsKeyboardFocus(false)` — the same flags the toolbar buttons use today. `Chip` sets `studioChip` 1 or 2. `CheckChip` sets `studioCheckChip` and uses the same focus flags. `SectionCaption` sets `studio::sectionFont()` and `theme::muted`.

- [ ] **Step 5: Teach the LookAndFeel the three surfaces** in `Theme.h`

`drawButtonBackground`: `studioCircle` → filled ellipse in `theme::accent` (hover `brighter(0.06f)`, down `darker(0.06f)`); `studioChip` → rounded rect radius `chipRadius`, fill `selection` when toggled else `panel`, border `line`; otherwise unchanged.
`drawButtonText`: `studioIcon` → `drawActionIcon` centred at 40% of the short side, no text; `studioChip` → centred `studio::controlFont()` in `text`/`muted`, then the trailing icon 12 px from the right edge when `studioTrailingIcon` is set; otherwise unchanged.
`drawToggleButton`: `studioCheckChip` → the same pill, with a 10 px tick box at the left inset 10 px (same tick path as the current box) and the label after it; otherwise unchanged.

- [ ] **Step 6: Run the suite**

Run: `cmake --build --preset debug --target SonicatorTests && $HOME/Library/Caches/sonicator-build/debug/SonicatorTests "[ui][studio]"`
Expected: all `[studio]` cases pass, including the old token and icon cases.

- [ ] **Step 7: Commit**

```bash
git add ui/shell tests/TestStudioControls.cpp tests/TestStudioTheme.cpp tests/CMakeLists.txt
git commit -m "feat(ui): add Studio icon button, chip and stat tile primitives"
```

---

### Task 2: Header on the 4/8/12 grid

**Files:**
- Modify: `ui/MainComponent.h` (`settings` member type)
- Modify: `ui/MainComponent.cpp:548-609` (construction), `:4990-5011` (paint and header layout)
- Modify: `ui/library/SearchField.h` (leading icon)
- Test: `tests/TestWorkspaceStudio.cpp` (create), `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `studio::IconButton`, `studio::headerHeight`, `studio::brandingWidth`.
- Produces: `SearchField::leadingIcon() -> ActionIcon`; `MainComponent` header height `72`; `settings` becomes `studio::IconButton settings { ActionIcon::settings, "Settings" }`.

- [ ] **Step 1: Write the failing test**

```cpp
// Shared by every case in this file: the shell finds its buttons by text today, and the restyle
// keeps that text as the accessible name.
juce::Button* buttonWithText(juce::Component& parent, const juce::String& text) {
    for (auto* child : parent.getChildren())
        if (auto* button = dynamic_cast<juce::Button*>(child))
            if (button->getButtonText() == text) return button;
    return nullptr;
}
TEST_CASE("The header keeps search and settings on the grid", "[ui][studio]") {
    TestDirectory t;
    MainComponent app(t.dir.getChildFile("header.sqlite"), false);
    app.setSize(880, 580); app.resized();
    auto* search = dynamic_cast<SearchField*>(app.findChildWithID("search"));
    REQUIRE(search != nullptr);
    REQUIRE(search->getHeight() == 32);
    REQUIRE(search->leadingIcon() == ActionIcon::search);
    auto* settings = buttonWithText(app, "Settings");
    REQUIRE(settings != nullptr);
    REQUIRE(settings->getWidth() == 32);
    REQUIRE(settings->getHeight() == 32);
    REQUIRE(settings->getRight() <= app.getWidth() - 8);
    REQUIRE(search->getRight() < settings->getX());
    REQUIRE(search->getWidth() >= 300);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build --preset debug --target SonicatorTests`
Expected: compile error, `leadingIcon` missing.

- [ ] **Step 3: Implement the search leading icon**

`SearchField` gains `ActionIcon leading = ActionIcon::search;`, `leadingIcon()`, and a `paintOverChildren` that strokes the icon at `{spacingTight, centre-7, 14, 14}` in `theme::muted`. `setBorder(juce::BorderSize<int>(0, 26, 0, 30))` and remove the `search.setIndents(10, 0)` call from `MainComponent`, so text starts after the icon.

- [ ] **Step 4: Rebuild the header layout** in `MainComponent::resized`

`headerBounds` height `studio::headerHeight`; header reduced by `studio::spacingWide` horizontally and 14 vertically; branding column `studio::brandingWidth`; `settings` at `removeFromRight(studio::iconButtonSize)` with `withSizeKeepingCentre`; `search` fills the gap with height 32 at the right of the branding column plus `studio::spacing`. `MainComponent::paint` draws the divider at `headerBounds.getBottom()` and the wordmark into the branding column instead of the literal `(16, 8, 310, 70)`.

- [ ] **Step 5: Run both tests**

Run: `cmake --build --preset debug --target SonicatorTests && $HOME/Library/Caches/sonicator-build/debug/SonicatorTests "[ui][studio]" && $HOME/Library/Caches/sonicator-build/debug/SonicatorTests "[ui][shell]"`
Expected: pass; the shell cases that click Settings by text still find it.

- [ ] **Step 6: Commit**

```bash
git add ui/MainComponent.cpp ui/MainComponent.h ui/library/SearchField.h tests/TestWorkspaceStudio.cpp tests/CMakeLists.txt
git commit -m "feat(ui): put the workspace header on the Studio grid"
```

---

### Task 3: Toolbar and filter chips

**Files:**
- Modify: `ui/MainComponent.h:137-144` (member types)
- Modify: `ui/MainComponent.cpp:791-913` (construction and focus flags), `:1599-1607` (`updateTagControls`), `:5068-5078` (layout)
- Test: `tests/TestShell.cpp:911-914` (hide-empty assertions), `tests/TestWorkspaceStudio.cpp` (new case)

**Interfaces:**
- Consumes: `studio::IconButton`, `studio::Chip`, `studio::CheckChip`, `studio::chipHeight`, `studio::chipSmallHeight`.
- Produces: `previousPage`/`nextPage` are `studio::IconButton` with texts `"<"`/`">"` (tests and click paths unchanged); `tagFilter`, `starredButton`, `clearFilters`, `libraryMenu` are `studio::Chip`; `hideEmpty` is `studio::CheckChip { "Hide empty" }` with tooltip `"Hide empty and unsupported"`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("Filter chips keep their state and the shorter hide-empty label", "[ui][studio]") {
    TestDirectory t;
    MainComponent app(t.dir.getChildFile("chips.sqlite"), false);
    app.setSize(880, 580); app.resized();
    auto* hide = dynamic_cast<juce::ToggleButton*>(app.findChildWithID("hideEmpty"));
    REQUIRE(hide != nullptr);
    REQUIRE(hide->getButtonText() == "Hide empty");
    REQUIRE(hide->getTooltip().contains("unsupported"));
    REQUIRE(hide->getWidth() >= 100);
    auto* tags = buttonWithText(app, "Tags");
    REQUIRE(tags != nullptr);
    REQUIRE(tags->getWidth() >= 64);
    auto* next = buttonWithText(app, ">");
    REQUIRE(next != nullptr);
    REQUIRE(next->getWidth() == studio::iconButtonSize);
}
```

Update `tests/TestShell.cpp:911-914` to `REQUIRE(hide->getButtonText() == "Hide empty");` and `REQUIRE(hide->getWidth() >= 100);`.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build --preset debug --target SonicatorTests && $HOME/Library/Caches/sonicator-build/debug/SonicatorTests "[ui][studio]" "[ui][filters]"`
Expected: `Hide empty and unsupported` vs `Hide empty` fails, 30 vs 32 width fails.

- [ ] **Step 3: Implement**

Replace the member types; construct `libraryMenu` as `Chip("LIBRARY")` with `setTrailingIcon(ActionIcon::chevronDown)`, page buttons as `IconButton(ActionIcon::chevronLeft, "<")` / `(ActionIcon::chevronRight, ">")`, filters as `Chip`, `hideEmpty` as `CheckChip`. Keep every `onClick`, `setToggleState` and focus-flag loop as it is. In `resized()`, measure chip widths from their text with `juce::GlyphArrangement::getStringWidthInt` + `2 * studio::spacing`, keep the vertical separator (`1×20`, `theme::line`, `studio::spacingTight` on both sides) before `clearFilters`, and hide `clearFilters` when `filterBar.getWidth() < 390`.

- [ ] **Step 4: Run**

Run: `cmake --build --preset debug --target SonicatorTests && $HOME/Library/Caches/sonicator-build/debug/SonicatorTests "[ui][studio]" "[ui][filters]" "[ui][shell]"`
Expected: pass.

- [ ] **Step 5: Commit**

```bash
git add ui/MainComponent.cpp ui/MainComponent.h tests/TestShell.cpp tests/TestWorkspaceStudio.cpp
git commit -m "feat(ui): restyle the library toolbar and filters as Studio chips"
```

---

### Task 4: Table typography and selection stripe

**Files:**
- Modify: `ui/library/FileListPanel.cpp:114` (row height), `:472-482` (row background)
- Modify: `ui/shell/Theme.h:372-403` (table header)
- Test: `tests/TestWorkspaceStudio.cpp` (new case)

**Interfaces:**
- Consumes: `studio::tableRowHeight`, `studio::sectionFont()`.
- Produces: `FileListPanel` row height 36; selected rows carry a 2 px `theme::accent` stripe at the left edge; header text is `studio::sectionFont()` with `theme::muted`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("A selected library row carries the accent stripe", "[ui][studio]") {
    TestDirectory t;
    Database db(t.dir.getChildFile("rows.sqlite"));
    SampleRepository repo(db); repo.createTables();
    for (int i = 0; i < 4; ++i) { auto r = BasicAnalyzer().analyze(t.wave("s" + juce::String(i) + ".wav",
        (double) (200 + i * 40))); r.fileName = "s" + juce::String(i) + ".wav"; repo.upsert(r); }
    FileListPanel panel; panel.setSamples(repo.all());
    panel.setSize(700, 400); panel.resized();
    auto* table = dynamic_cast<juce::TableListBox*>(panel.getChildComponent(0));
    REQUIRE(table != nullptr);
    REQUIRE(table->getRowHeight() == studio::tableRowHeight);
    table->selectRow(0, false);
    const auto image = panel.createComponentSnapshot(panel.getLocalBounds());
    const int y = table->getHeaderHeight() + table->getRowHeight() / 2;
    // The stripe is a filled 2 px rectangle on the row's left edge, so both its pixels are exact.
    REQUIRE(image.getPixelAt(table->getX(), y) == theme::accent);
    REQUIRE(image.getPixelAt(table->getX() + 1, y) == theme::accent);
    REQUIRE(image.getPixelAt(table->getX() + 20, y) != theme::accent);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build --preset debug --target SonicatorTests && $HOME/Library/Caches/sonicator-build/debug/SonicatorTests "[ui][studio]"`
Expected: row height 34 vs 36, stripe is `selection`.

- [ ] **Step 3: Implement**

`table.setRowHeight(studio::tableRowHeight)`. In `paintRowBackground`, after the existing fill, when `selected` fill `juce::Rectangle<int>(0, 0, 2, height)` with `theme::accent`. In `drawTableHeaderColumn`, replace the literal font with `studio::sectionFont()` and keep the sort arrow.

- [ ] **Step 4: Run**

Run: `cmake --build --preset debug --target SonicatorTests && $HOME/Library/Caches/sonicator-build/debug/SonicatorTests "[ui][studio]" "[library]"`
Expected: pass, including `TestLibraryDrag` which reads the row height from the table.

- [ ] **Step 5: Commit**

```bash
git add ui/library/FileListPanel.cpp ui/shell/Theme.h tests/TestWorkspaceStudio.cpp
git commit -m "feat(ui): give the library table Studio typography and a selection stripe"
```

---

### Task 5: Analysis card

**Files:**
- Create: `ui/analysis/AnalysisCard.h`, `ui/analysis/AnalysisCard.cpp`
- Modify: `ui/MainComponent.h:145` (`analysisDetails` member), `ui/MainComponent.cpp:564-569`, `:1901-1904`, `:1914-1959`, `:1999-2001`, `:4951`, `:5066-5067`
- Modify: `CMakeLists.txt:137` (add `ui/analysis/AnalysisCard.cpp` to the explicit `SonicatorUI` source list)
- Test: `tests/TestWorkspaceStudio.cpp` (new cases)

**Interfaces:**
- Consumes: `studio::bodyFont`, `studio::captionFont`, `studio::statValueFont`, `StatTile`.
- Produces:

```cpp
namespace sonicator {
struct AnalysisContent {
    juce::String title, meta, tooltip;
    std::vector<studio::Stat> stats;
    std::vector<juce::String> notes;
};
class AnalysisCard final : public juce::Component {
public:
    void setContent(const AnalysisContent&);
    void showPlaceholder(const juce::String&);
    const AnalysisContent& content() const noexcept;
    int preferredHeight(int width) const;
    static int columnsFor(int width);   // 2 when width >= 240, else 1
    void resized() override;
    void paint(juce::Graphics&) override;
};
}
```

`MainComponent` keeps the componentID `analysisDetails` on the card so the existing `findChildWithID("analysisDetails")` lookups still resolve.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("The analysis card shows one tile per statistic", "[ui][studio]") {
    AnalysisCard card;
    AnalysisContent c;
    c.title = "FINE_CRASH_01.wav";
    c.meta = "WAV · 2 ch · 48 kHz";
    c.stats = { { "2.184 s", "Length" }, { "48.0 kHz", "Rate" }, { "-0.3 dBFS", "Peak" } };
    c.notes = { "Shown copy is not the catalog representative." };
    card.setContent(c);
    card.setSize(270, 400); card.resized();
    REQUIRE(card.content().stats.size() == 3);
    REQUIRE(card.getNumChildComponents() == 6);   // title, meta, three tiles, one note
    REQUIRE(AnalysisCard::columnsFor(270) == 2);
    REQUIRE(AnalysisCard::columnsFor(220) == 1);
    REQUIRE(card.preferredHeight(270) > card.preferredHeight(220) - 1); // two columns are shorter
}
TEST_CASE("The analysis card keeps every note the old label printed", "[ui][studio]") {
    AnalysisCard card;
    AnalysisContent c; c.title = "x.wav"; c.notes = { "File is missing", "Inside image disc.iso" };
    card.setContent(c);
    card.setSize(270, 300); card.resized();
    REQUIRE(card.getNumChildComponents() == 3);   // title and both notes
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build --preset debug --target SonicatorTests`
Expected: compile error, `AnalysisCard.h` not found.

- [ ] **Step 3: Implement the card**

`AnalysisCard` owns a `juce::Label title, meta` and `std::vector<std::unique_ptr<StatTile>>` plus `std::vector<std::unique_ptr<juce::Label>>` notes, rebuilt in `setContent`. `resized()` lays the title (20 px), the meta (16 px), then a grid of `columnsFor(width)` columns with `StatTile::preferredHeight()` rows and `studio::spacingTight` gaps, then the notes at `studio::captionFont()` with wrapping height from `juce::TextLayout`. `paint` fills nothing (the panel behind it is the panel) and sets the tooltip from `content().tooltip`.

- [ ] **Step 4: Move the formatting out of `loadSelectedSample`**

Build an `AnalysisContent` where the old string is built today: title = `r.fileName`; meta from format, channels, rate, bit depth; stats Length/Rate/Bit depth/Peak/RMS/Centroid/Rolloff/Flatness plus Genre (value `"Tech House (automatic)"`, caption `"Genre"`) and Duplicates (`"×2 copies"`) when present; notes for the disc lines, `File is missing`, `r.error`, the duplicate representative sentence and the copy paths. Multi-selection becomes a card with no stats and one note (the existing sentence). Placeholders use `showPlaceholder("Select a sample to inspect its measured audio features.")`.

- [ ] **Step 5: Size it**

In `resized()`, `analysisCard.setBounds(0, 0, availableWidth, analysisCard.preferredHeight(availableWidth))` and keep the viewport, so tall content scrolls instead of using the fixed 550.

- [ ] **Step 6: Run**

Run: `cmake --build --preset debug --target SonicatorTests && $HOME/Library/Caches/sonicator-build/debug/SonicatorTests "[ui][studio]" "[ui][selection]"`
Expected: pass.

- [ ] **Step 7: Commit**

```bash
git add ui/analysis ui/MainComponent.cpp ui/MainComponent.h tests/TestWorkspaceStudio.cpp
git commit -m "feat(ui): show analysis values as statistic tiles"
```

---

### Task 6: Preview transport and the format block

**Files:**
- Modify: `ui/preview/PreviewPanel.h:156-201` (members and helpers)
- Modify: `ui/preview/PreviewPanel.cpp:100-120` (construction), `:1190-1255` (paint and layout), timer at `:1150`, `:1192`
- Test: `tests/TestWorkspaceStudio.cpp` (new case)

**Interfaces:**
- Consumes: `studio::IconButton`, `studio::CheckChip`, `studio::setIcon`, `studio::setCircle`, `studio::circleButtonSize`, `studio::chipSmallHeight`.
- Produces: `juce::Label formatLine` (componentID `previewFormat`), `void refreshFormatLine();`; `play` stays a `juce::TextButton` with `studio::setCircle(play, true)` and `studio::setIcon(...)` following its `"Play"`/`"Pause"` text; `stop` becomes `studio::IconButton { ActionIcon::stop, "Stop" }`; the five toggles become `studio::CheckChip` with `small = true`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("Preview shows the format line and fits the 156 px panel", "[ui][studio]") {
    TestDirectory t;
    AudioEngine engine(false);
    PreviewPanel panel(engine, t.dir);
    REQUIRE(panel.loadFile(t.wave("fmt.wav", 440.0, 2), false));
    panel.setSize(1100, 156); panel.resized();
    auto* format = dynamic_cast<juce::Label*>(panel.findChildWithID("previewFormat"));
    REQUIRE(format != nullptr);
    REQUIRE(format->getText().contains("WAV"));
    REQUIRE(format->getText().contains("44.1"));
    REQUIRE(format->getText().contains("Stereo"));
    auto* play = dynamic_cast<juce::TextButton*>([&]{ juce::Button* found = nullptr;
        for (auto* child : panel.getChildren()) if (auto* b = dynamic_cast<juce::Button*>(child))
            if (b->getButtonText() == "Play") found = b; return found; }());
    REQUIRE(play != nullptr);
    REQUIRE(play->getWidth() == studio::circleButtonSize);
    for (auto* child : panel.getChildren())
        REQUIRE(child->getBottom() <= panel.getHeight());
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build --preset debug --target SonicatorTests && $HOME/Library/Caches/sonicator-build/debug/SonicatorTests "[ui][studio]"`
Expected: `previewFormat` is null, `Play` is 78 wide.

- [ ] **Step 3: Implement the format line**

`refreshFormatLine()` sets the label from what the panel already has: ordinary files use the extension from `loadedFile` plus the reader the panel opens in `loadFile` (store `sampleRate`, `channels`, `bits` in three ints at that point); disc, instrument and tracker sources use `SampleAudio::sampleRate/channels` and `"Instrument"`/`"Tracker"` instead of the extension. `clearSource()` sets it to empty and hides the label. No file access from the timer.

- [ ] **Step 4: Rebuild the layout to the budget**

Title row 20; left column: waveform then the 26 px row with `selectionStatus`, `clearSelectionButton`, `sampleDragButton`; right column 300 wide: 30 px time block (`time` 15 px, `formatLine` 12 px under it), 36 px transport (`play` 36 circle, `stop` 32, `volume`), two 26 px chip rows (`Loop`, `Auto Preview` / `MIDI Preview`, `Skip Silence`, with `rexGate` joining the first row when visible). Gaps 4 below 160 px panel height, 8 above. `paint` keeps the current rounded panel fill.

- [ ] **Step 5: Switch the icon with the existing state update**

Where `play.setButtonText(engine.isPlaying() ? "Pause" : "Play")` runs, also call `studio::setIcon(play, engine.isPlaying() ? ActionIcon::pause : ActionIcon::play)`.

- [ ] **Step 6: Run**

Run: `cmake --build --preset debug --target SonicatorTests && $HOME/Library/Caches/sonicator-build/debug/SonicatorTests "[ui][studio]" "[preview]" "[reason]" "[disc]"`
Expected: pass; `TestDiscBrowserService` still finds Play/Pause/Stop by text.

- [ ] **Step 7: Commit**

```bash
git add ui/preview/PreviewPanel.h ui/preview/PreviewPanel.cpp tests/TestWorkspaceStudio.cpp
git commit -m "feat(ui): rebuild the preview transport on Studio controls"
```

---

### Task 7: Dialog surface and Split

**Files:**
- Create: `ui/shell/DialogSurface.h`, `ui/shell/DialogSurface.cpp`
- Modify: `ui/split/SplitDialog.cpp:144-190` (paint and layout)
- Modify: `ui/shell/Theme.h:124-185` (alert fonts, button height, `drawAlertBox`)
- Modify: `CMakeLists.txt:137` (add `ui/shell/DialogSurface.cpp` to `SonicatorUI`)
- Test: `tests/TestWorkspaceStudio.cpp` (new case)

**Interfaces:**
- Consumes: `studio::dialogRadius`, `studio::spacing`, `studio::controlHeight`, `theme::`.
- Produces:

```cpp
namespace sonicator::dialogs {
class Surface {
public:
    Surface();
    // Paints only the panel: every dialog keeps its own title label inside titleArea(), so the
    // existing label text, tooltips and tests stay as they are.
    void paint(juce::Graphics&, juce::Rectangle<int> bounds) const;
    static juce::Rectangle<int> contentArea(juce::Rectangle<int> bounds);   // reduced(20)
    static juce::Rectangle<int> titleArea(juce::Rectangle<int> bounds);     // 24 px of contentArea
    static juce::Rectangle<int> footerArea(juce::Rectangle<int> bounds);    // 32 px at the bottom
    static void placeControlRow(juce::Rectangle<int>& area, juce::Component& label, juce::Component& control);
    static void placeSliderRow(juce::Rectangle<int>& area, juce::Component& label, juce::Slider& slider);
    static void placeCheck(juce::Rectangle<int>& area, juce::Component& toggle); // 24 px row
};
}
```

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("The split dialog keeps its sliders and visible check boxes", "[ui][studio]") {
    TestDirectory t;
    SplitDialog dialog({ t.wave("a.wav") }, AppSettings{}, [](SplitOptions) { return true; });
    dialog.setSize(520, 430); dialog.resized();
    int sliders = 0, checks = 0;
    for (auto* child : dialog.getChildren()) {
        if (auto* slider = dynamic_cast<juce::Slider*>(child)) { ++sliders;
            REQUIRE(slider->getWidth() > 150); REQUIRE(slider->getTextBoxWidth() == 90); }
        if (auto* toggle = dynamic_cast<juce::ToggleButton*>(child)) { ++checks; REQUIRE(toggle->getHeight() == 24); }
        REQUIRE(child->getBounds().getRight() <= dialog.getWidth());
        REQUIRE(child->getBottom() <= dialog.getHeight());
    }
    REQUIRE(sliders == 3); REQUIRE(checks == 3);
    const auto on = dialog.createComponentSnapshot(dialog.getLocalBounds());
    dialog.enterRunningState();
    const auto running = dialog.createComponentSnapshot(dialog.getLocalBounds());
    REQUIRE(on != running);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build --preset debug --target SonicatorTests && $HOME/Library/Caches/sonicator-build/debug/SonicatorTests "[ui][studio]"`
Expected: compile error, `DialogSurface.h` not found.

- [ ] **Step 3: Implement `Surface`**

`paint` fills a `theme::panel` rounded rect radius `studio::dialogRadius` over the bounds and a 1 px `theme::line` border. The dialog's own title label sits in `titleArea()`. `placeControlRow` puts the label in 140 px and the control in the rest; `placeSliderRow` then reserves 90 px on the right for the slider's text box (slider width = rest − 90); `placeCheck` gives a 24 px row. Every gap is `studio::spacingTight`.

- [ ] **Step 4: Move `SplitDialog` onto it**

Replace the local `reduced(18)`/`removeFromTop(26)` layout with `Surface` helpers; keep the widgets, the folder row (`folder` editor + `browse`), the error/progress/result rows and the Split/Cancel → OK logic untouched. `paint` becomes `surface.paint(g, getLocalBounds())`.

- [ ] **Step 5: Alert window on the same tokens**

`getAlertWindowButtonHeight()` returns 32; `drawAlertBox` uses `studio::dialogRadius`, 20 px margins and `studio::spacing`; the icon disc and warning colour stay.

- [ ] **Step 6: Run**

Run: `cmake --build --preset debug --target SonicatorTests && $HOME/Library/Caches/sonicator-build/debug/SonicatorTests "[ui][studio]" "[split]" && ctest --preset debug`
Expected: pass.

- [ ] **Step 7: Commit**

```bash
git add ui/shell/DialogSurface.h ui/shell/DialogSurface.cpp ui/split/SplitDialog.cpp ui/shell/Theme.h tests/TestWorkspaceStudio.cpp
git commit -m "feat(ui): add a shared Studio dialog surface and rebuild Split on it"
```

---

### Task 8: Extract, AutoTag and Tap tempo on the same surface

**Files:**
- Modify: `ui/extract/ExtractDialog.cpp` (paint and `resized`)
- Modify: `ui/tagging/AutoTagDialog.cpp:192-208`
- Modify: `ui/library/TapTempoDialog.cpp` (paint and `resized`)
- Test: `tests/TestWorkspaceStudio.cpp` (new case)

**Interfaces:**
- Consumes: `dialogs::Surface` from Task 7.
- Produces: the three dialogs paint through `Surface::paint` and lay out through `Surface::contentArea/titleArea/footerArea`; their controls and callbacks are unchanged.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("Every dialog lays its content out from the shared surface", "[ui][studio]") {
    TestDirectory t;
    ExtractDialog extract({ juce::File("/tmp/bank.sf2") }, AppSettings{},
                          [](ExtractOptions) { return true; });
    Database db(t.dir.getChildFile("auto.sqlite"));
    SampleRepository repo(db); repo.createTables();
    AutoTagService service(repo);
    AutoTagResult result; result.checked = 1; result.scope = "selected files";
    AutoTagDialog autoTag(std::move(result), service, [] {});
    TapTempoDialog tap("Tap tempo");
    struct Case { juce::Component* dialog; juce::Size size; };
    for (const auto& c : std::array<Case, 3> { Case { &extract, { 520, 430 } },
                                               Case { &autoTag, { 640, 460 } },
                                               Case { &tap, { 420, 260 } } }) {
        c.dialog->setSize(c.size); c.dialog->resized();
        for (auto* child : c.dialog->getChildren()) {
            if (! child->isVisible()) continue;         // hidden controls keep their old bounds
            REQUIRE(child->getX() >= studio::spacing * 2);
            REQUIRE(child->getY() >= studio::spacing * 2);
            REQUIRE(child->getBounds().getRight() <= c.dialog->getWidth() - studio::spacing * 2);
            REQUIRE(child->getBottom() <= c.dialog->getHeight() - studio::spacing * 2);
        }
    }
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build --preset debug --target SonicatorTests && $HOME/Library/Caches/sonicator-build/debug/SonicatorTests "[ui][studio]"`
Expected: FAIL on `getX() >= 20` — `ExtractDialog` lays its title out at 18 and `AutoTagDialog` its summary at 14 today.

- [ ] **Step 3: Implement**

Replace each dialog's `paint` fill with `Surface::paint` and its literal margins with the surface areas; keep the tab component in AutoTag, the toggles in Extract and the buttons in Tap tempo exactly where they are functionally. Persist no new state.

- [ ] **Step 4: Run**

Run: `cmake --build --preset debug --target SonicatorTests && $HOME/Library/Caches/sonicator-build/debug/SonicatorTests "[ui][studio]" "[extract]" "[autotag]" && ctest --preset debug`
Expected: pass.

- [ ] **Step 5: Commit**

```bash
git add ui/extract/ExtractDialog.cpp ui/tagging/AutoTagDialog.cpp ui/library/TapTempoDialog.cpp tests/TestWorkspaceStudio.cpp
git commit -m "feat(ui): move the remaining dialogs onto the shared surface"
```

---

### Task 9: Screenshots, acceptance and release

**Files:**
- Modify: `tests/TestStudioRendering.cpp`
- Create: `docs/WORKSPACE_STUDIO_ACCEPTANCE.md`
- Modify: `docs/SETTINGS_HOTKEYS_LAYOUT_ACCEPTANCE.md` (point the "workspace still uses previous styling" note at the new document)
- Modify: `AGENTS.md` (feature specification list)

**Interfaces:**
- Consumes: everything above.
- Produces: `SONICATOR_SCREENSHOT_DIR` support; sixteen PNGs named `<view>-<theme>-<size>.png` for views `library`, `analysis`, `preview`, `dialog`, themes `dark`/`light`, sizes `1400x900`/`1120x720`.

- [ ] **Step 1: Extend the render test**

When `SONICATOR_SCREENSHOT_DIR` is set, write fourteen PNGs named
`<view>-<theme>-<size>.png`: views `library`, `analysis`, `preview` at `1400x900` and `1120x720` in
both themes, and `dialog` at `520x430` in both themes. Build one `MainComponent` per theme and
size, insert a decoded wav through `SampleRepository` and select it so the Analysis card and the
Preview have content, then snapshot. Build one `SplitDialog` per theme for the dialog shots. Windows
are never shown; the files come from `createComponentSnapshot`. Keep the existing single-file env
variables working.

- [ ] **Step 2: Run and inspect**

Run: `SONICATOR_SCREENSHOT_DIR=$PWD/build-shots cmake --build --preset debug --target SonicatorTests && SONICATOR_SCREENSHOT_DIR=$PWD/build-shots $HOME/Library/Caches/sonicator-build/debug/SonicatorTests "[ui][studio]"` then open the sixteen files.
Expected: header and filter row complete at 1120×720 in both themes, Analysis shows tiles, Preview shows the 36 px Play and the format line, the dialog shows sliders and ticks.

- [ ] **Step 3: Run at 125% for real**

Launch the built application, open Settings → General, set Interface scale to 125%, and look at the
workspace: header, filter row, table and Preview must not clip. Set it back to 100% afterwards so
the owner's saved preference is unchanged. If screen capture is unavailable, record the manual look
in the acceptance document and mark the screenshot as missing — never substitute a test snapshot.

- [ ] **Step 4: Full verification**

Run: `ctest --preset debug` (expect 1/1 passed) and `cmake --build --preset release --target Sonicator` (expect `SONICATOR.app` linked and signed).
Run: `graphify update .`

- [ ] **Step 5: Write the acceptance document**

Sections: what shipped, verification actually run (with counts and file names), not verified (Windows, real DAW host, interactive 125% if capture failed), known limits, and how to check by hand.

- [ ] **Step 6: Commit**

```bash
git add tests/TestStudioRendering.cpp docs/WORKSPACE_STUDIO_ACCEPTANCE.md docs/SETTINGS_HOTKEYS_LAYOUT_ACCEPTANCE.md AGENTS.md
git commit -m "docs: record the workspace Studio stage and its screenshots"
```
