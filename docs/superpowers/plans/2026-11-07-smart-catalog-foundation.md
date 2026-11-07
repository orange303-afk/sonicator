# Smart catalog foundation — implementation plan

**Goal:** укрепить хранение и фоновый pipeline smart-анализа: версии, зависимости,
атомарная публикация, обнаружение устаревания, структурированные ошибки и
устойчивость к гонкам. Без DSP, новых колонок и инспектора.

**Spec:** `docs/superpowers/specs/2026-11-07-smart-catalog-foundation-design.md`

**Tech:** C++20, JUCE 9.0.5, bundled SQLite, Catch2 3.7.1.

## Steps

- [x] **1. Types and versions.** Расширить `SmartTypes` состояниями, версиями,
      provenance-полями кандидата, флагами override, полями Pitch и ревизии.
- [x] **2. Schema 7.** Миграция с backup: новые колонки и таблицы
      `smart_extractions`, `smart_failures`, `smart_meta`; индексы.
- [x] **3. Atomic publish.** `publishSmartAnalysis`: одна транзакция, проверка
      ревизии, чтение overrides внутри транзакции, запись кандидатов, извлечений,
      fingerprints, результата и поколения.
- [x] **4. Staleness and failures.** `markSmartStale`, `markSmartError`,
      `samplesNeedingSmartAnalysis` с версиями, `samplesWithSmartResultAfterId`,
      `recordSmartFailure`.
- [x] **5. Override separation.** `setSmartOverride`/`clearSmartOverride`
      пересчитывают результат из сохранённых кандидатов, флаги override отдельны
      от автоматического состояния. Поиск учитывает override.
- [x] **6. Resolver policy.** Half/double BPM → ambiguous, variable tempo без
      единственного числа, энгармоника без конфликта, relative key → ambiguous,
      Pitch отдельно от Key/RootNote.
- [x] **7. Service.** Режимы incremental/selected, очередь, pause/resume/cancel,
      fingerprints до/после, изоляция ошибок по файлам, проверка каталога и
      ревизии, прогресс по этапам.
- [x] **8. UI wiring (минимально).** Кнопка запуска smart-анализа и отображение
      прогресса; без колонок и инспектора.
- [x] **9. Tests.** Миграция, атомарность, сохранность overrides, изменение и
      удаление sidecar, отмена, смена каталога, отсутствие пересчёта basic
      features, версии, half/double, энгармоника, Pitch vs Key.
- [x] **10. Docs and graph.** ARCHITECTURE, SMART_ANALYSIS, README, `graphify update`.

## Acceptance for this slice

- Повторный запуск после изменения sidecar пересчитывает результат.
- Ошибка/отмена оставляет предыдущий завершённый результат.
- Override переживает reanalysis и не теряется при гонке с job.
- Старый job не перезаписывает более новый результат.
- `basicFeaturesVersion` не меняется, hash/descriptors не пересчитываются.
- `ctest` на macOS проходит.
