# Sample Type Editing & Adaptive Hybrid Learning Design

## Context & Intent
In SONICATOR, sample classification currently operates via a two-stage hybrid engine:
1. Deterministic acoustic DSP rules (`FastAcousticRules`).
2. Machine learning inference via ONNX Runtime (`OnnxAudioClassifier`) using 64-band log Mel-spectrograms.

While the SQLite catalog supports storing manual overrides (`SampleRepository::setSmartOverride(id, SmartField::Type, ...)`), the UI has no way for the user to edit or correct a sample's `Type`. Furthermore, user corrections and assigned tags are not fed back into the classification engine or collected for model training, as envisioned in `Sonicator Features.md` (items 11 & 12).

This design introduces:
1. **Sample Type Context Menu & Custom Types:** Full UI for editing the sample `Type` column (single and batch), adding custom types, and resetting overrides.
2. **Adaptive Local Learning (Prototype Memory):** Instant on-device adaptation using user corrections and assigned tags to influence future classifications of acoustically similar samples.
3. **Training Data Collection & Opt-in Setting:** Structured storage of acoustic feature vectors with corrected types/tags for model retraining, accompanied by an opt-in toggle in Settings.

---

## Requirements & Scope

### 1. UI: Sample Type Editing
- **Context Menu Integration:**
  Right-clicking one or more samples in the library table presents a `Type ▶` submenu (alongside `Tag ▶`, `Star`, `Split`, etc.):
  - Standard/canonical taxonomy items:
    - Drums / Hits: `Kick`, `Snare`, `Hat`, `Clap`, `Percussion`, `Cymbal`, `Tom`
    - Tonal / Bass: `Bass`, `Sub`, `Lead`, `Pad`, `Pluck`, `Chord`
    - Acoustic / Organic: `Guitar`, `Piano`, `Strings`, `Brass`, `Vocal`
    - Sound FX: `FX`, `Riser`, `Drop`, `Texture`, `Atmosphere`
  - Distinct separator, followed by:
    - User-defined custom types registered in the catalog.
  - Distinct separator, followed by:
    - `+ Custom type...` (opens a modal dialog prompting for the type name).
    - `Reset type override` (clears manual override and reverts to auto-detected type).
- **Batch Editing:** When multiple files are selected, choosing a type or custom type applies the override to all selected files simultaneously.
- **Table Visual Feedback:** The `TYPE` column reflects the updated type immediately, displaying the `*` suffix indicating manual override (e.g. `Kick *`).
- **Search & Filter:** Overridden types are fully indexable and searchable via existing `type:` filter syntax.

### 2. Adaptive Local Learning Engine (`AdaptiveClassifier`)
- **Prototype Memory on Acoustic Descriptors:**
  - When a user sets a type override or assigns tags, acoustic descriptors (normalized 14D vector: centroid, rolloff, flatness, peak, RMS, zero-crossing rate, spectral flux, attack time, decay time, crest factor, duration, etc.) are recorded in `ml_feedback_samples`.
  - Feature extraction for overrides is handled asynchronously via a background task so UI batch operations remain instantaneous.
  - Stored fields: `sample_id`, `file_hash`, `target_type` (nullable if only tags modified), `tags` (comma-separated list), `features` (BLOB of float32), `updated_at`.
- **Dual-Prediction: Type and Tag Prediction:**
  - **Type Inference:** When analyzing a sample, k-NN similarity search across user-corrected prototypes emits a `SmartCandidate` with `source = "user_adaptive"` and `field = SmartField::Type`. In `SmartResolver`, this takes precedence over generic DSP/ONNX heuristics.
  - **Tag Prediction (AutoTag):** The adaptive classifier also associates acoustic feature clusters with user tags. During `AutoTag selected...` or automatic tagging passes, the system predicts and suggests tags learned from acoustically similar prototypes.
- **Scope & Privacy:**
  - **Local learning is always active:** The user immediately benefits from their corrections locally without needing any cloud connection.
  - **`contributeTrainingData` (Settings toggle):** Strictly governs external contribution/export of anonymous feature vectors for global ONNX model retraining.

### 3. Lifecycle & Re-analysis
- Learned prototypes take effect immediately for all subsequent scans, imports, and manual "Analyze selected" / "AutoTag selected..." runs.
- Removing or resetting an override removes the corresponding prototype from `ml_feedback_samples` and refreshes in-memory prototype weights.

---

## Architectural Changes & Components

### 1. Database Schema (`core/database/SampleRepository`)
- **Table: `custom_types`**
  ```sql
  CREATE TABLE IF NOT EXISTS custom_types (
      name TEXT PRIMARY KEY NOT NULL COLLATE NOCASE,
      created_at INTEGER NOT NULL
  );
  ```
- **Table: `ml_feedback_samples`**
  ```sql
  CREATE TABLE IF NOT EXISTS ml_feedback_samples (
      sample_id INTEGER PRIMARY KEY,
      file_hash TEXT NOT NULL,
      target_type TEXT NOT NULL COLLATE NOCASE,
      tags TEXT NOT NULL DEFAULT '',
      features BLOB NOT NULL, -- IEEE 754 float array of acoustic descriptors
      updated_at INTEGER NOT NULL
  );
  CREATE INDEX IF NOT EXISTS idx_ml_feedback_type ON ml_feedback_samples(target_type);
  ```
- Repository methods:
  - `std::vector<juce::String> customTypes() const;`
  - `bool addCustomType(const juce::String& name);`
  - `void recordFeedback(int64_t sampleId, const juce::String& targetType, const std::vector<juce::String>& tags, const std::vector<float>& features);`
  - `void removeFeedback(int64_t sampleId);`
  - `std::vector<FeedbackSample> allFeedbackSamples() const;`

### 2. Core ML / DSP (`core/ml/` and `core/smart/`)
- **`core/ml/AdaptiveClassifier.h / .cpp`**:
  - Maintains in-memory indexed prototypes loaded from `ml_feedback_samples`.
  - Vector similarity search (`predict(const std::vector<float>& features, int k = 3)`).
  - Returns top predicted type and confidence, plus relevant associated tags.
- **`core/smart/AcousticClassifierService.cpp`**:
  - Calls `AdaptiveClassifier::predict(...)`.
  - Emits candidate `SmartCandidate` with source `"user_adaptive"`.
- **`core/smart/SmartResolver.cpp`**:
  - Treats `"user_adaptive"` with higher precedence than `"ml_classifier"` and `"dsp_acoustic"`.

### 3. Settings (`core/settings/AppSettings.h`, `ui/settings/SettingsPanel.cpp`)
- Add `bool contributeTrainingData = false;` to `AppSettings`.
- Serialization & deserialization in `AppSettings::load` / `save`.
- UI Toggle in `SettingsPanel`: "Contribute anonymous feedback for model training".

### 4. UI Context Menu & Interaction (`ui/MainComponent.cpp`, `ui/library/FileListPanel.cpp`)
- In `MainComponent::showSampleContextMenu`:
  - Build `typeMenu` with standard categories and registered custom types.
  - Add `+ Custom type...` opening `ThemedTextPrompt` or dialog.
  - Add `Reset type override`.
  - On selection, apply `writeRepo->setSmartOverride(id, SmartField::Type, override)` and trigger `AdaptiveClassifier` update.

---

## Verification & Testing
1. **Unit Tests (`tests/TestAdaptiveClassifier.cpp` & `tests/TestSmartDatabase.cpp`):**
   - Verify `ml_feedback_samples` upsert and retrieval.
   - Verify `AdaptiveClassifier` similarity search and prototype updates.
   - Verify candidate priority in `SmartResolver` (`user_adaptive` beating generic predictions).
   - Verify custom type creation and persistence.
2. **UI Integration:**
   - Verify context menu reflects current selection type (checkmark if uniform).
   - Verify batch override across multiple files.
   - Verify reset override reverts to automatic prediction.
   - Verify settings toggle persists across restarts.
