# VST3/AU, этап P1: плагин как продукт без нового DSP

Исходный спайк: `docs/plans/2026-11-11-vst3-au-plugin-spike.md`. ТЗ этапа — предложение
владельцу от 2026-11-15, принятое ответом «поехали». Спека отдельным файлом не заводится:
ниже зафиксированы требования, решения и границы этапа.

Цель: превратить `Sonicator Spike` в обычный плагин `SONICATOR`, который работает с той же
библиотекой и настройками, что приложение, хранит своё состояние в проекте DAW и не мешает
приложению длинными записями в каталог.

## Требования (ТЗ)

1. **Идентификаторы и имя.** Продукт `SONICATOR`, компания `Ilya Orange`, производитель `Orng`,
   код плагина `Sncr`, bundle id `com.sonicator.plugin`. Опция сборки — `SONICATOR_BUILD_PLUGIN`;
   без неё приложение не меняется. Временные `Snsp`/`Sonicator Spike`/`plugin-spike` удаляются.
2. **Общий каталог и настройки.** Плагин открывает `MainComponent::defaultDatabaseFile()` и
   `AppSettings::defaultSettingsFile()`; копии каталога и settings больше нет. Плагин — браузер:
   preview, drag, теги, звёзды, Genre, BPM. Скан, smart-анализ, Split, Extract, Batch, Normalize,
   смена каталога и добавление корней в плагине отключаются с пояснением.
3. **Состояние в проекте.** Версия формата, выбранный сэмпл, последняя папка, размер окна.
   Каталог не сохраняется. Восстановление без autoplay. `pluginLayout` остаётся в настройках;
   `lastFolder`/`lastSamplePath` приложения плагин не читает и не пишет.
4. **Гейты.** Audio/MIDI Settings и смена каталога недоступны; устройство не открывается;
   session state приложения не пишется; диалоги остаются встроенными.
5. **Сборка и установка.** Universal `arm64;x86_64`, `libonnxruntime` в бандле, ad-hoc подпись,
   `auval`, VST3 manifest; копирование в `~/Library/Audio/Plug-Ins` только по явной опции.
6. **Тесты.** State round-trip, повторное открытие редактора, два экземпляра, отсутствие
   открытия устройства, редактор без каталога, embedded `MainComponent` не пишет session state.
7. **Приёмка в хостах.** Ручной чек-лист для Live 12, REAPER, Cubase 15, Bitwig — владелец.

## Решения этапа

- `Ruling:` плагин и приложение делят живой каталог (WAL + `busy_timeout`), а тяжёлые операции
  остаются в приложении. Цена: пользователь не может сканировать библиотеку из DAW; зато нет
  второй копии каталога и расхождения между окнами.
- `Ruling:` состояние проекта хранится в XML-блоке процессора (`SONICATOR_PLUGIN`, version 1),
  а не в общем `settings.json`; `pluginLayout` по-прежнему общий. Цена: у каждого проекта свой
  выбор сэмпла и папки, как и ожидается от плагина.
- `Ruling:` Match BPM / Match Scale вынесены в P2: нужен time-stretch/pitch-shift и ответы
  хостов про темп/тональность. В P1 их нет.

## Steps

- [x] P1.1 Ветка, план, ledger.
- [x] P1.2 `PluginState` (version 1) + тест round-trip/мусор/клампы.
- [x] P1.3 Переименование целей и файлов, финальные идентификаторы, опция `SONICATOR_BUILD_PLUGIN`.
- [x] P1.4 Процессор: общий каталог/настройки, state в `getStateInformation`, хост-аудио.
- [x] P1.5 Редактор: общие пути, восстановление проекта без autoplay, размер окна, нет каталога.
- [x] P1.6 `MainComponent`: embedded session, гейты тяжёлых операций и записи session state.
- [x] P1.7 Тесты: state, два экземпляра, открытие/закрытие, нет устройства, нет каталога,
      embedded не пишет `lastFolder`/`lastSamplePath`.
- [x] P1.8 Сборка universal, подпись, manifest, `auval`, opt-in установка.
- [x] P1.9 CI: сборка VST3+AU и проверки `codesign`/`auval` на macOS.
- [x] P1.10 Документация, `graphify update .`, полный `ctest`, отчёт.

## Interfaces

- `plugin/PluginState.{h,cpp}` — `sonicator::plugin::PluginState`: `version`, `lastFolder`,
  `lastSamplePath`, `windowWidth`, `windowHeight`; `toMemoryBlock` / `fromMemoryBlock`.
- `plugin/PluginProcessor.{h,cpp}` — `PluginProcessor(databaseFile, settingsFile)`, `engine()`,
  `pluginState()`, `setPluginState`, `updateSession`, `updateWindowSize`.
- `plugin/PluginEditor.{h,cpp}` — редактор с `MainComponent`; component ID `pluginMain`,
  сообщение об отсутствии каталога — `pluginMissingCatalog`.
- `plugin/PluginFactory.cpp` — только `createPluginFilter`, чтобы процессор и редактор
  компилировались в тестовый бинарник без фабрики.
- `MainComponent`: `EmbeddedSession { lastFolder, lastSamplePath, onChanged }` в конструкторе,
  `selectedFolderPath()`, `selectedSamplePath()`; `saveSessionState` не пишет при `embeddedUi`.
- CMake: `option(SONICATOR_BUILD_PLUGIN ...)`, цели `SonicatorPlugin`,
  `SonicatorPlugin_VST3`, `SonicatorPlugin_AU`, `SonicatorPluginSmoke`;
  `option(SONICATOR_INSTALL_PLUGIN ...)`; preset `plugin-universal`.

## Приёмка в хостах (владелец)

Форматы: Live 12 — VST3 и AU; REAPER — VST3 и AU; Cubase 15 — VST3; Bitwig — VST3.

| № | Проверка |
|---|---|
| 1 | Плагин «SONICATOR» находится в списке инструментов |
| 2 | Загружается, окно открывается, список библиотеки заполнен |
| 3 | Поиск, стрелки, теги/звёзды/жанр/BPM работают |
| 4 | Drag строки и выделения на волне создаёт клип |
| 5 | Preview звучит, MIDI-клавиатура играет выбранный сэмпл |
| 6 | Кнопки скана/анализа/add folder отключены с пояснением; Audio/MIDI Settings нет |
| 7 | Окно закрыть/открыть три раза, без падения |
| 8 | Второй экземпляр на другой дорожке |
| 9 | Сохранить проект, закрыть, открыть: выбранный сэмпл и папка восстановлены, autoplay нет |
| 10 | Второй проект с другим выбором не меняет первый |
| 11 | Удалить плагин, выйти из хоста: без зависания |
| 12 | Параллельно открыто приложение: теги и звёзды видны в обоих окнах |

## Review Focus

- Гонки двух процессов на SQLite: плагин только читает каталог, кроме коротких записей
  тегов/звёзд/жанра/BPM; проверить, что ни один путь плагина не запускает скан.
- Жизненный цикл редактора: `MainComponent` не переживает процессор, state-колбэк не
  разыменовывает мёртвый процессор.
- `setStateInformation` до создания редактора и повторное открытие окна.
- Universal-бандл: `libonnxruntime` внутри, `otool` без путей сборки, подпись после manifest.
