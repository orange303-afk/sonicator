# SONICATOR user manual

The source of the English user manual and the script that builds the PDF.

## Build

```sh
typst compile --font-path "$PWD/resources/fonts" --root . \
  docs/manual/manual.typ docs/manual/sonicator-manual.pdf
python3 docs/manual/verify.py docs/manual/sonicator-manual.pdf
```

The font path must be absolute: with `--root .`, Typst resolves a relative font path against its
own working directory and silently falls back to a serif face.

## Screenshots

```sh
cmake --build --preset debug --target SonicatorTests
SONICATOR_MANUAL_SHOT_DIR="$PWD/docs/manual/assets" \
  ~/Library/Caches/sonicator-build/debug/tests/SonicatorTests "[.manual-shots]"
```

The test seeds a temporary catalog of 313 synthetic WAV files — tags, stars, metadata overrides,
one duplicate pair, one missing file and one decode error — and snapshots the real components. No
image is drawn by hand and nothing comes from a private library. Provenance per plate:
[`assets/SOURCES.md`](assets/SOURCES.md).

## Layout

- `style.typ` — page geometry, type scale, callouts, figures, table style.
- `manual.typ` — cover, colophon, reading guide, contents, part dividers, includes.
- `chapters/` — one file per chapter, numbered in reading order.
- `appendices/` — reference tables.
- `assets/` — generated PNG plates.
