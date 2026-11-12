import assert from "node:assert/strict";
import test from "node:test";
import { fileURLToPath } from "node:url";
import { ingest, splitForFold } from "../datasets/urbansound8k/ingest.mjs";

const fixtureRoot = fileURLToPath(new URL("fixtures", import.meta.url));

test("urban classes keep the group without inventing a type", () => {
  const { clips, skipped } = ingest({ root: fixtureRoot });
  assert.equal(clips.length, 2);
  assert.equal(skipped.missingAudio, 1);
  for (const clip of clips) assert.equal(clip.label, null);
  const dog = clips.find((clip) => clip.clipId === "100032-3-0-0");
  assert.equal(dog.group, "foley");
  assert.equal(dog.license, "CC-BY-NC-3.0");
  assert.equal(dog.tier, "B");
});

test("official folds 1-8 train, 9 val, 10 test", () => {
  assert.equal(splitForFold(1), "train");
  assert.equal(splitForFold(8), "train");
  assert.equal(splitForFold(9), "val");
  assert.equal(splitForFold(10), "test");
  const { clips } = ingest({ root: fixtureRoot });
  const byId = Object.fromEntries(clips.map((clip) => [clip.clipId, clip.split]));
  assert.equal(byId["100032-3-0-0"], "train");
  assert.equal(byId["100263-2-0-117"], "val");
});
