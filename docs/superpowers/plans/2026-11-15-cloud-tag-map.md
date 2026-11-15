# Облако тегов: карта библиотеки — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Второе представление библиотеки — карта размеченных файлов: острова тегов, внутри них соседство по реальным акустическим признакам, прослушивание при наведении, выбор по щелчку.

**Architecture:** `core/cloud/` считает детерминированную раскладку в фоновом воркере и кэширует её по fingerprint каталога; `ui/cloud/CloudView` рисует 35k точек одним проходом, попадание под курсор ищет по сетке. Звук при наведении идёт отдельным голосом `HoverAudition`, который микшируется в существующий `PreviewOutput` и не касается выбора, правок волны и транспорта.

**Tech Stack:** C++20, JUCE 9.0.5, SQLite (существующий слой), Catch2.

**Spec:** `docs/superpowers/specs/2026-11-15-cloud-tag-map-design.md`

## Global Constraints

- Никаких новых зависимостей; всё считается на месте, без Python и внешних моделей.
- Раскладка детерминирована: одинаковый `CloudInput` даёт побитово одинаковый `CloudLayout`.
- Только размеченные файлы: 35 636 на каталоге владельца; неразмеченные не показываются.
- 13 991 файл имеют v2; 33 862 — базовые дескрипторы; 1 774 (metadata-only и нечитаемые) не имеют ни того, ни другого.
- Звучат только форматы общего декодирования (базовые + EBL + VOC); остальные 5 % молчат и не притворяются звучащими.
- Каталог открывается только на чтение; плагин не мигрирует схему.
- Расчёт не на message thread и не в аудиопотоке; смена фильтра раскладку не пересчитывает.
- Тесты не трогают живой каталог и живой `settings.json`: только временные каталоги (`TestDirectory`).
- Ничего не удаляется из `~/Library/Sonicator/exports/` и `exports/drag/`.

## Review Focus

- Файл без дескрипторов (metadata-only RX2/EXS/NKI/AKAI/tracker/SF2) — стоит в центре острова, помечен, не звучит, не ломает kNN ни делением на ноль, ни NaN.
- Один тег на 9 339 файлов рядом с тегом на 1 файл — радиусы не дают Dubtechno съесть карту и не схлопывают Shaker в невидимую точку.
- Тот же каталог, открытый дважды (приложение + плагин), даёт одинаковую карту; повторный расчёт не портит кэш и не блокирует редактор.
- Щелчок по точке, которой нет на загруженной странице списка (10 000 из 35 636) — preview показывает её, список остаётся без чужого выделения.
- Курсор проезжает по 50 точкам за секунду — ни очереди декодирований, ни звука, оставшегося после ухода, ни щелчка при остановке.

---

### Task 1: Модель и загрузка данных карты

**Files:**
- Create: `core/cloud/CloudModel.h`, `core/cloud/CloudSource.h`, `core/cloud/CloudSource.cpp`
- Modify: `core/database/SampleRepository.h/.cpp` (пакетное чтение размеченных и векторов)
- Modify: `CMakeLists.txt:154` (список исходников `SonicatorCore`)
- Test: `tests/TestCloudSource.cpp`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `SampleRepository::tags()`, `tagsForSamples()`, `Database`.
- Produces: `cloud::CloudInput loadCloudInput(SampleRepository&)`, `std::vector<SampleRecord> SampleRepository::taggedSamples()`, `std::map<int64_t, std::vector<float>> SampleRepository::feedbackV2()`, `uint64_t fingerprintCloudInput(const CloudInput&)`.

- [x] **Step 1: Написать падающий тест** `tests/TestCloudSource.cpp`: три файла, два размечены с разными дескрипторами, у одного тег Kick, у второго Kick+Drums, третий metadata-only (`version = -1`); проверить: в `nodes` три записи по возрастанию id; `hasBasic` true/true/false; `tagIds` верные; `tags` содержит оба тега с цветами; `cooccurrence` содержит пару Kick–Drums = 1; `fingerprint` меняется после `setSampleTag(..., false)` и не меняется после `setStarred`.
- [x] **Step 2: Убедиться, что тест не собирается** — `cmake --build build --target SonicatorTests` → ошибка о неизвестном `cloud::CloudInput`.
- [x] **Step 3: Реализовать** `taggedSamples()` (`JOIN sample_tags`, `GROUP BY samples.id`, порядок по `id`), `feedbackV2()` (`SELECT sample_id, features_v2 FROM ml_feedback_samples WHERE features_v2 IS NOT NULL`), `CloudModel.h` (структуры из спеки: `CloudNode`, `CloudInput`, `CloudPoint`, `CloudIsland`, `CloudLayout`, флаги) и `loadCloudInput()`: дескрипторы считаются пригодными, если `version >= 1` и все значения конечны; цвет точки — среднее цветов её тегов; fingerprint — FNV-1a по версии алгоритма, отсортированным `(tag.id, colour, count)`, хешу назначений, битовым картам и максимуму `dateAnalyzed`.
- [x] **Step 4: Прогнать тест** — `./build/tests/SonicatorTests "[cloud]"` → PASS.
- [x] **Step 5: Коммит** — `feat(cloud): load tagged samples and acoustic vectors for the map`.

### Task 2: Раскладка — острова, якоря, проекции

**Files:**
- Create: `core/cloud/CloudLayoutBuilder.h/.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/TestCloudLayout.cpp`

**Interfaces:**
- Consumes: `cloud::CloudInput`.
- Produces: `cloud::CloudLayout buildCloudLayout(const CloudInput&, const CloudLayoutOptions& = {})`; `CloudLayoutOptions { float islandBaseRadius, islandRadiusScale, islandGap, pointSpread, v2Spread, refinementAlpha; int isotropy; }`.

- [x] **Step 1: Падающий тест**: детерминизм (два прогона дают одинаковые `x/y` побитово); все точки внутри `bounds`; острова попарно не пересекаются; файл Kick+Drums лежит ближе к середине отрезка Kick–Drums, чем к центру Kick; metadata-only точка в центре острова; все 84 значения NaN не дают NaN в координатах; 5 одинаковых векторов не совпадают попарно; тег с 1 файлом имеет радиус не меньше `islandBaseRadius`.
- [x] **Step 2: Прогнать** → FAIL (нет `buildCloudLayout`).
- [x] **Step 3: Реализовать**: инициализация островов золотой спиралью по `tag.id`; 400 итераций пружин по `w = cooccurrence / sqrt(n_i*n_j)` и отталкивания; раздвижка пересекающихся окружностей; радиус `r = base + scale*sqrt(n)`; якорь — среднее центров тегов; базовая проекция: устойчивая z-нормализация 8 дескрипторов → ковариация 8×8 → две главные компоненты степенным методом с детерминированным стартом → нормализация по 2–98 перцентилям; v2: PCA до 8 компонент, kNN (k=8) среди точек с v2 в радиусе сетки, 2 итерации притяжения, смещение не больше `v2Spread * radius`; одинаковые 2D-координаты разводятся золотой спиралью по возрастанию id.
- [x] **Step 4: Прогнать** `[cloud]` → PASS; зафиксировать в тесте время расчёта на 35 636 синтетических точках не более 3 с в Release (тег `[.][cloud][perf]`).
- [x] **Step 5: Коммит** — `feat(cloud): deterministic tag-island layout with acoustic projection`.

### Task 3: Кэш, fingerprint и фоновый воркер

**Files:**
- Create: `core/cloud/CloudCache.h/.cpp`, `core/cloud/CloudBuildService.h/.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/TestCloudBuild.cpp`

**Interfaces:**
- Produces: `juce::File cloudCacheFile(uint64_t fingerprint)` (`~/Library/Sonicator/cache/cloud/layout-<hex>.bin`), `bool writeCloudLayout(const CloudLayout&, const juce::File&)`, `std::optional<CloudLayout> readCloudLayout(const juce::File&, uint64_t expectedFingerprint)`; `CloudBuildService::start(CloudInput&&)`, `cancel()`, `progress()`, `poll() -> std::optional<CloudLayout>`, `isRunning()`.

- [x] **Step 1: Падающий тест**: запись/чтение совпадает по точкам; чужой fingerprint отвергается; битый файл отвергается без исключения; `start` + `poll` возвращает раскладку; `cancel` до публикации не публикует; второй `start` отменяет первый и публикует только второй; старые файлы кэша сверх двух удаляются.
- [x] **Step 2: Прогнать** → FAIL.
- [x] **Step 3: Реализовать**: двоичный формат с версией и fingerprint, `std::jthread`-воркер с `std::atomic<bool>` отменой, публикация только при совпадении поколения.
- [x] **Step 4: Прогнать** → PASS.
- [x] **Step 5: Коммит** — `feat(cloud): layout cache and cancellable build service`.

### Task 4: Инструмент проверки раскладки

**Files:**
- Create: `tools/cloud-layout-report/main.cpp` (собственный target), `docs/CLOUD_MAP.md`
- Modify: `CMakeLists.txt` (target `cloud-layout-report`, только при `SONICATOR_BUILD_TOOLS`, как остальные tools)
- Test: `tests/TestCloudLayout.cpp` (проверка форматирования соседей на синтетическом входе)

**Interfaces:**
- Produces: `std::vector<std::pair<size_t,float>> nearestNeighbours(const CloudLayout&, const CloudInput&, size_t index, int k)` в `CloudLayoutBuilder.h`.

- [x] **Step 1: Тест**: для трёх точек на одной прямой сосед ближайший — тот, что ближе по x; k больше числа точек возвращает все.
- [x] **Step 2: Прогнать** → FAIL.
- [x] **Step 3: Реализовать** поиск по сетке и CLI: аргументы `--db <path> --out <dir> --seeds 12`; пишет `layout.csv`, `layout.svg`, `neighbours.txt` (5 соседей с путями и расстоянием), печатает счётчики `v2 / basic-only / no position` и время.
- [x] **Step 4: Собрать и прогнать на живом каталоге** (read-only): `./build/tools/cloud-layout-report --db ~/Library/Sonicator/sonicator.sqlite --out /tmp/cloud-report`; записать время и счётчики в `docs/CLOUD_MAP.md`.
- [x] **Step 5: Коммит** — `feat(cloud): layout report tool for listening checks`.

### Task 5: Звук при наведении

**Files:**
- Create: `core/audio/HoverAudition.h/.cpp`
- Modify: `core/audio/AudioEngine.h/.cpp`, `core/audio/MidiSampler.h/.cpp` (новый параметр `PreviewOutput`)
- Test: `tests/TestHoverAudition.cpp`, поправить `tests/TestAudioEngine.cpp:24`

**Interfaces:**
- Produces: `HoverAudition::setAudio(juce::AudioBuffer<float>&&, double rate)`, `play()`, `stop(bool fadeOut)`, `clear()`, `render(juce::AudioBuffer<float>&, int start, int count)`, `bool isPlaying()`, `AudioEngine::hoverAudition()`.

- [x] **Step 1: Падающий тест**: после `setAudio` + `play` рендер даёт ненулевой сигнал; первые 256 сэмплов нарастают (рампа), после `stop` сигнал затухает до нуля за ≤ 12 мс и дальше тишина; повторный `setAudio` во время игры не даёт щелчка (нет скачка больше 0.5); 10-секундный лимит аудио (буфер длиннее обрезается при загрузке — проверяется на `loadForHover`).
- [x] **Step 2: Прогнать** → FAIL.
- [x] **Step 3: Реализовать** голос с gain-рампой 10 мс, моно/стерео, ресемплинг как у `SampleAudition`; `PreviewOutput` добавляет его в микс; `AudioEngine::hoverAudition()`.
- [x] **Step 4: Прогнать** `[cloud][hover]` и весь `TestAudioEngine` → PASS.
- [x] **Step 5: Коммит** — `feat(audio): fade-safe hover audition voice`.

### Task 6: Сетка попадания под курсор

**Files:**
- Create: `ui/cloud/CloudIndex.h/.cpp`
- Modify: `CMakeLists.txt` (`SonicatorUI`)
- Test: `tests/TestCloudIndex.cpp`

**Interfaces:**
- Produces: `CloudIndex::build(const CloudLayout&)`, `size_t hitTest(float x, float y, float radiusPx) const` (SIZE_MAX = мимо), `std::vector<size_t> within(juce::Rectangle<float>) const`.

- [x] **Step 1: Падающий тест**: точное попадание в точку; промах в пустой ячейке; две точки в одной ячейке — возвращается ближайшая; плотный кластер 10 000 точек в квадрате 100×100 находит точку за < 1 мс на 1 000 запросов; точки на границе ячейки находятся.
- [x] **Step 2: Прогнать** → FAIL.
- [x] **Step 3: Реализовать** равномерную сетку 24 px, построение в порядке возрастания id, поиск по 3×3 ячейкам.
- [x] **Step 4: Прогнать** → PASS.
- [x] **Step 5: Коммит** — `feat(cloud): spatial index for hover hit testing`.

### Task 7: Представление «Облако»

**Files:**
- Create: `ui/cloud/CloudView.h/.cpp`
- Modify: `ui/MainComponent.h/.cpp` (переключатель, область библиотеки, hooks), `core/settings/AppSettings.h` + `SettingsStore` (режим представления)
- Test: `tests/TestCloudView.cpp`

**Interfaces:**
- Produces: `CloudView::setLayout(CloudLayout)`, `setTags(std::map<int64_t, std::vector<Tag>>)`, `setFilterMatches(std::vector<bool>)`, `onActivate(int64_t sampleId)`, `onHover(int64_t sampleId)`, `onHoverEnd()`, `fitAll()`, `zoomBy(float, juce::Point<float>)`, `cameraBack()`, `setViewportSize`; в `MainComponent`: `void showCloudView(bool)`, `bool cloudViewActive() const`.

- [x] **Step 1: Падающий тест** (без окна, прямой вызов): `fitAll` вписывает `bounds` в компонент; колесо сохраняет точку под курсором (±0.5 px); «Назад» возвращает предыдущую камеру; `hitTest` выбирает точку; клик по погашенной фильтром точке не вызывает `onActivate`; `onHover` вызывается один раз при входе и `onHoverEnd` при уходе; смена layout не сбрасывает камеру, если fingerprint тот же.
- [x] **Step 2: Прогнать** → FAIL.
- [x] **Step 3: Реализовать** `CloudView`: кэш кадра в `juce::Image`, рисование одним `Path`, LOD (при `scale < 0.6` — плотность вместо точек), подписи островов с порога зума, камера с анимацией и стеком, сетка попадания, ореол наведения с затуханием, фильтр гасит до 12 %. В `MainComponent`: сегмент «Список / Облако» в шапке LIBRARY, `CloudBuildService` + `CloudSource`, инвалидация в точках записи тегов и после индексации с дебаунсом 500 мс, `onActivate` → `selectSample` + `selectSampleById` (без подмены чужой строки), hover → `HoverAudition` через воркер декодирования, стоп при уходе/переключении/закрытии; режим сохраняется в настройках.
- [x] **Step 4: Собрать и прогнать** `[cloud]` и UI-тесты → PASS.
- [x] **Step 5: Коммит** — `feat(cloud): tag map view with hover audition`.

### Task 8: Приёмка и документация

**Files:**
- Modify: `docs/CLOUD_MAP.md` (замеры, константы, скриншоты), `docs/ARCHITECTURE.md` (раздел «Облако тегов»), `AGENTS.md` (строка в спецификациях), `~/Documents/orange303/Sonicator Features.md` (отметка ✅)
- Test: существующие + прогон на живом каталоге

- [x] **Step 1:** Debug и Release сборка, весь `SonicatorTests`.
- [x] **Step 2:** Замеры на 35 636 точках: расчёт (кэш-промах), кадр панорамы/зума, отклик наведения; записать числа.
- [x] **Step 3:** `cloud-layout-report` на живом каталоге; разобрать 12 опорных точек; исправить константы, если соседи не того типа.
- [x] **Step 4:** Снимки 100 %/125 %, светлая тема, плагин с двумя экземплярами; проверить, что аудиопоток не блокируется.
- [x] **Step 5:** `graphify update .`, коммит и пуш.
