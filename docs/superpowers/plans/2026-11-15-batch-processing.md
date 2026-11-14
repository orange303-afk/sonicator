# Batch Processing v1 — Implementation Plan

> **For agentic workers:** после согласования ТЗ и переключения в Build использовать
> `executing-plans` для последовательной реализации с TDD и независимым финальным ревью.
> Не запускать реализацию из текущего Plan mode. Выполнение задач не означает разрешение
> экспериментировать с оригиналами пользовательской библиотеки.

**Goal:** законченная пакетная обработка обычных аудиофайлов с WAV/AIFF/FLAC-копиями,
пресетами, preview, отчётом и безопасной отменой.

**Architecture:** `core/batch` содержит снимок входов, проверяемый recipe, повторно читаемый
поток DSP, строгие encoder/publisher и один worker. `ui/batch` содержит диалог и управление
preview через существующий AudioEngine. MainComponent связывает menus/commands, busy state,
жизненный цикл и явный New files после результата; не содержит DSP.

**Tech Stack:** C++20, JUCE 9.0.5, SQLite (без изменения схемы Batch), Catch2, CMake/Ninja;
предлагаемая новая статическая зависимость libsamplerate 0.2.2 / BSD-2-Clause.

**Spec:** `docs/superpowers/specs/2026-11-15-batch-processing-tz.md`.

## Global Constraints

- Создавать копии; ни один input/alias не может стать заменяемым output.
- Нет Split, lossy output codecs, original overwrites, библиотечного обучения или plugin SDK.
- Шесть видов шагов, произвольный порядок и дубли, максимум 16; Output conversion после них.
- Анализ префикса перед DC/Trim/Normalize; никакого полного файла/undo в RAM.
- Один источник/encoder; блок 16 384 кадров, int64 positions, finite checks, bounded buffers.
- Точные capabilities writer; не округлять неподдерживаемую битность без сообщения.
- Temp → полная validation → atomic/no-clobber commit; не использовать delete-then-copy.
- Catalog исходников/sidecars не менять; новые результаты индексировать только по явному выбору.
- У тестов свой settings directory, scratch и fixtures. Тестовый вывод ≤ 1 GiB, не полный live corpus.
- Сохранить существующие Normalize/Split/Edit/Drag/Extract сценарии без побочных изменений.
- Не обещать Windows/manual audio acceptance без фактически выполненной проверки.

## Review Focus

1. Same-path/hardlink/symlink alias и гонка имени должны сохранять все источники и прежний output.
2. Несколько Trim/Fade/Normalize в разном порядке должны работать на текущем промежуточном
   диапазоне, а не на исходных stats/границах; тесты на разных размерах блока.
3. Integer dither не должен применяться дважды; float input, SRC overshoot и all-silent не могут
   превратиться в незамеченный clipped/NaN/пустой файл.
4. Cancel/exception/ENOSPC в validation/commit, закрытие окна и catalog switch должны
   завершать worker/readers до cleanup, без stale callbacks и блокирующего message-thread join.
5. Длинные файлы, большие frame counts, недоступный диск и corrupt/future presets не должны
   вызывать unbounded allocations, молчаливый fallback или потерю пользовательских настроек.

## 0. Подготовка исполнителя

- [ ] Прочитать ТЗ, `AGENTS.md`, актуальный внешний бэклог и состояние `git status`.
- [ ] Создать/использовать согласованную feature-ветку; worktree только с разрешения.
  При ~16 GiB свободного места переиспользовать внешние кэшированные сборки, не создавать вторую
  универсальную сборку/копию датасета. Не сбрасывать чужие изменения.
- [ ] Проверить актуальный `main`: Batch-кода ещё нет, Genre schema 19 уже поставлена. При
  изменениях структуры пересогласовать только реально изменившиеся интерфейсы, не угадывать.
- [ ] Выполнить baseline `ctest --preset debug`; сохранить реальные результаты в ledger.
- [ ] В Build mode перенести согласованные документы в `docs/superpowers/specs/` и `plans/`,
  обновить Spec-ссылку и добавить журнал прогресса. В Plan mode этого не делать.
- [ ] Создать свой scratch под разрешённым OpenCode temporary directory; для тестов с инъекцией
  I/O отказов использовать тестовые зависимости, не product-only «forceFailure» flags.

## Карта файлов

Новые модули:
- `core/batch/BatchTypes.{h,cpp}` — recipe/request/results, codes, validation и fingerprint.
- `core/batch/BatchSources.{h,cpp}` — snapshot, входной reader-probe, roots/stereo/ignore handling.
- `core/batch/BatchPaths.{h,cpp}` — output layout, reservation, aliases, space estimates.
- `core/batch/BatchPipeline.{h,cpp}` — PreparedAudio и prefix analysis, block reads шести операций.
- `core/batch/BatchResampler.{h,cpp}` — непрерывный libsamplerate adapter с точным frame contract.
- `core/batch/BatchQuantizer.{h,cpp}` — encoding-aware quantization, TPDF и seed.
- `core/batch/BatchEncoder.{h,cpp}` — capabilities и строгая запись WAV/AIFF/FLAC.
- `core/batch/BatchMetadata.{h,cpp}` — whitelist, marker policy и предупреждения.
- `core/batch/BatchPublisher.{h,cpp}` — temp ownership, verify, atomic publication и rollback cleanup.
- `core/batch/BatchService.{h,cpp}` — один worker, progress/results и report persistence.
- `core/batch/BatchPresetStore.{h,cpp}` — schema-1 JSON и атомарное сохранение.
- `core/batch/BatchPreviewService.{h,cpp}` — подготовка dry/wet snippets через тот же DSP.
- `ui/batch/BatchProcessingDialog.{h,cpp}` — конфигурация/прогресс/отчёт.
- `ui/batch/BatchStepList.{h,cpp}` — операции и валидируемые параметры.
- `ui/batch/BatchOutputPanel.{h,cpp}` — capabilities, destination и conflict/quantization controls.
- `ui/batch/BatchPreviewControls.{h,cpp}` — prepare/A-B/stop, состояние устаревшего preview.
- `tests/TestBatch*.cpp` — отдельные tests по контрактам ниже, fixtures в TestHelpers либо локально.

Изменяемые:
- `CMakeLists.txt`, `tests/CMakeLists.txt` — source lists, target libsamplerate, лицензия.
- `core/database/SampleRepository.{h,cpp}` — только bounded physical folder enumeration API.
- `ui/MainComponent.{h,cpp}` — контекст/команды/ownership/busy/каталог/preview takeover.
- `ui/commands/*`, только если требуется расширить существующую регистрацию; новый command registry не создавать.
- `ui/settings/SettingsPanel.cpp`/текущий About attribution — libsamplerate в кредитсах.
- Новые `docs/BATCH_PROCESSING.md`, `docs/BATCH_PROCESSING_ACCEPTANCE.md`, кратко `docs/ARCHITECTURE.md`.

Объединять маленькие private helper-файлы можно, если интерфейсы/ответственность сохраняются;
не переписывать существующие edit/export writers ради Batch.

## Общие интерфейсы

Все новые типы — `namespace sonicator::batch`. Существующий `sonicator::CancelCheck` переиспользуется.

`BatchTypes.h` задаёт:
- `Format { wav, aiff, flac }`, `Encoding { sameAsSource, int16, int24, float32 }`;
  `ChannelMode { sameAsSource, mono, stereo }`, `DitherMode { automatic, tpdf, off }`;
  `ConflictPolicy { rename, skip, replaceOutput }`, `ClippingPolicy { rejectInteger, clipInteger }`.
- `StepParameters = std::variant<RemoveDc, TrimSilence, Normalize, Gain, FadeIn, FadeOut>`;
  поля параметров/диапазоны — строго ТЗ §6. `ProcessStep { bool enabled; StepParameters parameters; }`.
- `OutputPreset { Format format; Encoding encoding; int flacCompressionLevel=5; }`.
- `Recipe { vector<ProcessStep> steps; vector<OutputPreset> outputs; int sampleRate=0;
  ChannelMode channels; DitherMode dither; ClippingPolicy clipping; }`, где 0 Гц = Same as source.
- `Destination { bool nextToSources=true; juce::File folder; }`;
  `SourceSelection` различает snapshot списка row IDs и один физический folder path.
- `Request { SourceSelection sources; Recipe recipe; Destination destination;
  ConflictPolicy conflicts; bool replacementConfirmed=false; }`.
- `Issue { ErrorCode code; juce::String message; }`, warning codes отдельны от fatal error codes.
- `SourceSnapshot` содержит sample ID, source/base root, physical identity, size/mtime,
  reader header и skip reason. `OutputPlan` — источник, format/settings, target, previous-target
  fingerprint и upper frame/byte estimate; фактическая длина уточняется после подготовки DSP.
- `PreparedBatch` содержит immutable request и полный snapshot входов/пропусков/выходов,
  job ID, source protection set и space estimates. Не содержит UI pointers или открытых writer.
- `OutputResult`, `SourceResult`, `StepOutcome`, `Progress`, `Report` соответствуют ТЗ §14;
  успешный OutputResult обязательно содержит фактический committed путь.

## Task 1. Контракт, сбор входов и безопасный preflight

**Files:** BatchTypes, BatchSources, BatchPaths; SampleRepository; CMake source lists;
`tests/TestBatchPreflight.cpp`, `tests/TestBatchPaths.cpp`.

**Consumes:** `SampleRepository`, `registerSonicatorFormats`, `isPathIgnored`, stereo pair partners.
**Produces:** `PreflightResult prepareBatch(SampleRepository&, const Request&, CancelCheck)`;
`vector<PathEntry> SampleRepository::physicalPathsInFolder(const juce::String&, int limit=100001) const` — без
container rows/ignored, с обоими mono-партнёрами, не зависящий от LibraryFilter.
LIMIT позволяет отклонить >100 000 входов, не загружая весь каталог.

- [ ] RED: recursion включает `pack/a.wav` и `pack/deep/b.wav`, не `pack-extra/c.wav`;
  filter mismatch не влияет, ignored/virtual/unsaved имеют явные причины. Snapshot не включает
  файл, добавленный после подготовки. Stereo pair даёт два физических mono inputs.
- [ ] RED: WAV+FLAC для двух входов даёт четыре OutputPlan, WAV float и FLAC24 имеют разные
  encoding; FLAC float и 17-й шаг запрещены. `sameAsSource` не округляет float/32int в PCM24.
- [ ] RED: Unicode/одинаковые stem/два root names/корни с `..` и symlink parents;
  planned target не может совпасть с входом/hardlink даже при Replace. Stable Rename создаёт
  `a processed.wav`, затем `a processed 2.wav`; Skip не требует удаления существующего файла.
- [ ] RED: верхняя оценка места/4-GiB container limit/переполнение frame math; unknown free space
  — warning, недостаток — блокировка. Reader-only проверки выполняются без DSP в message thread.
- [ ] Запуск: build SonicatorTests и `[batch-preflight],[batch-paths]`; ожидаются отсутствующие
  интерфейсы/падающие конкретные проверки. Зафиксировать RED, затем реализовать минимально.
- [ ] GREEN: те же tests + `[database],[folders]`, затем полный debug ctest; commit задачи.

## Task 2. Повторно читаемая DSP-цепочка

**Files:** BatchPipeline; `tests/TestBatchPipeline.cpp`.
**Consumes:** SourceSnapshot, Recipe, CancelCheck.
**Produces:** `PrepareAudioResult prepareAudio(const SourceSnapshot&, const Recipe&, CancelCheck)`;
`PreparedAudio` владеет reader/подготовленными steps и предоставляет
`int64_t frames() const`, `int sampleRate() const`, `int channels() const`,
`ReadResult readFrames(int64_t start, int count, juce::AudioBuffer<float>& destination)`.
`ReadResult` различает EOF, error, cancelled и число прочитанных кадров. Один PreparedAudio
использует только один worker; reader не разделяется между UI и job.

- [ ] RED: DC для `[0.25,0.5,0.75]` даёт `[-0.25,0,0.25]`; разные channel means не смешиваются.
- [ ] RED: Trim при padding=0 для `[0,0,0.5,0]` оставляет один кадр; все нули сохраняют длину;
  звук только в правом канале сохраняется. Реальные границы после нескольких Trim композиционны.
- [ ] RED: Normalize `[0.25,-0.5]` до 0 dB даёт `[0.5,-1]`; Gain+6.0205999 и перестановка
  Gain/Normalize дают разные ожидаемые peak. Silent/NaN/Inf/extreme params строго по ТЗ.
- [ ] RED: linear Fade in M=3 для `[1,1,1]` даёт `[0,0.5,1]`, Fade out — `[1,0.5,0]`;
  M<2 no-op. Cosine endpoints, crossing block boundary и overlap соответствуют формулам ТЗ.
- [ ] RED: block sizes 1/257/16384, повторное чтение диапазона и отключённый step дают одинаковую
  семантику; анализ Normalize после Gain использует изменённый префикс, а не исходный peak.
- [ ] Реализовать block-replay prefix graph без full-duration allocation/spool. Не переносить
  normalizeFile, который самостоятельно пишет/заменяет файлы, внутрь цепочки.
- [ ] GREEN: `[batch-pipeline]`, полный debug ctest, commit. Измерить allocated buffer sizes
  на виртуальном длинном reader и maximum steps; разрешённый scratch reader seam — production abstraction.

## Task 3. Output conversion, качественный SRC и quantizer

**Files:** BatchResampler, BatchQuantizer, CMake dependency wrapper;
`tests/TestBatchResampler.cpp`, `tests/TestBatchQuantizer.cpp`.

**Consumes:** PreparedAudio, resolved output settings.
**Produces:** `ConvertedStream` с readNext блоками конечных mono/stereo float frames и Nout;
`Quantizer` с `quantize(...)` для left-aligned int32 и `reset(seed, encoding, mode)`.
Вне integer encoder float32 проходит без квантования.

- [ ] Закрепить libsamplerate 0.2.2 immutable revision/archive SHA256; проверить COPYING.
  Статическая сборка, examples/install выключены; не менять глобальный BUILD_TESTING Sonicator.
  Wrapper CMake использует scoped settings. Добавить лицензию/кредитсы как часть этой задачи.
- [ ] RED: Same rate identity; Stereo antiphase→Mono zero, Mono→Stereo equal channels.
- [ ] RED: SRC 44.1↔48, 96→44.1, 192→48: Nout, импульс, EOF drain, short/1-frame source,
  amplitude/alias thresholds из ТЗ §15. Сравнение разных block sizes не использует один и тот же
  ошибочный расчёт ожидаемого массива; reference — независимый whole-input вызов/явные сигналы.
- [ ] Зафиксировать конкретный EOF adapter contract: точное правило trim/zero-extension и
  допустимая конечная разница с количеством кадров libsamplerate; подтвердить тестами до UI.
- [ ] RED: int16/int24 endpoints, negative samples без UB; no dither при identity PCM24;
  TPDF statistics/seed/channel independence и отсутствие dither в float32.
- [ ] RED: overflow checked при Nout/буферах; NaN/Inf при input/SRC → error, не тишина.
- [ ] GREEN: `[batch-src],[batch-quantizer]`, полный ctest, commit. Не заменять SRC «временно»
  линейной интерполяцией при проблемах зависимости — остановить этот task с конкретным blocker.

## Task 4. Строгие кодировщики, метаданные и публикация

**Files:** BatchEncoder, BatchMetadata, BatchPublisher;
`tests/TestBatchEncoding.cpp`, `tests/TestBatchPublish.cpp`, `tests/TestBatchMetadata.cpp`.

**Consumes:** PreparedAudio/ConvertedStream/Quantizer, OutputPlan, protected input identities.
**Produces:** `OutputResult renderAndPublish(PreparedAudio&, const OutputPlan&,
const InputProtectionSet&, CancelCheck)`; capability lookup для UI/preflight, общий validator.

- [ ] RED: выбранные WAV16/24/float, AIFF16/24 и FLAC16/24/levels1..8 подтверждаются новым reader;
  invalid encoding rejected. Проверять bits/float flag, не только frame count/channels.
- [ ] Реализовать отдельный strict BatchEncoder с JUCE WriterOptions и quality index.
  Существующий edit::writeAudioStream оставляется с прежним поведением. Переиспользовать
  AudioExporter::validateWavOutput там, где подходит, но не его неявный format fallback.
- [ ] RED: реальный Gain clipping reject/explicit clip/float warning и SRC overshoot;
  пик измеряется после conversion, а не только до неё. Quantization saturation ±LSB не путать с audio clipping.
- [ ] RED: source/старый output SHA256 до/после: write/flush/late read/validation/rename failure,
  ENOSPC и Cancel. Подменённый destination fingerprint не заменяется. Rename no-clobber
  гонка не портит внешний файл; source alias остаётся защищённым непосредственно перед publish.
- [ ] Реализовать same-dir temp RAII, full decode validation, platform atomic publisher;
  запретить fallback delete/copy. Неуспех удаляет только собственный temp, old target сохраняется.
- [ ] RED: WAV root/loop/cue/INFO whitelist сохраняется без temporal change; при реальном
  Trim/SRC остаётся root, timed metadata удаляется с warning. AIFF/FLAC metadata loss отражён.
  Sidecar contents и каталог исходника не меняются.
- [ ] GREEN: `[batch-encoding],[batch-publish],[batch-metadata]` + existing edit/normalize/drag
  tests, полный ctest, commit.

## Task 5. BatchService, отмена и отчёты

**Files:** BatchService, при необходимости небольшой Report serializer;
`tests/TestBatchService.cpp`.

**Consumes:** PreparedBatch; prepareAudio; renderAndPublish.
**Produces:** `BatchService::start(PreparedBatch) -> bool`, `cancel()`, `progress() const -> Progress`,
`results() const -> Report` после завершения, `waitUntilFinished(int timeoutMs) -> bool`.

- [ ] RED: 2 sources×2 formats =4 planned outputs; одно encode failure не скрывает/не удаляет
  остальные. Source step no-change всё равно создаёт выбранные копии.
- [ ] RED: deterministic cancellation barriers во всех фазах; не начатые outputs перечислены,
  completed paths сохранены. Нет product-only sleep/force-failure switches ради теста.
- [ ] RED: snapshot/late new inputs не попадают в job; источник, изменённый между двумя
  проходами/форматами, даёт source_changed и cleanup только текущего temp.
- [ ] RED: длинный виртуальный поток, >2^31 frames counters, buffer cap, cancellation latency;
  сравнивать реально выделенную память потока, не кэш всего Sonicator.
- [ ] RED: report JSON/text, actual paths after Rename, errors/warnings/counts, max20/30day
  retention; crash temps собираются только по собственному manifest, без broad folder cleanup.
- [ ] Реализовать worker с поллингом progress, единым ownership temps/readers и no-UI callbacks;
  freeze request при start, двойной start не меняет активный job.
- [ ] GREEN: `[batch-service]`, полный ctest, commit.

## Task 6. Пресеты и честный preview

**Files:** BatchPresetStore, BatchPreviewService;
`tests/TestBatchPresets.cpp`, `tests/TestBatchPreview.cpp`.

**Consumes:** Recipe; SettingsStore::file().getParentDirectory(); prepareAudio/render stream.
**Produces:** `PresetLoadResult BatchPresetStore::load() const`, `SaveResult save(...)`;
`BatchPreviewService::start(SourceSnapshot, Recipe, generation)`, `cancel()`, `progress()`,
`takeResult() -> PreviewResult` с dry/wet files, диапазонами и измерениями. Не владеет device.

- [ ] RED: recipe JSON roundtrip сохраняет order/disabled/duplicate steps, outputs и dither;
  источник/confirmation/destination не попадают в переносимый preset. Neutral и Cleanup неизменяемы.
- [ ] RED: corrupt/future JSON не перетирается, save failure сохраняет предыдущий файл;
  одинаковые имена presets требуют явного replace. Весь I/O в TestDirectory, не live settings.
- [ ] RED: глобальный Normalize/DC по файлу, у которого первые 10 секунд отличаются от остатка,
  даёт те же параметры, что партия; preview short-circuit по локальному fragment запрещён.
- [ ] RED: Trim offsets для dry/wet соответствуют одному сохранённому source range; max10sec,
  настройки меняются → старый generation не принимается. Cancel/delete cleanup после reader close.
- [ ] GREEN: `[batch-presets],[batch-preview]`, полный ctest, commit.

## Task 7. Диалог, команды, A/B и жизненный цикл владельца

**Files:** ui/batch modules, MainComponent, About/command registrations;
`tests/TestBatchDialog.cpp`, `tests/TestBatchIntegration.cpp`, rendering fixture.

**Consumes:** все перечисленные contracts; текущие theme/Surface/AudioEngine; BatchService.
**Produces:** публичный UI-контракт `BatchProcessingDialog` с request/preflight/progress/result
обновлениями и callbacks onPrepare/onStart/onCancel/onAudition/onClose. Owner удерживает service
до конца, dialog не пишет файлы/каталог и не создаёт аудиоустройство.

- [ ] RED: source counts/0 formats/invalid settings/unsupported per-source fields управляют Start;
  reorder/enable/double-step/output depth/FLAC level сериализуются в точный Request.
- [ ] RED: row и folder контекст передаются снимком, а не повторно читаются из selection.
  Зарегистрировать `library.batchProcess` и `folders.batchProcess`, без default hotkeys.
  Menu и shortcut handlers используют общий openBatchProcessing(context) и readiness helper;
  для clicked folder контекст не подменяется текущей выделенной папкой.
- [ ] RED: busy helper блокирует filesystem mutators/catalog switch публичными API;
  menu-open→другой job→click не запускает параллельный writer.
- [ ] RED: A/B takeover не теряет несохранённые edits/lastSamplePath, завершение/закрытие
  освобождает transient readers перед удалением; restore выбранного источника без autoplay.
- [ ] RED: управляемый in-flight barrier при Start/Cancel/close/shutdown/catalog switch;
  safe callbacks/generation, отсутствие message-thread ожидания подготовки preview.
- [ ] Реализовать UI по ТЗ §11 без копирования DSP в MainComponent. Диалог использует viewport,
  текущие Manrope/theme tokens, resizable bounds; Progress и report не исчезают при partial errors.
- [ ] Добавить явные Open output folder / Export report / Scan new files для допустимых корней;
  scan только после worker и по подтверждению, без автоматического наследования annotations.
- [ ] GREEN: `[batch-dialog],[batch-integration]`, `[ui],[commands]`, полный ctest, commit.

## Task 8. Приёмка, документация и завершение этапа

**Files:** Batch documentation/acceptance; только исправления реальных findings с RED→GREEN.

- [ ] Пройти каждый критерий ТЗ §15 и указать тест/реальное свидетельство или честный blocker.
  Не компенсировать отсутствующий сценарий новым текстом «реализовано».
- [ ] Проверить небольшие реальные копии WAV/AIFF/FLAC, короткий VOC/другие заявленные readers,
  тихий/пиковый/стерео/длинный synthetic source. Проверить слухом A/B/границы SRC; не трогать originals.
- [ ] Сделать реальные screenshots light/dark при100%/125%, минимум configure/progress/report.
  Synthetic mockup не является доказательством UI-приёмки.
- [ ] Build/debug/full ctest и release; codesign verify. Windows build/tests — только доступный CI,
  не добавлять новый гигантский toolchain локально ради галочки при ограниченном диске.
- [ ] Один независимый whole-branch review: source protection, output atomicity, numerical
  correctness, ownership/cancellation и Review Focus выше. Исправления Critical/Important —
  failing test → fix → focused+full suite. Minor отдельно перечислить, не спрятать.
- [ ] Обновить `docs/BATCH_PROCESSING.md`, acceptance и Architecture (включая уже актуальную
  schema19, если там ещё18); старое broad ТЗ пометить реализованным только в пределах v1.
- [ ] `graphify update .` после кода. Проверить git diff/check/status и свободное место.
- [ ] По принятому порядку владельца: commit, ff merge main, полный ctest на merged result,
  push и удаление merged feature branch. При конфликте/красном ctest не пушить «на удачу».
- [ ] В финале дать путь release app, фактические тесты/ограничения/remaining work и короткую
  инструкцию проверки; не перезапускать приложение с несохранёнными правками без решения владельца.

## Команды проверки

Из корня репозитория, после перехода в Build:

```bash
cmake --preset debug
cmake --build --preset debug --target SonicatorTests
"$HOME/Library/Caches/sonicator-build/debug/tests/SonicatorTests" "[batch]"
ctest --preset debug
cmake --build --preset release --target Sonicator
codesign --verify --deep --strict "$HOME/Library/Caches/sonicator-build/release/Sonicator_artefacts/Release/SONICATOR.app"
```

Все TestBatch cases получают общий `[batch]` плюс узкий тег из задач. Настройки/пресеты и
temporary audio в тестах всегда из TestDirectory. Полный ctest не заменять зелёным только `[batch]`.

## Порядок и граница ночного задания

Задачи 1→2→3→4→5 образуют backend; затем 6→7 и приёмка8. Параллелить только действительно
независимое чтение/проверки, а не давать двум агентам править MainComponent/CMake одновременно.
Эта функция связная, поэтому workflow fan-out на десятки агентов не нужен.

Функция считается поставленной только после8, не после появления кнопки. Если ночь закончилась
раньше, сохранить проверенные commits/ledger на feature branch, отметить точный незавершённый
task и не включать недоделанную команду в пользовательский release. Нельзя ради срока убрать
anti-alias, no-clobber, validation, dither или проверки сохранности исходников.

## Самопроверка плана / журнал

- Все требования относятся к v1 из ТЗ; original-overwrite/Split/codecs не попали в интерфейсы скрыто.
- Tasks1→2: SourceSnapshot/Recipe;2→3: PreparedAudio;3→4: ConvertedStream/Quantizer;
  1+2+4→5: PreparedBatch/renderAndPublish;2+3→6: preview;1+5+6→7: UI. Циклических интерфейсов нет.
- Test isolation/limited disk/output file protection/bounded memory перечислены как обязательные
  проверки в соответствующих задачах, не отложены только на ревью.
- Открытый технический gate Task3: подтвердить точный EOF adapter contract reference-тестами;
  это работа исполнителя, не неизвестное пользовательское предпочтение.
- План составлен, но ещё не исполнен. Никакие build/test результаты для нового Batch не заявляются.
- Независимый read-only builder check подробного ТЗ: существенных неразрешённых вопросов
  для планирования нет. Численные reference tests/EOF и atomic primitives остаются обязанностью
  исполнителя. Повторное квантование проверяется по фиксированным float inputs; между SRC
  block partitions допускается 2e−6, не cross-platform encoded-byte identity.
