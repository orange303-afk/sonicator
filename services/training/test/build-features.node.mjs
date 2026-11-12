import assert from "node:assert/strict";
import { mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import { isShardDone, joinRows, planShards, validateFeatureLine } from "../build-features.mjs";

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
