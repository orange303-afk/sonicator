import assert from "node:assert/strict";
import test from "node:test";
import { applyMap, fitLocalCalibration, isotonicMap } from "../v2/calibration.mjs";
import { evaluateGold } from "../v2/evaluate.mjs";
import { fitScaler, indicesFor, transform } from "../v2/features.mjs";
import { trainCentroids } from "../v2/models.mjs";

function row(label, value, index, recordId) {
  return {
    recordId: recordId ?? `r${label}-${index}`,
    label,
    type: label,
    typeIsCustom: false,
    origin: "user",
    tags: [],
    inBandEnergyShare: 1,
    featureSchema: 2,
    dimensions: 84,
    features: Array.from({ length: 84 }, (_, dimension) =>
      Math.min(1, Math.max(0, dimension < 42 ? value : 1 - value))),
  };
}

test("the isotonic map never falls and stays inside 0..1", () => {
  const map = isotonicMap([
    { x: 0.1, hit: 0 },
    { x: 0.2, hit: 1 },
    { x: 0.3, hit: 0 },
    { x: 0.4, hit: 1 },
    { x: 0.9, hit: 1 },
  ]);
  assert.ok(map.length >= 2);
  for (let i = 0; i < map.length; i += 1) {
    assert.ok(map[i][0] >= 0 && map[i][0] <= 1);
    assert.ok(map[i][1] >= 0 && map[i][1] <= 1);
    if (i > 0) {
      assert.ok(map[i][0] >= map[i - 1][0]);
      assert.ok(map[i][1] >= map[i - 1][1] - 1e-12);
    }
  }
  assert.equal(applyMap(map, 0), map[0][1]);
  assert.equal(applyMap(map, 1), map.at(-1)[1]);
});

test("a twin row never calibrates against its own content", () => {
  // Ten contents, each confirmed twice with conflicting labels. If a twin were a prototype for the
  // other twin, every prediction would look perfect and the map would claim 100% precision.
  const rows = [];
  for (let index = 0; index < 10; index += 1) {
    const shared = row("kick", 0.1 + index * 0.05, index, `twin-${index}`);
    rows.push(shared, { ...shared, label: "hat", type: "hat" });
  }
  const map = fitLocalCalibration(rows, { indices: indicesFor(["G1"]), folds: 5 });
  assert.ok(map.length >= 2);
  assert.ok(map.at(-1)[1] <= 0.5, `precision ${map.at(-1)[1]}`);
});

test("mode 4 merges on the calibrated scale, not the raw cosine", () => {
  const rows = [
    ...Array.from({ length: 12 }, (_, index) => row("kick", 0.2, index)),
    ...Array.from({ length: 12 }, (_, index) => row("hat", 0.8, 100 + index)),
  ];
  const indices = indicesFor(["G1"]);
  const scaler = fitScaler(rows, indices);
  const model = trainCentroids(rows, scaler, { maxPerLabel: 2 });
  // A local map that never reaches the acceptance threshold: with raw cosine the coverage would be
  // 100%, on the calibrated scale nothing is accepted.
  const flat = [[0, 0.2], [1, 0.2]];
  const result = evaluateGold({
    gold: rows,
    publicModel: model,
    scaler,
    mode: 2,
    indices,
    localMap: flat,
  });
  assert.equal(result.coverage, 0);
  assert.equal(result.accepted, 0);

  const local = evaluateGold({ gold: rows, publicModel: model, scaler, mode: 4, indices, localMap: flat });
  assert.ok(local.globalWins > 0, "the public model cannot win when local scores are capped low");
});

test("the local calibration is deterministic", () => {
  const rows = Array.from({ length: 40 }, (_, index) => row(index % 2 === 0 ? "kick" : "hat", (index % 2) * 0.7 + 0.1, index));
  const first = JSON.stringify(fitLocalCalibration(rows, { indices: indicesFor(["G1"]) }));
  const second = JSON.stringify(fitLocalCalibration(rows, { indices: indicesFor(["G1"]) }));
  assert.equal(first, second);
});
