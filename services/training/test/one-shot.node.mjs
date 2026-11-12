import assert from "node:assert/strict";
import { join } from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";
import { ingest, labelForClip } from "../datasets/freesound-one-shot-percussive/ingest.mjs";

const fixtureRoot = fileURLToPath(new URL("fixtures/one-shot", import.meta.url));

test("names and tags map to Sonicator percussion types", () => {
  assert.equal(labelForClip({ name: "acoustic_closed_hihat1.wav", tags: [] }), "hat");
  assert.equal(labelForClip({ name: "kick_01.wav", tags: [] }), "kick");
  assert.equal(labelForClip({ name: "Zyl_Finger_Cymbals_Mute.wav", tags: ["Cymbals"] }), "cymbal");
  assert.equal(labelForClip({ name: "mystery.wav", tags: ["percussion"] }), "percussion");
  assert.equal(labelForClip({ name: "mystery.wav", tags: ["noise"] }), null);
});

test("only CC0/BY clips are used and missing licences are counted", () => {
  const { clips, skipped } = ingest({ root: fixtureRoot });
  assert.deepEqual(clips.map((clip) => clip.license).sort(), ["CC-BY-3.0", "CC0-1.0"]);
  assert.equal(skipped.noLicense, 1);
  assert.equal(skipped.licenseTier, 1);
  assert.ok(clips.every((clip) => clip.group === "percussion" && clip.domain === "music"));
  assert.ok(clips.every((clip) => ["train", "val", "test"].includes(clip.split)));
  assert.equal(new Set(clips.map((clip) => clip.extras.uploader)).size, 2);
});

test("the audio directory is found under the manifest id", () => {
  const { clips } = ingest({ root: fixtureRoot });
  assert.ok(clips.every((clip) => clip.file.startsWith(join(fixtureRoot, "freesound-one-shot-percussive"))));
});
