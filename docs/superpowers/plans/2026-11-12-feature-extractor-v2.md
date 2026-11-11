# Stage 3.2: `FeatureExtractorV2` and the feature CLI — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Считать богатое спектральное описание звука одним C++-кодом и уметь выгружать его в JSONL для обучения на открытых датасетах.

**Architecture:** Новый класс `FeatureExtractorV2` рядом с `AcousticFeatures`: один проход STFT по кадрам 2048/512, из него — группы признаков, все в 0..1, фиксированный порядок. Анализ ограничен полосой 40–8000 Гц, поэтому вектор одинаков для источника 16 кГц (NSynth, One-Shot) и 44,1 кГц (FSDK50, библиотека владельца). Тонкий CLI `sonicator-features` обходит папку и пишет JSONL; сериализация живёт в ядре и тестируется Catch2.

**Tech Stack:** C++20, JUCE (`juce_dsp` FFT, `juce_audio_formats`), Catch2 3.7.1, CMake 3.22, Ninja. Сборка — `~/Library/Caches/sonicator-build/release` (вне iCloud).

**Spec:** `docs/superpowers/specs/2026-11-12-feature-schema-2-public-datasets-design.md` (части 6, 7.1–7.2)

## Global Constraints

- Все значения — в 0..1, `NaN`/`inf` недопустимы; тишина даёт нулевой вектор, а не отказ.
- Детерминизм: одинаковый буфер даёт одинаковый вектор; порядок кадров и полос фиксирован.
- Анализ всегда в полосе 40–8000 Гц; частота дискретизации источника на значения не влияет
  (тест: 16/44,1/48 кГц, расхождение ≤ 0.02).
- Усиление ±12 дБ не меняет вектор (нормировка по пику перед анализом); `i16` RMS-признак схемы 1
  остаётся как есть.
- Порядок измерений фиксирован и объявлен в заголовке; `FeatureExtractorV2::dimensions()` = 84.
- C++ не читает сеть и не пишет файлы: чтение — `readAnalysisAudio`, запись — CLI.
- Пороговые константы (полосы, число мел-фильтров) — только в одном месте (`FeatureExtractorV2.cpp`),
  не дублируются в тестах числами.
- `featureSchema 1` и все существующие теги/прототипы не меняются.

## Review Focus

- Тишина и файл короче одного кадра FFT: вектор из нулей, без деления на ноль и без чтения за границей.
- Моно-файл: стерео-признаки дают 0 и 1, а не мусор из второго канала.
- Клип длиной 20 мс (один удар): все группы считаются по одному-двум кадрам, без исключений и без NaN.
- `NaN`/`inf` во входном буфере: отказ с пустым вектором и сообщением, а не распространение в файл.
- Сэмпл, у которого весь спектр выше 8 кГц (хай-хэт из 44,1 кГц): вектор не должен стать пустым или
  однородным до неразличимости с соседним классом — в тесте участвует хэт 6–10 кГц.

---

### Task 1: Скелет, раскладка и первый блок признаков

**Files:**
- Create: `core/dsp/FeatureExtractorV2.h`
- Create: `core/dsp/FeatureExtractorV2.cpp`
- Modify: `CMakeLists.txt` (SonicatorCore sources)
- Create: `tests/TestFeatureExtractorV2.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `AcousticFeatures::extract`, `AnalysisAudio`.
- Produces:
  ```cpp
  namespace sonicator {
  struct FeatureVectorV2 {
      std::vector<float> values;   // dimensions() значений в 0..1
      juce::String error;          // пусто при успехе
      bool ok() const { return error.isEmpty() && !values.empty(); }
  };
  class FeatureExtractorV2 {
  public:
      static constexpr int dimensions() { return 84; }
      static constexpr int version() { return 2; }
      static FeatureVectorV2 extract(const juce::AudioBuffer<float>&, double sampleRate);
      static juce::StringArray dimensionNames(); // 84 имени, порядок = порядок значений
  };
  }
  ```
- Раскладка: 0–13 блок схемы 1 (attack, decay, crest, temporal centroid, доли 5 полос
  `<80, 80–350, 350–2500, 2500–7000, 7000–8000` Гц, ZCR, flux, pitch clarity, длительность/30, RMS×2);
  14–31 форма спектра (9 статистик × mean/std); 32–37 контраст 6 полос; 38–63 MFCC 1–13 (mean и std);
  64–75 огибающие 4 полос (attack, decay, ранняя доля); 76–77 гармоническое/перкуссионное;
  78–81 модуляция; 82–83 стерео.

- [ ] **Step 1: Написать падающий тест на раскладку и блок 0–13**

```cpp
// tests/TestFeatureExtractorV2.cpp
#include "core/dsp/FeatureExtractorV2.h"
#include "core/ml/AdaptiveClassifier.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace sonicator;

static juce::AudioBuffer<float> makeSine(double sampleRate, double frequency, double seconds, float gain = 0.5f)
{
    const int samples = static_cast<int>(sampleRate * seconds);
    juce::AudioBuffer<float> buffer(1, samples);
    buffer.clear();
    for (int i = 0; i < samples; ++i)
        buffer.setSample(0, i, gain * std::sin(static_cast<float>(2.0 * juce::MathConstants<double>::pi * frequency * i / sampleRate)));
    return buffer;
}

TEST_CASE("FeatureExtractorV2 layout is fixed and declared", "[dsp][features]")
{
    REQUIRE(FeatureExtractorV2::dimensions() == 84);
    REQUIRE(FeatureExtractorV2::dimensionNames().size() == 84);
    REQUIRE(FeatureExtractorV2::dimensionNames()[0] == "attack");
    REQUIRE(FeatureExtractorV2::dimensionNames()[83] == "stereo_correlation");
}

TEST_CASE("FeatureExtractorV2 keeps the schema-1 block below 7 kHz", "[dsp][features]")
{
    const auto buffer = makeSine(44100.0, 1000.0, 0.5);
    const auto vector = FeatureExtractorV2::extract(buffer, 44100.0);
    REQUIRE(vector.ok());
    REQUIRE(vector.values.size() == 84);
    const auto legacy = AdaptiveClassifier::extractFeatures(buffer, 44100.0);
    REQUIRE(legacy.size() == 14);
    for (int i = 0; i < 13; ++i)
        REQUIRE(vector.values[static_cast<size_t>(i)] == Catch::Approx(legacy[static_cast<size_t>(i)]).margin(0.02));
    for (float value : vector.values)
        REQUIRE((value >= 0.0f && value <= 1.0f));
}
```

- [ ] **Step 2: Собрать и увидеть падение**

Run:
```bash
ninja -C ~/Library/Caches/sonicator-build/release SonicatorTests
```
Expected: FAIL на компиляции — `core/dsp/FeatureExtractorV2.h` не найден.

- [ ] **Step 3: Создать заголовок и минимальную реализацию**

Заголовок — структуры и объявление из блока Interfaces с комментарием-таблицей раскладки. В `.cpp`:
один проход STFT (`fftSize 2048`, `hop 512`, Hann, окно по каждому каналу, сумма магнитуд каналов),
пик-нормировка буфера в копии перед анализом, доли 5 полос по этим кадрам, остальные значения —
из `AcousticFeatures::extract`, нормировки как в `AdaptiveClassifier::extractFeatures`. Остальные группы
на этом шаге заполняются нулями, чтобы тест раскладки проходил. В пустой/тихий буфер возвращается
`values` из 84 нулей и пустой `error`; при `NaN` во входе — `error = "non-finite sample"`.

- [ ] **Step 4: Прогнать тесты**

Run: `cd ~/Library/Caches/sonicator-build/release && ./tests/SonicatorTests "[features]"`
Expected: PASS, 2 теста.

- [ ] **Step 5: Коммит**

```bash
git add core/dsp/FeatureExtractorV2.* CMakeLists.txt tests/TestFeatureExtractorV2.cpp tests/CMakeLists.txt
git commit -m "feat(dsp): feature vector v2 skeleton with the schema-1 block"
```

---

### Task 2: Форма спектра, контраст и MFCC

**Files:**
- Modify: `core/dsp/FeatureExtractorV2.cpp`
- Modify: `tests/TestFeatureExtractorV2.cpp`

**Interfaces:**
- Consumes: кадровые магнитуды из Task 1.
- Produces: значения 14–63 по раскладке; ничего нового наружу.

- [ ] **Step 1: Написать тесты на смысл значений**

```cpp
TEST_CASE("FeatureExtractorV2 separates bright noise from a low sine", "[dsp][features]")
{
    const auto sine = FeatureExtractorV2::extract(makeSine(44100.0, 200.0, 0.5), 44100.0);
    const juce::AudioBuffer<float> noise = makeNoise(44100.0, 0.5); // белый шум, см. helper ниже
    const auto noisy = FeatureExtractorV2::extract(noise, 44100.0);
    const auto centre = [](const FeatureVectorV2& v) { return v.values[14]; }; // centroid, mean
    REQUIRE(centre(sine) < 0.2f);
    REQUIRE(centre(noisy) > centre(sine) + 0.2f);
    // flatness: у шума выше, чем у синуса
    REQUIRE(noisy.values[28] > sine.values[28]); // flatness, mean
    // MFCC c1 у шума близок к нулю, у синуса заметно отличается от нуля
    REQUIRE(std::abs(noisy.values[38]) < 0.2f);
    REQUIRE(std::abs(sine.values[38]) > 0.2f);
}

TEST_CASE("FeatureExtractorV2 contrast follows a rising spectrum", "[dsp][features]")
{
    const auto low = FeatureExtractorV2::extract(makeSine(44100.0, 100.0, 0.5), 44100.0);
    const auto high = FeatureExtractorV2::extract(makeSine(44100.0, 5000.0, 0.5), 44100.0);
    REQUIRE(high.values[37] > low.values[37]); // контраст верхней полосы
    REQUIRE(low.values[32] > high.values[32]); // контраст нижней полосы
}
```

Дописать `makeNoise(sampleRate, seconds)` в helpers: детерминированный ГПСЧ (seed 1234), амплитуда 0.3.

- [ ] **Step 2: Собрать и увидеть падение**

Run: `ninja -C ~/Library/Caches/sonicator-build/release SonicatorTests && cd ~/Library/Caches/sonicator-build/release && ./tests/SonicatorTests "[features]"`
Expected: FAIL — значения на местах 14…63 равны нулю.

- [ ] **Step 3: Реализовать**

Форма спектра считается по кадрам в полосе 40–8000 Гц: центроид, разброс, асимметрия, эксцесс,
rolloff 85% и 95%, плоскостность, энтропия, наклон (линейная регрессия по лог-магнитуде против бина) —
среднее и стандартное отклонение по кадрам, каждый со своей фиксированной нормировкой в 0..1.
Контраст — 6 полос с центрами 100/300/800/2000/4000/7000 Гц, октавной ширины: `(пик−впадина в дБ)/60`.
Мел: 40 треугольных фильтров в 40–8000 Гц, лог, DCT-II, коэффициенты 1..13, среднее и σ по кадрам.

- [ ] **Step 4: Прогнать тесты**

Run: `cd ~/Library/Caches/sonicator-build/release && ./tests/SonicatorTests "[features]"`
Expected: PASS.

- [ ] **Step 5: Коммит**

```bash
git add core/dsp/FeatureExtractorV2.cpp tests/TestFeatureExtractorV2.cpp
git commit -m "feat(dsp): spectral shape, band contrast and MFCC in feature vector v2"
```

---

### Task 3: Огибающие по полосам, гармоническое/перкуссионное, модуляция и стерео

**Files:**
- Modify: `core/dsp/FeatureExtractorV2.cpp`
- Modify: `tests/TestFeatureExtractorV2.cpp`

**Interfaces:**
- Consumes: кадровые магнитуды и огибающую из Task 1–2.
- Produces: значения 64–83.

- [ ] **Step 1: Написать тесты**

```cpp
TEST_CASE("FeatureExtractorV2 sees a pulse train as periodic onsets", "[dsp][features]")
{
    const auto pulses = makePulseTrain(44100.0, 4.0 /*Гц*/, 2.0 /*с*/, 1000.0 /*Гц*/);
    const auto vector = FeatureExtractorV2::extract(pulses, 44100.0);
    REQUIRE(vector.values[78] > 0.25f); // onset density: 4/с при нормировке на 10/с
    REQUIRE(vector.values[79] > 0.3f);  // периодичность огибающей
    const auto pad = FeatureExtractorV2::extract(makeSine(44100.0, 440.0, 2.0), 44100.0);
    REQUIRE(pad.values[78] < 0.1f);
    REQUIRE(pad.values[79] < vector.values[79]);
}

TEST_CASE("FeatureExtractorV2 keeps mono stereo features at their defaults", "[dsp][features]")
{
    const auto vector = FeatureExtractorV2::extract(makeSine(44100.0, 440.0, 0.5), 44100.0);
    REQUIRE(vector.values[82] == Catch::Approx(0.0f));   // ширина
    REQUIRE(vector.values[83] == Catch::Approx(1.0f));   // корреляция каналов
}

TEST_CASE("FeatureExtractorV2 separates a tonal pad from a noise burst by harmonic ratio", "[dsp][features]")
{
    const auto pad = FeatureExtractorV2::extract(makeSine(44100.0, 440.0, 1.0), 44100.0);
    const auto burst = FeatureExtractorV2::extract(makeNoise(44100.0, 1.0), 44100.0);
    REQUIRE(pad.values[76] > burst.values[76] + 0.2f); // гармоническая доля
    REQUIRE(burst.values[77] > pad.values[77] + 0.1f); // перкуссионная доля
}
```

`makePulseTrain(sampleRate, rateHz, seconds, toneHz)` — гейтированный синус, 25 мс импульс, детерминированно.

- [ ] **Step 2: Собрать и увидеть падение**

Run: `ninja -C ~/Library/Caches/sonicator-build/release SonicatorTests && cd ~/Library/Caches/sonicator-build/release && ./tests/SonicatorTests "[features]"`
Expected: FAIL на значениях 64–83.

- [ ] **Step 3: Реализовать**

Огибающие: энергия кадров в 4 полосах, лог-атака (10→90% пика), спад (дБ/с после пика), доля энергии
в первые 50 мс. Гармоническая доля: энергия в пределах ±3% от найденного основного тона и его
гармоник, делённая на полную (основной тон берётся из `AcousticDescriptors::pitchClarity`-окна —
переиспользовать найденный YIN-минимум, не считая второй раз). Перкуссионная доля — средний
положительный прирост спектрального потока, нормированный. Модуляция: число пиков огибающей в секунду
(÷10), автокорреляция огибающей (максимум на лагах 80–1000 мс), средняя сила онсетов, периодичность
низкой полосы. Стерео: ширина `RMS(L−R)/RMS(L+R)`, корреляция Пирсона каналов; для моно — 0 и 1.

- [ ] **Step 4: Прогнать тесты**

Run: `cd ~/Library/Caches/sonicator-build/release && ./tests/SonicatorTests "[features]"`
Expected: PASS.

- [ ] **Step 5: Коммит**

```bash
git add core/dsp/FeatureExtractorV2.cpp tests/TestFeatureExtractorV2.cpp
git commit -m "feat(dsp): band envelopes, harmonic ratio, modulation and stereo in feature vector v2"
```

---

### Task 4: Инвариантность, устойчивость и сериализация

**Files:**
- Modify: `core/dsp/FeatureExtractorV2.cpp`
- Create: `core/dsp/FeatureJson.h`
- Create: `core/dsp/FeatureJson.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/TestFeatureExtractorV2.cpp`
- Create: `tests/TestFeatureJson.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  namespace sonicator {
  juce::String featureJsonLine(const FeatureVectorV2&, const juce::String& path,
                               double sampleRate, double durationSeconds, double bandwidthHz);
  }
  ```
  Одна строка JSON без перевода строки в конце: `path`, `features` (84 числа, 3 знака), `featureSchema`
  (2), `dimensions`, `sampleRate`, `durationSec`, `bandwidthHz`.

- [ ] **Step 1: Написать тесты**

```cpp
TEST_CASE("FeatureExtractorV2 is gain invariant within 12 dB", "[dsp][features]")
{
    const auto quiet = FeatureExtractorV2::extract(makeSine(44100.0, 440.0, 0.5, 0.1f), 44100.0);
    const auto loud = FeatureExtractorV2::extract(makeSine(44100.0, 440.0, 0.5, 0.4f), 44100.0);
    for (size_t i = 0; i < quiet.values.size(); ++i)
        if (i != 13) // RMS-признак схемы 1 зависит от уровня по смыслу
            REQUIRE(quiet.values[i] == Catch::Approx(loud.values[i]).margin(0.02));
}

TEST_CASE("FeatureExtractorV2 is sample-rate invariant inside the analysis band", "[dsp][features]")
{
    const auto a = FeatureExtractorV2::extract(makeSine(16000.0, 440.0, 0.5), 16000.0);
    const auto b = FeatureExtractorV2::extract(makeSine(48000.0, 440.0, 0.5), 48000.0);
    for (size_t i = 0; i < a.values.size(); ++i)
        if (i != 13)
            REQUIRE(a.values[i] == Catch::Approx(b.values[i]).margin(0.02));
}

TEST_CASE("FeatureExtractorV2 reports a non-finite buffer instead of NaN", "[dsp][features]")
{
    auto buffer = makeSine(44100.0, 440.0, 0.1);
    buffer.setSample(0, 10, std::numeric_limits<float>::quiet_NaN());
    const auto vector = FeatureExtractorV2::extract(buffer, 44100.0);
    REQUIRE_FALSE(vector.ok());
    REQUIRE(vector.error.isNotEmpty());
}

TEST_CASE("FeatureExtractorV2 treats silence as a zero vector", "[dsp][features]")
{
    juce::AudioBuffer<float> silence(2, 4410);
    silence.clear();
    const auto vector = FeatureExtractorV2::extract(silence, 44100.0);
    REQUIRE(vector.ok());
    for (float value : vector.values) REQUIRE(value == 0.0f);
}
```

```cpp
// tests/TestFeatureJson.cpp
TEST_CASE("featureJsonLine is one deterministic JSON line", "[dsp][features][json]")
{
    FeatureVectorV2 vector;
    vector.values.assign(84, 0.25f);
    const auto line = featureJsonLine(vector, "/tmp/a.wav", 44100.0, 1.5, 22050.0);
    REQUIRE(line.startsWith("{"));
    REQUIRE_FALSE(line.contains("\n"));
    REQUIRE(line.contains("\"featureSchema\":2"));
    REQUIRE(line.contains("\"dimensions\":84"));
    REQUIRE(line.contains("\"durationSec\":1.5"));
    REQUIRE(line == featureJsonLine(vector, "/tmp/a.wav", 44100.0, 1.5, 22050.0));
}
```

- [ ] **Step 2: Собрать и увидеть падение**

Run: `ninja -C ~/Library/Caches/sonicator-build/release SonicatorTests && cd ~/Library/Caches/sonicator-build/release && ./tests/SonicatorTests "[features]"`
Expected: FAIL на инвариантности, на `featureJsonLine`.

- [ ] **Step 3: Реализовать пик-нормировку, отказ на `NaN`, сериализацию**

Нормировка: копия буфера делится на пик (если `peak > 1e-5`), анализ идёт по ней; RMS-признак
считается от исходного уровня. `NaN` проверяется до нормировки. `featureJsonLine` собирает строку
вручную (`juce::String`), числа — `juce::String(value, 3)`; экранирование пути — `juce::JSON::toString`
для строки пути.

- [ ] **Step 4: Прогнать тесты**

Run: `cd ~/Library/Caches/sonicator-build/release && ./tests/SonicatorTests "[features]"`
Expected: PASS.

- [ ] **Step 5: Коммит**

```bash
git add core/dsp/FeatureExtractorV2.cpp core/dsp/FeatureJson.* CMakeLists.txt tests/TestFeatureExtractorV2.cpp tests/TestFeatureJson.cpp tests/CMakeLists.txt
git commit -m "feat(dsp): gain and sample-rate invariants, JSON lines for feature vectors"
```

---

### Task 5: CLI `sonicator-features`

**Files:**
- Create: `tools/sonicator-features/main.cpp`
- Create: `tools/sonicator-features/CMakeLists.txt`
- Modify: `CMakeLists.txt` (опция `SONICATOR_BUILD_TRAINING_TOOLS`, OFF по умолчанию)
- Modify: `services/training/README.md`

**Interfaces:**
- Consumes: `readAnalysisAudio`, `FeatureExtractorV2`, `featureJsonLine`.
- Produces: `sonicator-features --in <путь> [--out <jsonl>] [--limit N] [--walk]`.

- [ ] **Step 1: Собрать тул и проверить вручную**

```bash
cmake --preset release -DSONICATOR_BUILD_TRAINING_TOOLS=ON
ninja -C ~/Library/Caches/sonicator-build/release sonicator-features
cd ~/Library/Caches/sonicator-build/release
./tools/sonicator-features/sonicator-features --in /Users/ilyaorange/Documents/Sonicator/tests/fixtures --walk --out /tmp/features.jsonl --limit 5
head -1 /tmp/features.jsonl
```

Expected: пять строк, в каждой `"featureSchema":2` и 84 числа; файл валиден как JSONL.

- [ ] **Step 2: Проверить отказы**

```bash
./tools/sonicator-features/sonicator-features --in /нет/такого/пути
```
Expected: сообщение в stderr, код возврата 2, файл не создан.

- [ ] **Step 3: Записать в README как это запускать**

В `services/training/README.md` — раздел «Feature extraction»: сборка с опцией, команда, формат строки.

- [ ] **Step 4: Полный прогон тестов C++**

Run: `cd ~/Library/Caches/sonicator-build/release && ./tests/SonicatorTests`
Expected: все тесты зелёные (полный набор; ранее падавшие не появились).

- [ ] **Step 5: Коммит**

```bash
git add tools/sonicator-features CMakeLists.txt services/training/README.md
git commit -m "feat(tools): sonicator-features dumps feature vectors as JSONL"
```

---

## Результат (записано после выполнения)

- `FeatureExtractorV2`: 84 измерения плюс `inBandShare` вне вектора, полоса 40–8000 Гц, анализ по кадрам 2048/512 при частоте
  источника (без ресемплера). 15 новых тестов; полный набор C++ — 701 тест, 11 106 утверждений, зелёный.
- `featureJsonLine` + `sonicator-features`: каждая строка JSONL несёт `featureSchema 2`, `dimensions 84`,
  84 значения, `nyquistHz` и `inBandEnergyShare`; отсутствующий путь — код возврата 2.
- После ревью исправлены: периодичность по онсетам, затухание до последнего полного кадра,
  интерполяция атаки, отсечение кадров тишины, ширина стерео `side/(side+middle)`, `inBandShare`.
  Итог: 707 тестов, 11 291 утверждение, зелёные.
- Известные ограничения записаны в заголовке `core/dsp/FeatureExtractorV2.h`: средняя разница между
  16 и 48 кГц ≤ 0.06, отдельный контраст узкого пика до 0.5, skewness/kurtosis зависят от разрешения.
- Отклонения от плана (оценочные отображения, отказ от ресемплинга, инвариантность как средняя
  метрика) внесены в спецификацию, часть 6.6.
