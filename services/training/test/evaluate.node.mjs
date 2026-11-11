import assert from "node:assert/strict";
import test from "node:test";
import { evaluate, renderReport, summarizeEvaluation } from "../evaluate.mjs";
import { ACOUSTIC, dataset, features, record } from "./fixtures.mjs";

function globalWorld() {
  const out = [];
  for (const installId of ["a", "b", "c"])
    for (let i = 0; i < 10; i += 1)
      out.push(record({
        installId,
        recordId: `${installId}-${String(i).padStart(4, "0")}`,
        type: "snare",
        tags: [{ name: "acoustic", custom: false }],
        features: features(ACOUSTIC, 0.002 * i),
      }));
  out.push(record({
    installId: "d",
    recordId: "held-out",
    type: "snare",
    tags: [{ name: "acoustic", custom: false }],
    features: features(ACOUSTIC, 0.004),
  }));
  return out;
}

test("leave-one-install-out shows the gain on an install with no local prototypes", () => {
  const summary = summarizeEvaluation(evaluate(globalWorld()));
  assert.equal(summary.installs, 4);
  assert.equal(summary.records, 31);
  assert.equal(summary.type.before, 30 / 31);
  assert.ok(summary.type.after > 0.99);
  assert.ok(summary.type.after >= summary.type.before);
  assert.ok(summary.type.gate);
  assert.ok(summary.tags.afterRecall > 0.9);
  assert.ok(summary.tags.afterPrecision > 0.9);
  assert.ok(summary.tags.beforePrecision <= summary.tags.afterPrecision);
});

test("the gate fails when there is not enough evidence", () => {
  const records = [];
  for (const installId of ["a", "b", "c", "d"])
    for (let i = 0; i < 2; i += 1)
      records.push(record({
        installId,
        recordId: `${installId}-${String(i).padStart(3, "0")}`,
        type: i === 0 ? "kick" : "snare",
        tags: [],
      }));
  const summary = summarizeEvaluation(evaluate(records));
  assert.equal(summary.type.after, 0);
  assert.equal(summary.type.gate, false);
  assert.match(renderReport(summary, { generatedAt: "2026-11-11T00:00:00Z" }), /needs the owner's decision/);
});

test("an empty dataset evaluates to zero without throwing", () => {
  const summary = summarizeEvaluation(evaluate([]));
  assert.equal(summary.records, 0);
  assert.equal(summary.type.after, 0);
  assert.equal(summary.type.gate, false);
});
