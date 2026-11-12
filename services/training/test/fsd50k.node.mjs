import assert from "node:assert/strict";
import test from "node:test";
import { fileURLToPath } from "node:url";
import { groupForLabel, ingest, readVocabulary, typeForLabel } from "../datasets/fsd50k/ingest.mjs";

const fixtureRoot = fileURLToPath(new URL("fixtures", import.meta.url));

test("every vocabulary class gets exactly one group, unknown types stay null", () => {
  const vocabulary = readVocabulary(fixtureRoot);
  assert.equal(vocabulary.length, 4);
  for (const label of vocabulary)
    assert.ok(["percussion", "tonal", "vocal", "fx", "foley", "ambience"].includes(groupForLabel(label).group));
  assert.equal(typeForLabel("Bass_drum"), "kick");
  assert.equal(typeForLabel("Door"), null);
  assert.equal(groupForLabel("Door").group, "foley");
  assert.equal(groupForLabel("Rain").group, "ambience");
  assert.equal(groupForLabel("Rain").domain, "ambience");
});

test("only CC0/BY clips are kept and the split follows the ground truth", () => {
  const { clips, skipped } = ingest({ root: fixtureRoot, split: "dev" });
  assert.deepEqual(clips.map((clip) => clip.split).sort(), ["train", "val"]);
  assert.equal(skipped.licenseTier, 1);
  assert.equal(skipped.missingAudio, 1);
  const one = clips.find((clip) => clip.clipId === "1");
  assert.deepEqual(one.extras.labels, ["Bass_drum", "Drum"]);
  assert.equal(one.label, "kick");
  assert.equal(one.group, "percussion");
});
