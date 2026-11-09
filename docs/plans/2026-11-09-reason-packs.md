# Поддержка Reason Packs

## Steps
- [x] Ограниченный парсер CMB/REPATCH и распознавание ReFill; синтетические тесты.
- [x] Scanner, metadata-only indexing и однократное добавление Packs.
- [x] Карточка Reason и прослушивание локальных ссылок без воспроизведения патча.
- [ ] Полная проверка, обзор ветки, graphify, ff merge/push и перезапуск.

Спецификация: утверждённый план пользователя в сессии от 2026-11-09.
Не декодировать ReFill; CMB не включать в Extract samples; не менять существующие форматы.
IFF: проверять границы и лимиты, пропускать BODY; XML читать через XmlDocument.
Локальные ссылки разрешать только внутри пака, без переходов по symlink.

## Ledger
Pre-flight: parser → scanner/indexer/engine → preview; общая модель метаданных, не sampler instrument.
Ruling: отдельная ветка в текущем checkout, без нового worktree — пользователь запросил ветку, а существующий build нужен для проверки и перезапуска; цена: checkout занят до завершения этапа.
Baseline: ctest --test-dir build --output-on-failure — passed (29.35 s).
Task 1: complete — три теста RED → GREEN, 22 assertions; полный ctest passed (28.74 s).
Ruling: обход только известных контейнеров REFS/DEVL/DEVI; BODY пропускается seek, метаданные ограничены 4 MiB/20000 чанков. Поиск сэмплов ограничен 20000 записей, неоднозначные имена не выбираются — безопасность важнее угадывания; цена: очень большие паки потребуют ручного выбора файла.
Task 2: complete — scanner/indexing и регистрация RED → GREEN, 51 assertions; полный ctest passed (28.85 s).
Ruling: RFL считается поддерживаемой metadata-карточкой (не аудио), иначе Hide empty скрывает его; содержимое не хешируется и не читается за сигнатурой. Цена: статус supported означает только распознавание, ограничение явно показано в карточке.
Ruling: флаг автодобавления глобальный; при отсутствии Packs остаётся unset, чтобы обнаружить будущую установку. Уже покрытый корень не дублируется. Цена: новый каталог после первого добавления требует ручного добавления Packs.
Task 3: complete — карточка, выбор локального WAV, блокировка ReFill, переходы к обычному аудио и дискам RED → GREEN. 6978 assertions / 8 Reason tests; полный ctest passed (31.47 s), Sonicator build passed.
Ruling: поддержан XML-атрибут deviceProductID наряду с deviceProduct — реальный Runner Bass.repatch использует первый вариант; цена: дополнительный допустимый атрибут.
Ruling: из 690 аудиофайлов 10 WAV находятся в Audio Previews (Skyline Drifter), поэтому библиотека получает 680, а не 690. Приоритет явного требования исключить demo-папки целиком; цена: демо-WAV тоже не видны в библиотеке.
Read-only acceptance: 1140 CMB разобраны, локальных ссылок 674, ReFill-ссылок 2573; WAV/AIF/RX2 открываются штатным AudioEngine. ReFill не декодируется; Extract samples не менялся.
Final review: независимый read-only reviewer, 2 Important, 0 Critical, 0 Minor; Declined to judge: empty.
Final: fixed полный SHA-читал CMB BODY — Reason metadata rows теперь возвращаются без полного хеша; fileHash.isEmpty RED → GREEN, полный ctest passed (35.22 s).
Final: fixed ложное исключение обычных Audio Previews рядом с _samples/в произвольной Packs — обычная библиотека из 3 WAV RED → GREEN, полный ctest passed (35.22 s).
Ruling: Reason CMB/REPATCH/RFL не получают full-file SHA, чтобы ограничение чтения действовало и при индексации; цена: эти metadata-карточки не участвуют в группировке байтовых дублей. Обычные аудиофайлы хешируются без изменений.
Ruling: установленный абсолютный Packs-корень подтверждает контекст; у скопированных паков demo-папки исключаются только при наличии читаемого CMB/REPATCH в родительской папке (поиск глубиной 3, 2048 записей). Одних имён Packs/_samples недостаточно; цена: необычно вложенные/огромные скопированные паки могут сохранить демо в каталоге.
Runtime acceptance: reasonPacksAdded=true, один Packs root, 1140 CMB / 10 REPATCH / 537 WAV / 136 RX2 / 7 AIF / 120 обычных MP3, 0 файлов из demo-папок. Первое автосканирование завершено без ошибок.
