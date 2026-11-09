# DSP key and one-shot pitch — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Derive musical key for loops and MIDI pitch for one-shots from one shared PCM decode, store the values in the existing smart fields, show Pitch in the bottom selection bar, and measure accuracy on the owner's key-labelled library.

**Architecture:** A bounded `readAnalysisAudio` decodes the prefix once. `RhythmAnalyzer::analyze(buffer, rate)` and the new `TonalAnalyzer` consume that buffer; the smart layer gates and maps their results to `SmartCandidate` rows. Key uses a chromagram with tuning estimate, percussion suppression and Krumhansl–Kessler/Temperley profiles. Pitch uses FFT-accelerated YIN over the most stable segment of a one-shot. `smartDspVersion` becomes 2.

**Tech Stack:** C++20, JUCE 9.0.5 (`juce_dsp`, `juce_audio_formats`), bundled SQLite, Catch2 3.7.1.

**Spec:** `docs/superpowers/specs/2026-11-09-dsp-key-pitch-design.md`

## Global Constraints

- One PCM decode per file in the smart pipeline; no second `createReaderFor` for the same sample.
- No ML, network, cache files or training data.
- No schema migration. Use existing `key_*` and `pitch_*` columns.
- `basicFeaturesVersion` stays 1; measured Phase 1 features are never rewritten.
- DSP is the lowest-priority source (`smartSourcePriority("dsp") == 5`); metadata wins.
- Drums and non-harmonic hits answer `unknown`, never an invented key or note.
- Confident tonic without a mode publishes a bare tonic (`"C"`); a tonic dispute publishes nothing.
- Pitch only for one-shots with no `Valid` Pitch or Root candidate; a `DefaultValue` root does not block.
- No new library column; Pitch appears in the bottom selection bar.
- All analysis is read-only, bounded, cooperative with `CancelCheck`, and never on the audio thread.
- macOS Debug is the verified platform. Tests use synthetic fixtures; the real library test is opt-in.

## Review Focus

- A drum loop with no key label must stay `unknown`, not receive the key of its tuned kick.
- A one-shot kick with `smpl` unity 60 must not be blocked from pitch by the mapping default, and must not itself publish a note when it glides.
- A file with a filename key must never be overwritten by a DSP key.
- A loop must never receive a pitch, and a one-shot with no stable segment must stay `unknown`.
- An anti-phase stereo one-shot must not cancel to silence in the pitch pass.

---

### Task 1: Shared bounded decode

**Files:**
- Create: `core/dsp/AnalysisAudio.h`, `core/dsp/AnalysisAudio.cpp`
- Modify: `core/dsp/RhythmAnalyzer.cpp:530-558` (use the shared reader)
- Modify: `CMakeLists.txt`
- Test: `tests/TestAnalysisAudio.cpp`, add to `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
```cpp
struct AnalysisAudioOptions
{
    double maxSeconds = 60.0;
    int64_t maxDecodedBytes = 64ll * 1024 * 1024;
};
struct AnalysisAudio
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 0;
    bool truncated = false;
    juce::String error;
    bool ok() const { return error.isEmpty() && sampleRate > 0 && buffer.getNumSamples() > 0; }
};
AnalysisAudio readAnalysisAudio(const juce::File&, const AnalysisAudioOptions& = {},
                                const CancelCheck& = {});
```

- [ ] **Step 1: Write the failing tests**

`tests/TestAnalysisAudio.cpp`, tag `[analysis-audio]`:
- a 2 s 44.1 kHz WAV round-trips with rate and sample count;
- `maxSeconds = 0.5` returns 0.5 s and `truncated == true`;
- `maxDecodedBytes` caps a stereo file to the matching frame count;
- a text file returns a non-empty `error` and `!ok()`;
- a cancellation callback returns `error == "Cancelled"`;
- `RhythmAnalyzer::analyzeFile` still reports `"first 2 s analysed"` when capped.

```cpp
auto file = writeWav(clickTrack(120.0, 4.0, 44100.0), 44100.0);
AnalysisAudioOptions options; options.maxDecodedBytes = (int64_t) (44100 * 4.0 * 2.0);
auto audio = readAnalysisAudio(file, options);
REQUIRE(audio.ok());
REQUIRE(audio.buffer.getNumSamples() == 88200);
REQUIRE(audio.truncated);
```

- [ ] **Step 2: Run and watch it fail**

Run: `cmake --build build --target SonicatorTests -j 8 && build/tests/SonicatorTests "[analysis-audio]"`
Expected: link error for `readAnalysisAudio`.

- [ ] **Step 3: Implement the reader**

Open one `AudioFormatManager`, register basic formats, bound by `maxSeconds` and
`maxDecodedBytes / (4 * channels)`, read into an `AudioBuffer<float>`, set
`truncated` when the source is longer. Return the same error strings the rhythm
reader uses (`"unsupported, empty or corrupt audio file"`, `"empty audio file"`,
`"audio decode failed"`, `"Cancelled"`). Change `RhythmAnalyzer::analyzeFile` to
call it and append the existing `"(first N s analysed)"` note.

- [ ] **Step 4: Run the tests and the rhythm suite**

Run: `build/tests/SonicatorTests "[analysis-audio],[rhythm]"`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add core/dsp/AnalysisAudio.h core/dsp/AnalysisAudio.cpp core/dsp/RhythmAnalyzer.cpp tests/TestAnalysisAudio.cpp tests/CMakeLists.txt CMakeLists.txt
git commit -m "feat(dsp): share one bounded PCM decode between analyses"
```

---

### Task 2: Key chroma and profiles

**Files:**
- Create: `core/dsp/TonalAnalyzer.h`, `core/dsp/TonalAnalyzer.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/TestTonalAnalyzer.cpp`, add to `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
```cpp
struct TonalOptions
{
    double maxKeySeconds = 30.0;
    int keyFftSize = 8192;
    int keyHop = 1024;
    double minKeyFrequency = 55.0, maxKeyFrequency = 5000.0;
    double tuningMinFrequency = 60.0, tuningMaxFrequency = 3000.0;
    double minTonality = 0.30;
    double minKeyCorrelation = 0.35;
    double minTonicMargin = 0.04;
    double modeMargin = 0.03;
    double percussiveFlatness = 0.55;
    // pitch fields are added in Task 4
};
struct KeyAnalysis
{
    bool found = false;
    int tonic = -1;                 // 0 = C .. 11 = B
    juce::String mode;              // "major", "minor" or empty for a bare tonic
    double confidence = 0.0;        // documented heuristic 0..1
    double bestCorrelation = 0.0, runnerUpCorrelation = 0.0;
    int runnerUpTonic = -1;
    juce::String runnerUpMode;
    double tuningCents = 0.0, tonality = 0.0;
    bool percussive = false;
    juce::String explanation, error;
};
class TonalAnalyzer
{
public:
    static KeyAnalysis analyzeKey(const juce::AudioBuffer<float>&, double sampleRate,
                                  const TonalOptions& = {}, const CancelCheck& = {});
};
```

- [ ] **Step 1: Write the failing key tests**

Generate notes with a short attack (10 ms) and 2 s of decay-free sines, channel 0.
- C major triad `{C4,E4,G4}` resolves `C major`;
- A minor triad `{A3,C4,E4}` resolves `A minor`;
- all 12 major and 12 minor triads resolve to their root and mode;
- a C major scale resolves `C major`;
- a single A440 sine resolves tonic 9 with `mode.isEmpty()` (bare tonic);
- silence and white noise are `!found`;
- 48 kHz and 96 kHz triads resolve;
- an anti-phase stereo C major triad resolves `C major`;
- a file shorter than 0.2 s is `!found`.

```cpp
const auto audio = triad(0, 4, 44100.0, 2.0); // C E G
const auto key = TonalAnalyzer::analyzeKey(audio, 44100.0);
REQUIRE(key.found);
REQUIRE(key.tonic == 0);
REQUIRE(key.mode == "major");
```

- [ ] **Step 2: Run and watch it fail**

Run: `cmake --build build --target SonicatorTests -j 8 && build/tests/SonicatorTests "[tonal][key]"`
Expected: link error for `TonalAnalyzer`.

- [ ] **Step 3: Implement chroma and profile scoring**

- Frame the buffer at `keyFftSize`/`keyHop`, Hann window, average channel powers.
- For each frame, pick local spectral maxima in `[minKeyFrequency, maxKeyFrequency]`
  and store `(frequency, magnitude)` plus RMS, spectral flatness and positive log
  flux.
- Estimate tuning from peak deviations to the nearest equal-tempered semitone over
  `[tuningMinFrequency, tuningMaxFrequency]`; smooth the histogram and take its peak.
- Fold peaks into 12 pitch classes with a Gaussian semitone kernel and the tuning
  correction; keep a 12-vector per frame.
- Median-filter each chroma bin along time (9 frames), weight frames by
  `1 - flatness / percussiveFlatness` (clamped to 0), and accumulate.
- Score 24 keys with the mean Pearson correlation of the Krumhansl–Kessler and
  Temperley major/minor profiles. Choose the best key; keep the best different-tonic
  runner-up and the other mode at the same tonic for diagnostics.
- Apply the decision in Task 3; for now return `found` when the best correlation is
  above `minKeyCorrelation`.

- [ ] **Step 4: Run the tests**

Run: `build/tests/SonicatorTests "[tonal][key]"`
Expected: all pass. If a triad fails, print the top three profile scores in the
failure message before changing any threshold.

- [ ] **Step 5: Commit**

```bash
git add core/dsp/TonalAnalyzer.h core/dsp/TonalAnalyzer.cpp tests/TestTonalAnalyzer.cpp tests/CMakeLists.txt CMakeLists.txt
git commit -m "feat(dsp): chromagram and key profiles"
```

---

### Task 3: Key tuning, percussion suppression and decision rules

**Files:**
- Modify: `core/dsp/TonalAnalyzer.cpp`
- Test: `tests/TestTonalAnalyzer.cpp`

**Interfaces:**
- Consumes: `TonalAnalyzer::analyzeKey` from Task 2.

- [ ] **Step 1: Write the failing decision tests**

- A C major triad transposed by +25 cents still resolves `C major`, and
  `tuningCents` is within 8 cents of +25;
- a click train at 120 BPM with no tonal content is `!found` and `percussive`;
- a drum-like pattern (short noise bursts, 60 ms decay) is `!found`;
- a chroma made from equal C major and F# major triads is `!found` (tonic dispute);
- a chroma made from equal C major and C minor triads is `found`, tonic 0,
  `mode.isEmpty()` (bare tonic);
- `confidence` is finite and within 0..1 for every found key;
- `explanation` names the profile set and the margin.

- [ ] **Step 2: Run and watch them fail**

Run: `build/tests/SonicatorTests "[tonal][key][decision]"`
Expected: failures for tuning/percussive/bare-tonic.

- [ ] **Step 3: Implement the decision**

- Reject when `tonality < minTonality`, when the best correlation is below
  `minKeyCorrelation`, or when the signal is shorter than 0.2 s.
- Compute `tonicMargin = best - bestDifferentTonic` and
  `modeMargin = best - otherModeAtSameTonic`.
- `tonicMargin < minTonicMargin` → `!found` (tonic dispute).
- `modeMargin < modeMargin` threshold → bare tonic (`mode` empty).
- Otherwise publish the full mode.
- `confidence = clamp((best - minKeyCorrelation) / (1 - minKeyCorrelation), 0, 1)
  * clamp(tonicMargin / 0.20, 0, 1)`; for a bare tonic use the same tonic margin.
- Mark `percussive` when `tonality` is below the gate or flatness dominates.

- [ ] **Step 4: Run the tests**

Run: `build/tests/SonicatorTests "[tonal][key]"`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add core/dsp/TonalAnalyzer.cpp tests/TestTonalAnalyzer.cpp
git commit -m "feat(dsp): key tuning, percussion gate and margin rule"
```

---

### Task 4: Pitch with FFT-accelerated YIN

**Files:**
- Modify: `core/dsp/TonalAnalyzer.h`, `core/dsp/TonalAnalyzer.cpp`
- Test: `tests/TestTonalAnalyzer.cpp`

**Interfaces:**
- Adds to `TonalOptions`:
```cpp
double maxPitchSeconds = 5.0;
double minPitchFrequency = 50.0, maxPitchFrequency = 2000.0;
double yinThreshold = 0.15;
double minPitchClarity = 0.5;
int pitchHop = 256;
int minStableFrames = 4;
double maxRunCents = 50.0;
double maxStableDeviationCents = 30.0;
double minHarmonicity = 0.55;
double minActiveDbfs = -45.0;
double attackSkipSeconds = 0.02;
```
- Adds:
```cpp
struct PitchAnalysis
{
    bool found = false;
    double midi = 0.0;          // exact fractional MIDI
    double cents = 0.0;         // (midi - round(midi)) * 100
    double clarity = 0.0, stabilityCents = 0.0, harmonicity = 0.0;
    double confidence = 0.0;
    int stableFrames = 0;
    juce::String explanation, error;
};
static PitchAnalysis analyzePitch(const juce::AudioBuffer<float>&, double sampleRate,
                                  const TonalOptions& = {}, const CancelCheck& = {});
```

- [ ] **Step 1: Write the failing pitch tests**

- A440 → `midi` within 0.02 of 69, `cents` within 2 of 0;
- A4 +25 cents → `midi` within 0.03 of 69.25, cents within 3 of 25;
- C3 (130.81 Hz) → 48; E2 (82.41 Hz) → 40;
- 48 kHz and 96 kHz A440 → 69;
- a 2 s sine with a 10 ms attack and a stable sustain → found;
- silence and white noise → `!found`;
- a sine that glides from 220 to 110 Hz → `!found`;
- a noise burst with a 60 ms decay → `!found`;
- a C major triad → `!found` (polyphonic);
- anti-phase stereo A440 → found, midi 69;
- `confidence` finite and within 0..1.

- [ ] **Step 2: Run and watch them fail**

Run: `build/tests/SonicatorTests "[tonal][pitch]"`
Expected: link error for `analyzePitch`.

- [ ] **Step 3: Implement pitch**

- Find the active region from frame RMS; skip `attackSkipSeconds`; cap at
  `maxPitchSeconds`.
- For each channel, compute YIN difference with FFT autocorrelation:
  `d(tau) = prefix(W-tau) + suffix(tau) - 2*acf(tau)`, then cumulative mean
  normalization, first dip below `yinThreshold`, local minimum, parabolic
  interpolation.
- Keep frames with clarity ≥ `minPitchClarity`; convert f0 to fractional MIDI;
  group consecutive frames within `maxRunCents` into runs; take the longest run of at
  least `minStableFrames`.
- Median MIDI of the run; reject when the run's standard deviation exceeds
  `maxStableDeviationCents`.
- Harmonicity: FFT of the stable segment, sum magnitudes around the first 5–8
  partials of the median f0, divide by the total in 50 Hz–5 kHz; reject below
  `minHarmonicity`.
- Run per channel, keep the higher-confidence channel; if both are found, differ by
  more than 50 cents and the lower confidence is at least 70% of the higher, return
  `!found`.
- `confidence = clarity * clamp(1 - deviation / maxStableDeviationCents, 0, 1)
  * harmonicity`, clamped to 0..1.
- `cents = (midi - round(midi)) * 100`.

- [ ] **Step 4: Run the tests**

Run: `build/tests/SonicatorTests "[tonal][pitch]"`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add core/dsp/TonalAnalyzer.h core/dsp/TonalAnalyzer.cpp tests/TestTonalAnalyzer.cpp
git commit -m "feat(dsp): YIN pitch on a stable one-shot segment"
```

---

### Task 5: Tonal candidates and gates

**Files:**
- Create: `core/smart/TonalCandidates.h`, `core/smart/TonalCandidates.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/TestTonalCandidates.cpp`, add to `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
```cpp
bool hasValidKeyMetadata(const std::vector<SmartCandidate>&);
bool hasValidPitchOrRootMetadata(const std::vector<SmartCandidate>&);
bool typeIsDrum(const juce::String& type);       // kick, snare, clap, hat, percussion, cymbal
bool typeAllowsPitch(const juce::String& type);  // false for drums, vocal, fx
struct TonalExtraction
{
    std::vector<SmartCandidate> candidates;
    juce::String status = "ok";   // ok, unsupported, error
    juce::String error;
};
TonalExtraction extractTonalCandidates(const AnalysisAudio&,
                                       const std::vector<SmartCandidate>& metadata,
                                       const juce::String& structure,
                                       const juce::String& type,
                                       const TonalOptions& = {});
```

- [ ] **Step 1: Write the failing candidate tests**

- a valid `filename` key suppresses the DSP key;
- a valid `wav_smpl` Pitch suppresses the DSP pitch;
- a `DefaultValue` root at 60 does not suppress the DSP pitch;
- a loop structure produces no pitch even with no pitch metadata;
- a one-shot with no pitch/root produces a pitch candidate with `source == "dsp"`;
- Type `kick`/`snare`/`vocal`/`fx` produce no pitch; `kick` produces no key;
- Type `bass`/`keys`/empty allows pitch;
- a bare-tonic key result stores `textValue == "C"`;
- a full key stores `textValue == "C# minor"`;
- an ambiguous tonic produces no key candidate;
- key `auxValue` is the confidence and `limitation` explains the estimate;
- pitch `numericValue` is the fractional MIDI and `auxValue == 0`.

- [ ] **Step 2: Run and watch them fail**

Run: `cmake --build build --target SonicatorTests -j 8 && build/tests/SonicatorTests "[tonal][candidates]"`
Expected: link error for the new functions.

- [ ] **Step 3: Implement the mapping and gates**

Run `TonalAnalyzer::analyzeKey` only when `!hasValidKeyMetadata(metadata)` and
`!typeIsDrum(type)`. Run `TonalAnalyzer::analyzePitch` only when
`structure == "one-shot" && typeAllowsPitch(type) && !hasValidPitchOrRootMetadata(metadata)`.
Set `source = "dsp"`, `extractorId = "dsp"`, `extractorVersion = smartDspVersion`,
`limitation` and a deterministic `detail`. Key text is
`keyDisplayName(tonic, mode)`; pitch `numericValue = midi`, `auxValue = 0`.

- [ ] **Step 4: Run the tests**

Run: `build/tests/SonicatorTests "[tonal][candidates]"`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add core/smart/TonalCandidates.h core/smart/TonalCandidates.cpp tests/TestTonalCandidates.cpp tests/CMakeLists.txt CMakeLists.txt
git commit -m "feat(smart): gate and map DSP key and pitch candidates"
```

---

### Task 6: Smart pipeline integration

**Files:**
- Modify: `core/smart/SmartAnalysisService.cpp`
- Modify: `core/smart/SmartTypes.h` (`smartDspVersion = 2`)
- Test: `tests/TestSmartIndexing.cpp` or `tests/TestSmartFoundation.cpp`

**Interfaces:**
- Consumes: `readAnalysisAudio`, `TonalAnalyzer`, `extractTonalCandidates`.
- Produces: the existing `SmartCollection` with `dsp` extraction and candidates.

- [ ] **Step 1: Write the failing pipeline tests**

On synthetic WAVs in a `TestDirectory`:
- a C major triad WAV with no key/BPM metadata publishes a `dsp` key candidate;
- a WAV whose filename contains `_Cm` publishes no `dsp` key candidate (metadata wins);
- a short one-shot WAV (single click) with no pitch/root publishes a `dsp` pitch
  candidate when its structure is one-shot;
- a 120 BPM click loop produces no `dsp` pitch candidate;
- the `dsp` extraction record is `skipped` when all metadata answers exist;
- `smartDspVersion == 2`;
- a decode failure records `unsupported` and does not throw.

- [ ] **Step 2: Run and watch them fail**

Run: `build/tests/SonicatorTests "[smart][dsp]"`
Expected: no tonal candidates and DSP version 1.

- [ ] **Step 3: Implement the shared pass**

In `collect`, after the metadata extractors:
1. Compute `needRhythm = !needsRhythmAnalysis(out.candidates)`.
2. Compute `needKey = !hasValidKeyMetadata(out.candidates)`.
3. Compute `pitchEligible = !hasValidPitchOrRootMetadata(out.candidates)` and the
   metadata Type; `needPitch = pitchEligible && typeAllowsPitch(type) &&
   (known structure == one-shot || (structure unknown && needRhythm))`.
4. If `needRhythm || needKey || needPitch`, call `readAnalysisAudio` once.
5. Run `RhythmAnalyzer::analyze` when `needRhythm`; append its candidates.
6. Resolve Type/Structure from metadata plus rhythm candidates with `resolveSmart`.
7. Run `extractTonalCandidates(audio, metadata, structure, type)` and append.
8. Record one `dsp` extraction: `skipped` when no decode was needed, `unsupported`
   for a non-decodable file, `error` for a decode failure, otherwise `ok`.
9. Bump `smartDspVersion` to 2.

- [ ] **Step 4: Run the tests**

Run: `build/tests/SonicatorTests "[smart],[tonal]"`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add core/smart/SmartAnalysisService.cpp core/smart/SmartTypes.h tests/TestSmartIndexing.cpp tests/TestSmartFoundation.cpp
git commit -m "feat(smart): run key and pitch from the shared decode"
```

---

### Task 7: Pitch display

**Files:**
- Modify: `core/smart/SmartTypes.h` (`SmartDisplay` pitch fields)
- Modify: `core/smart/KeyNames.h/.cpp` (`midiNoteName`, `formatCents`)
- Modify: `core/database/SampleRepository.cpp` (`smartDisplays`)
- Modify: `ui/MainComponent.cpp` (bottom selection bar)
- Test: `tests/TestSmartFoundation.cpp`, `tests/TestUI.cpp`

**Interfaces:**
- Adds to `SmartDisplay`:
```cpp
bool hasPitch = false;
int pitchMidi = -1;
double pitchCents = 0;
SmartState pitchState = SmartState::Unknown;
OverrideState pitchOverride = OverrideState::None;
```
- Adds `juce::String midiNoteName(int midi);` and
  `juce::String formatCents(double cents);` in `KeyNames`.
- The bottom bar appends `Pitch: A4 +12c`, `Pitch: ?`, `Pitch: n/a`,
  `Pitch: manual: unknown`, `Pitch: A4 +12c (conflict)`, `(ambiguous)` or `(stale)`.

- [ ] **Step 1: Write the failing tests**

- Publish a `SmartResult` with `hasPitch`, `pitchMidi = 69`, `pitchCents = 12`;
  `smartDisplays({id})[id]` returns the same values.
- A pitch override is reflected in `pitchOverride` and the display value.
- `midiNoteName(69) == "A4"`, `midiNoteName(61) == "C#4"`, `formatCents(12.0) == "+12c"`.
- The bottom bar text for a selected sample contains `"Pitch:"` and `"A4 +12c"`.

- [ ] **Step 2: Run and watch them fail**

Run: `cmake --build build --target SonicatorTests -j 8 && build/tests/SonicatorTests "[smart][display],[ui]"`
Expected: missing fields/query columns.

- [ ] **Step 3: Implement**

Add the four pitch columns to the `smartDisplays` SELECT, read `pitch_midi`,
`pitch_cents`, `pitch_state`; include `'pitch'` in the overrides query and apply
the override state/value. Add the bottom-bar field next to Key, reusing the existing
state markers. `hasPitch` is true when `pitchMidi >= 0` and the state is not
`Unknown`/`NotApplicable`.

- [ ] **Step 4: Run the tests**

Run: `build/tests/SonicatorTests "[smart][display],[ui]"`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add core/smart/SmartTypes.h core/smart/KeyNames.h core/smart/KeyNames.cpp core/database/SampleRepository.cpp ui/MainComponent.cpp tests/TestSmartFoundation.cpp tests/TestUI.cpp
git commit -m "feat(ui): show resolved pitch in the selection bar"
```

---

### Task 8: Hidden real-library evaluation and calibration

**Files:**
- Create: `tests/TestTonalRealSamples.cpp`, add to `tests/CMakeLists.txt`
- Create: `docs/key-pitch-accuracy.md`

**Interfaces:**
- Environment: `SONICATOR_TONAL_EVAL_DIR`, optional `SONICATOR_TONAL_EVAL_LIMIT`.
- Tag: `[.][tonal][real]`.

- [ ] **Step 1: Write the evaluation test**

- Traverse `*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg` recursively.
- Parse a trailing key token after `_`, `-` or whitespace: letter A–G, optional
  `#`/`b`, optional `m|min|minor|maj|major`; bare letters are rejected.
- Normalize enharmonics to pitch classes.
- Call `TonalAnalyzer::analyzeKey` directly on `readAnalysisAudio` output.
- Categorize loop / one-shot / drum / other from the path.
- Deterministic split: `hash(path) % 2` selects calibration vs report.
- Write a CSV with path, category, reference, detected, mode, confidence, best,
  runner-up, tuning, tonality, split.
- Tally: files, labelled, published, correct key, correct tonic, relative/parallel,
  wrong, unknown, coverage, false publications on unlabelled files, wall time.
- Fail only when no file was processed.

- [ ] **Step 2: Run a small calibration sample**

Run:
```bash
SONICATOR_TONAL_EVAL_DIR=/Volumes/1TBSSD/Splice/Splice/Samples/packs \
SONICATOR_TONAL_EVAL_LIMIT=500 \
build/tests/SonicatorTests "[.][tonal][real]"
```
Expected: a tally and a CSV. Record the first confusion patterns.

- [ ] **Step 3: Calibrate thresholds on the calibration half only**

Adjust `minTonality`, `minKeyCorrelation`, `minTonicMargin`, `modeMargin` and the
percussion gate. Re-run the calibration half until the wrong-publication rate is
acceptable. Do not look at the report half while tuning.

- [ ] **Step 4: Run the full evaluation**

Run without `SONICATOR_TONAL_EVAL_LIMIT`. Record the report-half numbers and the
wall time. Write `docs/key-pitch-accuracy.md` with the exact command, machine,
counts and the split. Include the limitation that pack labels are not guaranteed
musically exact.

- [ ] **Step 5: Commit**

```bash
git add tests/TestTonalRealSamples.cpp tests/CMakeLists.txt docs/key-pitch-accuracy.md
git commit -m "test(tonal): evaluate key detection on a labelled library"
```

---

### Task 9: Documentation and verification

**Files:**
- Modify: `docs/SMART_ANALYSIS.md`, `docs/ARCHITECTURE.md`, `docs/phase2-progress.md`
- Modify: `README.md` if it lists analysis limits

- [ ] **Step 1: Update the docs**

Describe the shared decode, key algorithm, pitch algorithm, gates, versions
(`smartDspVersion = 2`), storage/UI, calibration and measured limits. State that
Pitch is bottom-bar only and that drums/non-harmonic hits remain unknown.

- [ ] **Step 2: Run the full default suite**

Run: `cmake --build build -j 8 && ctest --test-dir build --output-on-failure`
Expected: all pass, output pristine. Report any pre-existing failure by name.

- [ ] **Step 3: Update the graph**

Run: `graphify update .`

- [ ] **Step 4: Final commit**

```bash
git add docs/SMART_ANALYSIS.md docs/ARCHITECTURE.md docs/phase2-progress.md README.md
git commit -m "docs(smart): document DSP key and one-shot pitch"
```

---

## Self-review notes

- Spec coverage: shared decode (Task 1), key algorithm and profiles (2–3), pitch (4),
  gates/mapping (5), pipeline/version (6), storage/UI (7), hidden eval/report (8),
  docs/verification (9).
- Type consistency: `readAnalysisAudio`, `TonalAnalyzer::analyzeKey/analyzePitch`,
  `extractTonalCandidates`, `SmartDisplay` pitch fields, `midiNoteName` are used with
  the same names in every task.
- Review Focus tests: drum loop (Task 2/3/5), kick unity 60 (Task 5), metadata key
  wins (Task 5/6), loop pitch (Task 5/6), anti-phase (Task 2/4).
