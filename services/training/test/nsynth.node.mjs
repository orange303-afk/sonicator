import assert from "node:assert/strict";
import { mkdirSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";
import { familyLabel, familyMap, ingest } from "../datasets/nsynth/ingest.mjs";

const fixture = fileURLToPath(new URL("fixtures/nsynth-examples.json", import.meta.url));
const ids = ["bass_acoustic_001-003-025", "bass_acoustic_001-007-025", "bass_acoustic_001-011-025",
  "mallet_acoustic_002-003-025", "vocal_synthetic_003-003-025"];

function makeRoot() {
  const root = mkdtempSync(join(tmpdir(), "nsynth-"));
  const splitDir = join(root, "nsynth", "nsynth-test");
  mkdirSync(join(splitDir, "audio"), { recursive: true });
  writeFileSync(join(splitDir, "examples.json"), readFileSync(fixture));
  for (const id of ids) writeFileSync(join(splitDir, "audio", `${id}.wav`), "");
  return root;
}

test("families map to Sonicator types and unmapped ones keep a group", () => {
  const { clips } = ingest({ root: makeRoot(), split: "test" });
  assert.deepEqual(clips.map((clip) => [clip.clipId, clip.label]), [
    ["bass_acoustic_001-003-025", "bass"],
    ["bass_acoustic_001-007-025", "bass"],
    ["bass_acoustic_001-011-025", "bass"],
    ["mallet_acoustic_002-003-025", null],
    ["vocal_synthetic_003-003-025", "vocal"],
  ]);
  assert.equal(clips[3].labelRaw, "mallet");
  assert.equal(clips[3].group, "tonal");
  assert.equal(clips[0].split, "test");
  assert.equal(clips[0].extras.source, "acoustic");
});

test("the family table covers exactly the eleven NSynth families", () => {
  assert.deepEqual(Object.keys(familyMap().families).sort(), ["bass", "brass", "flute", "guitar", "keyboard",
    "mallet", "organ", "reed", "string", "synth_lead", "vocal"]);
  assert.throws(() => familyLabel("theremin"), /unknown instrument family/);
});
