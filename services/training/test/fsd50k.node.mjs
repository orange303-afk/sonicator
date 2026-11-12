import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import { fileURLToPath } from "node:url";
import { GROUPS, groupForLabel, ingest, pickLabelAndGroup, readVocabulary, typeForLabel } from "../datasets/fsd50k/ingest.mjs";
import { STANDARD_TYPES } from "../standard.mjs";

const fixtureRoot = fileURLToPath(new URL("fixtures", import.meta.url));
const vocabularyFile = new URL("fixtures/fsd50k-vocabulary.csv", import.meta.url);

function vocabularyClasses(text) {
  return text
    .split("\n")
    .map((line) => line.trim())
    .filter((line) => line.length > 0)
    .map((line) => line.split(",")[1]);
}

test("all 200 FSD50K classes get one taxonomy group each", () => {
  const classes = vocabularyClasses(readFileSync(vocabularyFile, "utf8"));
  assert.equal(classes.length, 200);
  for (const name of classes) {
    const { group, domain } = groupForLabel(name);
    assert.ok(GROUPS.includes(group), `${name} got group ${group}`);
    assert.ok(typeof domain === "string" && domain.length > 0, `${name} has no domain`);
  }
});

test("every label target in every mapping is a standard type", () => {
  const fsd50k = JSON.parse(readFileSync(new URL("../datasets/fsd50k/labels.map.json", import.meta.url), "utf8"));
  for (const [label, type] of Object.entries(fsd50k.types))
    assert.ok(STANDARD_TYPES.includes(type), `fsd50k ${label} maps to unknown type ${type}`);
  const nsynth = JSON.parse(readFileSync(new URL("../datasets/nsynth/labels.map.json", import.meta.url), "utf8"));
  for (const [family, entry] of Object.entries(nsynth.families))
    if (entry.label !== null)
      assert.ok(STANDARD_TYPES.includes(entry.label), `nsynth ${family} maps to unknown type ${entry.label}`);
  const oneShot = JSON.parse(readFileSync(new URL("../datasets/freesound-one-shot-percussive/labels.map.json", import.meta.url), "utf8"));
  for (const rule of oneShot.rules)
    assert.ok(STANDARD_TYPES.includes(rule.type), `one-shot rule maps to unknown type ${rule.type}`);
});

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

test("the group comes from the label that produced the type", () => {
  assert.deepEqual(pickLabelAndGroup(["Male_speech_and_man_speaking", "Hi-hat"]),
    { label: "hat", group: "percussion", domain: "music" });
  assert.equal(pickLabelAndGroup(["Door"]).group, "foley");
});
