# Индексация содержимого образов

## Steps
- [x] Виртуальные записи в каталоге: схема, SampleRecord, репозиторий, поиск и изоляция от обычных файлов.
- [x] DiscContentIndexer: обход ISO/IMG/CUE и Akai через существующий DiscBrowserService, состояние образа, инкрементальность.
- [x] Интеграция в IndexingService: фоновый проход, отмена, очистка отсутствующих образов.
- [x] UI: выбор виртуального сэмпла декодирует и играет, Enter/двойной клик открывает образ на папке записи.
- [ ] Документация, полный ctest, ревью, graphify, ff merge/push и перезапуск.

Спецификация: Todo `254b0a7d` и запрос пользователя 2026-11-10.
Только чтение образа: без распаковки, без изменения образов, без MDX, без полного PCM при индексации.
Виртуальные записи видны в All samples и общем поиске; переход к сэмплу — через существующее
декодирование DiscBrowserService. Remove/Ignore папок и образов не возвращают исключённое.
Тесты: синтетический ISO с WAV, поиск без открытия образа, дубли имён в разных папках,
инкрементальность по size/mtime, удаление образа, отмена, UI preview/navigation.

## Interfaces
- `SampleRecord` получает `containerPath`, `discEntry`, `discParent`, `discPartner`, `discPath`.
- `SampleRepository`: `discIndexState`, `replaceDiscContents`, `deleteDiscContents`, `pruneDiscContents`.
- `core/disc/DiscContentIndexer`: `isDiscContentFormat`, `indexDiscContents`.
- `DiscBrowserPanel::previewEntry` и `onSampleSelected` без требования видимости панели.

## Ledger
Pre-flight: repository/schema → indexer → IndexingService → UI. Общий интерфейс — виртуальные строки samples.
Baseline: ctest --test-dir build --output-on-failure — passed (33.14 s) на main до ветки.
Ruling: виртуальные записи живут в `samples` с путём `<image>::<internal path>` — так бесплатно
работают поиск, теги, сортировка, страницы и Remove; цена: синтетический путь нужно явно исключать
из проверок существования, drag и Smart.
Ruling: индексируются только открываемые аудио-записи (ISO: wav/aif/aiff/flac/mp3/ogg; Akai: sample
records), а не все файлы и не программы — пользователь просил доступные сэмплы, а метаданные
программ уже показывает браузер; цена: текстовые/неигровые файлы образа в поиск не попадают.
Ruling: duration/rate/peak виртуальных строк равны 0, version=metadataOnlyVersion, hash пуст —
индексация не декодирует PCM; цена: колонки длительности/частоты пусты до выбора сэмпла.
Ruling: BIN с парным CUE не индексируется отдельно, контейнером считается CUE; цена: одна запись
контейнера вместо двух.
Task 1-4: complete — схема 12, виртуальные строки, DiscContentIndexer, IndexingService, UI.
`[disc-content]` 8 test cases / 107 assertions; полный ctest passed (47.69 s). Реальная Akai-ISO
601 MiB: проиндексирована, запись декодируется и играет через preview без распаковки.
Ruling: частичный листинг сохраняет найденные строки, но стирает disc_index state, поэтому
следующий скан повторяет образ; цена: повторный обход вместо молчаливо неполного каталога.
Ruling: relocateRoots переносит не только пути виртуальных строк, но и их container_path и
ключи disc_index; цена: больше SQL в редкой операции, зато нет «битых» путей после переноса.
Ruling: нечитаемые/неподдерживаемые образы не получают state и пробуются снова при следующем
полном скане; цена: повторная быстрая попытка на каждую такую запись.
Ruling: состояние образа — только size+mtime; содержимое, изменённое без смены этих полей,
не переиндексируется до изменения файла; цена: редкий устаревший список.
Ruling: индексатор обрабатывает только образы, уже найденные сканером в обычных roots; отдельного
фонового автоскана при старте нет — содержимое появляется после Rescan/добавления папки.
Final review: 0 Critical, 8 Important, 6 Minor; Declined to judge: 8 пунктов (MDX, Akai-программы,
строка BIN, папочные представления, AutoTag, аудиодвижок, DiscLimits, ignore одной строки).
Final: fixed Smart folder/selected больше не берут виртуальные строки; idsInFolder/countInFolder
получили includeDiscContents=false, nonDiscIds, processSample считает их обработанными — тест RED → GREEN.
Final: fixed partial listing использует merge-only replace и сохраняет старые строки/теги — тест.
Final: fixed CUE identity включает size/mtime парного data-файла (discImageStamp) — тест.
Final: fixed скрытый preview сверяется с текущим selectedSampleId; поздний decode не перекрывает
новый выбор — тест, добавлен isDiscPreview().
Final: fixed NewOnly больше не запускает дисковый проход — тест.
Final: fixed ошибки/пропуски/число записей дискового прохода выводятся в status и tooltip.
Final: fixed missing-образ больше не удаляет виртуальные строки и теги: prune только синхронизирует
missing и удаляет сирот; tags/stars сохраняются — тест.
Final: fixed (re-graded) internalPath строится одним map + cancel каждые 256 записей; revealPending
сбрасывается в open/close; relocation probe не считает синтетические пути файлами; CUE ищется
case-insensitively; неудачный reindex инвалидирует state. Тесты частично покрывают.
Final: fixed тесты CUE stamp, partial merge, Smart scopes, NewOnly, missing-image tags, preview race.
Final: minor (deferred): deleteDiscContents не используется; discPartner хранится без потребителя;
countUnderRoot считает виртуальные строки; AutoTag поведение на виртуальных строках не специфицировано;
нет отдельного UI-теста status-строки дискового прохода; нет теста multi-track CUE индексатора.
Final verification: `[disc-content]` 13 test cases / 142 assertions; полный ctest passed (54.50 s);
real Akai ISO повторно проходит opt-in тест после изменений.
Ruling: состояние образа сравнивается по size+mtime; правка внутри той же секунды на файловых
системах с секундным разрешением может не заметиться до следующего изменения; цена: редкий
устаревший список содержимого, исправляется полным Rescan после изменения mtime.
Ruling: при удалении папки удаляются и строки disc_index её образов, иначе после повторного
добавления неизменённый образ не переиндексировался бы; цена: повторный обход образа.
Ruling: виртуальные строки не попадают в Smart, byte-hash дубликаты, row-drag как OS-файл и
sparse-проверки missing; их состояние определяет образ (missing/ignored) и индексатор.
Ruling: программы Akai и неигровые записи остаются только в браузере образа; цена: их нельзя
найти общим поиском, но они и не являются аудио.
Ruling: CUE с несколькими data track пропускается индексатором с сообщением, выбор трека остаётся
в браузере; цена: такой образ не даёт строк до выбора конкретного трека в UI.
Ruling: виртуальная строка при восстановлении сессии только выбирается, без автодека; Enter/двойной
клик декодирует; цена: после перезапуска звук не стартует сам.
