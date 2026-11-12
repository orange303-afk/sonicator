import assert from "node:assert/strict";
import test from "node:test";
import { fileURLToPath } from "node:url";
import { groupForCategory, ingest, typeForCategory } from "../datasets/esc50/ingest.mjs";

const fixtureRoot = fileURLToPath(new URL("fixtures", import.meta.url));

test("ESC-50 maps the few classes that have a Sonicator type", () => {
  assert.equal(typeForCategory("clapping"), "clap");
  assert.equal(typeForCategory("laughing"), "vocal");
  assert.equal(typeForCategory("dog"), null);
  assert.deepEqual(groupForCategory("Animals"), { group: "ambience", domain: "ambience" });
  assert.deepEqual(groupForCategory("Human, non-speech sounds"), { group: "vocal", domain: "voice" });
});

test("only clips with audio are kept, missing files are counted", () => {
  const { clips, skipped } = ingest({ root: fixtureRoot });
  assert.equal(clips.length, 3);
  assert.equal(skipped.missingAudio, 1);
  const clap = clips.find((clip) => clip.label === "clap");
  assert.equal(clap.group, "percussion");
  assert.equal(clap.domain, "music");
  assert.equal(clap.license, "CC-BY-NC-4.0");
  assert.equal(clap.tier, "B");
});

test("folds become train/val/test without overlap", () => {
  const { clips } = ingest({ root: fixtureRoot });
  const byId = Object.fromEntries(clips.map((clip) => [clip.clipId, clip.split]));
  assert.equal(byId["1-100032-A-0"], "train");
  assert.equal(byId["1-100041-A-4"], "train");
  assert.equal(byId["1-100038-A-14"], "val");
  assert.equal(byId["1-100039-A-1"], undefined);
});
