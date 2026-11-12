# FSD50K: как получить данные

Источник: <https://zenodo.org/records/4060432> — FSD50K (Fonseca et al., 2022), MTG-UPF, Zenodo. 51 197 клипов,
200 классов AudioSet, dev 40 966 / eval 10 231, 44.1 кГц. Лицензия на каждый клип своя: CC0, CC BY, CC BY-NC,
CC Sampling+. В волну 1 идут только CC0 и CC-BY (уровень A).

## Файлы

| файл | байт | md5 |
|---|---|---|
| `FSD50K.dev_audio.z01`…`z05` | по 3 221 225 472 | см. `services/training/datasets/manifest.json` |
| `FSD50K.dev_audio.zip` | 2 306 663 327 | `c480d119b8f7a7e32fdb58f3ea4d6c5a` |
| `FSD50K.eval_audio.z01` | 3 221 225 472 | `3090670eaeecc013ca1ff84fe4442aeb` |
| `FSD50K.eval_audio.zip` | 3 037 675 767 | `6fa47636c3a3ad5c7dfeba99f2637982` |
| `FSD50K.metadata.zip` | 6 700 838 | `b9ea0c829a411c1d42adb9da539ed237` |
| `FSD50K.ground_truth.zip` | 334 701 | `ca27382c195e37d2269c4c866dd73485` |
| `FSD50K.doc.zip` | 6 984 | `3516162b82dc2945d3e7feba0904e800` |

Точные md5 всех частей — в `manifest.json` рядом с URL; там же указан zenodo-record.

## Распаковка

`dev_audio` и `eval_audio` — многотомные zip: `.z01`…`.z05` плюс последний том `.zip`. `7zz x <….zip>` собирает
тома из той же папки; `unzip` сам по себе их не читает. Остальные архивы — обычные zip.

## Раскладка после распаковки

```
<SONICATOR_DATASETS_ROOT>/fsd50k/
  FSD50K.dev_audio/<fname>.wav
  FSD50K.eval_audio/<fname>.wav
  FSD50K.ground_truth/{dev.csv,eval.csv,vocabulary.csv}
  FSD50K.metadata/{dev,eval}_clips_info_FSD50K.json  (+ class_info, pp_pnp_ratings, collection)
```

Метки — `ground_truth/*.csv` (мультиметки через запятую), лицензии и авторы — `*_clips_info_FSD50K.json`.
Разбиение: `dev.csv.split` (`train`/`val`), eval — тест.

## В репозиторий

Аудио не попадает; каталог данных вне репозитория и вне iCloud. Повторная выкачка —
`/Volumes/1TBSSD/Datasets/_fetch/fetch.sh`.
