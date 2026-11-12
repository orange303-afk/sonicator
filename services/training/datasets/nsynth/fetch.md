# NSynth: как получить данные

Источник: <https://magenta.tensorflow.org/datasets/nsynth> — NSynth (Engel et al., 2017), Google Magenta,
CC BY 4.0. Полный релиз 1.0.3: 305 979 нот, 1006 инструментов, 11 семейств, 4 секунды на ноту, 16 кГц.

## Файлы

| файл | байт | md5 |
|---|---|---|
| `nsynth-test.jsonwav.tar.gz` | 349 501 546 | `5e6f8719bf7e16ad0a00d518b78af77d` |
| `nsynth-valid.jsonwav.tar.gz` | 1 068 767 009 | `87e94a00a19b6dbc99cf6d4c0c0cae87` |
| `nsynth-train.jsonwav.tar.gz` | 23 815 298 079 | `fde6665a93865503ba598b9fac388660` |

## Откуда качать

Оригинальный адрес `download.magenta.tensorflow.org` отдаёт сертификат `*.storage.googleapis.com`, поэтому HTTPS
не проходит проверку имени, а HTTP даёт около 0.8 МБ/с. Поэтому архивы берутся с зеркала Hugging Face
`confit/nsynth` (`https://huggingface.co/datasets/confit/nsynth/resolve/main/<файл>`) со скоростью ~15 МБ/с.
md5 выше — из исходного бакета GCS, зеркало проверяется по ним, так что подмена не пройдёт.

## Раскладка после распаковки

```
<SONICATOR_DATASETS_ROOT>/nsynth/
  nsynth-train/{audio,examples.json}
  nsynth-valid/{audio,examples.json}
  nsynth-test/{audio,examples.json}
```

`audio/<note_str>.wav`. `examples.json` — объект `note_str -> {instrument_family_str, instrument_str,
instrument_source_str, pitch, ...}`. Метки берутся из `instrument_family_str`, а не из имени файла.

## В репозиторий

Аудио и признаки не попадают: каталог данных лежит вне репозитория и вне iCloud
(`SONICATOR_DATASETS_ROOT`, по умолчанию `/Volumes/1TBSSD/Datasets`). Повторная выкачка —
`/Volumes/1TBSSD/Datasets/_fetch/fetch.sh` (aria2, докачка, md5, сторож).
