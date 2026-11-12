import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdtempSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";
import { checkCriteria, chooseModel } from "../train.mjs";
import { renderReport } from "../v2/report.mjs";

const trainScript = fileURLToPath(new URL("../train.mjs", import.meta.url));

function features(value, index) {
  return Array.from({ length: 84 }, (_, dimension) =>
    Math.min(1, Math.max(0, value + (((index * 31 + dimension * 7) % 13) / 200))));
}

function publicRow(label, value, split, index) {
  return {
    source: "fixture",
    clipId: `${label}-${split}-${index}`,
    label,
    group: label === "kick" || label === "hat" ? "percussion" : "tonal",
    domain: "music",
    split,
    license: "CC0-1.0",
    tier: "A",
    featureSchema: 2,
    dimensions: 84,
    inBandEnergyShare: 1,
    features: features(value, index),
  };
}

function goldRow(label, value, index) {
  return {
    recordId: `g-${label}-${index}`,
    type: label,
    typeIsCustom: false,
    tags: [],
    origin: "user",
    featureSchema: 2,
    dimensions: 84,
    inBandEnergyShare: 1,
    features: features(value, index),
  };
}

function writeFixture() {
  const directory = mkdtempSync(join(tmpdir(), "train-fixture-"));
  const publicRows = [
    ...Array.from({ length: 30 }, (_, index) => publicRow("kick", 0.15, "train", index)),
    ...Array.from({ length: 30 }, (_, index) => publicRow("hat", 0.85, "train", index)),
    ...Array.from({ length: 12 }, (_, index) => publicRow("kick", 0.15, "val", index)),
    ...Array.from({ length: 12 }, (_, index) => publicRow("hat", 0.85, "val", index)),
    ...Array.from({ length: 8 }, (_, index) => publicRow("kick", 0.15, "test", index)),
    ...Array.from({ length: 8 }, (_, index) => publicRow("hat", 0.85, "test", index)),
  ];
  const goldRows = [
    ...Array.from({ length: 12 }, (_, index) => goldRow("kick", 0.15, index)),
    ...Array.from({ length: 12 }, (_, index) => goldRow("hat", 0.85, index)),
  ];
  const publicFile = join(directory, "features.jsonl");
  const goldFile = join(directory, "gold-v2.jsonl");
  writeFileSync(publicFile, publicRows.map((row) => JSON.stringify(row)).join("\n") + "\n");
  writeFileSync(goldFile, goldRows.map((row) => JSON.stringify(row)).join("\n") + "\n");
  return { directory, publicFile, goldFile };
}

test("chooseModel needs a full two points, not 1.9", () => {
  assert.equal(chooseModel({ m1: { top1: 0.8 }, m2: { top1: 0.819 } }), "M1");
  assert.equal(chooseModel({ m1: { top1: 0.8 }, m2: { top1: 0.82 } }), "M2");
});

test("checkCriteria passes exactly on its boundaries", () => {
  const modes = (mode3, mode4, precision = 0.95, coverage = 0.5, mode1 = 0.6) => ({
    modes: {
      1: { top1: mode1, coverage: 0.5, perGroup: [{ group: "percussion", top1: mode1 }, { group: "tonal", top1: mode1 }] },
      3: { top1: mode3 },
      4: {
        top1: mode4,
        coverage,
        precisionAtThreshold: precision,
        perGroup: [{ group: "percussion", top1: mode4 }, { group: "tonal", top1: mode4 }],
      },
    },
    reproducible: true,
  });
  const pass = checkCriteria(modes(0.7, 0.65));
  assert.equal(pass.length, 4);
  assert.ok(pass.every((entry) => entry.pass), JSON.stringify(pass));
  assert.equal(checkCriteria(modes(0.699, 0.65))[0].pass, false);
  assert.equal(checkCriteria(modes(0.7, 0.649))[1].pass, false);
  assert.equal(checkCriteria(modes(0.7, 0.65, 0.89))[2].pass, false);
  assert.equal(checkCriteria(modes(0.7, 0.65, 0.95, 0.49))[2].pass, false);
  assert.equal(checkCriteria({ ...modes(0.7, 0.65), reproducible: false })[3].pass, false);
});

test("the report carries no path and no file name", () => {
  const text = renderReport({
    generatedAt: "2026-11-12T00:00:00.000Z",
    rows: { public: 120, gold: 24 },
    comparisons: [
      { mode: 1, name: "v1, local", top1: 0.8, top3: 0.9, coverage: 0.8, precision: 0.9, records: 24,
        perGroup: [{ group: "percussion", top1: 0.8 }, { group: "tonal", top1: 0.75 }] },
      { mode: 4, name: "v2, local + public", top1: 0.9, top3: 0.95, coverage: 0.8, precision: 0.92, records: 24,
        globalWins: 3, perGroup: [{ group: "percussion", top1: 0.9 }, { group: "tonal", top1: 0.85 }] },
    ],
    curves: {
      one: [{ threshold: 0.8, coverage: 0.8, precision: 0.9 }],
      four: [{ threshold: 0.8, coverage: 0.7, precision: 0.92 }],
    },
    criteria: [{ name: "cold_start", value: "0.75", threshold: "0.70", pass: true }],
    ablation: [{ group: "G2", valDelta: "0.6", goldDelta: "0.1", kept: true }],
    confusion: { "snare/clap/hat/rim": { labels: ["snare", "hat"], cells: [[10, 2], [1, 9]] } },
    worst: [{ label: "hat", records: 30, top1: 0.5 }],
    foley: [{ group: "foley", records: 10, top1: 0.4 }],
    model: { kind: "M2", file: "global_model.json", bytes: 12345, parseMs: 1.5 },
  });
  assert.ok(text.includes("|"));
  assert.equal(text.includes("/Users/"), false);
  assert.equal(text.includes(".wav"), false);
  assert.match(text, /Verification|Проверка|Критерии/i);
  assert.match(text, /По группам/);
  assert.match(text, /Порог приёма/);
  assert.match(text, /решила \*\*3\*\* из \*\*24\*\*/);
  assert.match(text, /выбирались по этой же золотой выборке/);
});

test("two train.mjs runs write byte-identical files", () => {
  const fixture = writeFixture();
  const run = (name) => {
    const out = join(fixture.directory, name);
    const doc = join(fixture.directory, `${name}.md`);
    const result = spawnSync(process.execPath, [
      trainScript,
      "--public", fixture.publicFile,
      "--gold", fixture.goldFile,
      "--out", out,
      "--doc", doc,
    ], { encoding: "utf8" });
    assert.equal(result.status, 0, result.stderr);
    return { out, doc };
  };
  const first = run("run-a");
  const second = run("run-b");
  const firstFiles = readdirSync(first.out).sort();
  assert.deepEqual(firstFiles, readdirSync(second.out).sort());
  for (const file of firstFiles)
    assert.equal(readFileSync(join(first.out, file), "utf8"), readFileSync(join(second.out, file), "utf8"), file);
  assert.equal(readFileSync(first.doc, "utf8"), readFileSync(second.doc, "utf8"));
});
