# Stage 3.2: ингест датасетов и волна 1 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Превратить скачанные NSynth, Freesound One-Shot Percussive и FSD50K в размеченный набор признаков
`featureSchema 2` (`out/wave1/features.jsonl`) и отчёт о составе, лицензиях и балансе.

**Architecture:** Три независимых адаптера датасетов приводят свои метаданные к одному типу `Clip`; общий модуль
проверяет лицензии, режет выборку по квотам и делит её без утечки. `build-features.mjs` строит из выбранных клипов
ферму символических ссылок по сплитам, гоняет на ней `sonicator-features` (по процессу на сплит), склеивает строки
признаков с разметкой по пути и проверяет схему. Аудио и признаки остаются вне репозитория, в git попадают только
конфиги разметки и отчёт.

**Tech Stack:** Node 20+, `node:test`, без зависимостей; JSON/JSONL; существующий CLI `tools/sonicator-features`
(84 измерения, `featureSchema 2`).

**Spec:** `docs/superpowers/specs/2026-11-12-feature-schema-2-public-datasets-design.md` (части 5, 7.1–7.4, 11)

## Global Constraints

- `services/training` без внешних зависимостей: только стандартная библиотека Node и `node:test`.
- Датасеты вне репозитория и вне iCloud: корень из `SONICATOR_DATASETS_ROOT`, по умолчанию
  `/Volumes/1TBSSD/Datasets`. В git не попадают ни аудио, ни `features.jsonl` (каталог `services/training/out/`
  уже в `.gitignore`).
- Признаки не пересчитываются в Node: единственная реализация — `core/dsp/FeatureExtractorV2` через
  `sonicator-features`. Node только выбирает файлы, запускает CLI и склеивает.
- `featureSchema 2`, 84 измерения, значения 0..1, округление 0.001 уже делает CLI. Маски полосы нет (решение 6.6):
  вместо неё `inBandEnergyShare`, порог отбраковки применяется на обучении, а не здесь.
- Уровень C лицензий не используется. У датасетов с лицензией на клип (FSD50K, One-Shot) берутся только CC0 и
  CC-BY: NC, SA, ND и Sampling+ в волну 1 не входят.
- Детерминизм: порядок строк не зависит от локали, сортировка по `clipId`; один и тот же вход даёт побайтно тот же
  `clips.jsonl`.
- Язык новых документов — русский; существующие тексты не переписываются без необходимости.

## Review Focus

- Клип One-Shot без записи в `licenses.txt` (2 из 10 254): исключается, но попадает в счётчик отчёта, прогон не падает.
- Клип с меткой вне стандартных типов (NSynth `mallet/flute/reed`, ~190 классов FSD50K): остаётся в таблице с
  `label: null`, заполненной `group`, и перечисляется в отчёте как «нет соответствия» — не теряется молча.
- Инструмент NSynth с одной-двумя нотами и семейство с единственным инструментом: квоты не дают пустой выборки и
  не падают на делении.
- Одинаковые имена файлов в разных сплитах: признаки соединяются с разметкой по пути внутри сплита, а не по
  глобальному basename.
- Прерванный прогон CLI: незавершённый сплит перезапускается целиком, `clips.jsonl` не получает дубликатов.

---

### Task 1: Модель клипа, лицензии и квоты

**Files:**
- Create: `services/training/datasets/clip.mjs`
- Test: `services/training/test/clip.node.mjs`

**Interfaces:**
- Consumes: ничего.
- Produces:
  - `licenseName(url: string) -> string | null` — `"CC0-1.0"`, `"CC-BY-3.0"`, `"CC-BY-NC-3.0"`,
    `"CC-BY-SA-3.0"`, `"CC-Sampling+-1.0"`; неизвестный URL → `null`.
  - `isShareableLicense(name: string | null) -> boolean` — только CC0 и CC-BY (без NC/SA/ND/Sampling).
  - `GROUPS = ["percussion", "tonal", "vocal", "fx", "foley", "ambience", "loop"]`,
    `DOMAINS = ["music", "foley", "ambience", "voice"]`.
  - `validateClip(clip: object) -> object` — бросает `Error` с текстом `clip: ...`; `label` допускает `null`.
  - `makeClip(fields) -> object` — заполняет `license`/`tier` из `licenseUrl` (`A` для CC0/CC-BY, `B` иначе).
  - `capClips(clips, { perLabel = 2000, perInstrument = Infinity }) -> object[]` — детерминированно, вход
    сортируется по `clipId`; при `perInstrument` выбирает ноты круговым обходом по инструментам.
  - `splitByUploader(uploader: string) -> "train" | "val" | "test"` — sha256, 80/10/10, устойчиво между машинами.
  - `assertUnique(clips, key = (clip) => clip.clipId)`.

- [ ] **Step 1: Написать падающий тест**

```js
// services/training/test/clip.node.mjs
import assert from "node:assert/strict";
import test from "node:test";
import { capClips, isShareableLicense, licenseName, makeClip, splitByUploader, validateClip } from "../datasets/clip.mjs";

test("only CC0 and CC-BY are shareable", () => {
  assert.equal(licenseName("http://creativecommons.org/publicdomain/zero/1.0/"), "CC0-1.0");
  assert.equal(licenseName("http://creativecommons.org/licenses/by/3.0/"), "CC-BY-3.0");
  assert.equal(licenseName("http://creativecommons.org/licenses/by-nc/3.0/"), "CC-BY-NC-3.0");
  assert.equal(licenseName("http://example.com/none"), null);
  assert.equal(isShareableLicense("CC0-1.0"), true);
  assert.equal(isShareableLicense("CC-BY-3.0"), true);
  assert.equal(isShareableLicense("CC-BY-NC-3.0"), false);
  assert.equal(isShareableLicense("CC-Sampling+-1.0"), false);
  assert.equal(isShareableLicense(null), false);
});

test("clip with a null label is valid, a clip without a licence tier is not", () => {
  const base = makeClip({ source: "nsynth", clipId: "a", file: "/x/a.wav", label: null,
    labelRaw: "mallet", group: "tonal", domain: "music", licenseUrl: "http://creativecommons.org/licenses/by/4.0/",
    split: "train" });
  assert.equal(base.tier, "A");
  assert.equal(validateClip(base).clipId, "a");
  assert.throws(() => validateClip({ ...base, group: "drums" }), /clip: group/);
  assert.throws(() => validateClip({ ...base, source: "" }), /clip: source/);
});

test("caps are deterministic and round-robin across instruments", () => {
  const clips = [];
  for (const instrument of ["bass_a", "bass_b"])
    for (const note of ["03", "01", "02"])
      clips.push({ clipId: `${instrument}-${note}`, instrument, label: "bass" });
  const capped = capClips(clips.reverse(), { perLabel: 4, perInstrument: 2 });
  assert.deepEqual(new Set(capped.map((clip) => clip.clipId)),
    new Set(["bass_a-01", "bass_a-02", "bass_b-01", "bass_b-02"]));
  assert.deepEqual(capClips([...clips].reverse(), { perLabel: 4, perInstrument: 2 }), capped);
});

test("an uploader always lands in the same split", () => {
  const split = splitByUploader("mhc");
  assert.equal(splitByUploader("mhc"), split);
  assert.ok(["train", "val", "test"].includes(split));
  const shares = { train: 0, val: 0, test: 0 };
  for (let i = 0; i < 1000; i += 1) shares[splitByUploader(`user-${i}`)] += 1;
  assert.ok(shares.train > 750 && shares.val > 50 && shares.test > 50);
});
```

- [ ] **Step 2: Прогнать тест и убедиться, что он падает**

Run: `cd services/training && node --test test/clip.node.mjs`
Expected: FAIL — `Cannot find module '../datasets/clip.mjs'`.

- [ ] **Step 3: Реализовать `services/training/datasets/clip.mjs`**

Точные сигнатуры из блока Interfaces. `makeClip` хранит `licenseUrl` и производные `license`/`tier`;
`capClips` сначала применяет квоту на инструмент (круговой обход, внутри инструмента сортировка по `clipId`), затем
квоту на метку; клипы с `label: null` квотой на метку не ограничиваются.

- [ ] **Step 4: Прогнать тест**

Run: `cd services/training && node --test test/clip.node.mjs`
Expected: PASS.

- [ ] **Step 5: Коммит**

```bash
git add services/training/datasets/clip.mjs services/training/test/clip.node.mjs
git commit -m "feat(training): clip model with licence policy and deterministic caps"
```

---

### Task 2: NSynth

**Files:**
- Create: `services/training/datasets/nsynth/ingest.mjs`
- Create: `services/training/datasets/nsynth/labels.map.json`
- Create: `services/training/test/fixtures/nsynth-examples.json`
- Test: `services/training/test/nsynth.node.mjs`

**Interfaces:**
- Consumes: `makeClip`, `capClips`, `assertUnique` из Task 1.
- Produces: `ingest({ root, split }) -> object[]` — читает `<root>/nsynth/nsynth-<split>/examples.json`, проверяет
  наличие `<root>/nsynth/nsynth-<split>/audio/<clipId>.wav`; возвращает клипы, отсортированные по `clipId`.
  `familyMap()` возвращает разобранный `labels.map.json`; `familyLabel(family) -> { label, group }` бросает
  `unknown instrument family: ...` на неизвестном семействе.

Разметка (полная таблица, 11 семейств; менять только с тестом):

| `instrument_family_str` | `label` | `group` |
|---|---|---|
| bass | bass | tonal |
| brass | brass | tonal |
| guitar | guitar | tonal |
| keyboard | keys | tonal |
| organ | keys | tonal |
| string | strings | tonal |
| synth_lead | lead | tonal |
| vocal | vocal | vocal |
| flute | null | tonal |
| mallet | null | tonal |
| reed | null | tonal |

`domain: "music"`. `instrument_source_str` (`acoustic`/`electronic`/`synthetic`) сохраняется в `extras.source`.
`clipId` — `note_str`. Сплит — имя каталога (`train`/`valid`/`test`), официальные разбиения NSynth, утечки нет.
Квоты: `perInstrument = 5`, `perLabel = 2000`.

- [ ] **Step 1: Написать падающий тест с фикстурой**

`services/training/test/fixtures/nsynth-examples.json` — 5 нот: `bass_acoustic_001-003-025`,
`bass_acoustic_001-007-025`, `bass_acoustic_001-011-025`, `mallet_acoustic_002-003-025`,
`vocal_synthetic_003-003-025`, у каждой `instrument_str`, `instrument_family_str`, `instrument_source_str`, `pitch`.
Тест кладёт файл в `os.tmpdir()/nsynth-root/nsynth/nsynth-test/examples.json`, создаёт пустые `audio/<id>.wav`
и передаёт `root = os.tmpdir()/nsynth-root`.

```js
test("families map to Sonicator types and unmapped ones keep a group", () => {
  const clips = ingest({ root: fixtureRoot, split: "test" });
  assert.deepEqual(clips.map((clip) => [clip.clipId, clip.label]), [
    ["bass_acoustic_001-003-025", "bass"],
    ["bass_acoustic_001-007-025", "bass"],
    ["bass_acoustic_001-011-025", "bass"],
    ["mallet_acoustic_002-003-025", null],
    ["vocal_synthetic_003-003-025", "vocal"],
  ]);
  assert.equal(clips[3].labelRaw, "mallet");
  assert.equal(clips[3].group, "tonal");
  assert.equal(clips[0].split, "test");
  assert.equal(clips[0].extras.source, "acoustic");
});

test("the family table covers exactly the eleven NSynth families", () => {
  assert.deepEqual(Object.keys(familyMap().families).sort(), ["bass", "brass", "flute", "guitar", "keyboard",
    "mallet", "organ", "reed", "string", "synth_lead", "vocal"]);
  assert.throws(() => familyLabel("theremin"), /unknown instrument family/);
});
```

- [ ] **Step 2: Прогнать тест, убедиться, что падает**

Run: `cd services/training && node --test test/nsynth.node.mjs`
Expected: FAIL — module not found.

- [ ] **Step 3: Реализовать `labels.map.json` и `ingest.mjs`**

`ingest` читает `examples.json` (объект `note_str -> {instrument_family_str, instrument_str, instrument_source_str}`),
проверяет файл на диске, строит клипы через `makeClip`, затем `capClips(clips, { perLabel: 2000, perInstrument: 5 })`
и `assertUnique`. Неизвестное семейство — `throw new Error("unknown instrument family: ...")`.

- [ ] **Step 4: Прогнать тесты**

Run: `cd services/training && node --test test/nsynth.node.mjs test/clip.node.mjs`
Expected: PASS.

- [ ] **Step 5: Коммит**

```bash
git add services/training/datasets/nsynth services/training/test/nsynth.node.mjs services/training/test/fixtures
git commit -m "feat(training): NSynth ingest with family labels and instrument caps"
```

---

### Task 3: Freesound One-Shot Percussive

**Files:**
- Create: `services/training/datasets/freesound-one-shot-percussive/ingest.mjs`
- Create: `services/training/datasets/freesound-one-shot-percussive/labels.map.json`
- Create: `services/training/test/fixtures/one-shot-licenses.json`
- Test: `services/training/test/one-shot.node.mjs`

**Interfaces:**
- Consumes: Task 1 (`makeClip`, `isShareableLicense`, `splitByUploader`, `capClips`).
- Produces: `ingest({ root }) -> { clips, skipped }`; `labelForClip({ name, tags }) -> string | null`.

Раскладка на диске: `<root>/freesound-one-shot-percussive/one_shot_percussive_sounds/<1..5>/<id>.wav`,
`licenses.txt` (JSON `id -> { name, username, license }`), `sound_info_analysis.json` (список объектов
`{ id, name, tags }`; используется только для тегов).

Правила метки (порядок важен, применяется первое совпадение по имени файла, затем по тегам):
`bass drum|bd|kick → kick`, `snare|sd → snare`, `hi.?hat|hh|closed hat|open hat → hat`, `clap → clap`,
`rim|rimshot → rim`, `tom → tom`, `crash → crash`, `ride → ride`, `cymbal → cymbal`, `cowbell → cowbell`,
`shaker → shaker`, `tambourine → tamb`, `snap → snap`, `conga/bongo/djembe/tabla/timbale/agogo/clave/cabasa → одноимённый тип`.
Если ни одно правило не сработало и есть тег `percussion|drum` → `percussion`; иначе `null`. Группа всегда
`percussion`, домен `music`.

Лицензии: только CC0/BY; отсутствующий id в `licenses.txt` исключается и считается. `clipId` — id Freesound,
`split` — `splitByUploader(username)`, `extras.uploader` — автор (нужен для разбиения без утечки).

Фикстура: `test/fixtures/one-shot/` с `<root>/freesound-one-shot-percussive/one_shot_percussive_sounds/{1,2}/`,
четырьмя пустыми WAV (CC0, CC-BY, CC-BY-NC, без записи в лицензиях), `licenses.txt` на три из них и
`sound_info_analysis.json` с тегами.

- [ ] **Step 1: Написать падающий тест**

```js
test("names and tags map to Sonicator percussion types", () => {
  assert.equal(labelForClip({ name: "acoustic_closed_hihat1.wav", tags: [] }), "hat");
  assert.equal(labelForClip({ name: "kick_01.wav", tags: [] }), "kick");
  assert.equal(labelForClip({ name: "Zyl_Finger_Cymbals_Mute.wav", tags: ["Cymbals"] }), "cymbal");
  assert.equal(labelForClip({ name: "mystery.wav", tags: ["percussion"] }), "percussion");
  assert.equal(labelForClip({ name: "mystery.wav", tags: ["noise"] }), null);
});

test("only CC0/BY clips are used and missing licences are counted", () => {
  const { clips, skipped } = ingest({ root: fixtureRoot });
  assert.deepEqual(clips.map((clip) => clip.license).sort(), ["CC-BY-3.0", "CC0-1.0"]);
  assert.equal(skipped.noLicense, 1);
  assert.equal(skipped.licenseTier, 1);
  assert.ok(clips.every((clip) => clip.group === "percussion" && clip.domain === "music"));
  assert.ok(clips.every((clip) => ["train", "val", "test"].includes(clip.split)));
});
```

- [ ] **Step 2: Прогнать тест, убедиться, что падает**

Run: `cd services/training && node --test test/one-shot.node.mjs`
Expected: FAIL.

- [ ] **Step 3: Реализовать `ingest.mjs` и `labels.map.json`**

`ingest` возвращает `{ clips, skipped }`, где `skipped = { noLicense, licenseTier, missingAudio }`; квота
`perLabel = 2000`; сортировка по `clipId` (числовая, не строковая).

- [ ] **Step 4: Прогнать тесты**

Run: `cd services/training && node --test test/one-shot.node.mjs test/clip.node.mjs`
Expected: PASS.

- [ ] **Step 5: Коммит**

```bash
git add services/training/datasets/freesound-one-shot-percussive services/training/test/one-shot.node.mjs
git commit -m "feat(training): one-shot ingest with per-clip licensing and author split"
```

---

### Task 4: FSD50K

**Files:**
- Create: `services/training/datasets/fsd50k/ingest.mjs`
- Create: `services/training/datasets/fsd50k/labels.map.json`
- Test: `services/training/test/fsd50k.node.mjs`

**Interfaces:**
- Consumes: Task 1.
- Produces: `ingest({ root, split }) -> { clips, skipped }`; `readVocabulary(root) -> string[]`;
  `typeForLabel(label) -> string | null`; `groupForLabel(label) -> { group, domain }`.

Источники разметки: `FSD50K.ground_truth/{dev,eval}.csv` (`fname,labels,mids,split`; `split` у dev —
`train|val`), `FSD50K.ground_truth/vocabulary.csv`, `FSD50K.metadata/{dev,eval}_clips_info_FSD50K.json`
(`license`, `uploader`, `tags`). Файл — `<root>/fsd50k/FSD50K.<split>_audio/<fname>.wav`.

Правила типа — явная таблица в `labels.map.json`, всё остальное `null` (порядок = приоритет):
`Bass_drum → kick`, `Snare_drum → snare`, `Hi-hat → hat`, `Crash_cymbal → crash`, `Cymbal → cymbal`,
`Drum → percussion`, `Drum_kit → percussion`, `Percussion → percussion`, `Mallet_percussion → percussion`,
`Tabla → tabla`, `Cowbell → cowbell`, `Tambourine → tamb`, `Clapping → clap`, `Finger_snapping → snap`,
`Guitar|Acoustic_guitar|Electric_guitar → guitar`, `Bass_guitar → bass`, `Piano → piano`, `Organ|Keyboard_(musical) → keys`,
`Harmonica|Accordion → keys`, `Trumpet|Brass_instrument → brass`, `Bowed_string_instrument|Plucked_string_instrument → strings`,
`Harp|Glockenspiel|Marimba_and_xylophone → percussion`, `Singing|Male_singing|Female_singing → vocal`.

Группа и домен — по спискам ключевых слов в `labels.map.json` (первое совпадение): `vocal` (voice) для речи и пения,
`tonal` (music) для музыкальных инструментов и `Music`, `ambience` (ambience) для природы/города/погоды/транспорта,
иначе `foley` (foley). Полный список из 200 классов обязан получить ровно одну группу — это проверяет тест.

Клип с несколькими метками: `label` — первый по порядку CSV класс, у которого есть тип; `extras.labels` — все
исходные классы. Сплит: `dev.csv.split` (`train`/`val`) для dev, `test` для eval. Квота `perLabel = 2000` на
источник, лицензии CC0/BY.

- [ ] **Step 1: Написать падающий тест**

```js
test("every vocabulary class gets exactly one group, unknown types stay null", () => {
  const vocabulary = readVocabulary(fixtureRoot);
  assert.equal(vocabulary.length, 4);
  for (const label of vocabulary) assert.ok(["percussion", "tonal", "vocal", "fx", "foley", "ambience"].includes(groupForLabel(label).group));
  assert.equal(typeForLabel("Bass_drum"), "kick");
  assert.equal(typeForLabel("Door"), null);
  assert.equal(groupForLabel("Door").group, "foley");
  assert.equal(groupForLabel("Rain").domain, "ambience");
});

test("only CC0/BY clips are kept and the split follows the ground truth", () => {
  const { clips, skipped } = ingest({ root: fixtureRoot, split: "dev" });
  assert.deepEqual(clips.map((clip) => clip.split).sort(), ["train", "val"]);
  assert.equal(skipped.licenseTier, 1);
  assert.equal(skipped.missingAudio, 1);
  assert.deepEqual(clips.find((clip) => clip.clipId === "1").extras.labels, ["Bass_drum", "Drum"]);
  assert.equal(clips.find((clip) => clip.clipId === "1").label, "kick");
});
```

- [ ] **Step 2: Прогнать тест, убедиться, что падает**

Run: `cd services/training && node --test test/fsd50k.node.mjs`
Expected: FAIL.

- [ ] **Step 3: Реализовать `ingest.mjs` и `labels.map.json`**

CSV читается встроенным парсером (кавычки, запятые внутри кавычек) — без зависимостей. `skipped` считает
`licenseTier`, `missingAudio`, `unmappedLabel` (клип без единого типа).

- [ ] **Step 4: Прогнать тесты**

Run: `cd services/training && node --test test/fsd50k.node.mjs`
Expected: PASS.

- [ ] **Step 5: Коммит**

```bash
git add services/training/datasets/fsd50k services/training/test/fsd50k.node.mjs
git commit -m "feat(training): FSD50K ingest with ontology groups and per-clip licences"
```

---

### Task 5: `build-features.mjs` — выборка, CLI и склейка

**Files:**
- Create: `services/training/build-features.mjs`
- Test: `services/training/test/build-features.node.mjs`
- Test: `services/training/test/features-cli.node.mjs` (интеграционный, пропускается без `SONICATOR_FEATURES_CLI`)

**Interfaces:**
- Consumes: `ingest` из задач 2–4, `validateClip`.
- Produces:
  - `planShards(clips) -> { name, directory, links }[]` — по одному шарду на `source + split`.
  - `joinRows({ lines, clipsByPath }) -> { rows, unmatched, unlabelled }`.
  - `validateFeatureLine(line) -> object` — `featureSchema === 2`, `dimensions === 84`, все значения конечны и в 0..1.
  - `isShardDone({ out, shard, expected }) -> boolean` — `.done` на месте и строк ровно `expected`. Шард,
    прерванный на половине, возвращает `false`; повторный запуск перезаписывает его файл целиком.
  - CLI: `node build-features.mjs --root <root> --out <dir> [--jobs N] [--source nsynth|one-shot|fsd50k|all]
    [--cli <path>] [--full]`.

Порядок работы: ингест всех источников → `clips.jsonl` (отсортирован по `source`, `split`, `clipId`) →
`<out>/links/<source>-<split>/<index>-<basename>` (символические ссылки на выбранные файлы; индекс в имени снимает
совпадения имён) → `sonicator-features --in <links> --walk --out <out>/<source>-<split>.features.jsonl` на шард
(`--jobs` процессов одновременно, по умолчанию `min(4, cpus)`) → склейка по полному пути ссылки → `<out>/features.jsonl`.

Возобновление: шард пропускается, если рядом лежит `<out>/<shard>.done` и число строк в JSONL равно числу ссылок.
Прерванный шард перезапускается целиком, файл перезаписывается. `--full` обходит настоящие каталоги датасетов без
выборки (для отладки), ссылки не создаются.

- [ ] **Step 1: Написать падающий тест на склейку и валидацию**

```js
test("rows join by full link path, unmatched files are reported", () => {
  const clips = [{ clipId: "a", file: "/data/a.wav" }, { clipId: "b", file: "/data/b.wav" }];
  const links = new Map([["/out/links/x/0-a.wav", clips[0]], ["/out/links/x/1-b.wav", clips[1]]]);
  const lines = [
    { path: "/out/links/x/0-a.wav", featureSchema: 2, dimensions: 84, features: Array(84).fill(0.5) },
    { path: "/out/links/x/9-ghost.wav", featureSchema: 2, dimensions: 84, features: Array(84).fill(0.5) },
  ];
  const { rows, unmatched } = joinRows({ lines, clipsByPath: links });
  assert.deepEqual(rows.map((row) => row.clipId), ["a"]);
  assert.equal(unmatched.length, 1);
  assert.throws(() => validateFeatureLine({ ...lines[0], dimensions: 14 }), /dimensions/);
  assert.throws(() => validateFeatureLine({ ...lines[0], features: [1.2, ...Array(83).fill(0)] }), /outside 0..1/);
});

test("shard names are stable and one per source and split", () => {
  const shards = planShards([{ source: "nsynth", split: "test" }, { source: "nsynth", split: "valid" },
    { source: "fsd50k", split: "train" }]);
  assert.deepEqual(shards.map((shard) => shard.name), ["fsd50k-train", "nsynth-test", "nsynth-valid"]);
});

test("a half-written shard is not treated as done", () => {
  const out = mkdtempSync(join(tmpdir(), "shard-"));
  writeFileSync(join(out, "nsynth-test.features.jsonl"), "{}\n{}\n");
  assert.equal(isShardDone({ out, shard: "nsynth-test", expected: 3 }), false);
  writeFileSync(join(out, "nsynth-test.done"), "3\n");
  assert.equal(isShardDone({ out, shard: "nsynth-test", expected: 2 }), true);
  writeFileSync(join(out, "nsynth-test.done"), "3\n");
  assert.equal(isShardDone({ out, shard: "nsynth-test", expected: 4 }), false);
});
```

- [ ] **Step 2: Прогнать тест, убедиться, что падает**

Run: `cd services/training && node --test test/build-features.node.mjs`
Expected: FAIL.

- [ ] **Step 3: Реализовать `build-features.mjs`**

Строка результата: поля клипа (`source`, `clipId`, `license`, `tier`, `group`, `domain`, `label`, `labelRaw`,
`split`) плюс `featureSchema`, `dimensions`, `sampleRate`, `durationSec`, `nyquistHz`, `inBandEnergyShare`,
`features`, `extras`. `features.jsonl` пишется только при `--out` вне репозитория; тест проверяет, что путь не внутри
`services/training`.

- [ ] **Step 4: Прогнать тесты**

Run: `cd services/training && node --test test/build-features.node.mjs`
Expected: PASS.

- [ ] **Step 5: Интеграционный тест на настоящем CLI (пропускается без переменной)**

`features-cli.node.mjs` генерирует два WAV (синус и шум, 0.5 с, 44.1 кГц) в `os.tmpdir()`, запускает
`process.env.SONICATOR_FEATURES_CLI` с `--in ... --out ...` и проверяет две строки, `dimensions === 84`,
`featureSchema === 2`. Run:

```bash
cd services/training && SONICATOR_FEATURES_CLI=~/Library/Caches/sonicator-build/release/tools/sonicator-features/sonicator-features_artefacts/Release/sonicator-features node --test test/features-cli.node.mjs
```

Expected: PASS, 2 строки.

- [ ] **Step 6: Коммит**

```bash
git add services/training/build-features.mjs services/training/test/build-features.node.mjs services/training/test/features-cli.node.mjs
git commit -m "feat(training): select clips, run sonicator-features and join labels"
```

---

### Task 6: Волна 1 на реальных данных, отчёт и реестр

**Files:**
- Create: `services/training/wave1-report.mjs`
- Create: `services/training/test/wave1-report.node.mjs`
- Create: `services/training/datasets/nsynth/fetch.md`, `.../freesound-one-shot-percussive/fetch.md`, `.../fsd50k/fetch.md`
- Create: `docs/DATASETS_WAVE1.md` (сводка; полный отчёт — `out/wave1/report.md`)
- Modify: `services/training/datasets/manifest.json` (`clipsUsed`, `clipsSeen`, `downloadedAt`)
- Modify: `services/training/datasets.mjs` (необязательные поля и их проверка)
- Modify: `docs/DATA_CREDITS.md` (перегенерация)

**Interfaces:**
- Consumes: `out/wave1/features.jsonl`, `clips.jsonl`.
- Produces: `summarise({ clips, rows }) -> report`; CLI `node wave1-report.mjs --in out/wave1`.

- [ ] **Step 1: Написать падающий тест отчёта**

```js
const clipA = { source: "nsynth", split: "test", label: "bass", license: "CC-BY-4.0", group: "tonal" };
const clipB = { source: "nsynth", split: "test", label: null, license: "CC0-1.0", group: "tonal" };
const rowA = { inBandEnergyShare: 0.9 };
const rowB = { inBandEnergyShare: 0.05 };

test("report counts licences, unmapped labels and band share", () => {
  const report = summarise({ clips: [clipA, clipB], rows: [rowA, rowB] });
  assert.equal(report.total, 2);
  assert.equal(report.perSource.nsynth, 2);
  assert.equal(report.unmapped.nsynth, 1);
  assert.deepEqual(report.licenses, { "CC-BY-4.0": 1, "CC0-1.0": 1 });
  assert.equal(report.lowBandShare, 1);
});
```

- [ ] **Step 2: Прогнать тест, убедиться, что падает**

Run: `cd services/training && node --test test/wave1-report.node.mjs`
Expected: FAIL.

- [ ] **Step 3: Реализовать отчёт**

Разделы: объём по источникам и сплитам; лицензии (URL и короткое имя); `label: null` по источникам с примерами
исходных меток; распределение `inBandEnergyShare` (доли < 0.1, < 0.5, ≥ 0.5); покрытие инструментов NSynth;
число клипов на метку (топ-10 и хвост); пропуски (`skipped` каждого ингеста). Отчёт пишет `report.md` и
`summary.json`; `docs/DATASETS_WAVE1.md` — таблица источников, лицензий, выбранных/увиденных клипов и дата.

- [ ] **Step 4: Собрать CLI и прогнать волну 1**

```bash
cmake --preset release -DSONICATOR_BUILD_TRAINING_TOOLS=ON
ninja -C ~/Library/Caches/sonicator-build/release sonicator-features
cd services/training
node build-features.mjs --root /Volumes/1TBSSD/Datasets --out out/wave1 --jobs 4
node wave1-report.mjs --in out/wave1
```

Expected: `out/wave1/clips.jsonl` ≈ 50–60 тыс. строк (NSynth ≤ 5030, One-Shot ≈ 9 500, FSD50K ≈ 43 000);
`out/wave1/features.jsonl` — столько же строк, `dimensions: 84`; отчёт с нулём строк `featureSchema !== 2`.
Ориентир времени: 30–60 минут на 4 процесса.

- [ ] **Step 5: Обновить реестр и атрибуцию**

`manifest.json`: у `nsynth`, `freesound-one-shot-percussive`, `fsd50k` — `downloadedAt: "2026-11-12"`,
`clipsSeen` (файлов на диске) и `clipsUsed` (строк в волне 1). `validateManifest` принимает эти поля
необязательными и проверяет, что `clipsUsed <= clipsSeen`; тест `datasets.node.mjs` дополняется этим случаем.
`node generate-credits.mjs` перегенерирует `docs/DATA_CREDITS.md` с числом клипов.

Заодно `datasets/<name>/fetch.md` на каждый датасет: страница-источник, контрольные md5 из `manifest.json`,
корень `SONICATOR_DATASETS_ROOT`, раскладка каталогов после распаковки и напоминание, что аудио в git не попадает.
Для NSynth указать, что архив берётся с зеркала `confit/nsynth` и проверяется по md5 исходного бакета (GCS).

- [ ] **Step 6: Полный прогон тестов, граф и коммит**

```bash
cd services/training && npm test
cd ../.. && graphify update .
git add services/training docs/DATA_CREDITS.md docs/DATASETS_WAVE1.md
git commit -m "feat(training): wave 1 feature set from public datasets with a composition report"
```

Expected: `npm test` зелёный; в `git status` нет аудио, `clips.jsonl` и `features.jsonl`.

---

## После этого плана

- **Этап 5** (отдельный план): золотая выборка владельца (`sonicator-features --catalog` ещё не существует — это
  C++-задача того плана), абляция G1–G9, M1 против M2, `training-report.md` и критерии 7.6.
- **Этап 6** (отдельный план): БД 16, `features_v2`, загрузчик модели, резолвер, About, воркер вкладов и согласие 4.
- **Этап 7**: волна 2 — Slakh2100, SONYC-UST и датасеты уровня B; скачивание уже отлажено
  (`/Volumes/1TBSSD/Datasets/_fetch/fetch.sh`, архивы после распаковки удалены).
- **Этап 8**: foley-таксономия и групповая модель по FSD50K; типы foley в интерфейс не выводятся.

Риски волны 1: FSD50K даёт много меток без типа (в модель типов не пойдут, но останутся для группы); NSynth покрывает
только тональные семейства, ударные — One-Shot; 16 кГц у NSynth/One-Shot оставляют `hat`/`cymbal` без воздуха выше
8 кГц — это проверяется критериями этапа 5.
