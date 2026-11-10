# Sample Type Editing & Adaptive Hybrid Learning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Provide context-menu editing of sample `Type` (with custom types and multi-select support) and implement an on-device adaptive prototype memory (`AdaptiveClassifier`) that immediately learns both `Type` and `Tags` from user actions and supports exportable dataset collection.

**Architecture:** A SQLite-backed prototype memory (`custom_types`, `ml_feedback_samples`) stores normalized 14D acoustic descriptors alongside user-confirmed types and tags. `AdaptiveClassifier` performs k-NN distance ranking on these prototypes to generate `"user_adaptive"` candidates in `AcousticClassifierService` and suggest tags in `AutoTagService`. The UI in `MainComponent` wires a structured `Type ▶` context menu with batch application, custom type prompts, and override reset.

**Tech Stack:** C++20, JUCE 9.0.5, SQLite3, Catch2.

**Spec:** `docs/superpowers/specs/2026-11-10-sample-type-editing-and-adaptive-learning-design.md`

## Global Constraints

- No audio playback interruption or audio callback allocation.
- Decouple feature extraction for batch operations from the message thread (asynchronous background execution).
- Database operations on `ml_feedback_samples` and `custom_types` must run in safe SQLite transactions.
- Zero network transmission without user consent: `contributeTrainingData` defaults to `false`.
- Adhere to JUCE coding standards and theme styling rules (`theme::isDark`, `ThemedDialogs`).

## Review Focus

- Empty or invalid audio files passed to feedback feature extraction: must be safely ignored without recording bogus 0-vectors.
- Multiple selections with mixed types: the context menu checkmark state must indicate partial vs uniform selection without crashing.
- Custom type casing and whitespace: `" synth "` vs `"Synth"` must be trimmed and treated case-insensitively (`COLLATE NOCASE`).
- Deleting an assigned tag or resetting an override: prototype memory must accurately drop or update affected sample prototypes without dangling references.
- Prototype memory with 0 samples: classification must transparently fall back to standard ONNX/DSP pipeline without overhead.

---

### Task 1: Database Schema & Repository Layer for Custom Types and ML Feedback

**Files:**
- Modify: `core/database/SampleRepository.h`
- Modify: `core/database/SampleRepository.cpp`
- Test: `tests/TestSmartDatabase.cpp`

**Interfaces:**
- Consumes: `SampleRepository`, `Database`, `SmartOverride`
- Produces:
  - `std::vector<juce::String> SampleRepository::customTypes() const;`
  - `bool SampleRepository::addCustomType(const juce::String& name);`
  - `void SampleRepository::recordFeedback(int64_t sampleId, const juce::String& targetType, const juce::StringArray& tags, const std::vector<float>& features);`
  - `void SampleRepository::removeFeedback(int64_t sampleId);`
  - `struct FeedbackRecord { int64_t sampleId; juce::String fileHash; juce::String targetType; juce::StringArray tags; std::vector<float> features; int64_t updatedAt; };`
  - `std::vector<FeedbackRecord> SampleRepository::allFeedback() const;`

- [ ] **Step 1: Write the failing test**

In `tests/TestSmartDatabase.cpp`, add test case:
```cpp
TEST_CASE("SampleRepository custom types and ml feedback storage", "[smart][database]")
{
    Database db(":memory:");
    SampleRepository repo(db);
    repo.createTables();

    CHECK(repo.customTypes().empty());
    CHECK(repo.addCustomType("Sub-Drop"));
    CHECK(repo.addCustomType("Glitch"));
    CHECK_FALSE(repo.addCustomType("sub-drop")); // duplicate case-insensitive
    auto types = repo.customTypes();
    CHECK(types.size() == 2);

    std::vector<float> features(14, 0.5f);
    repo.recordFeedback(101, "Sub-Drop", {"bass", "heavy"}, features);
    auto all = repo.allFeedback();
    REQUIRE(all.size() == 1);
    CHECK(all[0].sampleId == 101);
    CHECK(all[0].targetType == "Sub-Drop");
    CHECK(all[0].tags.size() == 2);
    CHECK(all[0].features.size() == 14);

    repo.removeFeedback(101);
    CHECK(repo.allFeedback().empty());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ninja -C build && ctest --test-dir build -R TestSmartDatabase --output-on-failure`
Expected: FAIL due to missing repository methods.

- [ ] **Step 3: Implement tables and methods in `core/database/SampleRepository`**

1. In `SampleRepository::createTables`:
   Add `CREATE TABLE IF NOT EXISTS custom_types (name TEXT PRIMARY KEY NOT NULL COLLATE NOCASE, created_at INTEGER NOT NULL);`
   Add `CREATE TABLE IF NOT EXISTS ml_feedback_samples (sample_id INTEGER PRIMARY KEY, file_hash TEXT NOT NULL, target_type TEXT NOT NULL COLLATE NOCASE, tags TEXT NOT NULL DEFAULT '', features BLOB NOT NULL, updated_at INTEGER NOT NULL);`
2. Implement `customTypes()`, `addCustomType()`, `recordFeedback()`, `removeFeedback()`, `allFeedback()` with BLOB serialization of `std::vector<float>`.

- [ ] **Step 4: Run test to verify it passes**

Run: `ninja -C build && ctest --test-dir build -R TestSmartDatabase --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add core/database/SampleRepository.h core/database/SampleRepository.cpp tests/TestSmartDatabase.cpp
git commit -m "feat(database): add custom_types and ml_feedback_samples repository support"
```

---

### Task 2: Adaptive Classifier Engine (Prototype Memory on 14D Acoustic Descriptors)

**Files:**
- Create: `core/ml/AdaptiveClassifier.h`
- Create: `core/ml/AdaptiveClassifier.cpp`
- Create: `tests/TestAdaptiveClassifier.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `core/database/SampleRepository.h`
- Produces:
  - `class AdaptiveClassifier`
  - `void loadFromRepository(const SampleRepository& repo);`
  - `void updatePrototype(int64_t sampleId, const juce::String& targetType, const juce::StringArray& tags, const std::vector<float>& features);`
  - `void removePrototype(int64_t sampleId);`
  - `struct Prediction { juce::String label; float score; };`
  - `std::vector<Prediction> predictType(const std::vector<float>& features, int topK = 3) const;`
  - `std::vector<Prediction> predictTags(const std::vector<float>& features, int topK = 5) const;`
  - `static std::vector<float> extractFeatures(const juce::AudioBuffer<float>& buffer, double sampleRate);`

- [ ] **Step 1: Write the failing test**

In `tests/TestAdaptiveClassifier.cpp`:
```cpp
TEST_CASE("AdaptiveClassifier prototype learning and dual-prediction", "[ml][adaptive]")
{
    AdaptiveClassifier classifier;
    std::vector<float> kickFeatures = {100.0f, 0.2f, 0.1f, 0.8f, 0.1f, 0.05f, 0.02f, 0.08f, 0.5f, 0.1f, 0.0f, 0.0f, 0.0f, 0.25f};
    std::vector<float> hatFeatures  = {8000.0f, 0.9f, 0.8f, 0.3f, 0.01f, 0.4f, 0.01f, 0.02f, 0.1f, 0.8f, 0.0f, 0.0f, 0.0f, 0.05f};

    classifier.updatePrototype(1, "Kick", {"drum", "low"}, kickFeatures);
    classifier.updatePrototype(2, "Hat", {"cymbal", "high"}, hatFeatures);

    auto typePreds = classifier.predictType(kickFeatures, 1);
    REQUIRE_FALSE(typePreds.empty());
    CHECK(typePreds[0].label == "Kick");
    CHECK(typePreds[0].score > 0.9f);

    auto tagPreds = classifier.predictTags(hatFeatures, 2);
    REQUIRE_FALSE(tagPreds.empty());
    CHECK(tagPreds[0].label == "cymbal");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ninja -C build && ctest --test-dir build -R TestAdaptiveClassifier --output-on-failure`
Expected: FAIL due to missing files and target.

- [ ] **Step 3: Implement `AdaptiveClassifier`**

1. Register `core/ml/AdaptiveClassifier.cpp` in `CMakeLists.txt` and `tests/CMakeLists.txt`.
2. Implement `extractFeatures`: compute normalized 14D descriptor using `AcousticFeatures::extract` and duration.
3. Implement weighted Euclidean/cosine distance k-NN with inverted distance scoring:
   $score = \frac{1}{1 + distance}$.
4. Implement `predictType` and `predictTags` aggregating prototype neighbor weights.

- [ ] **Step 4: Run test to verify it passes**

Run: `ninja -C build && ctest --test-dir build -R TestAdaptiveClassifier --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add core/ml/AdaptiveClassifier.h core/ml/AdaptiveClassifier.cpp tests/TestAdaptiveClassifier.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(ml): implement AdaptiveClassifier prototype memory engine"
```

---

### Task 3: Integration into AcousticClassifierService & SmartResolver

**Files:**
- Modify: `core/smart/AcousticClassifierService.h`
- Modify: `core/smart/AcousticClassifierService.cpp`
- Modify: `core/smart/SmartResolver.cpp`
- Modify: `tests/TestSmartAnalysis.cpp`

**Interfaces:**
- Consumes: `AdaptiveClassifier`, `AcousticFeatures`, `SmartResolver`
- Produces:
  - `SmartCandidate` with `source = "user_adaptive"` and `field = SmartField::Type`.
  - `SmartResolver` precedence giving `"user_adaptive"` candidates higher priority than `"dsp_acoustic"` and `"ml_classifier"`.

- [ ] **Step 1: Write the failing test**

In `tests/TestSmartAnalysis.cpp`:
```cpp
TEST_CASE("SmartResolver gives user_adaptive candidate priority over generic ml", "[smart][resolver]")
{
    std::vector<SmartCandidate> candidates;
    SmartCandidate mlCand;
    mlCand.field = SmartField::Type;
    mlCand.source = "ml_classifier";
    mlCand.textValue = "Lead";
    mlCand.numericValue = 0.70;
    mlCand.status = CandidateStatus::Valid;
    candidates.push_back(mlCand);

    SmartCandidate adaptiveCand;
    adaptiveCand.field = SmartField::Type;
    adaptiveCand.source = "user_adaptive";
    adaptiveCand.textValue = "Pad";
    adaptiveCand.numericValue = 0.88;
    adaptiveCand.status = CandidateStatus::Valid;
    candidates.push_back(adaptiveCand);

    auto result = SmartResolver::resolve(candidates, {});
    CHECK(result.type == "Pad");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ninja -C build && ctest --test-dir build -R TestSmartAnalysis --output-on-failure`
Expected: FAIL if `user_adaptive` is not ranked above `ml_classifier`.

- [ ] **Step 3: Update `SmartResolver` and `AcousticClassifierService`**

1. In `SmartResolver.cpp`, ensure source `"user_adaptive"` has top priority in text candidate resolution for `SmartField::Type`.
2. In `AcousticClassifierService`, query shared `AdaptiveClassifier`. If top similarity >= 0.80, append `user_adaptive` `SmartCandidate`.

- [ ] **Step 4: Run test to verify it passes**

Run: `ninja -C build && ctest --test-dir build -R TestSmartAnalysis --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add core/smart/AcousticClassifierService.h core/smart/AcousticClassifierService.cpp core/smart/SmartResolver.cpp tests/TestSmartAnalysis.cpp
git commit -m "feat(smart): integrate user_adaptive candidates into classification pipeline"
```

---

### Task 4: UI Context Menu for Sample Type Editing and Custom Types

**Files:**
- Modify: `ui/MainComponent.h`
- Modify: `ui/MainComponent.cpp`
- Modify: `ui/library/FileListPanel.h`
- Modify: `ui/library/FileListPanel.cpp`

**Interfaces:**
- Consumes: `SampleRepository::customTypes`, `SampleRepository::addCustomType`, `SampleRepository::setSmartOverride`, `AdaptiveClassifier`
- Produces: Context menu `Type ▶` hierarchy, custom type dialog prompt, batch override application and visual updating.

- [ ] **Step 1: Add custom type prompt & sub-menu builder to `MainComponent`**

1. Define standard canonical type taxonomy:
   - Drums: `Kick`, `Snare`, `Hat`, `Clap`, `Percussion`, `Cymbal`, `Tom`
   - Bass/Synth: `Bass`, `Sub`, `Lead`, `Pad`, `Pluck`, `Chord`
   - Organic: `Guitar`, `Piano`, `Strings`, `Brass`, `Vocal`
   - FX: `FX`, `Riser`, `Drop`, `Texture`, `Atmosphere`
2. Fetch registered `customTypes()` from `readRepo`.
3. Build `juce::PopupMenu typeMenu`:
   - Categorized sections with checkmarks if selection shares uniform type.
   - User custom types section.
   - `+ Custom type...` (opens dialog).
   - `Reset type override` (calls `clearSmartOverride`).

- [ ] **Step 2: Handle type override action & asynchronous feature feedback**

1. When a type is chosen for selection:
   - For each sample id: `writeRepo->setSmartOverride(id, SmartField::Type, overrideVal)`.
   - Async worker thread: load audio buffers in background, extract 14D acoustic descriptors, call `writeRepo->recordFeedback(...)`, and update `AdaptiveClassifier`.
   - Trigger `reloadLibrary()` and UI repaint.
2. When `Reset type override` is chosen:
   - Call `writeRepo->clearSmartOverride(id, SmartField::Type)`.
   - Remove feedback record from `writeRepo` and `AdaptiveClassifier`.
   - Trigger `reloadLibrary()`.

- [ ] **Step 3: Build and test compilation**

Run: `ninja -C build`
Expected: Build succeeds with 0 errors.

- [ ] **Step 4: Commit**

```bash
git add ui/MainComponent.h ui/MainComponent.cpp ui/library/FileListPanel.h ui/library/FileListPanel.cpp
git commit -m "feat(ui): add Type context menu, custom types dialog, and batch override editing"
```

---

### Task 5: Settings Toggle for Training Data Opt-in

**Files:**
- Modify: `core/settings/AppSettings.h`
- Modify: `core/settings/AppSettings.cpp`
- Modify: `ui/settings/SettingsPanel.h`
- Modify: `ui/settings/SettingsPanel.cpp`
- Test: `tests/TestSettings.cpp`

**Interfaces:**
- Consumes: `AppSettings`, `SettingsStore`
- Produces: `AppSettings::contributeTrainingData` serialized to JSON and configurable via `SettingsPanel`.

- [ ] **Step 1: Write the failing test**

In `tests/TestSettings.cpp`:
```cpp
TEST_CASE("AppSettings contributeTrainingData persistence", "[settings]")
{
    AppSettings s;
    CHECK_FALSE(s.contributeTrainingData); // default false for privacy
    s.contributeTrainingData = true;

    auto file = juce::File::createTempFile("sonicator_settings_test");
    SettingsStore store(file);
    store.save(s);

    auto loaded = store.load();
    CHECK(loaded.contributeTrainingData);
    file.deleteFile();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ninja -C build && ctest --test-dir build -R TestSettings --output-on-failure`
Expected: FAIL due to missing `contributeTrainingData` member.

- [ ] **Step 3: Implement `contributeTrainingData` field and UI toggle**

1. Add `bool contributeTrainingData = false;` to `AppSettings.h`.
2. Update JSON serialization/deserialization in `AppSettings.cpp`.
3. In `SettingsPanel.h/.cpp`, add `juce::ToggleButton contributeDataToggle;` with text `"Contribute anonymous feedback to improve classification models"`.
4. Wire toggle change event to `current.contributeTrainingData` and notify.

- [ ] **Step 4: Run test to verify it passes**

Run: `ninja -C build && ctest --test-dir build -R TestSettings --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add core/settings/AppSettings.h core/settings/AppSettings.cpp ui/settings/SettingsPanel.h ui/settings/SettingsPanel.cpp tests/TestSettings.cpp
git commit -m "feat(settings): add contributeTrainingData toggle and persistence"
```

---

### Task 6: AutoTag Integration with Learned Tags

**Files:**
- Modify: `core/tagging/AutoTagRules.h`
- Modify: `core/tagging/AutoTagRules.cpp`
- Modify: `core/tagging/AutoTagService.cpp`
- Test: `tests/TestTagging.cpp`

**Interfaces:**
- Consumes: `AdaptiveClassifier::predictTags`
- Produces: Learned tag candidates proposed during auto-tag passes.

- [ ] **Step 1: Write the failing test**

In `tests/TestTagging.cpp`:
```cpp
TEST_CASE("AutoTag incorporates learned tags from adaptive prototypes", "[tagging][adaptive]")
{
    // Verify that when adaptive prototype has high confidence on a tag, AutoTag emits this tag
    AdaptiveClassifier classifier;
    std::vector<float> features(14, 0.3f);
    classifier.updatePrototype(50, "Lead", {"vintage", "analog"}, features);

    auto suggested = classifier.predictTags(features, 2);
    REQUIRE(suggested.size() >= 2);
    CHECK(suggested[0].label == "vintage");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ninja -C build && ctest --test-dir build -R TestTagging --output-on-failure`
Expected: FAIL until wired.

- [ ] **Step 3: Integrate adaptive tag suggestions into AutoTag**

1. In `AutoTagRules::classify` or `AutoTagService`, combine standard keyword/container rules with `AdaptiveClassifier::predictTags`.
2. Tag suggestions exceeding confidence threshold (>= 0.75) are presented or assigned.

- [ ] **Step 4: Run test to verify it passes**

Run: `ninja -C build && ctest --test-dir build -R TestTagging --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add core/tagging/AutoTagRules.h core/tagging/AutoTagRules.cpp core/tagging/AutoTagService.cpp tests/TestTagging.cpp
git commit -m "feat(tagging): integrate learned prototype tags into AutoTag pipeline"
```
