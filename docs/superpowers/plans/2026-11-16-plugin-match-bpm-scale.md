# Plugin Match BPM / Match Scale — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Две отключаемые галочки возле Play/Stop только в плагине; выбранный семпл слышен и экспортируется с одинаковым tempo/pitch match.

**Architecture:** Processor публикует host tempo атомарно. Чистая функция рассчитывает MatchPlan; постоянный latest-wins worker рендерит неизменяемую WAV-копию. UI показывает результат без подмены catalog identity и передаёт тот же файл в общие drag hooks. Никакого realtime stretching.

**Tech Stack:** C++20, JUCE 9.0.5, Catch2, Rubber Band offline R3 (новая pinned dependency, отдельная проверка portable static build).

**Spec:** `/Users/ilyaorange/.opencode/plan/2026-11-16-plugin-match-spec.md`.

**Status:** ТЗ утверждено владельцем 2026-11-16; документы перенесены в `docs/superpowers/`. Реализация задач 1–6 и автоматическая часть задачи 7 выполнены в `stage-plugin-match`; ручные host/validator/CI проверки явно оставлены открытыми в `docs/PLUGIN_MATCH_ACCEPTANCE.md`.

## Global Constraints

- Только plugin UI; обе галочки по умолчанию выключены, рядом с Play/Stop.
- Preview и drag одного подтверждённого выбора используют один готовый WAV.
- Host tempo из JUCE playhead, key вручную; никаких фиктивных 120 BPM или DAW key.
- TimeRatio 0.5…2.0; semitones −6…+6, тритон +6; mode mismatch не выдавать за смену лада.
- WAV PCM24, исходные rate/channels; mono/stereo, до 10 мин, worker memory budget 256 MiB.
- ProcessBlock только публикует tempo snapshot и воспроизводит готовый звук; без I/O/DSP jobs/stateLock.
- Не менять catalog schema, исходники, уже экспортированные WAV или exports/drag. Published exports/match не чистить автоматически.
- Все тесты settings/catalog/exports в TestDirectory; live library только отдельная явная read-only приёмка.
- Не помечать ручную приёмку, CI или validators завершёнными без фактического прогона.

## Review Focus

1. Старые/повреждённые plugin states, restore при открытом editor и два instance — независимые флаги, без autoplay (задача 1, 5).
2. Host без playhead, явный invalid BPM и suspend/resume — no fake tempo и no stale result при drag (задача 1, 4, 6).
3. Несохранённая волна и MIDI preview — отмена не теряет edits, MIDI не получает второй pitch shift (задача 5).
4. Другой файл/диапазон/metadata revision при готовящемся результате — generation/identity не дают чужой WAV (задача 4, 6).
5. Два instance с одним output key, нехватка места, повреждённый cache и закрытие окна — atomic no-clobber, без удаления exported audio (задача 3, 4).

## Карта файлов

- `plugin/HostTempoSnapshot.h`: compact atomic публикация host context.
- `plugin/PluginProcessor.h/.cpp`, `PluginState.h/.cpp`: host BPM, per-instance match prefs, backward-compatible state v2.
- `core/match/MatchTypes.h`, `MatchPolicy.h/.cpp`: типы и чистый расчёт, без UI/БД.
- `core/match/OfflineMatchRenderer.h/.cpp`: offline DSP, length contract, WAV metadata, atomic публикация.
- `core/match/MatchRenderService.h/.cpp`: latest-wins worker, progress/error/poll, не владеет UI.
- `cmake/RubberBand.cmake`, `CMakeLists.txt`, `THIRD_PARTY_NOTICES.md`: pinned static dependency, не системный пакет.
- `ui/preview/PluginMatchControls.h/.cpp`: галочки и target key controls.
- `ui/preview/PreviewPanel.h/.cpp`: prepared read-only source, selection/export routing, guards.
- `core/audio/AudioEngine.h/.cpp`, `core/audio/MidiSampler.h/.cpp`: независимый matched transport при сохранённом original MIDI sampler/container; никаких NoteOn-triggered перезагрузок.
- `ui/MainComponent.h/.cpp`, при необходимости новый `ui/preview/PluginMatchCoordinator.h/.cpp`: source metadata, controller, plugin context. Не раздувать MainComponent алгоритмом DSP.
- `ui/library/FileListPanel.h/.cpp`, `ui/cloud/CloudView.h/.cpp`: interception prepared drag при активном match.
- `plugin/PluginEditor.h/.cpp`: контекст и poll UI, restore вместе с поколением plugin state.
- `tests/TestPluginMatchState.cpp`, `TestMatchPolicy.cpp`, `TestOfflineMatch.cpp`, `TestMatchRenderService.cpp`, `TestPluginMatchUI.cpp`, `TestMatchedDrag.cpp`; регистрация в `tests/CMakeLists.txt`.

## Задача 1. BPM хоста и per-instance state

**Interfaces:** `HostTempoSnapshot::publish(std::optional<double> bpm) noexcept`, `read() noexcept -> HostTempoValue`; `PluginMatchPreferences { bool matchBpm=false, matchScale=false; int targetTonic=0; TargetMode targetMode=Minor; }`. HostTempoValue несёт valid/bpm/revision без ссылок на JUCE UI. Выбор coherent atomic представления проверить is_always_lock_free на целевых платформах; не несколько несогласованных mutable полей.

- [x] Написать TestPluginMatchState: old v1 defaults off; четыре комбинации flags round-trip; tonic 0…11, malformed enum/oversize state безопасно; открытый editor получает restore; два processors изолированы.
- [x] Написать fake AudioPlayHead: 120→128, missing position, NaN/0, stopped transport; processBlock публикует валидность и новый BPM, releaseResources инвалидирует. BPM не попадает в persisted state.
- [x] Запустить failing tests: `cmake --build --preset debug --target SonicatorTests`, затем `$HOME/Library/Caches/sonicator-build/debug/tests/SonicatorTests '[plugin-match]'` — должны отсутствовать новые API/функциональность.
- [x] Реализовать atomic bridge, plugin state v2 и безопасное чтение playhead без stateLock. Существующие session/window поля и restore generation сохранить.
- [x] Повторить `[plugin-match]` и существующие `[plugin]` → green; commit `feat(plugin): publish host tempo and persist match preferences`.

## Задача 2. Чистая policy совпадения

**Interfaces:** `MatchPlan resolveMatch(const MatchSource&, const PluginMatchPreferences&, HostTempoValue)`; `MatchSource` включает source identity/range, effective BPM/key/mode/root/pitch и их validity, loop/one-shot/unsupported. `MatchPlan` включает `Bypass|Transform|Blocked`, reason enum, timeRatio, semitones, targetFrames, target metadata. На ошибках нет неявного fallback.

- [x] TestMatchPolicy: 8 sec at 44100/120→128 = 330750 frames (7.5 s); unchanged pitch; Am→Cm=+3; Cm→Am=−3; tritone=+6; identity=Bypass, оба off bypass независимо от unknown metadata.
- [x] Тесты one-shot BPM inapplicable, missing/stale/conflicting metadata, mode mismatch, tonic-only root for tonal shot; ratio 0.5 и 2 допустим, вне — Blocked; NaN/overflow — Blocked; оба режима совместно рассчитывают длину один раз.
- [x] Подтверждённый one-shot с missing host BPM: BPM-only → Bypass, BPM+Scale → pitch-only Transform при валидной высоте. Host tempo проверять после applicability.
- [x] Проверить RED, реализовать `MatchTypes`/`MatchPolicy` и выбор effective source metadata из существующего SmartDisplay/SmartResult (не парсить строковый caption); повторить `[plugin-match][policy]` → green.
- [x] Commit `feat(match): resolve tempo and tonal transforms without guessing`.

## Задача 3. Offline DSP и неизменяемые outputs

**Interfaces:** `MatchRenderResult renderMatch(const MatchRenderRequest&, CancelCheck)`; request содержит source descriptor/range, MatchPlan, fingerprint и output root. Result содержит `file, frames, rate, channels, key, error`, а не AudioEngine/UI pointer.

- [x] Добавить failing fixtures: sustained sine (pitch accuracy ±5 cents), stereo identical/inverted, impulse/transient train, silence, odd frame counts. TestOfflineMatch: BPM-only сохраняет pitch, pitch-only сохраняет frames, combo даёт обе цели, no NaN/Inf и silence→silence.
- [ ] Проверить upstream release/компиляцию Rubber Band и точный набор source/backend: pinned archive+hash, R3/offline, без лишних CLI/плагинов/FFTW; standalone behavior без изменений. Проверить macOS arm64/x86_64 и Windows build requirements. Дополнить notices и dependency docs.
- [x] Реализовать bounded block study/process/retrieve + drain и минимальную length коррекцию ≤1 мс; большая ошибка → fail. Не обрезать атаку ради длины и не нормализовать материал автоматически.
- [x] Добавить metadata tests: new BPM/root, no stale loop offsets/slices; decode/reopen проверяет output format/frames. Identity recheck после рендера ловит changed source.
- [x] Тесты atomic no-clobber: два renderer с одним key получают валидный файл, corruption не переиспользуется; disk-full через injectable writer, partial не публикуется; прежний опубликованный WAV не трогается при новом target.
- [x] RED→GREEN для `[plugin-match][render]`, затем commit `feat(match): render offline tempo and pitch matched WAVs`.

## Задача 4. Отменяемая background служба

**Interfaces:** `MatchRenderService::submit(MatchRenderRequest) -> uint64_t`, `cancel()`, `poll() -> MatchRenderState { generation, status, result, error }`. Persistent worker + one pending request; submit не ждёт старый render. Controller знает актуальное generation и source identity.

- [x] TestMatchRenderService с fake renderer/barrier: rapid A→B→A, latest-wins queue, no stale ready, cancel/close, exception, unsupported/error; editor disappearance не освобождает объект на audio thread.
- [x] Тестировать tempo updates debounce 250 мс и последний snapshot перед drag: старый результат не Ready для нового desired key; host invalid блокирует cache hit при применимом к лупу Match BPM. Для подтверждённого one-shot отсутствие host BPM не блокирует независимый Scale.
- [x] Проверить RED, реализовать service с RAII shutdown и catch ошибок; worker progress без UI callbacks на каждый sample/block, публикация через poll.
- [x] GREEN и commit `feat(match): prepare latest matched sample in background`.

## Задача 5. Галочки возле Play и preview без потери identity

**Interfaces:** plugin editor передаёт `PluginMatchContext` в MainComponent/координатор: prefs get/update и host snapshot get. `PreviewPanel::loadPreparedPreview(sourceIdentity, preparedFile)` не заменяет original ID/path; `restoreOriginalPreview()` восстанавливает normal source. Конкретные имена интеграционных методов фиксировать до code edits после проверки текущего PreviewPanel.

Engine interfaces: `bool AudioEngine::setMatchedTransport(const juce::File& preparedFile)`, `void AudioEngine::clearMatchedTransport()`. Prepared reader удерживается отдельно от original MIDI sampler/REX container; loadFile(preparedWav) для этого не использовать. Storage lifecycle и переключение Play/MIDI не требуют ленивой перезагрузки на первую NoteOn.

- [x] TestPluginMatchUI: standalone controls absent; plugin две toggle рядом с Play/Stop на 880 px и при 100/125%; target picker enabled только при Scale; defaults off; toggles independent; restore не autoplay.
- [x] Integration fixture: выбрать исходник из списка/точки → исходный ID остаётся, waveform получает matched duration/file; preview и full drag далее используют тот же result key.
- [x] Проверить Save/Discard/Cancel guard: Cancel не меняет flags/file/selection; transformed waveform read-only, edit commands disabled; обе off восстанавливают оригинал. Проверить selection crop обработанного результата без второго pitch/stretch.
- [x] Тестировать Stop во время подготовки (новый Ready не стартует автоматически), source A→B, новые target prefs, отсутствие metadata, mismatch ладов; MIDI играет original pitch при matched Play и не получает двойную обработку. Hover clearly original, не submit на каждое наведение.
- [x] Отдельный engine/selection RED→GREEN gate: первая NoteOn сразу играет original source/slice, включая REX mapping; matched waveform selection влияет только на processed Play/drag, transformed seconds не идут в setMidiSelection. Both off восстанавливает normal selection-MIDI связь; set/clearMatchedTransport не теряет original sampler/container/ID.
- [x] RED→GREEN; реализовать controls и controller, маленькие MainComponent hooks. State принадлежит processor, не global settings. Reader swap через AudioEngine lifecycle вне worker/callback.
- [x] Commit `feat(plugin): add switchable match controls next to transport`.

## Задача 6. Общий drag routing

**Interfaces:** provider `resolveMatchedDrag(sourceIdentity/range) -> Ready(file)|Pending|Blocked(reason)`. FileListPanel, CloudView, PreviewPanel вызывают его до старого materialise/selection export; при bypass используют существующий путь без изменений.

- [x] TestMatchedDrag с fake external drag handler: список, облако, whole preview дают одинаковый prepared WAV; selection вырезан из него, duration соответствует выбранному transformed range.
- [x] Pending/failed/stale snapshot не вызывают OS drag; нельзя deferred OS gesture после callback. Unsupported source и batch >1 дают сообщение, не partial success. Both off воспроизводят прежние drag regression fixtures/metadata.
- [x] Проверить новый host BPM после Ready: old WAV hash и bytes не меняются; следующий gesture не отдаёт old result. Два instances не делят mutable prefs или current generation.
- [x] RED→GREEN; реализовать hooks без дублирования DSP в UI и без использования transient PreviewPanel selection как единственного source identity.
- [x] Commit `feat(plugin): share matched preview audio across drag entry points`.

## Задача 7. Проверка, сборки, приёмка

- [x] Debug/Release `[plugin-match]`, весь SonicatorTests; зафиксировать команды, exit codes и число тестов. Не использовать grep/head pipeline вместо проверки реального build exit code.
- [ ] VST3/AU plugin builds/validators плюс standalone regression: новые controls и render jobs в standalone отсутствуют. Windows CI и universal macOS фактически проверить; если не выполнены — явно оставить open.
- [ ] Прослушать lawful короткие drum/bass/pad/vocal loops на ratios 0.75/1/1.25/2 и shift ±3/±6; атака, стерео, бас, seamless boundary. Unit sine не доказывает музыкальное качество. Измерить preparation latency/memory на 4/16-bar loops и лимитном входе; показать реальные числа без обещания «мгновенно».
- [ ] В Cubase: 120 BPM/8 s loop → host128/7.5 s, Am→Cm +3, оба режима, both off, повторное открытие проекта, editor closed/open и два instances. Проверить Play, waveform, drag в timeline и reopen DAW после смены темпа — старые файлы доступны.
- [ ] При наличии Logic повторить AU; если недоступна, оставить ручную AU-приёмку open. Скриншоты транспорта 100/125% dark/light реальные.
- [x] Обновить `docs/PLUGIN.md`, acceptance notes, спецификации/план после разрешённого переноса, внешний backlog и AGENTS.md без ложных completed; `graphify update .`; review diff, commit и push согласно рабочему процессу проекта.

## Размер и handoff

Это ограниченный P2, но не косметическая задача: новая DSP dependency, фоновый lifecycle и развязка waveform/MIDI/drag. Не обещать выполнение за минуты. Самый короткий безопасный порядок — policy + offline renderer, затем один общий prepared-source path, затем UI и все drag hooks. Каждую задачу отмечать только по доказанной проверке; ручные/host gates не заменять checkbox массово.

Перед реализацией требуется утверждение ТЗ. В Plan mode не менять проект; пользователь должен переключить агента в режим выполнения.
