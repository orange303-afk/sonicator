import assert from "node:assert/strict";
import { mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";
import { assertOutAllowed, checkShardCount, datasetDirectory, isShardDone, joinRows, parseArgs, planShards, validateFeatureLine } from "../build-features.mjs";

const feature = (overrides = {}) => ({ featureSchema: 2, dimensions: 84, features: Array(84).fill(0.5), ...overrides });

test("rows join by full link path, unmatched files are reported", () => {
  const clips = [{ clipId: "a", file: "/data/a.wav" }, { clipId: "b", file: "/data/b.wav" }];
  const links = new Map([["/out/links/x/0-a.wav", clips[0]], ["/out/links/x/1-b.wav", clips[1]]]);
  const lines = [
    { path: "/out/links/x/0-a.wav", ...feature() },
    { path: "/out/links/x/9-ghost.wav", ...feature() },
  ];
  const { rows, unmatched } = joinRows({ lines, clipsByPath: links });
  assert.deepEqual(rows.map((row) => row.clipId), ["a"]);
  assert.equal(unmatched.length, 1);
  assert.throws(() => validateFeatureLine(feature({ dimensions: 14 })), /dimensions/);
  assert.throws(() => validateFeatureLine(feature({ features: [1.2, ...Array(83).fill(0)] })), /outside 0..1/);
  assert.throws(() => validateFeatureLine(feature({ featureSchema: 1 })), /featureSchema/);
});

test("shard names are stable and one per source and split", () => {
  const shards = planShards([{ source: "nsynth", split: "test" }, { source: "nsynth", split: "valid" },
    { source: "fsd50k", split: "train" }]);
  assert.deepEqual(shards.map((shard) => shard.name), ["fsd50k-train", "nsynth-test", "nsynth-valid"]);
});

test("a half-written shard is not treated as done", () => {
  const out = mkdtempSync(join(tmpdir(), "shard-"));
  writeFileSync(join(out, "nsynth-test.features.jsonl"), "{}\n{}\n");
  assert.equal(isShardDone({ out, shard: "nsynth-test", expected: 3 }), false);
  writeFileSync(join(out, "nsynth-test.done"), "3\n");
  assert.equal(isShardDone({ out, shard: "nsynth-test", expected: 2 }), true);
  writeFileSync(join(out, "nsynth-test.done"), "3\n");
  assert.equal(isShardDone({ out, shard: "nsynth-test", expected: 4 }), false);
});

test("joining counts rows whose clip has no label", () => {
  const clip = { clipId: "a", file: "/data/a.wav", label: null };
  const { unlabelled } = joinRows({ lines: [{ path: "/link/a.wav", ...feature() }], clipsByPath: new Map([["/link/a.wav", clip]]) });
  assert.equal(unlabelled, 1);
});

test("dataset directories follow the names on disk, not the canonical splits", () => {
  assert.match(datasetDirectory("/data", "nsynth", "val"), /nsynth-valid$/);
  assert.match(datasetDirectory("/data", "fsd50k", "train"), /FSD50K\.dev_audio$/);
  assert.match(datasetDirectory("/data", "fsd50k", "test"), /FSD50K\.eval_audio$/);
});

test("a non-numeric --jobs is refused instead of running nothing", () => {
  assert.throws(() => parseArgs(["--out", "/tmp/x", "--jobs", "abc"]), /--jobs must be a positive integer/);
  assert.throws(() => parseArgs(["--out", "/tmp/x", "--jobs", "0"]), /--jobs must be a positive integer/);
});

test("--out inside the repository must stay under services/training/out", () => {
  const repoRoot = fileURLToPath(new URL("../..", import.meta.url));
  assert.throws(() => assertOutAllowed(join(repoRoot, "services/training/wave1"), repoRoot), /services\/training\/out/);
  assert.throws(() => assertOutAllowed(repoRoot, repoRoot), /services\/training\/out/);
  assert.doesNotThrow(() => assertOutAllowed(join(repoRoot, "services/training/out/x"), repoRoot));
  assert.doesNotThrow(() => assertOutAllowed("/Volumes/1TBSSD/out/x", repoRoot));
  assert.doesNotThrow(() => assertOutAllowed(join(tmpdir(), "gold-v2.jsonl"), repoRoot));
});

test("parseArgs refuses a repository path outside services/training/out", () => {
  const repoRoot = fileURLToPath(new URL("../..", import.meta.url));
  assert.throws(() => parseArgs(["--out", join(repoRoot, "services/training/wave1")]), /services\/training\/out/);
});

test("a short shard stops the run instead of being marked done", () => {
  assert.doesNotThrow(() => checkShardCount({ shard: "s", lines: 3, expected: 3 }));
  assert.throws(() => checkShardCount({ shard: "s", lines: 2, expected: 3 }), /wrote 2 of 3 lines/);
  assert.doesNotThrow(() => checkShardCount({ shard: "s", lines: 99, expected: null }));
});
