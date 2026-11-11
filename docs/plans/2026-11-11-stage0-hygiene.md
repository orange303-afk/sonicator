# Этап 0: гигиена проекта

Цель этапа — подготовить проект к публикации и CI, не добавляя функций: лицензия, документы,
сборка вне iCloud, аудит репозитория. ТЗ: `~/.opencode/plan/2026-11-11-sonicator-stage0-hygiene-brief.md`.

## Steps
- [x] S1. Ветка `chore/stage0-hygiene`, исходная точка, базовый прогон тестов.
- [x] B1. `CMakePresets.json`: сборка в `~/Library/Caches/sonicator-build`, CI-пресеты.
- [x] B2. Новая сборка Debug и Release, проверка подписи на месте, перенос `manual-validation`.
- [ ] B3. Удаление старых `build`, `build-release`, `build-universal`.
- [x] L1. `LICENSE` (AGPL-3.0-or-later), раздел License в README.
- [x] L2. `THIRD_PARTY_NOTICES.md`, актуализация `docs/DEPENDENCIES.md`.
- [x] L3. Строка о лицензии в About и тест; проверка панели в обеих темах.
- [x] D1. `docs/ARCHITECTURE.md` под текущий код.
- [x] D2. `README.md`.
- [x] D3. `docs/BACKLOG.md` как таблица статусов.
- [x] D4. Отметки в внешнем бэклоге владельца.
- [x] D5. Раздел этапа в `docs/TESTING.md`.
- [x] P1. Аудит перед публикацией на свежем клоне.
- [ ] F1. Итоговый прогон, merge в `main`, push.
- [ ] Часть B: публикация репозитория, CI, Windows, формы issue.

## Ledger

Pre-flight: репозиторий в `~/Documents`, который синхронизирует iCloud. В `.git` нет pack-файлов
(258 каталогов несжатых объектов) и лежат дубликаты `index 2`, `index 3`. Команды, обходящие всю
историю (`git log` без `-n`, `git rev-list --count`) и всё дерево (`grep -r`, ripgrep по корню),
зависали дольше 60 с. Это наблюдение записано в память проекта.

Baseline: `main` `72f6387`, рабочее дерево чистое. Каталоги `build` 3.4 GiB, `build-release` 609 MiB,
`build-universal` 842 MiB; свободно 9.3 GiB из 460.

Fact: в старом `build/` 472 файла получили флаг `dataless` от iCloud, включая исходники Catch2 и
объектные файлы. Компилятор на них блокируется в `pread` на минуты при нулевом CPU (снято `sample`).
Следовательно исходный прогон тестов в существующем `build` невозможен: это не медленная сборка, а
ожидание выгрузки файлов из облака. Дополнительный симптом — дубликат `build/SonicatorUI.dir/ui/settings/SettingsPanel.cpp 3.o`.

Ruling: базовый и все последующие прогоны выполняются в каталоге вне iCloud
(`~/Library/Caches/sonicator-build`, пресеты `debug`/`release`), а существующий `build` удаляется
после зелёных прогонов. Цена: повторная загрузка зависимостей (~0.5 GiB) и полная пересборка;
изменение ТЗ, где baseline предполагался в старом каталоге.

Fact: в новом каталоге компиляция идёт на 94 % CPU, конфигурация и сборка Debug завершаются без
зависаний — подтверждает причину выше.

Fact: `settings.json` владельца изменил размер с 1115 до 1116 байт и время (18:25) во время этапа.
Ни одна команда этапа не открывала этот файл на запись; приложение владельца в это время не
работало (`pgrep` пуст). Записано как внешнее изменение, до и после работы файл не наш.

Task S1: complete. Baseline в новом каталоге: Debug собрана, `ctest --preset debug` passed (28.28 s).

Ruling B1: `CMakePresets.json` версии 3 (CMake >=3.22), пресеты `debug`, `release`, `universal` и
`ci-macos-debug`, `ci-macos-release`, `ci-windows`. Каталог сборки — `$env{HOME}/Library/Caches/sonicator-build/<preset>`;
в CI — `${sourceDir}/build`. Цена: у пресетов нет опции плагина, спайк по-прежнему собирается своей
командой.

Task L1: complete. `LICENSE` — неизменённый текст GNU AGPL v3 с gnu.org (661 строка,
sha256 `0d96a4ff68ad6d4b6f1f30f713b18d5184912ba8dd389f86aa7710db079abcb0`).

Task L2: complete. `THIRD_PARTY_NOTICES.md` перечисляет JUCE 9.0.5, SQLite 3.47.2, libxmp 4.6.2,
ONNX Runtime 1.20.1, zlib, Catch2, VelociLoops и сторонний код внутри модулей JUCE по
`JUCE.spdx.json`. `docs/DEPENDENCIES.md` переписан: лицензия проекта — AGPL-3.0-or-later, ONNX Runtime
и zlib добавлены.

Fact L2: `core/ml/OnnxAudioClassifier` загружает модель только через `loadModel`/`loadModelFromMemory`,
и ни один вызов в `core/`, `ui/`, `app/` этого не делает — тесты используют `loadModelFromMemory` с
синтетической моделью. В продукте модель не грузится, `isLoaded()` всегда false, ветка
`ml_classifier` в `AcousticClassifierService` недостижима, а ONNX Runtime (десятки мегабайт в бандле)
линкуется впустую. Решение оставить/убрать — за владельцем, записано в `docs/BACKLOG.md` и
`docs/DEPENDENCIES.md`.

Task L3: complete. Тест в `tests/TestUI.cpp` сначала падал на `REQUIRE(licence)`; после правки
`creditsLabel` и высоты блока (44 → 58 px) проходит. Тот же тест теперь проверяет, что высота метки
вмещает четыре строки, и рендерит панель в тёмной и светлой теме.

Fact L3: при переключении темы панель настроек сохраняла белый цвет у `title` и `productLabel` —
у них не задавался `Label::textColourId`, поэтому в светлой теме заголовок «Settings» и «Sonicator»
были невидимы. Исправлено двумя строками в `SettingsPanel::lookAndFeelChanged`; проверено снимками
560×800 в обеих темах. Дефект существовал до этапа и виден пользователю, который переключает тему при
открытых настройках.

Task B2: complete. Debug собран и проверен, Release собран и проверен. `ctest --preset debug` —
26.10 s, `ctest --preset release` — 22.48 s; бинарь в обеих конфигурациях: 651 тест-кейс,
10 549 (Debug) и 10 551 (Release) утверждений. Подпись бандла в новом каталоге проверена на месте:
`codesign --verify --deep --strict` → `valid on disk`, `satisfies its Designated Requirement`, без
`xattr` и без копирования. `build/manual-validation` (11 файлов, 54 MB) перенесён в
`~/Library/SonicatorDev/manual-validation`, число файлов и размер совпали.

Task B3: complete. Перед удалением: оба прогона зелёные, копия сверена, `pgrep` не показал
запущенного приложения. Три точных пути проверены `ls -d` и удалены. Свободно до удаления 6.7 GiB,
после — 11 GiB. Новый Release-бандл для запуска:
`~/Library/Caches/sonicator-build/release/Sonicator_artefacts/Release/SONICATOR.app`.

Task D5: complete. `docs/TESTING.md` начинается разделом этапа с числами, подписью и списком
непроверенного; заголовок документа больше не «Phase 1 verification».

Task F1: (заполняется при merge).

Task D1: complete. Заголовок без «Phase 1»; схема `user_version=12` с описанием версий 8–12 по коду
миграций и планам (`ignored_paths` — схема 11 по `docs/superpowers/plans/2026-11-09-library-ignore.md`);
новые разделы о плагине, тэгах и AutoTag, акустической классификации, образах дисков; строка
«Standalone GUI target only» заменена фактической.

Task D2: complete. README: сборка через пресеты с объяснением iCloud, полный список каталогов,
раздел License, раздел Limits без «Do not begin until Phase 1».

Task D3: complete. `docs/BACKLOG.md` — таблица 17 пунктов со статусами и решения владельца по ONNX и
переносу репозитория.

Task D4: complete. Во внешнем файле бэклога отмечены пункты 14, 15, 16, 17. Пункт 16 отмечен после
чтения `WaveformDisplay::mouseWheelMove`: колесо масштабирует выделение вокруг центра и публикует
живое выделение, то есть изменение размера скроллом реализовано.

Task P1: complete (клен `~/Library/Caches/sonicator-audit`, 142 коммита).
- Секреты в истории: нет (`ghp_`, `github_pat_`, `sk-`, `AKIA`, приватные ключи, `Authorization:`).
- `.env`, `*.sqlite`, `settings.json`, ключи и сертификаты в историю не попадали.
- Крупнейшие блобы: `resources/sqlite/sqlite3.c` 9.2 MB, оригиналы иконок 0.5–1.5 MB. Коммерческого
  аудио, образов дисков и банков в истории нет.
- Автор и почта во всех коммитах: `orange303 <242139551+orange303-afk@users.noreply.github.com>` —
  только noreply-адрес GitHub.
- Домашние пути `/Users/ilyaorange` найдены в 4 местах HEAD (`AGENTS.md` дважды, `docs/BACKLOG.md`,
  один план) и в двух коммитах истории. Заменены на `~/...` в текущих файлах; историю не переписывали.
- `tests/fixtures/*` — синтетические тоны 440 Гц, сгенерированные локально; `resources/branding/*` —
  оригинальная графика автора.
- `.superpowers`, `graphify-out`, `.opencode` в историю не попадали.
- `docs/TESTING.md` и планы упоминают коммерческие библиотеки и `/Volumes/1TBSSD/...` как ссылки на
  источники проверки. Содержимого там нет; оставлено как есть.
