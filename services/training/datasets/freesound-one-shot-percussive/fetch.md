# Freesound One-Shot Percussive Sounds: как получить данные

Источник: <https://zenodo.org/records/4687854> — Freesound One-Shot Percussive Sounds (Caillon and Esling, 2020),
Zenodo. 10 254 ваншота, 1 секунда, 16 кГц. Лицензия на каждый клип своя, поэтому в реестре
`licensePerClip: true`; в волну 1 идут только CC0 и CC-BY (уровень A), NC и Sampling+ отбрасываются.

## Файлы

| файл | байт | md5 |
|---|---|---|
| `one_shot_percussive_sounds.zip` | 112 614 838 | `278994c2a7b92a24a4daad99f40c13db` |
| `licenses.txt` | 1 350 776 | `25f95a0e38d3ac4ae868f56c378fbccb` |
| `sound_info_analysis.json` | 60 085 318 | `b51913a801bd59c2583d5f0e6f3c05b9` |
| `analysis.zip` | 5 610 013 | `c67ce39d5aa6c6a7f88eedf7eb7d933e` |
| `README.md` | 3 203 | `afec91c033db607e2fc83c09940abd15` |

## Раскладка после распаковки

```
<SONICATOR_DATASETS_ROOT>/freesound-one-shot-percussive/
  one_shot_percussive_sounds/{1..5}/<id>.wav
  analysis/{1..5}/<id>.json
  licenses.txt                 JSON: id -> { name, username, license }
  sound_info_analysis.json     список { id, name, tags, ... }
  README.md
```

`licenses.txt`, `sound_info_analysis.json` и `README.md` лежат в архивах россыпью, а не внутри zip: их надо
скопировать в каталог датасета до удаления архивов, иначе пер-клиповые лицензии пропадут.

## В репозиторий

Аудио не попадает; каталог данных вне репозитория и вне iCloud. Повторная выкачка —
`/Volumes/1TBSSD/Datasets/_fetch/fetch.sh`.
