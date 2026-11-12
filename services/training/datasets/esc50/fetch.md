# ESC-50

- Официальный репозиторий: https://github.com/karolpiczak/ESC-50
- Лицензия: CC BY-NC 4.0 (уровень B, некоммерческое использование).
- Объём: 2000 клипов по 5 с, 50 классов, 5 официальных фолдов.

## Загрузка

```bash
mkdir -p /Volumes/1TBSSD/Datasets/_archives/esc50
curl -L -o /Volumes/1TBSSD/Datasets/_archives/esc50/ESC-50-master.zip \
  https://github.com/karolpiczak/ESC-50/archive/refs/heads/master.zip
unzip -q /Volumes/1TBSSD/Datasets/_archives/esc50/ESC-50-master.zip -d /tmp/esc50
mkdir -p /Volumes/1TBSSD/Datasets/esc50
cp -R /tmp/esc50/ESC-50-master/audio /tmp/esc50/ESC-50-master/meta /Volumes/1TBSSD/Datasets/esc50/
```

Ожидаемая структура: `esc50/audio/*.wav`, `esc50/meta/esc50.csv`.

## Разбиение

Официального hold-out нет, есть 5 фолдов кросс-валидации. Волна 2 берёт фолды 1–3 в `train`, 4 в `val`,
5 в `test`; правило записано в `ingest.mjs`.

## Метки

Тип берётся только там, где класс совпадает с таксономией Sonicator (`clapping` → `clap`, несколько
голосовых классов → `vocal`). Остальные 43 класса остаются без типа, но с группой (`foley`, `ambience`,
`vocal`) — для будущей foley-модели.
