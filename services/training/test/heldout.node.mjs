import assert from "node:assert/strict";
import { mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import {
  classOf,
  evaluateContributed,
  isPlausibleAdaptiveTag,
  loadShippedModel,
  mergeForApp,
  readContributedRows,
  wilson,
  CLASS,
} from "../heldout.mjs";

const DIMENSIONS = 84;

// A tiny stand-in for the shipped softmax: label c responds to feature c, so an exact match is almost
// certain and the app's 0.95 penalty still leaves the answer above 0.80.
function toyModel(labels = ["kick", "hat", "snare"]) {
  return {
    kind: "M2",
    labels,
    W: labels.map((_, column) => Array.from({ length: DIMENSIONS }, (_, index) => (index === column ? 8 : 0))),
    b: new Array(labels.length).fill(0),
    temperature: 1,
    similarityMap: [[0, 0], [1, 1]],
  };
}

function toyScaler() {
  return {
    indices: Array.from({ length: DIMENSIONS }, (_, index) => index),
    mu: new Array(DIMENSIONS).fill(0),
    sd: new Array(DIMENSIONS).fill(1),
  };
}

function shippedOf(model) {
  return {
    file: "toy",
    generatedAt: null,
    labels: model.labels,
    labelSet: new Set(model.labels),
    featureCount: DIMENSIONS,
    scaler: toyScaler(),
    model,
  };
}

function features(dimension, wobble = 0) {
  const values = new Array(DIMENSIONS).fill(0);
  values[dimension] = 0.9;
  // Two different samples of the same family differ a little; an exact vector means the same audio.
  values[83] = 0.001 + wobble;
  return values;
}

function row(label, dimension, index, overrides = {}) {
  return {
    recordId: `${label}-${index}`,
    installId: "install-a",
    appVersion: "0.3.0",
    sentAt: "",
    label,
    typeIsCustom: false,
    tags: [],
    features: Float64Array.from(features(dimension, 0.0001 * index)),
    ...overrides,
  };
}

test("the loader refuses a file the app would refuse", () => {
  const dir = mkdtempSync(join(tmpdir(), "sonicator-heldout-"));
  const file = join(dir, "model.json");
  const document = {
    schema: "model.v1",
    featureSchema: 2,
    generatedAt: "2026-01-01T00:00:00.000Z",
    featureIndices: [0, 1],
    mu: [0, 0],
    sd: [1, 1],
    model: { kind: "M2", labels: ["Kick", "Hat"], W: [[1, 0], [0, 1]], b: [0, 0], temperature: 1 },
    similarityMap: [[0, 0], [1, 1]],
  };
  writeFileSync(file, JSON.stringify(document), "utf8");
  const shipped = loadShippedModel(file);
  assert.deepEqual(shipped.labels, ["kick", "hat"]);
  assert.equal(shipped.featureCount, 2);

  writeFileSync(file, JSON.stringify({ ...document, schema: "model.v2" }), "utf8");
  assert.throws(() => loadShippedModel(file), /schema is not model\.v1/);

  writeFileSync(file, JSON.stringify({ ...document, similarityMap: [[0.5, 0.2], [0.4, 0.9]] }), "utf8");
  assert.throws(() => loadShippedModel(file), /not monotone/);
});

test("a type is classified by what the model can and cannot name", () => {
  const labels = new Set(["kick", "hat"]);
  assert.equal(classOf({ label: "kick", typeIsCustom: false }, labels), CLASS.model);
  assert.equal(classOf({ label: "riser", typeIsCustom: false }, labels), CLASS.missing);
  assert.equal(classOf({ label: "drum loop", typeIsCustom: true }, labels), CLASS.custom);
  assert.equal(classOf({ label: "weird", typeIsCustom: false }, labels), CLASS.unknown);
});

test("rows with one recordId are kept once and conflicts are counted", () => {
  const dir = mkdtempSync(join(tmpdir(), "sonicator-heldout-"));
  const file = join(dir, "rows.jsonl");
  const line = (index, type) => JSON.stringify({
    installId: "install-a",
    recordId: `r${index}`,
    featureSchema: 2,
    features: features(0),
    type,
  });
  writeFileSync(file, [line(1, "Kick"), line(2, "Hat"), line(2, "Snare")].join("\n"), "utf8");
  const data = readContributedRows(file);
  assert.equal(data.rows.length, 2);
  assert.deepEqual(data.rows.map((entry) => entry.label), ["kick", "hat"]);
  assert.equal(data.stats.duplicates, 1);
  assert.equal(data.stats.conflicts, 1);
});

test("cold start counts a type the model cannot name as a miss", () => {
  const model = toyModel();
  const rows = [
    row("kick", 0, 0),
    row("kick", 0, 1),
    row("hat", 1, 0),
    row("riser", 0, 0),
  ];
  const summary = evaluateContributed({ rows, shipped: shippedOf(model) });
  assert.equal(summary.named.cold.records, 3);
  assert.equal(summary.named.cold.top1, 1);
  assert.equal(summary.standard.cold.records, 4);
  assert.ok(summary.standard.cold.top1 < 1);
  assert.equal(summary.standard.cold.coverage, 1);
  assert.equal(summary.missing.length, 1);
  assert.equal(summary.missing[0].label, "riser");
  assert.deepEqual(summary.missing[0].topCold, [{ label: "kick", count: 1 }]);
});

test("a row never votes for itself in the local path", () => {
  const model = toyModel();
  // The only kick looks like a hat, so the model answers hat. If the row could stand as its own
  // prototype, the local path would answer kick and the mistake would disappear.
  const rows = [row("kick", 1, 0), row("hat", 1, 1)];
  const summary = evaluateContributed({ rows, shipped: shippedOf(model) });
  assert.equal(summary.named.app.wrong[0].label, "hat");
  assert.equal(summary.named.app.top1, 0);
  // Both rows still see the other label's prototype, so both have local evidence; the kick simply
  // has none of its own.
  assert.equal(summary.localEvidence.recordsWithLocal, 2);
  assert.equal(summary.installs[0].appLocalWins, 2);
});

test("a copy of the same audio is not a second opinion", () => {
  const model = toyModel();
  // Two rows of the same content, both labelled kick but sounding like a hat to the model. Their
  // vectors are identical, so neither may serve as the other's prototype.
  const twin = row("kick", 1, 0, { features: Float64Array.from(features(1)) });
  const rows = [twin, { ...twin, recordId: "kick-copy" }];
  const summary = evaluateContributed({ rows, shipped: shippedOf(model) });
  assert.equal(summary.named.app.top1, 0);
  assert.equal(summary.installs[0].twins, 2);
  assert.equal(summary.localEvidence.recordsWithLocal, 0);
});

test("a second confirmation makes the local label win", () => {
  const model = toyModel();
  // Kicks live on a direction the model does not know (no weight), so cold start cannot answer them;
  // the person's own two confirmations can.
  const rows = [row("kick", 3, 0), row("kick", 3, 1), row("hat", 1, 2)];
  const summary = evaluateContributed({ rows, shipped: shippedOf(model) });
  assert.equal(summary.named.cold.top1, 1 / 3);
  assert.equal(summary.named.app.top1, 1);
  const install = summary.installs[0];
  assert.equal(install.appLocalWins, 2);
});

test("a custom type is out of the standard denominator but learnable locally", () => {
  const model = toyModel();
  const rows = [
    row("drum loop", 0, 0, { typeIsCustom: true }),
    row("drum loop", 0, 1, { typeIsCustom: true }),
    row("kick", 0, 0),
  ];
  const summary = evaluateContributed({ rows, shipped: shippedOf(model) });
  assert.equal(summary.named.cold.records, 1);
  assert.equal(summary.standard.cold.records, 1);
  assert.equal(summary.custom.length, 1);
  assert.equal(summary.custom[0].label, "drum loop");
  assert.equal(summary.custom[0].modes.app.top1, 1);
});

test("a row without a type is labelled by a single shipped type tag", () => {
  const dir = mkdtempSync(join(tmpdir(), "sonicator-heldout-"));
  const file = join(dir, "rows.jsonl");
  const line = (index, tags) => JSON.stringify({
    installId: "install-a",
    recordId: `r${index}`,
    featureSchema: 2,
    features: features(0),
    tags: tags.map((name) => ({ name, custom: false })),
  });
  writeFileSync(file, [
    line(1, ["Snare", "One-shot"]),
    line(2, ["Hat"]),
    line(3, ["Snare", "Kick"]),
    line(4, ["One-shot"]),
  ].join("\n"), "utf8");
  const data = readContributedRows(file);
  assert.deepEqual(data.rows.map((entry) => [entry.label, entry.labelSource]), [["snare", "tag"], ["hat", "tag"]]);
  assert.equal(data.stats.tagDerived, 2);
  assert.equal(data.stats.tagsOnly, 2);
  assert.equal(data.stats.noType, 4);

  const withoutTags = readContributedRows(file, { tagLabels: false });
  assert.equal(withoutTags.rows.length, 0);
  assert.equal(withoutTags.stats.tagDerived, 0);
});

test("the local label wins a tie within the margin and loses beyond it", () => {
  const globalRanked = [
    { label: "hat", score: 0.8 },
    { label: "kick", score: 0.4 },
  ];
  const local = new Map([["kick", 0.8]]);
  assert.equal(mergeForApp(globalRanked, local)[0].label, "kick");
  const stronger = new Map([["kick", 0.7]]);
  assert.equal(mergeForApp(globalRanked, stronger)[0].label, "hat");
  const clear = new Map([["kick", 0.9]]);
  assert.equal(mergeForApp(globalRanked, clear)[0].label, "kick");
  assert.equal(mergeForApp(globalRanked, new Map())[0].label, "hat");
});

test("a loop shorter than 1.2 s cannot vote as a loop", () => {
  const short = new Array(DIMENSIONS).fill(0);
  short[12] = 0.01;
  assert.equal(isPlausibleAdaptiveTag("loop", short), false);
  const long = new Array(DIMENSIONS).fill(0);
  long[12] = 0.2;
  assert.equal(isPlausibleAdaptiveTag("loop", long), true);
  assert.equal(isPlausibleAdaptiveTag("kick", short), true);
});

test("the Wilson interval stays inside 0..1 and narrows with support", () => {
  const small = wilson(8, 10);
  const large = wilson(800, 1000);
  assert.ok(small.low >= 0 && small.high <= 1);
  assert.ok(large.high - large.low < small.high - small.low);
});
