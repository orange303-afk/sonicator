import assert from "node:assert/strict";
import test from "node:test";
import { summarise } from "../wave1-report.mjs";

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

test("a row that carries the clip fields is used as it is", () => {
  const report = summarise({ clips: [], rows: [{ ...clipA, inBandEnergyShare: 0.2 }] });
  assert.equal(report.total, 1);
  assert.equal(report.perGroup.tonal, 1);
  assert.equal(report.labels.bass, 1);
});
