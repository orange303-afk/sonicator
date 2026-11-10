# Multi-Tag AutoTag & Modern Dialogs Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enable multi-tag proposals in AutoTag (instruments, drum machines, genres from folder hierarchy and filenames), fix AdaptiveClassifier false positive pads, and modernize JUCE AlertWindows into Sonicator's unified theme.

**Architecture:**
1. In `AutoTagRules`: improve tokenization (digit-letter boundary splitting), add Gear and Genre token extractors, use folder hierarchy context, and support multiple non-conflicting proposals per sample.
2. In `AdaptiveClassifier`: fix metric calculation and add duration/attack filters preventing percussive sounds from matching Pad.
3. In `AutoTagService`: process multi-tag proposals and auto-create tags with palette colours when new valid categories are assigned.
4. In `Theme.h`: implement `drawAlertBox` to replace the archaic JUCE alert window styling with modern minimalist dark theme.

**Tech Stack:** C++20, JUCE 9, SQLite3

**Spec:** `docs/superpowers/specs/2026-11-10-autotag-multitag-and-dialogs-design.md`

## Global Constraints
- Full backwards compatibility with existing databases and tests.
- Maintain existing test coverage while expanding tests for multi-tagging and tokenization.

---

### Task 1: AlertWindow Modern Styling in LookAndFeel

**Files:**
- Modify: `ui/shell/Theme.h`

- [ ] **Step 1: Implement `drawAlertBox` in `sonicator::LookAndFeel`**
Override `drawAlertBox(juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>&, juce::TextLayout&)`:
Render rounded background (`theme::panel`), thin border (`theme::line`), and a clean modern icon badge in `theme::accent` / `theme::warning` without the oversized overlapping circle.

---

### Task 2: Tokenization & Category Dictionaries in AutoTagRules

**Files:**
- Modify: `core/tagging/AutoTagRules.h`
- Modify: `core/tagging/AutoTagRules.cpp`
- Test: `tests/TestAutoTagRules.cpp` (or `tests/TestTagging.cpp`)

- [ ] **Step 1: Update `splitTokens` for alphanumeric transitions**
Ensure tokens like `bd02` split into `bd` and `02`, `909kick` into `909` and `kick`.

- [ ] **Step 2: Add Gear/Drum Machine and Genre dictionaries**
Add matchers for gear (`909`, `808`, `707`, `606`, `MPC`, `MPC60`, `LinnDrum`, etc.) and genre (`House`, `Techno`, `Hip Hop`, `Trap`, `DnB`, etc.).

- [ ] **Step 3: Support folder hierarchy evidence**
Inspect parent and ancestor folders for genre and instrument hints.

---

### Task 3: Multi-Tag Proposal Generation & AutoTagService Support

**Files:**
- Modify: `core/tagging/AutoTagTypes.h`
- Modify: `core/tagging/AutoTagRules.h`
- Modify: `core/tagging/AutoTagRules.cpp`
- Modify: `core/tagging/AutoTagService.cpp`

- [ ] **Step 1: Extend `AutoTagRules::evaluateMulti` to return multiple proposals**
Generate separate proposals for instrument, gear, and genre when identified.

- [ ] **Step 2: Wire `evaluateMulti` into `AutoTagService::prepare`**
Add all non-duplicate proposals for each sample into `result.proposals`.

- [ ] **Step 3: Ensure missing tags are auto-created during apply**
When applying proposals whose tag doesn't exist yet, auto-create the tag in `SampleRepository` using the standard palette.

---

### Task 4: Fix AdaptiveClassifier Metric & Sanity Filters

**Files:**
- Modify: `core/ml/AdaptiveClassifier.cpp`

- [ ] **Step 1: Add percussive/pad physical bounds check**
If a sample has attack time < 15ms and duration < 0.6s, forbid matching with `Pad`.

- [ ] **Step 2: Improve similarity calculation**
Prevent positive-orthant cosine collapse by penalizing feature discrepancies.
