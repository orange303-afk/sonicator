# VST3/AU, этап 1: технический спайк

Цель этапа — не продукт, а ответы. Спайк собирает одноразовый плагин `Sonicator Spike`, в окне
которого живёт существующий `MainComponent` без аудиоустройства, и проверяет, выдерживает ли
текущая кодовая база сборку, подпись и работу внутри DAW. По результату принимается решение:
идти дальше по плану с общим кодом `SonicatorCore`/`SonicatorUI` или менять архитектуру.

ТЗ исполнителю: `~/.opencode/plan/2026-11-11-sonicator-vst3-au-plugin-spike-brief.md`.

## Steps
- [x] A1. Ветка `spike/vst3-au-plugin`, копия плана в `docs/plans/`, опция CMake `SONICATOR_BUILD_PLUGIN_SPIKE` (по умолчанию OFF), каталог `plugin/`.
- [x] A2. Процессор-заглушка (инструмент, стерео-выход, тишина) и редактор с `MainComponent(openDevice=false)`.
- [x] A3. Самодостаточные бандлы: `libonnxruntime` в `Contents/Frameworks`, относительный rpath, манифест VST3, ad-hoc подпись.
- [x] A4. Установка в `~/Library/Audio/Plug-Ins`, автоматические проверки: `codesign`, `otool`, `auval`.
- [x] A5. Регрессия: сборка `build` с выключенной опцией и полный `ctest`.
- [x] A6. Отчёт в Ledger, `graphify update .`, локальные коммиты в ветке.
- [ ] U1. Ручная приёмка в хостах по чек-листу ниже (владелец).
- [ ] U2. Решение go / no-go и уточнение плана этапов 2–7.

Шаги A1–A6 выполняет агент, U1–U2 — владелец: агент не может управлять окнами DAW.

## Вопросы спайка и критерии

| № | Вопрос | Критерий «да» | Кто |
|---|---|---|---|
| Q1 | VST3 и AU собираются Ninja + Command Line Tools, без Xcode | оба бандла собраны | агент |
| Q2 | `SonicatorUI`/`SonicatorCore` линкуются в плагин как есть | нет duplicate symbol; предупреждения записаны | агент |
| Q3 | Бандл переносим | `otool` не показывает путей в дерево сборки, `libonnxruntime` внутри бандла | агент |
| Q4 | Подпись валидна | `codesign --verify --deep --strict` проходит для обоих бандлов | агент |
| Q5 | AU проходит валидацию | `auval -v aumu Snsp Orng` → `AU VALIDATION SUCCEEDED` | агент |
| Q6 | VST3 загружается вне хоста | `moduleinfo.json` создан помощником JUCE (он делает `dlopen` бандла) | агент |
| Q7 | Хосты видят и загружают плагин, окно открывается | Live 12, REAPER, Cubase 15, Bitwig | владелец |
| Q8 | Drag из окна плагина создаёт клип на дорожке | строка библиотеки и выделение на волне, в каждом хосте | владелец |
| Q9 | Клавиатура доходит до плагина | ввод в поиск, стрелки по списку | владелец |
| Q10 | Жизненный цикл без падений | повторное открытие окна, два экземпляра, удаление, выход из хоста | владелец |
| Q11 | Время открытия окна на каталоге ~300k строк | значение из `spike.log` | агент пишет лог, владелец снимает |

## Вне этапа
Звук из плагина (этап 3), состояние в проекте DAW, параметры, Match BPM / Match Scale, совместная
работа плагина и приложения над одним каталогом, universal-сборка, Windows, Developer ID и
нотаризация, выбор окончательных идентификаторов и лицензии JUCE. Спайк не чинит найденные
проблемы в `core/` и `ui/`, а фиксирует их.

## Interfaces
- CMake: `option(SONICATOR_BUILD_PLUGIN_SPIKE ... OFF)`; цели `SonicatorPluginSpike`, `SonicatorPluginSpike_VST3`, `SonicatorPluginSpike_AU`.
- Новые файлы: `plugin/CMakeLists.txt`, `plugin/SpikeProcessor.{h,cpp}`, `plugin/SpikeEditor.{h,cpp}`, при необходимости скрипт в `cmake/`.
- Временные идентификаторы: продукт `Sonicator Spike`, производитель `Orng` / `Ilya Orange`, код `Snsp`, bundle id `com.orangeaudio.sonicator.spike`.
- Данные спайка: `~/Library/Sonicator/plugin-spike/` — `sonicator.sqlite` (копия каталога), `settings.json`, `cache/`, `spike.log`.
- Дерево сборки: `~/Library/Caches/sonicator-plugin-spike` (вне iCloud).
- Потребляется без изменений: `sonicator::MainComponent(databaseFile, openDevice, initialSettings, settingsFile)`.

## Ручная приёмка (U1)

Закрывать SONICATOR.app не обязательно: спайк работает со своей копией каталога.
В окне плагина не использовать Settings → смена каталога и Audio/MIDI Settings: первое может
переключить спайк на рабочий каталог, второе откроет аудиоустройство внутри хоста.
Звука нет: Play ничего не воспроизводит, это ожидаемо.

Форматы: Live 12 — VST3 и AU; REAPER — VST3 и AU; Cubase 15 — VST3; Bitwig — VST3.

| № | Проверка | Live VST3 | Live AU | REAPER VST3 | REAPER AU | Cubase | Bitwig |
|---|---|---|---|---|---|---|---|
| 1 | После рескана виден инструмент «Sonicator Spike» | | | | | | |
| 2 | Загружается на инструментальную дорожку без ошибки | | | | | | |
| 3 | Окно открывается, список каталога заполнен | | | | | | |
| 4 | Размер окна меняется за угол, раскладка не ломается | | | | | | |
| 5 | Текст вводится в поле поиска, результат фильтруется | | | | | | |
| 6 | Стрелки вверх/вниз двигают выбор в списке | | | | | | |
| 7 | Пробел: что происходит (транспорт хоста или плагин) | | | | | | |
| 8 | Drag одной строки на аудиодорожку создаёт клип | | | | | | |
| 9 | Drag нескольких выделенных строк создаёт клипы | | | | | | |
| 10 | Выделение на волне → «Ready», drag создаёт клип | | | | | | |
| 11 | Закрыть и открыть окно три раза | | | | | | |
| 12 | Второй экземпляр на другой дорожке, оба окна открыты | | | | | | |
| 13 | Сохранить проект, закрыть, открыть: плагин загружается | | | | | | |
| 14 | Удалить плагин, выйти из хоста: без зависания и падения | | | | | | |

В ячейку: «да», «нет» или короткая заметка. После прохода приложить `spike.log`.

## Решение (U2)
- **Go**: Q1–Q6 «да»; в трёх из четырёх хостов плагин загружается, окно открывается и drag строки создаёт клип; падений в Q10 нет.
- **Go с оговорками**: drag или клавиатура не работают в одном хосте — фиксируем обход для него и идём дальше.
- **No-go для текущей архитектуры**: бандл нельзя собрать или подписать без Xcode, либо окно падает или зависает в двух и более хостах из-за общего состояния. Тогда пересматриваем план: отдельный процесс-компаньон с лёгким окном плагина.

## Ledger
Pre-flight: спайк опирается на то, что `MainComponent` уже создаётся без приложения и без устройства
(так делают UI-тесты); в VST3/AU `JUCEApplicationBase::getInstance()` равен nullptr, поэтому
регистрация паков и запись сессии выключаются сами.
Baseline: main `bf49a72`, `ctest --test-dir build --output-on-failure` — passed (48.73 s), 2026-11-11.
Fact: JUCE 9.0.5 содержит обёртки VST3 и AU; по `docs/CMake API.md` генератор Xcode обязателен только для AUv3.
Fact: `~/Documents` синхронизируется iCloud, бандл приложения в `build/` несёт `com.apple.FinderInfo`
и `com.apple.fileprovider.fpfs#P`; из-за этого существует `cmake/SignMacBundle.sh`.
Fact: свободно 13 GiB; `build` (Debug с тестами) занимает 3.9 GiB, `build-release` — 652 MiB, каталог — 0.9 GiB.
Fact: `libonnxruntime.1.20.1.dylib` — universal, install name `@rpath/...`, подпись ad-hoc linker-signed;
приложение сейчас находит её только по абсолютному rpath в `build/_deps`.
Fact: шаг манифеста VST3 в JUCE загружает собранный бандл на этапе сборки, поэтому библиотека
должна быть внутри бандла раньше этого шага; порядок задаётся `VST3_AUTO_MANIFEST FALSE` и
`juce_enable_vst3_manifest_step`.
Ruling: редактор спайка владеет `MainComponent` целиком, процессор пуст — это самый дешёвый способ
проверить сборку, подпись, окно и drag; цена: звука нет и каталог открывается при каждом открытии окна.
Ruling: спайк работает с копией каталога в `~/Library/Sonicator/plugin-spike/`, снятой `sqlite3 .backup`,
и со своим `settings.json`; цена: 0.9 GiB диска и нет проверки совместного доступа с приложением.
Ruling: дерево сборки вне iCloud и в конфигурации Release без тестов; цена: отдельный каталог
сборки и нет отладочных символов.
Ruling: формат Standalone не собирается — в нём приложение JUCE существует, включились бы
регистрация паков и запись сессии.
Ruling: идентификаторы временные и отличаются от будущих релизных, чтобы кэши хостов не связали
спайк с настоящим плагином; окончательные коды остаются решением владельца.
Ruling: выделение на волне экспортируется в общий `~/Library/Sonicator/exports` — путь задан
внутри `MainComponent`, а спайк не меняет `ui/`; цена: несколько новых WAV в рабочей папке.
Task A1-A4: complete. Сборка VST3 и AU проходит Ninja + Command Line Tools без Xcode.
Task A5: complete. Отдельная сборка с `SONICATOR_BUILD_PLUGIN_SPIKE=OFF` (Debug, BUILD_TESTING=ON):
целей плагина нет, ctest passed (138.33 s). Существующий `build` после правки `CMakeLists.txt`:
пересборка ok, ctest passed (65.82 s).
Q1: да. `SonicatorPluginSpike_VST3`, `SonicatorPluginSpike_AU`, `SonicatorPluginSpike_Smoke` собираются
без Xcode, только Command Line Tools.
Q2: да. В чистой пересборке нет `duplicate symbol`, `multiple definition` и `ld: warning`. Новых
предупреждений три: `-Wtautological-constant-out-of-range-compare` в `core/disc/AkaiSampleReader.cpp:63`,
два `-Wdeprecated-declarations` (`userArea`) в `app/Main.cpp`, плюс наследуемые от libxmp
`-Wtautological-compare` и пять `-W#pragma-messages` JUCE про splash screen. Первые два — существующий
код, плагин их просто компилирует повторно.
Q3: да. `otool -l` показывает один `LC_RPATH`: `@loader_path/../Frameworks`; зависимость —
`@rpath/libonnxruntime.1.20.1.dylib`, копия лежит в `Contents/Frameworks/libonnxruntime.dylib`.
Абсолютных путей в дерево сборки в бандлах нет.
Q4: да. `codesign --verify --deep --strict` для обоих установленных бандлов: `valid on disk`,
`satisfies its Designated Requirement`.
Q5: да. `auval -v aumu Snsp Orng` → `AU VALIDATION SUCCEEDED` после `killall -9 AudioComponentRegistrar`.
Q6: да. `Contents/Resources/moduleinfo.json` — 1065 байт, класс `Sonicator Spike`, CID
`ABCDEF019182FAEB4F726E67536E7370`, категории `Instrument`, `Synth`; помощник JUCE загрузил бандл
на этапе сборки.
Q11 (предварительно): `editor-open ready` в `spike.log` для редактора без хоста:
`constructMs=4021.4` на копии каталога в 350 788 строк; редактор открылся и закрылся без падения.
Ruling: библиотека лежит в бандле под именем `libonnxruntime.dylib`, а load command в бинарнике
переписывается `plugin/cmake/EmbedDylib.sh` на `@rpath/libonnxruntime.dylib`; цена: один
`install_name_tool` после линковки и обязательная подпись после него. Без перезаписи dyld искал
`libonnxruntime.1.20.1.dylib`, которого в `Frameworks` нет, и `auval` падал с `OpenAComponent: -1`.
Ruling: шаг манифеста VST3 объявлен после встраивания и до подписи; JUCE сам подписывает бандл
раньше, поэтому первая подпись недействительна и заменяется `plugin/cmake/SignMacPluginBundle.sh`.
Ruling: спайк дополнительно собирает `SonicatorPluginSpike_Smoke` — обычное GUI-приложение с теми же
`SpikeProcessor`/`SpikeEditor`, которое открывает окно и выходит через 4 с; цена: ещё одна цель,
зато падение `MainComponent` видно до передачи плагина в DAW.
Ruling: `juce_add_plugin` требует `JUCE_VST3_CAN_REPLACE_VST2=0` в сборке без VST2, иначе
`juce_audio_plugin_client_VST3.cpp` останавливает компиляцию `#error` про конфликт параметров.
Ruling: `SonicatorPluginSpike_Smoke` не линкует цель плагина, а компилирует те же файлы: у
`juce_add_plugin`-цели публичная линковка только с JUCE-модулями, и заголовки `SonicatorUI` через
неё не передаются.
Final verification: 2026-11-11, `auval` PASS, подпись и `dlopen` обоих установленных бандлов OK,
`moduleinfo.json` на месте, регрессия дважды passed. Окно в хостах и drag не проверялись — это U1.
Установлено: `~/Library/Audio/Plug-Ins/VST3/Sonicator Spike.vst3` и
`~/Library/Audio/Plug-Ins/Components/Sonicator Spike.component`. Данные спайка:
`~/Library/Sonicator/plugin-spike/` (копия каталога 0.86 GiB), сборка:
`~/Library/Caches/sonicator-plugin-spike` (355 MiB). Рабочий каталог и `settings.json` владельца
не изменились (mtime 01:05, до начала работы).
