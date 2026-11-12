import assert from "node:assert/strict";
import test from "node:test";
import { fitScaler, indicesFor, transform } from "../v2/features.mjs";
import { fitSimilarityMap, predict, toSimilarity, trainCentroids, trainSoftmax } from "../v2/models.mjs";

function cloud(label, value, count, offset) {
  return Array.from({ length: count }, (_, index) => ({
    label,
    source: "public",
    features: Array.from({ length: 84 }, (_, dimension) => {
      const wobble = ((index * 7 + dimension * 3 + offset) % 11) / 100;
      return Math.max(0, Math.min(1, value + wobble - 0.05));
    }),
  }));
}

const rows = [...cloud("kick", 0.15, 40, 1), ...cloud("hat", 0.85, 40, 5)];
const indices = indicesFor(["G1"]);
const scaler = fitScaler(rows, indices);

function accuracy(model) {
  let correct = 0;
  for (const row of rows)
    if (predict(model, scaler, row.features).label === row.label) correct += 1;
  return correct / rows.length;
}

test("M2 reaches 99% on two separable clouds", () => {
  const model = trainSoftmax(rows, rows, scaler, { epochs: 300 });
  assert.equal(model.kind, "M2");
  assert.ok(accuracy(model) >= 0.99, `accuracy ${accuracy(model)}`);
});

test("M1 reaches 99% on two separable clouds", () => {
  const model = trainCentroids(rows, scaler, { maxPerLabel: 4 });
  assert.equal(model.kind, "M1");
  assert.ok(accuracy(model) >= 0.99, `accuracy ${accuracy(model)}`);
});

test("two softmax runs produce byte-identical weights", () => {
  const first = JSON.stringify(trainSoftmax(rows, rows, scaler, { epochs: 20, patience: 5 }));
  const second = JSON.stringify(trainSoftmax(rows, rows, scaler, { epochs: 20, patience: 5 }));
  assert.equal(first, second);
});

test("predict never returns NaN, even for a zero vector", () => {
  const model = trainSoftmax(rows, rows, scaler, { epochs: 20 });
  const result = predict(model, scaler, Array(84).fill(0));
  assert.equal(typeof result.label, "string");
  assert.ok(Number.isFinite(result.score));
  assert.equal(result.ranked.length, 2);
  for (const entry of result.ranked) assert.ok(Number.isFinite(entry.score));
  const centroid = trainCentroids(rows, scaler);
  assert.ok(Number.isFinite(predict(centroid, scaler, Array(84).fill(0)).score));
});

test("the similarity table is monotone and inside 0..1", () => {
  const model = trainSoftmax(rows, rows, scaler, { epochs: 60 });
  const map = fitSimilarityMap(model, scaler, rows);
  assert.ok(map.length >= 2);
  for (let i = 0; i < map.length; i += 1) {
    const [p, s] = map[i];
    assert.ok(p >= 0 && p <= 1);
    assert.ok(s >= 0 && s <= 1);
    if (i > 0) {
      assert.ok(p >= map[i - 1][0], `p fell at ${i}`);
      assert.ok(s >= map[i - 1][1] - 1e-12, `s fell at ${i}`);
    }
  }
  assert.equal(toSimilarity(map, 0), map[0][1]);
  assert.equal(toSimilarity(map, 1), map.at(-1)[1]);
  const middle = toSimilarity(map, 0.5);
  assert.ok(Number.isFinite(middle));
});

test("the scaler applied by the model matches a manual transform", () => {
  const model = trainCentroids(rows, scaler, { maxPerLabel: 2 });
  assert.equal(model.prototypes.length, 2);
  const vector = transform(scaler, rows[0].features);
  assert.ok(vector.every((value) => Number.isFinite(value)));
});
