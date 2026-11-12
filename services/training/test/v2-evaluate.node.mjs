import assert from "node:assert/strict";
import test from "node:test";
import { fitScaler, indicesFor, transform } from "../v2/features.mjs";
import { evaluateGold, labelIntersection, mergeCandidates } from "../v2/evaluate.mjs";

function features(value, wobble = 0) {
  return Array.from({ length: 84 }, (_, index) => Math.min(1, Math.max(0, value + ((index * 13) % 7) * wobble)));
}

function goldRow(label, value, index, overrides = {}) {
  return {
    recordId: `r${label}-${index}`,
    label,
    type: label,
    typeIsCustom: false,
    origin: "user",
    tags: [],
    inBandEnergyShare: 1,
    featureSchema: 2,
    dimensions: 84,
    features: features(value, 0.001 * index),
    ...overrides,
  };
}

function m1Model(labels, vectors) {
  return {
    kind: "M1",
    labels,
    prototypes: labels.map((label, index) => ({ label, prototypes: [vectors[index]] })),
  };
}

test("a row never votes for itself in modes 1, 2 and 4", () => {
  const gold = [
    goldRow("kick", 0.2, 0),
    goldRow("hat", 0.2, 1),
  ];
  const scaler = fitScaler(gold, indicesFor(["G1"]));
  const model = m1Model(["kick"], [transform(scaler, features(0.2))]);
  for (const mode of [1, 2, 4]) {
    const result = evaluateGold({
      gold,
      publicModel: model,
      scaler,
      mode,
      indices: indicesFor(["G1"]),
      minRowsPerLabel: 1,
    });
    assert.equal(result.records, 2);
    assert.ok(result.top1 <= 0.5, `mode ${mode} top1 ${result.top1}`);
  }
});

test("a local label wins a tie within the 0.02 margin", () => {
  const local = { label: "kick", score: 0.8 };
  assert.equal(mergeCandidates(local, { label: "hat", score: 0.8 }).label, "kick");
  assert.equal(mergeCandidates(local, { label: "hat", score: 0.82 }).label, "kick");
  assert.equal(mergeCandidates(local, { label: "hat", score: 0.83 }).label, "hat");
  assert.equal(mergeCandidates(null, { label: "hat", score: 0.83 }).label, "hat");
  assert.equal(mergeCandidates(local, null).label, "kick");
});

test("a custom type is outside the type denominator", () => {
  const gold = [
    ...Array.from({ length: 12 }, (_, index) => goldRow("kick", 0.2, index)),
    ...Array.from({ length: 3 }, (_, index) =>
      goldRow("drum loop", 0.6, index, { typeIsCustom: true })),
  ];
  const scaler = fitScaler(gold, indicesFor(["G1"]));
  const model = m1Model(["kick"], [transform(scaler, features(0.2))]);
  const result = evaluateGold({ gold, publicModel: model, scaler, mode: 1, indices: indicesFor(["G1"]) });
  assert.equal(result.records, 12);
});

test("mode 3 counts only labels present in the public data", () => {
  const gold = [
    ...Array.from({ length: 12 }, (_, index) => goldRow("kick", 0.2, index)),
    ...Array.from({ length: 12 }, (_, index) => goldRow("riser", 0.9, index)),
  ];
  const scaler = fitScaler(gold, indicesFor(["G1"]));
  const model = m1Model(["kick"], [transform(scaler, features(0.2))]);
  const intersection = labelIntersection(model.labels, gold);
  assert.deepEqual(intersection, ["kick"]);
  const result = evaluateGold({ gold, publicModel: model, scaler, mode: 3, indices: indicesFor(["G1"]) });
  assert.equal(result.records, 12);
  assert.deepEqual([...new Set(result.perLabel.map((entry) => entry.label))], ["kick"]);
});

test("thresholds report coverage and precision separately from top-1", () => {
  const gold = [
    ...Array.from({ length: 12 }, (_, index) => goldRow("kick", 0.2, index)),
    ...Array.from({ length: 12 }, (_, index) => goldRow("hat", 0.8, index)),
  ];
  const scaler = fitScaler(gold, indicesFor(["G1"]));
  const model = m1Model(["kick", "hat"], [transform(scaler, features(0.2)), transform(scaler, features(0.8))]);
  const result = evaluateGold({ gold, publicModel: model, scaler, mode: 3, indices: indicesFor(["G1"]) });
  assert.equal(result.mode, 3);
  assert.equal(result.records, 24);
  assert.ok(result.coverage > 0 && result.coverage <= 1);
  assert.ok(result.precisionAtThreshold >= 0.9, `precision ${result.precisionAtThreshold}`);
  assert.ok(result.top1 >= 0.9, `top1 ${result.top1}`);
});
