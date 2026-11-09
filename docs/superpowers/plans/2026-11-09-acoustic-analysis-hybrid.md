# Hybrid Acoustic & Genre Analysis Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement hybrid acoustic classification (DSP + ONNX) and genre/subgenre extraction from path and container metadata for Sonicator.

**Architecture:** Two-stage acoustic classification (AcousticFeatures + FastAcousticRules -> OnnxAudioClassifier) combined with GenreContextExtractor, integrated into SmartAnalysisService and AutoTagRules.

**Tech Stack:** C++20, JUCE DSP, ONNX Runtime C/C++ API, CMake.

**Spec:** docs/superpowers/specs/2026-11-09-acoustic-analysis-design.md

## Global Constraints

- Pure offline operation, runs strictly in background threads of SmartAnalysisService, never blocking UI or audio thread.
- User manual overrides (SmartOverride) take strict precedence over any automatic prediction.
- Bundle size overhead under 20-25 MB (quantized INT8 model + ONNX Runtime).
- Thread-safe inference and buffers for parallel background indexing.

## Review Focus

- Unnamed files (e.g. Audio_01.wav, sample.wav) correctly identified from audio acoustics.
- Name vs audio conflicts (e.g. snare_pad.wav) resolved by acoustics when name token is misleading.
- Empty or corrupted audio handled gracefully without crash or NaN.
- High sample rates (96/192 kHz) resampled accurately for mel-spectrogram.
- Thread-safety across concurrent workers in SmartAnalysisService.

---

### Task 1: Contextual Genre and Subgenre Extractor (core/smart/GenreContextExtractor)

**Files:**
- Create: `core/smart/GenreContextExtractor.h`
- Create: `core/smart/GenreContextExtractor.cpp`
- Create: `tests/TestGenreContext.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  namespace sonicator {
  struct GenreContextResult {
      juce::String genre;
      juce::String subgenre;
      double confidence = 0.0;
      juce::String source; // "path", "metadata", "id3"
  };

  class GenreContextExtractor {
  public:
      static GenreContextResult extract(const juce::File& file, const std::vector<SmartCandidate>& existingCandidates = {});
      static juce::String normalizeGenreToken(const juce::String& token);
      static juce::StringArray canonicalGenres();
  };
  }
  ```

- [ ] **Step 1: Write the failing test in tests/TestGenreContext.cpp**
  Test paths:
  - `/Samples/Drum and Bass/Breaks/break01.wav` -> `genre == "Drum & Bass"`, `confidence >= 0.8`
  - `/Packs/Trap Collection/808s/sub01.wav` -> `subgenre == "Trap"`, `genre == "Hip-Hop"`
  - Candidate with ID3 genre `"Techno"` -> `genre == "Techno"`
- [ ] **Step 2: Run test to verify it fails to compile/run**
  Run: `ctest --test-dir build -R TestGenreContext --output-on-failure`
- [ ] **Step 3: Implement GenreContextExtractor in core/smart/GenreContextExtractor.h and .cpp**
  Canonical dictionary and alias mapping for common styles (Techno, House, Hip-Hop, Trap, Drum & Bass, Dubstep, Ambient, Lo-Fi, Synthwave, Rock, Pop). Parse file path directory tokens from root down to file, checking tokens against aliases.
- [ ] **Step 4: Run test to verify it passes**
  Run: `ctest --test-dir build -R TestGenreContext --output-on-failure`
- [ ] **Step 5: Commit**
  ```bash
  git add core/smart/GenreContextExtractor.h core/smart/GenreContextExtractor.cpp tests/TestGenreContext.cpp core/CMakeLists.txt tests/CMakeLists.txt
  git commit -m "feat(smart): add GenreContextExtractor for path and metadata styles"
  ```

---

### Task 2: High-Precision Acoustic DSP Features (core/dsp/AcousticFeatures)

**Files:**
- Create: `core/dsp/AcousticFeatures.h`
- Create: `core/dsp/AcousticFeatures.cpp`
- Create: `tests/TestAcousticFeatures.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  namespace sonicator {
  struct AcousticDescriptors {
      double logAttackTime = 0.0;     // log10(seconds) from 10% to 90% peak power
      double decayRate = 0.0;         // dB/sec decay slope after peak
      double crestFactor = 0.0;       // peak / RMS
      double temporalCentroid = 0.0;  // normalized 0..1 energy centroid in time
      std::array<double, 5> subBandEnergy {}; // [Sub<80Hz, Low 80-350Hz, Mid 350-2500Hz, High-Mid 2.5-7kHz, Air>7kHz] normalized sum = 1.0
      double zeroCrossingRate = 0.0;
      double spectralFlux = 0.0;
      double pitchClarity = 0.0;      // 0..1 from YIN or harmonic autocorrelation
  };

  class AcousticFeatures {
  public:
      static AcousticDescriptors extract(const juce::AudioBuffer<float>& buffer, double sampleRate);
  };
  }
  ```

- [ ] **Step 1: Write the failing test in tests/TestAcousticFeatures.cpp**
  Generate synthetic buffers:
  - Instant click (fast attack < 2 ms, high crest factor)
  - Slow linear ramp pad (slow attack > 200 ms, low crest factor)
  - 50 Hz pure sine (Sub energy > 85%, high pitch clarity)
  - White noise burst (flat high/air energy, zero pitch clarity)
- [ ] **Step 2: Run test to verify it fails**
  Run: `ctest --test-dir build -R TestAcousticFeatures --output-on-failure`
- [ ] **Step 3: Implement AcousticFeatures in core/dsp/AcousticFeatures.h and .cpp**
  Compute temporal envelope via rolling RMS window; compute sub-band FFT energy integration; compute zero-crossing rate; compute YIN-based pitch clarity.
- [ ] **Step 4: Run test to verify it passes**
  Run: `ctest --test-dir build -R TestAcousticFeatures --output-on-failure`
- [ ] **Step 5: Commit**
  ```bash
  git add core/dsp/AcousticFeatures.h core/dsp/AcousticFeatures.cpp tests/TestAcousticFeatures.cpp core/CMakeLists.txt tests/CMakeLists.txt
  git commit -m "feat(dsp): implement AcousticFeatures descriptor extraction"
  ```

---

### Task 3: Fast Acoustic Deterministic Rules (core/dsp/FastAcousticRules)

**Files:**
- Create: `core/dsp/FastAcousticRules.h`
- Create: `core/dsp/FastAcousticRules.cpp`
- Create: `tests/TestFastAcousticRules.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `AcousticDescriptors` from `core/dsp/AcousticFeatures.h`
- Produces:
  ```cpp
  namespace sonicator {
  struct FastAcousticDecision {
      bool determined = false;
      juce::String canonicalType; // "Kick", "Snare", "Hat", "Cymbal", "Sub", "Percussion"
      double confidence = 0.0;    // 0.0 .. 1.0
      juce::String ruleReason;
  };

  class FastAcousticRules {
  public:
      static FastAcousticDecision evaluate(const AcousticDescriptors& desc, double duration);
  };
  }
  ```

- [ ] **Step 1: Write the failing test in tests/TestFastAcousticRules.cpp**
  Feed mock and synthetic AcousticDescriptors:
  - Kick profile (sub dominant, fast attack, short decay) -> Kick (conf >= 0.85)
  - Snare profile (body in Low-mid + high noise energy, fast attack) -> Snare (conf >= 0.85)
  - Hi-Hat profile (Air dominant, fast attack, short decay) -> Hat (conf >= 0.85)
  - Sub profile (Sub dominant, high pitch clarity, long duration) -> Sub (conf >= 0.85)
  - Ambiguous melodic chord -> determined == false (needs ML)
- [ ] **Step 2: Run test to verify it fails**
  Run: `ctest --test-dir build -R TestFastAcousticRules --output-on-failure`
- [ ] **Step 3: Implement FastAcousticRules in core/dsp/FastAcousticRules.h and .cpp**
- [ ] **Step 4: Run test to verify it passes**
  Run: `ctest --test-dir build -R TestFastAcousticRules --output-on-failure`
- [ ] **Step 5: Commit**
  ```bash
  git add core/dsp/FastAcousticRules.h core/dsp/FastAcousticRules.cpp tests/TestFastAcousticRules.cpp core/CMakeLists.txt tests/CMakeLists.txt
  git commit -m "feat(dsp): add FastAcousticRules for instant drum and sub resolution"
  ```

---

### Task 4: ONNX Runtime C++ Integration & Mel-Spectrogram Preprocessor (core/ml)

**Files:**
- Create: `core/ml/MelSpectrogram.h`
- Create: `core/ml/MelSpectrogram.cpp`
- Create: `core/ml/OnnxAudioClassifier.h`
- Create: `core/ml/OnnxAudioClassifier.cpp`
- Create: `tests/TestOnnxAudioClassifier.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  namespace sonicator {
  class MelSpectrogram {
  public:
      static std::vector<float> computeLogMel(const juce::AudioBuffer<float>& buffer, double sampleRate, int melBands = 64, int fftSize = 1024, int hopSize = 512);
  };

  struct MlPrediction {
      juce::String className;
      float probability = 0.0f;
  };

  class OnnxAudioClassifier {
  public:
      OnnxAudioClassifier();
      ~OnnxAudioClassifier();
      bool loadModel(const juce::File& modelFile);
      bool loadModelFromMemory(const void* data, size_t size);
      bool isLoaded() const;
      std::vector<MlPrediction> classify(const juce::AudioBuffer<float>& buffer, double sampleRate, int topK = 3);
  };
  }
  ```

- [ ] **Step 1: Write test in tests/TestOnnxAudioClassifier.cpp**
  Test MelSpectrogram output dimensions and non-emptiness. Test OnnxAudioClassifier mock/embedded model initialization and dummy buffer prediction output.
- [ ] **Step 2: Run test to verify it fails**
  Run: `ctest --test-dir build -R TestOnnxAudioClassifier --output-on-failure`
- [ ] **Step 3: Implement MelSpectrogram and OnnxAudioClassifier**
  Configure ONNX Runtime C++ headers/library in CMake. Implement mel-filterbank calculation with JUCE FFT. Implement Ort::Session inference.
- [ ] **Step 4: Run test to verify it passes**
  Run: `ctest --test-dir build -R TestOnnxAudioClassifier --output-on-failure`
- [ ] **Step 5: Commit**
  ```bash
  git add core/ml/MelSpectrogram.h core/ml/MelSpectrogram.cpp core/ml/OnnxAudioClassifier.h core/ml/OnnxAudioClassifier.cpp tests/TestOnnxAudioClassifier.cpp CMakeLists.txt tests/CMakeLists.txt
  git commit -m "feat(ml): implement MelSpectrogram and OnnxAudioClassifier engine"
  ```

---

### Task 5: Pipeline Service & SmartCandidate Integration (core/smart/AcousticClassifierService)

**Files:**
- Create: `core/smart/AcousticClassifierService.h`
- Create: `core/smart/AcousticClassifierService.cpp`
- Modify: `core/smart/SmartTypes.h`
- Modify: `core/smart/SmartAnalysisService.cpp`
- Create: `tests/TestAcousticClassifierService.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `FastAcousticRules`, `OnnxAudioClassifier`, `GenreContextExtractor`
- Produces:
  ```cpp
  namespace sonicator {
  class AcousticClassifierService {
  public:
      static std::vector<SmartCandidate> analyzeAudioAndContext(const juce::File& file, const SampleRecord& record, CancelCheck cancel = {});
  };
  }
  ```

- [ ] **Step 1: Write failing test in tests/TestAcousticClassifierService.cpp**
- [ ] **Step 2: Run test to verify it fails**
- [ ] **Step 3: Implement AcousticClassifierService and hook into SmartAnalysisService::collectCandidates**
- [ ] **Step 4: Run test to verify it passes**
- [ ] **Step 5: Commit**
  ```bash
  git add core/smart/AcousticClassifierService.h core/smart/AcousticClassifierService.cpp core/smart/SmartAnalysisService.cpp core/smart/SmartTypes.h tests/TestAcousticClassifierService.cpp CMakeLists.txt tests/CMakeLists.txt
  git commit -m "feat(smart): pipeline acoustic and genre candidates into SmartAnalysisService"
  ```

---

### Task 6: SmartResolver Arbitration & AutoTagRules Updates

**Files:**
- Modify: `core/smart/SmartResolver.cpp`
- Modify: `core/tagging/AutoTagRules.h`
- Modify: `core/tagging/AutoTagRules.cpp`
- Create: `tests/TestAcousticResolver.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Arbitrate SmartField::Type among filename tokens, container metadata, and acoustic classification candidates.
- Update AutoTagRules to propose tags when acoustic evidence is strong even if filename has no tokens.

- [ ] **Step 1: Write failing test in tests/TestAcousticResolver.cpp**
  - Case A: Unnamed file with acoustic candidate -> resolved as acoustic type
  - Case B: Conflict between misleading filename and clear acoustic evidence -> acoustic evidence arbitrates
- [ ] **Step 2: Run test to verify failure**
- [ ] **Step 3: Implement arbitration logic in SmartResolver and AutoTagRules**
- [ ] **Step 4: Run test to verify it passes**
- [ ] **Step 5: Commit**
  ```bash
  git add core/smart/SmartResolver.cpp core/tagging/AutoTagRules.h core/tagging/AutoTagRules.cpp tests/TestAcousticResolver.cpp tests/CMakeLists.txt
  git commit -m "feat(smart): arbitrate acoustic and genre candidates in SmartResolver and AutoTag"
  ```

---

### Task 7: End-to-End Acceptance Tests & Benchmark

**Files:**
- Create: `tests/TestAcousticAcceptance.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write acceptance and benchmark tests in tests/TestAcousticAcceptance.cpp**
  Verify accuracy across real audio samples (Kick, Snare, Hat, Cymbal, Guitar, Bass, Pad) > 90% and average speed < 3 ms per sample.
- [ ] **Step 2: Run test and ensure 100% pass**
  Run: `ctest --test-dir build -R TestAcousticAcceptance --output-on-failure`
- [ ] **Step 3: Commit**
  ```bash
  git add tests/TestAcousticAcceptance.cpp tests/CMakeLists.txt
  git commit -m "test(acceptance): verify end-to-end acoustic classification accuracy and speed"
  ```
