# UrbanSound8K

- Официальная страница: https://urbansounddataset.weebly.com/urbansound8k.html
- Лицензия: CC BY-NC 3.0 (уровень B, некоммерческое использование).
- Объём: 8732 клипа по 4 с, 10 городских классов, 10 официальных фолдов.

## Загрузка

```bash
mkdir -p /Volumes/1TBSSD/Datasets/_archives/urbansound8k
curl -L -o /Volumes/1TBSSD/Datasets/_archives/urbansound8k/UrbanSound8K.tar.gz \
  https://zenodo.org/record/1203745/files/UrbanSound8K.tar.gz
tar -xzf /Volumes/1TBSSD/Datasets/_archives/urbansound8k/UrbanSound8K.tar.gz \
  -C /Volumes/1TBSSD/Datasets/urbansound8k
```

Ожидаемая структура: `urbansound8k/UrbanSound8K/audio/fold*/`, `urbansound8k/UrbanSound8K/metadata/UrbanSound8K.csv`.

## Разбиение

Официальные фолды: 1–8 в `train`, 9 в `val`, 10 в `test` (правило в `ingest.mjs`).

## Метки

Ни один из 10 классов не является типом Sonicator: `drilling` и `jackhammer` — события, а не
инструменты. Клипы входят без типа, с группой (`foley`, `ambience`, `tonal` для `street_music`) и
доменом. Для модели типов волна 2 они ничего не добавляют, что и показывает отчёт.
