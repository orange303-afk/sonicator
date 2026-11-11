# Stage 3.1: реестр датасетов и сборка прототипов — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Записать открытые датасеты в реестр с лицензиями и атрибуцией, заменить сборку глобальных прототипов
на взвешенный k-means и измерять качество поставляемого файла кросс-валидацией.

**Architecture:** Две независимые, но небольшие работы этапов 0 и 1 спецификации. Реестр — данные плюс
генератор документа атрибуции и тест. Сборка прототипов — одна функция `selectPrototypes` в
`services/training/lib.mjs`: точки одного источника (установки) получают равный суммарный вес, центроиды
считаются k-means, в файл пишутся арифметические средние (значения остаются в 0..1, схема файла и C++ не
меняются). Метрика — пятифолдовая кросс-валидация по тем же записям, которые сейчас есть в датасете.

**Tech Stack:** Node 20+, `node:test`, без зависимостей; формат — JSON и JSONL. C++ в этом плане не меняется.

**Spec:** `docs/superpowers/specs/2026-11-12-feature-schema-2-public-datasets-design.md` (части 5 и 7.3–7.4)

## Global Constraints

- `services/training` остаётся без внешних зависимостей: только стандартная библиотека Node и `node:test`.
- Значения признаков и прототипов — в 0..1, округление 0.001 при записи (`QUANTIZE_STEP`).
- Файл прототипов воспроизводим побайтно: порядок не зависит от локали, сортировка по `recordId`.
- Схема файла `prototypes.v1` и `featureSchema 1` не меняются: C++ и приложение не трогаются.
- Пороги поставляемого файла не меняются: 3 установки (`MIN_INSTALLS`) и 10 записей (`MIN_SUPPORT`).
- Уровень C лицензий не используется; уровень B используется (решение владельца 2026-11-12).
- Язык документов проекта — русский для новых файлов этапа; текст в существующих файлах не переписывается
  без необходимости.

## Review Focus

- Метка, где одна установка даёт 1000 записей, а другая 3: суммарный вес установок равный, большая библиотека
  не занимает все прототипы.
- Запись с нулевым вектором признаков (тишина): нет NaN, нет падения, результат детерминирован.
- `max` больше числа различных векторов: прототипов меньше, но не ноль, когда записи есть.
- Одинаковые векторы у нескольких записей: детерминированный результат без дубликатов прототипов.
- Реестр с общей лицензией на весь датасет (без файла лицензий на клип) и идентификаторы с пробелами или
  не-ASCII: документ атрибуции собирается и сортируется стабильно.

---

### Task 1: Реестр датасетов с лицензиями

**Files:**
- Create: `services/training/datasets/manifest.json`
- Create: `services/training/datasets.mjs`
- Test: `services/training/test/datasets.node.mjs`

**Interfaces:**
- Consumes: ничего.
- Produces: `validateManifest(entries: object[]) -> object[]` (бросает `Error` с текстом `manifest: ...`),
  `usedDatasets(entries) -> object[]`, `loadManifest(path?: string) -> object[]`, константа `MANIFEST_PATH`.
  Поля записи: `id`, `title`, `version`, `url`, `license`, `licensePerClip` (boolean), `tier` (`"A"|"B"|"C"`),
  `used` (boolean), `creditLine`, `audio` (`format`, `sampleRateHz`), `notes` (необязательно).

- [ ] **Step 1: Написать падающий тест**

```js
// services/training/test/datasets.node.mjs
import assert from "node:assert/strict";
import test from "node:test";
import { MANIFEST_PATH, loadManifest, usedDatasets, validateManifest } from "../datasets.mjs";

test("the shipped manifest lists every dataset with a licence and a tier", () => {
  const entries = loadManifest();
  assert.ok(entries.length >= 13);
  for (const entry of entries) {
    assert.ok(entry.id.length > 0, "id");
    assert.ok(entry.title.length > 0, `${entry.id}: title`);
    assert.ok(entry.version.length > 0, `${entry.id}: version`);
    assert.match(entry.url, /^https:\/\//, `${entry.id}: url`);
    assert.ok(entry.license.length > 0, `${entry.id}: license`);
    assert.ok(["A", "B", "C"].includes(entry.tier), `${entry.id}: tier`);
    assert.equal(typeof entry.used, "boolean", `${entry.id}: used`);
    assert.ok(entry.creditLine.length > 0, `${entry.id}: creditLine`);
  }
  assert.equal(new Set(entries.map((entry) => entry.id)).size, entries.length, "ids are unique");
});

test("no level C dataset is used and every used one has an attribution line", () => {
  for (const entry of usedDatasets(loadManifest())) {
    assert.notEqual(entry.tier, "C", `${entry.id} is level C`);
    assert.match(entry.creditLine, /CC|Creative Commons|research/i, `${entry.id}: licence in the credit line`);
  }
});

test("a manifest without a licence or with a foreign tier is refused", () => {
  const base = loadManifest()[0];
  assert.throws(() => validateManifest([{ ...base, license: "" }]), /manifest: .*license/);
  assert.throws(() => validateManifest([{ ...base, tier: "D" }]), /manifest: .*tier/);
  assert.throws(() => validateManifest([base, base]), /manifest: .*duplicate/);
  assert.throws(() => validateManifest([{ ...base, used: true, tier: "C" }]), /manifest: .*level C/);
});

test("per-clip licensing is stated explicitly for Freesound-derived datasets", () => {
  const entries = loadManifest();
  for (const id of ["fsd50k", "freesound-one-shot-percussive"]) {
    const entry = entries.find((candidate) => candidate.id === id);
    assert.ok(entry, `${id} is missing`);
    assert.equal(entry.licensePerClip, true, `${id}: licensePerClip`);
  }
});

test("the manifest path points inside the repository", () => {
  assert.match(MANIFEST_PATH, /services\/training\/datasets\/manifest\.json$/);
});
```

- [ ] **Step 2: Прогнать тест и увидеть падение**

Run: `cd services/training && node --test test/datasets.node.mjs`
Expected: FAIL — `Cannot find module '../datasets.mjs'`.

- [ ] **Step 3: Создать `services/training/datasets.mjs`**

```js
export const MANIFEST_PATH = "services/training/datasets/manifest.json";
export function validateManifest(entries) { /* проверки из теста, тексты начинаются с "manifest: " */ }
export function usedDatasets(entries) { /* entries.filter((entry) => entry.used) */ }
export function loadManifest(path = MANIFEST_PATH) { /* readFileSync + JSON.parse + validateManifest */ }
```

`loadManifest` читает путь относительно корня репозитория: `new URL(\`../../${path}\`, import.meta.url)`.
`validateManifest` бросает `manifest: <id>: license is empty`, `manifest: <id>: tier D is not A, B or C`,
`manifest: duplicate id <id>`, `manifest: <id>: level C dataset cannot be used`.

- [ ] **Step 4: Заполнить `services/training/datasets/manifest.json`**

Записи берутся из части 5.2 спецификации; `used: true` у всего, что идёт в обучение или оценку (`nsynth`,
`freesound-one-shot-percussive`, `fsd50k`, `slakh2100`, `sonyc-ust`, `philharmonia`, `esc50`, `urbansound8k`,
`bbc-sfx`, `medleydb`, `medley-solos-db`, `idmt-smt-drums`, `enst-drums`), `used: false` у `sonniss-gdc`,
`audioset`, `audiocaps`, `wavcaps`. Для исключённых в `notes` — причина (`ML training prohibited`,
`YouTube audio, no explicit licence`, `not needed`). Точные record id Zenodo, не подтверждённые при сборе
данных, записать строкой `verify at download` в `notes` и не выдумывать: `url` — страница проекта или
поиск Zenodo.

- [ ] **Step 5: Прогнать тест**

Run: `cd services/training && node --test test/datasets.node.mjs`
Expected: PASS, 5 тестов.

- [ ] **Step 6: Коммит**

```bash
git add services/training/datasets/manifest.json services/training/datasets.mjs services/training/test/datasets.node.mjs
git commit -m "feat(training): register open datasets with licences and tiers"
```

---

### Task 2: Документ атрибуции из реестра

**Files:**
- Create: `services/training/generate-credits.mjs`
- Create: `docs/DATA_CREDITS.md` (генерируется)
- Modify: `THIRD_PARTY_NOTICES.md` (добавить раздел «Training data»)
- Modify: `services/training/package.json` (скрипт `credits`)
- Test: `services/training/test/credits.node.mjs`

**Interfaces:**
- Consumes: `loadManifest()`, `usedDatasets()` из Task 1.
- Produces: `renderCredits(entries) -> string`, `writeCredits({ output }?) -> Promise<void>`,
  CLI `node generate-credits.mjs [--out <path>]` (по умолчанию `docs/DATA_CREDITS.md`).

- [ ] **Step 1: Написать падающий тест**

```js
// services/training/test/credits.node.mjs
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import { loadManifest, usedDatasets } from "../datasets.mjs";
import { renderCredits } from "../generate-credits.mjs";

test("the committed credits file matches the manifest byte for byte", () => {
  const committed = readFileSync("../../docs/DATA_CREDITS.md", "utf8");
  assert.equal(committed, renderCredits(loadManifest()));
});

test("every used dataset is named with its licence", () => {
  const text = renderCredits(loadManifest());
  for (const entry of usedDatasets(loadManifest())) {
    assert.ok(text.includes(entry.title), `${entry.id}: title`);
    assert.ok(text.includes(entry.license), `${entry.id}: licence`);
  }
});

test("excluded datasets are listed as not used, level C with the reason", () => {
  const text = renderCredits(loadManifest());
  const sonniss = loadManifest().find((entry) => entry.id === "sonniss-gdc");
  assert.ok(text.includes("not used"));
  assert.ok(text.includes(sonniss.notes));
});

test("the notices file points at the credits document", () => {
  const notices = readFileSync("../../THIRD_PARTY_NOTICES.md", "utf8");
  assert.match(notices, /docs\/DATA_CREDITS\.md/);
});
```

- [ ] **Step 2: Прогнать тест и увидеть падение**

Run: `cd services/training && node --test test/credits.node.mjs`
Expected: FAIL — `Cannot find module '../generate-credits.mjs'`.

- [ ] **Step 3: Реализовать генератор**

`renderCredits` собирает markdown без даты и без путей машины (файл воспроизводим): заголовок, абзац «Файл
собран из `services/training/datasets/manifest.json`; править вручную нельзя», таблица используемых датасетов
(`id`, название, версия, лицензия, уровень) в порядке сортировки по `id`, затем раздел «Атрибуция» со строками
`creditLine` в том же порядке, затем раздел «Не используются» с причиной из `notes`. `writeCredits` пишет файл
через `mkdir(..., { recursive: true })`.

- [ ] **Step 4: Сгенерировать файл и добавить раздел в уведомления**

```bash
cd services/training && node generate-credits.mjs
```

В `THIRD_PARTY_NOTICES.md` добавить после существующих разделов:

```md
## Training data

The shipped global model is built from public audio datasets and from contributed records. Every
dataset, its version, its licence and the attribution line the licence requires are listed in
[docs/DATA_CREDITS.md](docs/DATA_CREDITS.md); that file is generated from
`services/training/datasets/manifest.json`.
```

Добавить в `services/training/package.json` скрипт `"credits": "node generate-credits.mjs"`.

- [ ] **Step 5: Прогнать тесты**

Run: `cd services/training && node --test test/credits.node.mjs && node --test test/*.node.mjs`
Expected: PASS.

- [ ] **Step 6: Коммит**

```bash
git add services/training/generate-credits.mjs services/training/test/credits.node.mjs docs/DATA_CREDITS.md THIRD_PARTY_NOTICES.md services/training/package.json
git commit -m "docs(training): generate dataset attribution from the licence registry"
```

---

### Task 3: Кросс-валидация поставляемых прототипов

**Files:**
- Modify: `services/training/evaluate.mjs`
- Test: `services/training/test/evaluate.node.mjs`

**Interfaces:**
- Consumes: `buildPrototypes`, `indexPrototypes`, `predictType`, `normalizeRecords` из `lib.mjs`.
- Produces: `crossValidateShipped(records, { folds = 5 } = {}) -> { folds, records, correct, accuracy,
  groups: Map<string, {records, correct}>, labels: Map<string, {records, correct}> }`.
  CLI-флаг `--cv <n>` (по умолчанию 5, `0` — выключено).

- [ ] **Step 1: Написать падающий тест**

```js
// дописать в services/training/test/evaluate.node.mjs
import { crossValidateShipped } from "../evaluate.mjs";

test("shipped prototypes are measured without seeing the record under test", () => {
  const rows = [];
  for (const [label, base] of [["kick", ACOUSTIC], ["snare", BRIGHT], ["hat", DARK]])
    for (let i = 0; i < 40; i += 1)
      rows.push(record({
        installId: `i${i % 3}`,
        recordId: `${label}-${String(i).padStart(4, "0")}`,
        type: label,
        tags: [],
        features: features(base, 0.01 * (i % 5)),
      }));
  const result = crossValidateShipped(rows, { folds: 5 });
  assert.equal(result.folds, 5);
  assert.equal(result.records, 120);
  assert.ok(result.accuracy > 0.9, `accuracy ${result.accuracy}`);
  assert.equal([...result.groups.keys()].length, 3);
});

test("cross-validation tolerates fewer records than folds", () => {
  const rows = [record({ recordId: "only-1", type: "kick", tags: [] })];
  const result = crossValidateShipped(rows, { folds: 5 });
  assert.equal(result.records, 1);
  assert.ok(result.accuracy >= 0 && result.accuracy <= 1);
});

test("custom types do not count towards shipped accuracy", () => {
  const rows = dataset(["a", "b", "c"], 20, { type: "MyLayer", typeIsCustom: true, tags: [] });
  assert.equal(crossValidateShipped(rows).records, 0);
});
```

- [ ] **Step 2: Прогнать тест и увидеть падение**

Run: `cd services/training && node --test test/evaluate.node.mjs`
Expected: FAIL — `crossValidateShipped is not a function`.

- [ ] **Step 3: Реализовать `crossValidateShipped`**

Фолд записи: внутри каждой установки записи сортируются по `recordId`, номер фолда — позиция `% folds`.
Для каждого фолда индекс строится из записей остальных фолдов
(`buildPrototypes(others, { minInstalls: 1, minSupport: 1 })`, пользовательские типы исключены), предсказание —
`predictType(features, emptyIndex, globalIndex)` (только глобальная часть, штраф 0.95 как в приложении).
Считаются все записи с непустым не-пользовательским типом; в `groups` — `groupForType(type)`, в `labels` —
тип в нижнем регистре.

- [ ] **Step 4: Прогнать тест**

Run: `cd services/training && node --test test/evaluate.node.mjs`
Expected: PASS.

- [ ] **Step 5: Замерить базовую линию до изменения сборки**

Run:
```bash
cd services/training
node evaluate.mjs --in ../../services/contribute-worker/out/contribute-export.jsonl --out out/baseline-report.md --cv 5
```
Записать в отчёт (в сообщение задачи или в `out/baseline-report.md`, который не коммитится) точность
«shipped prototypes, cross-validated». Ожидание по измерению на этапе проектирования: около 46%.

- [ ] **Step 6: Добавить раздел в отчёт и флаг CLI**

`renderReport` получает секцию «Shipped prototypes, cross-validated» с точностью, числом фолдов, таблицей по
группам и худшими 5 метками. `main` читает `--cv`, при `0` секция не печатается. Строка лога:
`shipped prototypes, 5-fold cv: NN.N%`.

- [ ] **Step 7: Коммит**

```bash
git add services/training/evaluate.mjs services/training/test/evaluate.node.mjs
git commit -m "feat(training): measure the shipped prototypes with cross-validation"
```

---

### Task 4: Взвешенный k-means вместо farthest-first с медоидами

**Files:**
- Modify: `services/training/lib.mjs`
- Modify: `services/training/test/lib.node.mjs`
- Modify: `services/training/build-prototypes.mjs` (текст лога)

**Interfaces:**
- Consumes: `cosine`, `compareText` из `lib.mjs`.
- Produces: `selectPrototypes(rows, max = MAX_PROTOTYPES_PER_LABEL) -> number[][]` — та же подпись, новый
  алгоритм; константа `MAX_ROWS_PER_INSTALL_PER_LABEL = 2000` вместо `MAX_RECORDS_PER_INSTALL_PER_LABEL`;
  `CLUSTER_ITERATIONS = 30`.

- [ ] **Step 1: Переписать тест «одна большая установка»**

```js
test("one large install cannot take every prototype", () => {
  const rows = [];
  for (let i = 0; i < 1000; i += 1)
    rows.push(record({
      installId: "big",
      recordId: `big-${String(i).padStart(4, "0")}`,
      features: features(ACOUSTIC),
    }));
  for (let i = 0; i < 20; i += 1)
    rows.push(record({
      installId: "small",
      recordId: `small-${String(i).padStart(4, "0")}`,
      features: features(BRIGHT),
    }));
  const selected = selectPrototypes(rows, 2);
  assert.equal(selected.length, 2);
  assert.ok(selected.some((feature) => cosine(feature, features(ACOUSTIC)) > 0.99));
  assert.ok(selected.some((feature) => cosine(feature, features(BRIGHT)) > 0.99));
});
```

- [ ] **Step 2: Добавить тесты краёв**

```js
test("zero vectors and identical rows stay finite and deterministic", () => {
  const rows = [
    record({ recordId: "z1", features: new Array(14).fill(0) }),
    record({ recordId: "z2", features: new Array(14).fill(0) }),
    record({ recordId: "z3", features: features(ACOUSTIC) }),
    record({ recordId: "z4", features: features(ACOUSTIC) }),
  ];
  const first = selectPrototypes(rows, 8);
  const second = selectPrototypes(rows, 8);
  assert.deepEqual(first, second);
  for (const feature of first) for (const value of feature) assert.ok(Number.isFinite(value));
});

test("more prototypes than distinct vectors yields the distinct ones", () => {
  const rows = [1, 2, 3].map((index) => record({ recordId: `r${index}`, features: features(ACOUSTIC) }));
  const selected = selectPrototypes(rows, 8);
  assert.equal(selected.length, 1);
});

test("prototypes stay inside 0..1", () => {
  const rows = dataset(["a", "b", "c"], 30);
  for (const feature of selectPrototypes(rows, 8))
    for (const value of feature) assert.ok(value >= 0 && value <= 1);
});
```

Удалить утверждение `assert.equal(MAX_RECORDS_PER_INSTALL_PER_LABEL, 20)` и импорт старой константы.

- [ ] **Step 3: Прогнать тесты и увидеть падение**

Run: `cd services/training && node --test test/lib.node.mjs`
Expected: FAIL на новом тесте большой установки (одна метка не даёт прототип BRIGHT).

- [ ] **Step 4: Реализовать взвешенный k-means**

В `lib.mjs`: `capPerInstall` заменяется на `capRows` (не более `MAX_ROWS_PER_INSTALL_PER_LABEL` записей
установки на метку, порядок — по `recordId`), затем вес строки `1 / (число строк этой установки в метке)`,
так что каждая установка даёт равную суммарную массу. Инициализация — farthest-first по косинусу от первой
точки; до `CLUSTER_ITERATIONS` итераций «назначение по максимальному косинусу к нормированному центроиду →
пересчёт центроида как взвешенного среднего»; остановка, когда назначения не изменились. В результат идут
взвешенные средние непустых кластеров (без нормировки, значения остаются в 0..1), дубликаты с точностью
`1e-6` удаляются, порядок — по индексу первого участника. Пустые кластеры в результат не попадают.

- [ ] **Step 5: Прогнать весь набор тестов услуги**

Run: `cd services/training && node --test test/*.node.mjs`
Expected: PASS, все тесты, включая прежние про приоритет локального и воспроизводимость.

- [ ] **Step 6: Обновить текст лога `build-prototypes.mjs`**

Строка «per label: up to 8 prototypes…» получает пояснение `weighted k-means, one install has the same total
weight as another`.

- [ ] **Step 7: Коммит**

```bash
git add services/training/lib.mjs services/training/test/lib.node.mjs services/training/build-prototypes.mjs
git commit -m "feat(training): cluster prototypes with weighted k-means instead of medoids"
```

---

### Task 5: Замер, документы и итог этапа

**Files:**
- Modify: `docs/BACKLOG.md` (пункт 12)
- Modify: `docs/superpowers/specs/2026-11-11-training-and-tag-promotion-design.md` (раздел «Решения исполнителя»)
- Modify: `services/training/README.md` (создать, если нет)

**Interfaces:**
- Consumes: `crossValidateShipped` (Task 3), `selectPrototypes` (Task 4).
- Produces: числа в документах и коммит этапа.

- [ ] **Step 1: Замерить после изменения**

Run:
```bash
cd services/training
node evaluate.mjs --in ../../services/contribute-worker/out/contribute-export.jsonl --out out/training-report.md --cv 5
```
Ожидание: 70–73% против ~46% базовой линии; при результате ниже 60% остановиться и разобрать причину, не
записывая числа в документы.

- [ ] **Step 2: Записать числа в документы**

В `docs/BACKLOG.md` пункт 12: добавить, что сборка прототипов — взвешенный k-means, прототипы каждого
источника имеют равный вес, качество поставляемого файла измеряется кросс-валидацией (`до NN% / после NN%`
на датасете владельца от 2026-11-11). В спецификации этапа 2 в разделе «Решения исполнителя» заменить
описание farthest-first на k-means и указать причину (измерение: 46% против 72%).

- [ ] **Step 3: Создать `services/training/README.md`**

Короткий файл: что собирается (`build-prototypes.mjs`, `evaluate.mjs`, `generate-credits.mjs`), откуда берётся
датасет (`services/contribute-worker/scripts/export.mjs`), что означают `--cv` и `--max-tier` (последний —
в следующем плане, упомянуть как будущий), где лицензии (`datasets/manifest.json`, `docs/DATA_CREDITS.md`).

- [ ] **Step 4: Полный прогон тестов**

Run: `cd services/training && node --test test/*.node.mjs`
Expected: PASS.

- [ ] **Step 5: Обновить граф и запушить**

```bash
graphify update .
git add docs/BACKLOG.md docs/superpowers/specs/2026-11-11-training-and-tag-promotion-design.md services/training/README.md docs/superpowers/specs/2026-11-12-feature-schema-2-public-datasets-design.md docs/superpowers/plans/2026-11-12-stage3-prototype-assembly.md
git commit -m "docs(training): record the measured prototype quality and the dataset registry"
git push origin main
```

---

## Результат (записано после выполнения)

| Что | До | После |
|---|---|---|
| Поставляемые прототипы, кросс-валидация 5 фолдов на экспорте владельца (4526 записей, 1 установка, 13 типов) | 57.1% (farthest-first с медоидами) | 65.7% (взвешенный k-means) |
| Локальный путь (каждая подтверждённая запись — прототип) | — | 80.5% |

Оценки 46% и 72% из фазы проектирования были завышены: там запись сравнивалась с прототипами, в
которые входила она сама. Кросс-валидация на тех же данных даёт честную пару 57.1% → 65.7%.
`evaluate.mjs` на этих данных выполняется за 1.4 с; локальный индекс в leave-one-install-out теперь
строится как в приложении (все подтверждённые строки), поэтому отчёт не кластеризует заново на каждую
запись.
