#!/usr/bin/env node
// Stage 5: train M1 and M2 on the public wave, choose the feature groups by ablation, evaluate on the
// owner's gold sample and write the candidate model plus the report. Node only, no dependencies.
//
// The public wave never contains a gold row: the gold file is read here and used for measurement only.

import { mkdirSync, readFileSync, statSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { assertOutAllowed } from "./build-features.mjs";
import { loadManifest, usedDatasets } from "./datasets.mjs";
import { groupForType } from "./groups.mjs";
import { STANDARD_TYPES } from "./standard.mjs";
import { fitScaler, indicesFor, loadGold, loadPublic, prepare } from "./v2/features.mjs";
import { fitLocalCalibration } from "./v2/calibration.mjs";
import { dedupeGold, evaluateGold } from "./v2/evaluate.mjs";
import { fitSimilarityMap, predict, trainCentroids, trainSoftmax } from "./v2/models.mjs";
import { renderReport } from "./v2/report.mjs";

export const MODEL_SCHEMA = "model.v1";
const GROUP_ORDER = ["G2", "G3", "G4", "G5", "G6", "G7", "G8"];

export function chooseModel({ m1, m2 }) {
  return m2.top1 - m1.top1 >= 0.02 - 1e-9 ? "M2" : "M1";
}

export function checkCriteria({ modes, reproducible }) {
  const mode1 = modes[1];
  const mode3 = modes[3];
  const mode4 = modes[4];
  const groupTop1 = (result, group) => (result.perGroup ?? []).find((entry) => entry.group === group)?.top1 ?? 0;
  const groupsNotWorse = ["percussion", "tonal"].every((group) => groupTop1(mode4, group) >= groupTop1(mode1, group) - 1e-9);
  return [
    {
      name: "холодный старт (3): top-1",
      value: mode3.top1,
      threshold: 0.70,
      pass: mode3.top1 >= 0.70 - 1e-9,
    },
    {
      name: "режим (4) против (1): top-1",
      value: mode4.top1 - mode1.top1,
      threshold: 0.05,
      unit: "pp",
      pass: mode4.top1 - mode1.top1 >= 0.05 - 1e-9 && groupsNotWorse,
    },
    {
      name: "точность при пороге приёма (4)",
      value: mode4.precisionAtThreshold,
      threshold: 0.90,
      pass: mode4.precisionAtThreshold >= 0.90 - 1e-9 && mode4.coverage >= mode1.coverage - 1e-9,
    },
    {
      name: "воспроизводимость",
      value: reproducible ? "да" : "нет",
      threshold: "побайтно",
      pass: reproducible === true,
    },
  ];
}

function valTop1(model, scaler, rows) {
  if (rows.length === 0) return 0;
  let correct = 0;
  for (const row of rows) if (predict(model, scaler, row.features).label === row.label) correct += 1;
  return correct / rows.length;
}

function goldTop1(model, scaler, gold) {
  return evaluateGold({ gold, publicModel: model, scaler, mode: 3, indices: scaler.indices }).top1;
}

// Greedy addition of G2..G8: a group stays only if it buys at least half a point on the public val
// split and costs no more than half a point on the gold sample. Every candidate is a fresh model.
export function ablate({ train, val, gold, epochs = 300, patience = 20 }) {
  const measure = (groups, { dropMoments = false } = {}) => {
    const scaler = fitScaler(train, indicesFor(groups, { dropMoments }));
    const model = trainSoftmax(train, val, scaler, { epochs, patience });
    return {
      valTop1: valTop1(model, scaler, val),
      goldTop1: goldTop1(model, scaler, gold),
      scaler,
      model,
    };
  };

  let groups = ["G1"];
  let best = measure(groups);
  const steps = [];
  for (const group of GROUP_ORDER) {
    const candidate = [...groups, group];
    const result = measure(candidate);
    const valDelta = result.valTop1 - best.valTop1;
    const goldDelta = result.goldTop1 - best.goldTop1;
    const kept = valDelta >= 0.005 - 1e-9 && goldDelta >= -0.005 - 1e-9;
    steps.push({ group, valDelta, goldDelta, kept });
    if (kept) {
      groups = candidate;
      best = result;
    }
  }

  // A diagnostic row, always measured: does the group fail because of the two moment pairs, or for
  // another reason? When G2 was not kept, the variant is measured on top of the final set, which is
  // the comparison the owner needs.
  const variantGroups = groups.includes("G2") ? groups : [...groups, "G2"];
  const variant = measure(variantGroups, { dropMoments: true });
  steps.push({
    group: groups.includes("G2") ? "G2 без моментов" : "G2 без моментов (добавка к набору)",
    valDelta: variant.valTop1 - best.valTop1,
    goldDelta: variant.goldTop1 - best.goldTop1,
    kept: false,
  });

  return { steps, indices: best.scaler.indices, groups, scaler: best.scaler };
}

function evaluateModes({ gold, model, scaler, indices, localMaps }) {
  const modes = {
    1: evaluateGold({ gold, publicModel: model, scaler, mode: 1, indices, localMap: localMaps.v1 }),
    2: evaluateGold({ gold, publicModel: model, scaler, mode: 2, indices, localMap: localMaps.v2 }),
    3: evaluateGold({ gold, publicModel: model, scaler, mode: 3, indices }),
    4: evaluateGold({ gold, publicModel: model, scaler, mode: 4, indices, localMap: localMaps.v2 }),
  };
  return {
    modes,
    comparisons: [
      { mode: 1, name: "v1, первые 14, локальный косинус", ...comparison(modes[1]) },
      { mode: 2, name: "v2, выбранные группы, локальный косинус", ...comparison(modes[2]) },
      { mode: 3, name: "v2, только публичная модель", ...comparison(modes[3]) },
      { mode: 4, name: "v2, публичная + локальная", ...comparison(modes[4]) },
    ],
  };
}

function comparison(result) {
  return {
    top1: result.top1,
    top3: result.top3,
    coverage: result.coverage,
    precision: result.precisionAtThreshold,
    records: result.records,
    perGroup: result.perGroup,
    globalWins: result.globalWins,
    missingLabels: result.missingLabels,
  };
}

// Rows are the truth labels of interest; the last column collects everything predicted outside the
// set, so a row still adds up to the number of gold rows with that truth.
function confusionFor(confusion, labels) {
  const present = labels.filter((label) => confusion.has(label));
  const columns = [...present];
  const cells = present.map((truth) => {
    const row = confusion.get(truth);
    const counts = present.map((predicted) => row.get(predicted) ?? 0);
    const total = [...row.values()].reduce((sum, value) => sum + value, 0);
    const inside = counts.reduce((sum, value) => sum + value, 0);
    return [...counts, total - inside];
  });
  if (cells.some((row) => row.at(-1) > 0)) columns.push("прочее");
  else for (const row of cells) row.pop();
  return { rows: present, columns, cells };
}

function foleyByGroup(model, scaler, testRows) {
  const groups = new Map();
  for (const row of testRows) {
    if (row.source !== "fsd50k") continue;
    if (typeof row.label !== "string" || !STANDARD_TYPES.includes(row.label)) continue;
    const group = groupForType(row.label);
    const entry = groups.get(group) ?? { group, records: 0, correct: 0 };
    entry.records += 1;
    if (predict(model, scaler, row.features).label === row.label) entry.correct += 1;
    groups.set(group, entry);
  }
  return [...groups.values()]
    .map((entry) => ({ ...entry, top1: entry.records === 0 ? 0 : entry.correct / entry.records }))
    .sort((a, b) => a.group.localeCompare(b.group));
}

function generatedAtOf(manifest) {
  const dates = usedDatasets(manifest).map((entry) => entry.downloadedAt).filter((value) => typeof value === "string");
  return dates.length === 0 ? "1970-01-01T00:00:00.000Z" : `${dates.sort().at(-1)}T00:00:00.000Z`;
}

function serialise(value) {
  return `${JSON.stringify(value, null, 2)}\n`;
}

export function buildModelFile({ model, scaler, generatedAt, manifest }) {
  const payload = model.kind === "M2"
    ? { kind: model.kind, labels: model.labels, W: model.W, b: model.b, temperature: model.temperature }
    : { kind: model.kind, labels: model.labels, prototypes: model.prototypes };
  return {
    schema: MODEL_SCHEMA,
    featureSchema: 2,
    generatedAt,
    featureIndices: scaler.indices,
    mu: scaler.mu,
    sd: scaler.sd,
    model: payload,
    similarityMap: model.kind === "M2" ? (model.similarityMap ?? []) : [],
    groups: Object.fromEntries(model.labels.map((label) => [label, groupForType(label)])),
    datasets: usedDatasets(manifest).map((entry) => ({
      id: entry.id,
      tier: entry.tier,
      clipsUsed: entry.clipsUsed ?? 0,
    })),
  };
}

function parseArgs(argv) {
  const options = {
    public: null,
    gold: null,
    out: "out/model-v2",
    doc: null,
    epochs: 300,
    patience: 20,
    manifest: null,
  };
  for (let i = 0; i < argv.length; i += 1) {
    const argument = argv[i];
    const value = () => {
      if (i + 1 >= argv.length) throw new Error(`${argument} needs a value`);
      return argv[++i];
    };
    if (argument === "--public") options.public = value();
    else if (argument === "--gold") options.gold = value();
    else if (argument === "--out") options.out = value();
    else if (argument === "--doc") options.doc = value();
    else if (argument === "--epochs") options.epochs = Number(value());
    else if (argument === "--patience") options.patience = Number(value());
    else if (argument === "--manifest") options.manifest = value();
    else throw new Error(`unknown argument: ${argument}`);
  }
  if (options.public === null) throw new Error("--public is required");
  if (options.gold === null) throw new Error("--gold is required");
  if (!Number.isInteger(options.epochs) || options.epochs < 1) throw new Error("--epochs must be a positive integer");
  return options;
}

const CURVE_THRESHOLDS = [0.5, 0.6, 0.7, 0.8, 0.9];

// The acceptance threshold decides how much of the library gets a label at all, so the report shows
// the trade-off instead of one operating point.
function coverageCurve(result) {
  const total = result.samples.length;
  return CURVE_THRESHOLDS.map((threshold) => {
    const accepted = result.samples.filter((sample) => sample.score >= threshold);
    return {
      threshold,
      coverage: total === 0 ? 0 : accepted.length / total,
      precision: accepted.length === 0 ? 0 : accepted.filter((sample) => sample.hit === 1).length / accepted.length,
    };
  });
}

function groupNote(modes) {
  const top1 = (result, group) => (result.perGroup ?? []).find((entry) => entry.group === group)?.top1 ?? 0;
  return ["percussion", "tonal"]
    .map((group) => `${group}: (1) ${(top1(modes[1], group) * 100).toFixed(1)}% → (4) ${(top1(modes[4], group) * 100).toFixed(1)}%`)
    .join("; ");
}

// Everything that depends on the inputs, with no file written and no wall-clock value inside, so two
// calls can be compared byte for byte to prove the reproducibility criterion instead of asserting it.
function runPipeline({ train, val, test, gold, manifest, generatedAt, epochs, patience }) {
  const ablation = ablate({ train, val, gold, epochs, patience });
  const scaler = fitScaler(train, ablation.indices);
  const m1 = trainCentroids(train, scaler);
  const m2 = trainSoftmax(train, val, scaler, { epochs, patience });
  m2.similarityMap = fitSimilarityMap(m2, scaler, val);
  const m1Score = goldTop1(m1, scaler, gold);
  const m2Score = goldTop1(m2, scaler, gold);
  const chosen = chooseModel({ m1: { top1: m1Score }, m2: { top1: m2Score } });
  const model = chosen === "M2" ? m2 : m1;
  const localMaps = {
    v1: fitLocalCalibration(gold, { indices: Array.from({ length: 14 }, (_, index) => index) }),
    v2: fitLocalCalibration(gold, { indices: scaler.indices }),
  };
  const { modes, comparisons } = evaluateModes({ gold, model, scaler, indices: scaler.indices, localMaps });
  const file = buildModelFile({ model, scaler, generatedAt, manifest });
  const confusion = modes[4].confusion;
  return {
    modes,
    modelFile: file,
    modelText: serialise(file),
    name: chosen === "M2" ? "global_model.json" : "global_prototypes.v2.json",
    report: {
      generatedAt,
      choice: { m1: m1Score, m2: m2Score, chosen },
      comparisons,
      curves: { one: coverageCurve(modes[1]), four: coverageCurve(modes[4]) },
      ablation: ablation.steps,
      featureCount: scaler.indices.length,
      groupNote: groupNote(modes),
      confusion: {
        "snare / clap / hat / rim": confusionFor(confusion, ["snare", "clap", "hat", "rim"]),
        "fx / impact / riser": confusionFor(confusion, ["fx", "impact", "riser"]),
      },
      worst: [...modes[4].perLabel]
        .filter((entry) => entry.records >= 10)
        .sort((a, b) => a.top1 - b.top1 || b.records - a.records || a.label.localeCompare(b.label))
        .slice(0, 10),
      foley: foleyByGroup(model, scaler, test),
      datasets: file.datasets,
    },
  };
}

export function main(argv = process.argv.slice(2)) {
  const options = parseArgs(argv);
  const out = resolve(options.out);
  assertOutAllowed(out);
  const doc = options.doc === null
    ? fileURLToPath(new URL("../../docs/TRAINING_REPORT_V2.md", import.meta.url))
    : resolve(options.doc);

  const publicRows = prepare(loadPublic(options.public));
  const exportedGold = loadGold(options.gold);
  const standardGold = prepare(exportedGold, { minBandShare: 0 });
  const deduped = dedupeGold(standardGold);
  const goldRows = deduped.rows;
  const train = publicRows.filter((row) => row.split === "train");
  const val = publicRows.filter((row) => row.split === "val");
  const test = publicRows.filter((row) => row.split === "test");
  if (train.length === 0) throw new Error("the public wave has no train rows");
  if (val.length === 0) throw new Error("the public wave has no val rows");

  const manifest = options.manifest === null ? loadManifest() : loadManifest(options.manifest);
  const generatedAt = generatedAtOf(manifest);
  const inputs = {
    train,
    val,
    test,
    gold: goldRows,
    manifest,
    generatedAt,
    epochs: options.epochs,
    patience: options.patience,
  };
  const first = runPipeline(inputs);
  const second = runPipeline(inputs);
  const reproducible = first.modelText === second.modelText
    && JSON.stringify(first.report) === JSON.stringify(second.report);
  const criteria = checkCriteria({ modes: first.modes, reproducible });

  mkdirSync(out, { recursive: true });
  const target = join(out, first.name);
  writeFileSync(target, first.modelText);
  const size = statSync(target).size;
  // The report is committed and must be byte-identical between two runs, so the load time goes in a
  // 10 ms bucket (best of five parses) instead of a raw measurement that changes every run.
  let bestParse = Infinity;
  for (let attempt = 0; attempt < 5; attempt += 1) {
    const started = process.hrtime.bigint();
    JSON.parse(readFileSync(target, "utf8"));
    bestParse = Math.min(bestParse, Number(process.hrtime.bigint() - started) / 1e6);
  }
  const parseMs = Math.max(10, Math.ceil(bestParse / 10) * 10);

  const report = {
    ...first.report,
    rows: {
      public: publicRows.length,
      gold: goldRows.length,
      exported: exportedGold.length,
      standard: standardGold.length,
      duplicates: deduped.duplicates,
      conflicts: deduped.conflicts,
    },
    criteria,
    model: { kind: first.report.choice.chosen, file: first.name, bytes: size, parseMs },
  };
  mkdirSync(resolve(doc, ".."), { recursive: true });
  writeFileSync(doc, renderReport(report));
  process.stdout.write(
    `model ${report.model.kind}, ${report.featureCount} features, gold top-1 ${(first.modes[4].top1 * 100).toFixed(1)}%\n`
    + `criteria: ${criteria.map((entry) => `${entry.name}: ${entry.pass ? "pass" : "fail"}`).join("; ")}\n`
    + `model: ${target}\nreport: ${doc}\n`);
  // A failed criterion is a decision for the owner, not a broken run: the report is still written,
  // so the command reports success once it has produced both files.
  return 0;
}

const invokedDirectly = process.argv[1] !== undefined
  && pathToFileURL(resolve(process.argv[1])).href === import.meta.url;
if (invokedDirectly) {
  try {
    process.exitCode = main();
  } catch (error) {
    process.stderr.write(`${error instanceof Error ? error.message : String(error)}\n`);
    process.exitCode = 2;
  }
}

