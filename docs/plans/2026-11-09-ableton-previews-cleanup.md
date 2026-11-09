# Ableton Packs: превью, поиск и управление папками

## Steps
- [x] OGG-декодирование и распознавание Ableton Preview; защищённые AIFC/able не индексируются.
- [x] Исключение CMB/RFL из сканирования и очистка существующих записей каталога.
- [x] Remove папки из библиотеки для любого узла боковой панели без изменения файлов.
- [x] Однократное добавление `~/Music/Ableton/Factory Packs`, общий поиск доступного содержимого.
- [x] Полный ctest, graphify, ff merge/push и перезапуск приложения.

Спецификация: договорённость в сессии 2026-11-09.
Ableton: 20 паков, 13 236 файлов. Доступно: 1 461 OGG-превью, 1 223 WAV/AIFF (PCM/fl32),
1 897 AIFC с кодеком `able` (Ableton Content) — закрытый/защищённый, не декодируем и не индексируем.
Превью лежат в `<Pack>/Ableton Folder Info/Previews/<category>/<name>.<alc|adg|adv>.ogg`.
Reason: `.cmb` и `.rfl` больше не индексируются; `.repatch` и playable WAV/AIFF/RX2 остаются.
Remove: удаляет только записи каталога; файлы на диске не меняются. Вложенная папка получает
ignore, чтобы родительский rescan её не вернул; добавление её снова как корня снимает ignore.
CMB/RFL: строки удаляются при старте и при переключении каталога; повторно не появляются.
Индексация содержимого ISO/IMG/BIN/CUE — отдельный бэклог, в этом этапе не реализуется.

## Ledger
Pre-flight: scanner -> indexing -> UI; Ableton helpers общие для scanner/preview.
Baseline: ctest --test-dir build --output-on-failure — passed.
Task 1-4: complete — RED→GREEN по сканеру/OGG/превью, очистке форматов, Remove и регистрации.
Полный ctest passed (35.37 s); Sonicator build passed. Реальные паки: OGG 1461, playable WAV/AIFF 1223,
protected able AIFC 1897 не индексируются; Reason: 680 audio, 10 repatch, 0 CMB/RFL.
Ruling: OGG индексируется как обычный формат во всех папках, не только в Ableton — это стандартный
аудиоформат; цена: OGG из других библиотек тоже появится в каталоге (ожидаемо).
Ruling: защищённые AIFC/able пропускаются сканером, а не создают unsupported-строки: пользователь
просил только доступные файлы, а такие сэмплы нельзя ни проиграть, ни экспортировать. Цена: поиск
по имени такого защищённого сэмпла ничего не найдёт.
Ruling: CMB/RFL удаляются из каталога при каждом открытии каталога и при переключении, а не по
флагу в настройках: требование относится к данным, а не к одной машине; цена: теги/анализ этих
строк теряются безвозвратно (строки metadata-only, файлы не меняются).
Ruling: Remove вложенной папки ставит exact ignore, поэтому родительский rescan её не вернёт;
повторное добавление через + Add folder снимает exact ignore. Цена: восстановление возможно
только явным добавлением папки, а не автоповтором скана.
Ruling: удаление корня также удаляет вложенные зарегистрированные корни и записи remove в
settings favoriteFolders; файлы на диске не трогаются.
Ruling: Ableton Factory Packs регистрируется тем же однократным механизмом, что Reason Packs;
первый запуск запускает фоновый скан (2684 доступных файла, ~3.2 GiB). Цена: первый старт
после обновления дольше обычного, но UI остаётся отзывчивым.
Ruling: ISO-индексация содержимого образов остаётся отдельным бэклогом (записано в To do), в
этот этап не входит.
Final review: независимый read-only reviewer, 0 Critical, 5 Important, несколько Minor; Declined to judge: 7 пунктов (ISO вне этапа, Favorites, глобальный флаг, прямой CMB/RFL, Unicode путей, multi-instance, произвольные пути API).
Final: fixed вложенный root теперь всегда получает ignore после Remove — `nested registered root` тест RED → GREEN, parent rescan не возвращает.
Final: fixed + Add folder снимает ignore самого пути и всех папок-предков — `Explicitly adding a folder clears ignores` RED → GREEN.
Final: fixed подпись Ableton Preview вынесена в отдельный label `abletonInfo`, скрывается при любом следующем файле — переход preview → WAV RED → GREEN.
Final: fixed старые error-строки защищённых AIFC удаляются при открытии каталога, обычные битые AIFF остаются — тест RED → GREEN.
Final: fixed обход AIFC ограничен 1024 чанками — тест с 1500 JUNK RED → GREEN.
Final: fixed удаление скрыто для не-папочных узлов (образы), lastFolder очищается отдельно от selectedFolder, добавлен <cstring>, убран неиспользуемый тестовый helper; независимый подсчёт raw AIF в real-тесте.
Final fixes: targeted ctest `[ableton],[remove],[reason]` — 22 test cases, 4413 assertions passed;
полный ctest passed (36.76 s).
Final: minor отложены — синхронный refreshDuplicates в UI после Remove, полный скан форматов при каждом старте, частичный успех при ошибке записи settings, очистка refreshDuplicates/кэша ignored при image-узлах, отсутствие теста каскада smart/duplicates и отдельного теста UI-пункта меню.
Final integration: ff merge feature/ableton-previews-library-cleanup → main (6b8bb19), graphify update выполнен, ctest на main passed (33.14 s), push выполнен. Приложение перезапущено из build/Sonicator_artefacts/Debug/SONICATOR.app; после старта 339 608 строк, 30 старых protected-AIF error-строк удалено миграцией, CMB/RFL 0, Ableton 2 684 без ошибок.
Runtime acceptance: каталог 339 638 строк; CMB/RFL 0; Ableton Factory Packs зарегистрирован
автоматически, проиндексировано 2 684 файла без ошибок (1 461 OGG-превью, 1 223 WAV/AIFF/AIF),
protected able не попали; 1 461 превью имеет длительность/частоту. Root виден в боковой панели,
All samples обновляется после завершения duplicate-группировки.
