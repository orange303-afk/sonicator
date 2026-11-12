# Kurzweil KRZ Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Kurzweil K2000/K2500/K2600 banks (`.krz`, `.k25`, `.k26`) appear in the library as one row, play their programs from MIDI through the existing external-instrument sampler, and extract their embedded samples to WAV with root key and loop in `smpl`.

**Architecture:** A bounded big-endian parser (`core/kurzweil/KrzFile`) reads header, objects (sample, keymap, program) and the PCM of all parts of a split set. A translator (`core/kurzweil/KrzInstrument`) turns it into the existing `FileInstrument` (+ a new per-program zone list), which `ExternalKitSampler` already plays. Extract gains a third source kind in `EmbeddedSampleSet`; the exporter gains an optional sampler chunk. `Sf2Bank`/`Sf2KitSampler` are not touched.

**Tech Stack:** C++20, JUCE 9.0.5, Catch2 3.7.1, CMake presets.

**Spec:** `docs/superpowers/specs/2026-11-13-kurzweil-krz-design.md` (copied in Task 0 from `/Users/ilyaorange/.opencode/plan/2026-11-13-kurzweil-krz-design.md`). Read it before every task.

## Global Constraints

- Sources read-only; never modify `.krz` files or anything under `/Volumes/1TBSSD/samples`, except creating `/Volumes/1TBSSD/samples/_Kurzweil/Factory` in Task 1.
- Every offset/length is checked against the file size before reading; corrupt input → `false` + human-readable error, never a crash or out-of-bounds read.
- Keymap entry stride comes from the keymap's `entry size` field, never from a table guess.
- PCM is signed 16-bit **big-endian**; keep each sample's own rate (no rounding).
- Velocity levels ppp…fff (0..7) map to MIDI velocity `[level*16, level*16+15]`.
- No Python in the app or in mandatory tests. No ROM dumps anywhere (spec, «Не входит»).
- Existing SF2, EXS24, NKI, tracker, EBL tests stay green with unchanged expectations.
- Build in the release preset only (disk is small): `cmake --build --preset release --target SonicatorTests`; run tests with `~/Library/Caches/sonicator-build/release/tests/SonicatorTests "<tags>"`. Check `df -h /` before the first build; stop and report if free space < 5 GB.
- The repo is in iCloud: never `grep -r`/`rg` from the repo root, never `git log` without `-n`. Before each commit run `git ls-files | grep -E ' [0-9]+\.'` and expect no output (iCloud conflict copies).
- New source files are added to `CMakeLists.txt` next to `core/emu/EblReader.cpp` (line ~85); new tests to `tests/CMakeLists.txt` with `target_sources(SonicatorTests PRIVATE …)`.
- After code changes in each task: `graphify update .` (AGENTS.md).

## Review Focus

1. A `.kr1.krz` whose `.kr2.krz` is missing or truncated: must open, play/extract only fully present samples, and say which part is missing (Task 2 test `missing continuation part`, Task 6 test `extract reports missing part`).
2. A bank whose objects reference ROM samples or keymaps only (33 corpus files, factory archives): must give the «заводские сэмплы» message, not a parse error and not silence (Task 3 test `ROM-only bank`).
3. A forged negative block length / object length pointing past the file or looping on itself: must terminate with an error (Task 2 test `forged block lengths`).
4. A program whose layers point to keymap ids that do not exist in the file: zones dropped, a warning listed, other layers still play (Task 3 test `missing keymap`).
5. Mixed-case and `.k25/.k26` names, and continuation parts named `X.KR2.KRZ`: hidden from the catalog and joined on load (Task 2 `part naming`, Task 5 `continuation parts hidden`).

---

### Task 0: Branch and documents

**Files:**
- Create: `docs/superpowers/specs/2026-11-13-kurzweil-krz-design.md`, `docs/superpowers/plans/2026-11-13-kurzweil-krz.md` (copies of the two files in `/Users/ilyaorange/.opencode/plan/`)

- [ ] **Step 1:** `git switch -c feature/kurzweil-krz` from clean `main`.
- [ ] **Step 2:** Copy both documents; commit `docs: kurzweil krz spec and plan`.

### Task 1: Format notes, corpus probe, factory archives

Research task; its output pins the values later tasks hard-code.

**Files:**
- Create: `docs/KURZWEIL_KRZ.md`
- Create (outside the repo, not committed): a throwaway probe tool in `/tmp/krz-probe/` (C++ or shell + `xxd`; no Python dependency is added to the repo)

- [ ] **Step 1: Collect the layout.** Read Geoffrey Mayer's «KurzFile» notes (search «KurzFile Kurzweil K2000 file format Mayer»; one mirror: https://pdfcoffee.com/kurzfile-4-pdf-free.html) and https://hed0rah.github.io/audio_files_anatomy/krz-anatomy.html. Write into `docs/KURZWEIL_KRZ.md` byte tables for: file header; block/object framing (type/id split for 6-bit types 36/37/38); Sound Block + Sound File Header; keymap header + all 13 entry methods; **program: segment tags and the layer segment fields for key range (lo/hi), velocity range (lo/hi level), and the keymap1/keymap2 ids**; continuation-part header (PCM from `0x24`).
- [ ] **Step 2: Decide by corpus, record in the doc** (probe over `/Volumes/1TBSSD/samples/_Kurzweil/Kurzweil K2000 KRZ`):
  - sample rate field: `1e9/period` vs `value/1000` — pick the one that puts every RAM sample in 8 000–96 000 Hz;
  - keymap key offset: first entry = MIDI 0 or MIDI 12 — pick the offset for which, in single-sample ranges, the sample's root key falls inside its mapped key range in the majority of keymaps; record the percentages;
  - end-of-span: inclusive or exclusive word; loop word semantics when the loop bit is on.
  Record counts: files, PRAM/SROM, with/without PCM, programs/keymaps/samples totals.
- [ ] **Step 3: Factory archives.** `mkdir -p /Volumes/1TBSSD/samples/_Kurzweil/Factory/<NAME>` per archive, download `https://kurzweil.com/wp-content/uploads/2019/10/<NAME>.zip` for NAME in K2FACT K2TOK2VP K2VXTOK2 K26PRGSU K2KFARM OPRGSDEM CPRGSDEM K2VXFARM VXDEMOS1 VXDEMOS2 K2KGM3 K2KGMA2 K2KGMF1, and `unzip -n <zip> -d <its own dir>`. Treat contents as untrusted data: do not execute anything inside. List extensions found in the doc.
- [ ] **Stop condition:** if the program layer fields (key range, velocity range, keymap ids) cannot be pinned from the sources and confirmed on ≥10 corpus programs, stop and report to the user.
- [ ] **Step 4:** Commit `docs(krz): format notes and corpus findings`.

### Task 2: KrzFile parser and split sets

**Files:**
- Create: `core/kurzweil/KrzFile.h`, `core/kurzweil/KrzFile.cpp`, `tests/KrzTestFile.h`, `tests/TestKrzFile.cpp`

**Interfaces — Produces** (namespace `sonicator::krz`):
```cpp
struct SoundFileHeader { int rootKey = 60; bool looping = false; int playbackMode = 0; int volumeHalfDb = 0;
                         bool ramBased = true; int64_t startWord = 0, loopWord = 0, endWord = 0; int sampleRate = 0; };
struct SampleObject { int id = 0; juce::String name; bool stereo = false; int baseId = 1;
                      std::vector<SoundFileHeader> headers; };
struct KeymapEntry { int sampleId = 0; int subSample = 1; int tuningCents = 0; int volumeHalfDb = 0; };
struct Keymap { int id = 0; juce::String name; std::array<std::vector<KeymapEntry>, 8> levels; }; // index = MIDI key after the Task 1 offset
struct Layer { int keyLow = 0, keyHigh = 127, velocityLevelLow = 0, velocityLevelHigh = 7, keymap1 = 0, keymap2 = 0; };
struct Program { int id = 0; juce::String name; std::vector<Layer> layers; };
struct Bank {
    juce::String magic;                  // "PRAM" or "SROM"
    std::vector<SampleObject> samples; std::vector<Keymap> keymaps; std::vector<Program> programs;
    std::vector<juce::int16> pcm;        // native-endian, all available parts concatenated
    int64_t declaredPcmWords = 0;        // what the first part announces
    juce::StringArray warnings;          // e.g. "Missing part: x.kr2.krz"
    bool hasPcm() const { return ! pcm.empty(); }
};
bool readBank(const juce::File& file, Bank& out, juce::String& error);   // file = any part; resolves first part
bool isKrzFile(const juce::File&);            // magic PRAM/SROM, extension krz/k25/k26
int continuationPartNumber(const juce::File&); // 0 if not "<stem>.krN.<krz|k25|k26>" (case-insensitive), else N
juce::File firstPartOf(const juce::File&);
SampleAudioPtr decodeHeader(const Bank&, const SampleObject&, int headerIndex, juce::String& error); // mono, loop in frames
SampleAudioPtr decodeStereo(const Bank&, const SampleObject&, juce::String& error);                 // headers[0]=L, [1]=R
```
Max file size 256 MB (as `Sf2Bank::maxFileSize`). Unknown object types skipped by their length.

- [ ] **Step 1: Fixture writer** `tests/KrzTestFile.h` (namespace `krztest`, modelled on `tests/EblTestFile.h`): builds PRAM bytes from `{samples, keymaps (method, entry size), programs (layers), pcm}`, and `makeContinuation(pcmTail)`; layouts exactly as recorded in `docs/KURZWEIL_KRZ.md`.
- [ ] **Step 2: Failing tests** in `TestKrzFile.cpp`, tag `[krz]`:
  - `mono looped sample`: rootKey, rate, `looping`, decoded frames and first/last PCM values equal fixture (BE→native).
  - `stereo sample`: `decodeStereo` gives 2 channels, L/R equal fixture.
  - `multisample`: 3 headers, `decodeHeader(…, 2)` returns the third span.
  - `keymap methods`: methods `0x03`, `0x13`, `0x0F` give the same `sampleId`/`subSample`; `0x13` tuning −250 cents; `0x0F` volume −6 → `volumeHalfDb == -6`.
  - `velocity levels`: keymap with ppp–mf → sample 200, f–fff → sample 201 lands in `levels[0..4]` / `levels[5..7]`.
  - `program layers`: two layers with key ranges 36–59 / 60–96 and keymap ids read back.
  - `split set`: `x.kr1.krz` + `x.kr2.krz` → `pcm.size() == declaredPcmWords`, sample spanning the boundary decodes equal to fixture; opening `x.kr2.krz` resolves to the same bank.
  - `missing continuation part`: only `.kr1` → `readBank` true, warning names `x.kr2.krz`, sample past available PCM → `decodeHeader` false with error.
  - `part naming`: `continuationPartNumber("A.KR2.KRZ") == 2`, `("a.kr1.k25") == 1`, `("a.krz") == 0`, `("akr2.krz") == 0`.
  - `no PCM bank`: `osize == file size` → true, `hasPcm()` false.
  - `forged block lengths` and rejects: not PRAM/SROM; osize past EOF; block length past EOF; positive non-zero block length; object length past its block; sample span past PCM → each false/err with non-empty error, no crash.
- [ ] **Step 3:** Run `SonicatorTests "[krz]"` → FAIL (not built).
- [ ] **Step 4:** Implement `KrzFile.cpp` per the interface; add to CMake.
- [ ] **Step 5:** Run `[krz]` → PASS. Commit `feat(krz): parse Kurzweil banks and split sets`.

### Task 3: Banks as FileInstrument programs

**Files:**
- Modify: `core/sampler/FileInstrument.h`, `core/sampler/FileInstrument.cpp`
- Create: `core/kurzweil/KrzInstrument.h`, `core/kurzweil/KrzInstrument.cpp`, `tests/TestKrzInstrument.cpp`

**Interfaces:**
- Consumes: Task 2 `readBank`, `decodeHeader`.
- Produces:
```cpp
enum class FileInstrumentFormat { exs24, nki, krz };
struct InstrumentProgram { juce::String name; int number = 0; std::vector<ExternalZone> zones; };
// in FileInstrument:
std::vector<InstrumentProgram> programs;   // empty for EXS24/NKI
int currentProgram = -1;
bool selectInstrumentProgram(FileInstrument&, int index, juce::String& error); // copies zones, refreshes sources[i].zones
bool loadKrzInstrument(const juce::File&, FileInstrument&, juce::String& error);
```
`loadFileInstrument(file, FileInstrumentFormat::krz, …)` dispatches to `loadKrzInstrument`.

Translation rules (spec goal 2): one mono source per Sound File Header with PCM (`SampleDescriptor` named `<object name>` or `<object name> <n>` for multisamples, `<name> L/R` for stereo); zones = keymap keys × layer key range × velocity levels; stereo object or `keymap2` → two zones with `pan` −1/+1; `rootKey`, `tuningCents` (entry + header), `attenuationDb = -(entry+header volumeHalfDb)/2`, loop from header. Programs with zero zones are omitted; fallback presets per spec «Решения исполнителя» (keymaps, then samples). `currentProgram` = first program.

- [ ] **Step 1: Failing tests** `[krz][sampler]`:
  - `program to zones`: two-layer program → zones only in 36–96, velocity level ranges `[0,79]`/`[80,127]`, root/tuning/attenuation as computed above.
  - `program switch`: bank with 2 programs; `selectInstrumentProgram(…,1)` replaces zones; index 5 → false.
  - `renders at keymap pitch`: `ExternalKitSampler` loaded from a KRZ instrument, note = root+12 produces a period half of note = root (count zero crossings of a sine fixture at 44.1 kHz render); note outside layer → silence; looped sample still sounding 2 s after note-on.
  - `stereo zones`: stereo object → two zones pan −1/+1, same ranges.
  - `missing keymap`: layer pointing to absent keymap → that layer dropped, `warnings` names it, other layer plays.
  - `ROM-only bank`: no PCM → `loadKrzInstrument` false with error exactly `"This bank uses the K2000 factory ROM samples; the file contains no samples of its own"`.
  - `no programs`: keymaps only → one program per keymap; samples only → one program per sample over 0–127.
- [ ] **Step 2:** Run `[krz][sampler]` → FAIL.
- [ ] **Step 3:** Implement; keep EXS/NKI paths unchanged (`programs` empty).
- [ ] **Step 4:** Run `[krz]` and `[sampler]` → PASS (EXS/NKI regressions included). Commit `feat(krz): play K2000 programs through the external sampler`.

### Task 4: Preview and program switcher

**Files:**
- Modify: `core/audio/AudioEngine.h`, `core/audio/AudioEngine.cpp` (`:162` extension dispatch, `previewFileInstrument` `:355`), `ui/preview/PreviewPanel.cpp` (`presetBox`, `:113-120`, `:300-345`)
- Test: `tests/TestKrzPreview.cpp`

**Interfaces — Produces** (AudioEngine, valid in external mode, programs empty → count 0):
```cpp
int instrumentProgramCount() const;
juce::String instrumentProgramLabel(int index) const; // "<number>  <name>"
int instrumentProgramIndex() const;
bool selectInstrumentProgram(int index);              // reloads ExternalKitSampler, bumps externalGeneration
```
`.krz;.k25;.k26` → `previewFileInstrument` with `FileInstrumentFormat::krz`; error text from Task 3 surfaces as `lastError`.

- [ ] **Step 1: Failing tests** `[krz][preview]`: engine opens a fixture `.krz` → `isExternalInstrumentMode()`, MIDI preview on, `instrumentProgramCount() == 2`, label `"200  Test Prog"`; `selectInstrumentProgram(1)` bumps `instrumentGeneration()`; ROM-only fixture → open false, `lastError` equals the Task 3 message; `PreviewPanel` with a 2-program bank shows `presetBox` with 2 items, with an EXS fixture hides it.
- [ ] **Step 2:** FAIL → implement → PASS (`[krz]`, `[sampler]`, `[ui]`). Commit `feat(krz): preview banks with a program switcher`.

### Task 5: Catalog rows

**Files:**
- Modify: `core/scanner/FileScanner.cpp` (`:10`, `:14`), `core/smart/AssetAnalyzer.cpp` (format branch next to `"SF2"`)
- Test: `tests/TestKrzCatalog.cpp`

- [ ] **Step 1: Failing tests** `[krz][catalog]`: `isMetadataOnlyFormat` true for `.krz/.k25/.k26`; `isSupportedFormat("x.kr2.krz")` and `("X.KR3.K25")` false, `("x.kr1.krz")` true (**continuation parts hidden**); `analyzeMetadataAsset` on fixture → format `"KRZ"`, compatibility supported; on a renamed WAV `.krz` → invalid with error `"Not a Kurzweil K2000/K2500/K2600 bank"`; indexing a temp folder with `x.kr1.krz`, `x.kr2.krz`, `y.krz` on a temp database gives 2 rows.
- [ ] **Step 2:** FAIL → implement (use `continuationPartNumber >= 2` and `isKrzFile`) → PASS. Commit `feat(krz): index Kurzweil banks as library rows`.

### Task 6: Extract with smpl

**Files:**
- Modify: `core/sampler/SamplerTypes.h`, `core/export/AudioExporter.h/.cpp` (`exportSampleFile` `:349`), `core/extract/EmbeddedSamples.h/.cpp`, `core/extract/ExtractService.cpp` (`:304`)
- Test: `tests/TestKrzExtract.cpp`

**Interfaces — Produces:**
```cpp
struct SamplerChunk { int rootKey = 60; bool looping = false; int64_t loopStart = 0, loopEndExclusive = 0; }; // SamplerTypes.h
ExportResult exportSampleFile(const SampleAudio&, const juce::File&, bool normalize,
                              CancelCheck cancel = {}, const SamplerChunk* sampler = nullptr) const;
std::optional<SamplerChunk> EmbeddedSampleSet::samplerChunk(int index) const; // KRZ only; nullopt for SF2/trackers
```
WAV metadata keys as in `core/export/ExternalFileDrag.cpp:21-26` plus `"MidiUnityNote"`; `Loop0End = loopEndExclusive - 1`. `isEmbeddedSampleSource` adds `krz;k25;k26` (context menu and drop then work through `embeddedSources`). Entries: every sample object with PCM in file order; stereo flag → one entry; multisample → one per header; ROM or missing-part spans → `empties` and a problem line `"<name>: skipped, needs missing part x.kr2.krz"` / `"<name>: factory ROM sample"`.

- [ ] **Step 1: Failing tests** `[krz][extract]`: extract fixture bank to a temp folder → expected WAV count and names `001 Guitar E1.wav`…; PCM equals `decodeHeader`; stereo object → one 2-channel WAV; looped sample WAV re-read with `WavAudioFormat` has `MidiUnityNote` = root and `Loop0Start/End`; SF2 extraction of `tests/Sf2TestBank.h` fixture writes no `smpl` (unchanged); `extract reports missing part`: `.kr1` alone → problems list names `x.kr2.krz`; `embeddedSources` keeps `.krz`, drops `.wav`.
- [ ] **Step 2:** FAIL → implement → PASS (`[krz]`, `[extract]`, `[export]`). Commit `feat(krz): extract bank samples with root key and loop`.

### Task 7: Real corpora

**Files:**
- Create: `tests/TestKrzCorpus.cpp`

- [ ] **Step 1:** Tests `[.krz-corpus]` (`SONICATOR_KRZ_DIR`) and `[.krz-factory]` (`SONICATOR_KRZ_FACTORY_DIR`), skip when unset (pattern: `tests/TestEblCorpus.cpp:99`). For every non-continuation file: `readBank` ok or explained; for banks with PCM: every RAM span inside PCM, every rate in 8 000–96 000, `loadKrzInstrument` ok with ≥1 program or explained; print one line per failure with reason and a summary (files, ok, rom-only, explained, unexplained). Assert unexplained == 0.
- [ ] **Step 2:** Run with `SONICATOR_KRZ_DIR="/Volumes/1TBSSD/samples/_Kurzweil/Kurzweil K2000 KRZ"` and `SONICATOR_KRZ_FACTORY_DIR=/Volumes/1TBSSD/samples/_Kurzweil/Factory`. Expected corpus summary: 238 files seen, 23 parts merged into their sets, 0 unexplained.
- [ ] **Stop conditions** (spec): unexplained failures; no rate interpretation fits all; need to change EXS/NKI/SF2 expectations → stop and report.
- [ ] **Step 3:** Put the summaries into `docs/KURZWEIL_KRZ.md`. Commit `test(krz): real and factory corpus checks`.

### Task 8: Docs, notices, finish

**Files:**
- Modify: `docs/KURZWEIL_KRZ.md`, `THIRD_PARTY_NOTICES.md` (format knowledge from G. Mayer «KurzFile» and hed0rah «krz anatomy»; no code copied), `docs/ARCHITECTURE.md` (one paragraph: `core/kurzweil`), `~/Documents/orange303/Sonicator Features.md` line 19 → append `✅ сделано (<commit>)`.

- [ ] **Step 1:** Full suite: `ctest --preset release` → all green; then `graphify update .`.
- [ ] **Step 2:** Leave for the user's manual check (do not claim it done): in the app, scan `_Kurzweil`, play `acguit`, `b3`, `akaifunk`, `basvoice.kr1`, `chorus` from MIDI on correct pitch, switch programs, Extract one bank and compare by ear.
- [ ] **Step 3:** Commit `docs: kurzweil krz`; iCloud duplicate check; merge to `main` (fast-forward), re-run `ctest --preset release` on main, push, delete the branch.
