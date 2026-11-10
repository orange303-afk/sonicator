# Multi-Tag AutoTag & Modern Dialogs Design

**Date:** 2026-11-10  
**Status:** Approved  

## 1. Goals
1. **Multi-Tag AutoTagging:**
   - Allow AutoTag to identify and propose multiple tags per sample across different dimensions:
     - **Instrument Type:** `Kick` (matching `bd`, `kick`, `kik`), `Snare`, `Hat`, `Clap`, `Percussion`, `Bass`, `Lead`, `Pad`, `Keys`, `Vocal`, `FX`, etc.
     - **Drum Machine / Gear:** `909`, `808`, `707`, `606`, `MPC` (`MPC60`, `MPC3000`, `MPC2000`), `LinnDrum`, `SP1200`, `CR78`, `DMX`, `303`, etc.
     - **Genre / Style:** `House`, `Techno`, `Hip Hop`, `Trap`, `Drum & Bass` (DnB), `Dubstep`, `Ambient`, `Trance`, `Disco`, `Funk`, `Lo-Fi`, etc.
     - **Structure / Type:** `Loop`, `One-shot`.
   - Utilize folder hierarchy (e.g. parent folder `/Kick` or ancestor folder `/House/`) as reliable evidence.
   - Separate numbers from abbreviations during tokenization (`bd02` -> `bd`, `02`).
   - If a proposed tag does not exist yet in the catalog, AutoTag can automatically create it with an assigned palette colour upon application.
2. **Fix `AdaptiveClassifier` False Positives:**
   - Fix metric calculation so that normalized features in positive orthant do not universally yield cosine similarity >= 0.80.
   - Add physical reality checks (e.g., short percussive samples with fast attack cannot be classified as `Pad` or `Loop`).
   - High-confidence rule evidence (filename and folder) takes precedence over generic acoustic guesses.
3. **Modern Dialog Styling:**
   - Override `drawAlertBox` in `sonicator::LookAndFeel` (`ui/shell/Theme.h`):
     - Remove the legacy JUCE 130px teal circle and oversized question/exclamation mark.
     - Render a sleek, themed alert window adhering to Sonicator's dark flat visual identity (`theme::panel`, `theme::line`, `theme::accent`).
     - Render a clean, compact modern vector icon indicator.

## 2. Architecture & Changes

### A. AutoTag Rules & Tokenization (`core/tagging/AutoTagRules.h`, `AutoTagRules.cpp`)
- Update `splitTokens`:
  - When transitioning between letters and digits (e.g., `bd02`, `909kick`, `k114`), split into separate tokens so `bd` and `02`, `909` and `kick` are parsed independently.
- Introduce Gear / Drum Machine dictionary:
  - `909`, `808`, `707`, `606`, `727`, `MPC`, `MPC60`, `MPC3000`, `MPC2000`, `Linn`, `LinnDrum`, `SP1200`, `CR78`, `DMX`, `303`.
- Introduce Genre / Style dictionary:
  - `House`, `Techno`, `Hip Hop`, `Trap`, `DnB`, `Drum & Bass`, `Dubstep`, `Ambient`, `Trance`, `Disco`, `Funk`, `Lo-Fi`, `Rock`, `Pop`, `EDM`, `Garage`.
- Folder hierarchy extraction:
  - Extract tokens not just from immediate parent folder name, but also check parent folder for Instrument (`/Kick`), and ancestors for Genre (`/House`).
- Multi-tag evaluation:
  - Update `AutoTagRules::evaluateMulti(const AutoTagInput& in, const AutoTagOptions& options)` returning `std::vector<AutoTagProposal>` + `std::optional<AutoTagSkip>`.
  - Propose distinct tags across dimensions without false conflicts (an instrument `Kick` does not conflict with gear `909` or genre `House`).

### B. AutoTagService & AutoTagDialog (`core/tagging/AutoTagService.cpp`, `ui/tagging/AutoTagDialog.cpp`)
- `AutoTagService::prepare()` collects all multi-tag proposals per sample.
- If a tag is not in `options.tagIds`, AutoTag can auto-create the tag or queue it for creation on apply.
- In `AdaptiveClassifier`:
  - Replace naive positive cosine similarity with standardized Z-score / weighted Euclidean or correlation metric, plus guard against percussive sounds matching `Pad`.

### C. AlertWindow LookAndFeel (`ui/shell/Theme.h`)
- Override `drawAlertBox` in `sonicator::LookAndFeel`:
  - Draw rounded rectangle with `theme::panel` background and `theme::line` border.
  - Draw compact, modern icon in `theme::accent` (for Info / Question) or `theme::warning` (for Warning), without overlapping text area.
  - Format title and message cleanly.
