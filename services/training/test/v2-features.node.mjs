import assert from "node:assert/strict";
import { mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";
import {
  FEATURE_GROUPS,
  fitScaler,
  indicesFor,
  loadGold,
  loadPublic,
  prepare,
  transform,
} from "../v2/features.mjs";
import { STANDARD_TYPES } from "../standard.mjs";

const header = fileURLToPath(new URL("../../../core/dsp/FeatureExtractorV2.h", import.meta.url));

function headerBounds() {
  const lines = readFileSync(header, "utf8").split("\n");
  const bounds = [];
  for (const line of lines) {
    const match = line.match(/^\s*\/\/\s+(\d+)\.\.(\d+)\s/);
    if (match !== null) bounds.push([Number(match[1]), Number(match[2]) + 1]);
  }
  return bounds;
}

function features(fill = 0.5, overrides = {}) {
  return Array.from({ length: 84 }, (_, index) => (overrides[index] !== undefined ? overrides[index] : fill));
}

function row(overrides = {}) {
  return {
    recordId: "r1",
    type: "snare",
    typeIsCustom: false,
    tags: [],
    origin: "user",
    featureSchema: 2,
    dimensions: 84,
    inBandEnergyShare: 0.9,
    features: features(),
    ...overrides,
  };
}

function writeJsonLines(prefix, rows) {
  const directory = mkdtempSync(join(tmpdir(), prefix));
  const file = join(directory, "rows.jsonl");
  writeFileSync(file, rows.map((entry) => JSON.stringify(entry)).join("\n") + "\n");
  return file;
}

test("feature groups match the layout comment in FeatureExtractorV2.h", () => {
  const bounds = headerBounds();
  assert.equal(bounds.length, 8);
  assert.deepEqual(Object.values(FEATURE_GROUPS), bounds);
  assert.equal(Object.values(FEATURE_GROUPS).at(-1)[1], 84);
});

test("indicesFor concatenates groups in order and can drop the moment pairs", () => {
  const all = indicesFor(["G1", "G2", "G3"]);
  assert.deepEqual(all, Array.from({ length: 38 }, (_, index) => index));
  const withoutMoments = indicesFor(["G1", "G2", "G3"], { dropMoments: true });
  assert.equal(withoutMoments.length, 34);
  assert.deepEqual(withoutMoments.filter((index) => [16, 17, 25, 26].includes(index)), []);
});

test("a constant dimension is dropped instead of dividing by zero", () => {
  const rows = [
    { features: features(0.4, { 5: 0.2, 0: 0.1 }) },
    { features: features(0.4, { 5: 0.2, 0: 0.3 }) },
  ];
  const scaler = fitScaler(rows, [0, 5]);
  assert.deepEqual(scaler.indices, [0]);
  const vector = transform(scaler, rows[1].features);
  assert.equal(vector.length, 1);
  assert.ok(Math.abs(vector[0] - 1) < 1e-9);
});

test("loadGold refuses a row that carries a path", () => {
  const file = writeJsonLines("gold-path-", [row({ path: "/Users/someone/kick.wav" })]);
  assert.throws(() => loadGold(file), /path/);
});

test("loaders refuse a foreign schema, out-of-range values and NaN", () => {
  assert.throws(() => loadGold(writeJsonLines("gold-schema-", [row({ featureSchema: 1 })])), /featureSchema/);
  assert.throws(() => loadGold(writeJsonLines("gold-range-", [row({ features: features(0.5, { 3: 1.2 }) })])), /0\.\.1/);
  assert.throws(() => loadGold(writeJsonLines("gold-nan-", [row({ features: features(0.5, { 3: NaN }) })])), /0\.\.1/);
  const publicFile = writeJsonLines("public-schema-", [{ label: "kick", source: "nsynth", features: features(), featureSchema: 3, dimensions: 84 }]);
  assert.throws(() => loadPublic(publicFile), /featureSchema/);
});

test("prepare drops a quiet band, a missing label and a custom type", () => {
  const rows = [
    { label: "snare", inBandEnergyShare: 0.05, features: features() },
    { label: "snare", inBandEnergyShare: 0.6, features: features() },
    { label: null, inBandEnergyShare: 0.6, features: features() },
    { label: "Drum Loop", inBandEnergyShare: 0.6, features: features(), typeIsCustom: true },
    { label: "Snare", inBandEnergyShare: undefined, features: features() },
  ];
  const kept = prepare(rows);
  assert.deepEqual(kept.map((entry) => entry.label), ["snare", "snare"]);
  assert.ok(kept.every((entry) => STANDARD_TYPES.includes(entry.label)));
});

test("loadGold normalises the type and keeps the tags", () => {
  const file = writeJsonLines("gold-load-", [row({ type: "Snare", tags: ["Hat", "Snap"] })]);
  const [loaded] = loadGold(file);
  assert.equal(loaded.label, "snare");
  assert.deepEqual(loaded.tags, ["Hat", "Snap"]);
});
