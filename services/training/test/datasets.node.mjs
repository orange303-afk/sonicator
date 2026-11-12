import assert from "node:assert/strict";
import test from "node:test";
import { MANIFEST_PATH, loadManifest, usedDatasets, validateManifest } from "../datasets.mjs";

test("the shipped manifest lists every dataset with a licence and a tier", () => {
  const entries = loadManifest();
  assert.ok(entries.length >= 13);
  for (const entry of entries) {
    assert.ok(entry.id.length > 0, "id");
    assert.ok(entry.title.length > 0, `${entry.id}: title`);
    assert.ok(entry.version.length > 0, `${entry.id}: version`);
    assert.match(entry.url, /^https:\/\//, `${entry.id}: url`);
    assert.ok(entry.license.length > 0, `${entry.id}: license`);
    assert.ok(["A", "B", "C"].includes(entry.tier), `${entry.id}: tier`);
    assert.equal(typeof entry.used, "boolean", `${entry.id}: used`);
    assert.ok(entry.creditLine.length > 0, `${entry.id}: creditLine`);
  }
  assert.equal(new Set(entries.map((entry) => entry.id)).size, entries.length, "ids are unique");
});

test("no level C dataset is used and every used one has an attribution line", () => {
  for (const entry of usedDatasets(loadManifest())) {
    assert.notEqual(entry.tier, "C", `${entry.id} is level C`);
    assert.match(entry.creditLine, /CC|Creative Commons|research/i, `${entry.id}: licence in the credit line`);
  }
});

test("a manifest without a licence or with a foreign tier is refused", () => {
  const base = loadManifest()[0];
  assert.throws(() => validateManifest([{ ...base, license: "" }]), /manifest: .*license/);
  assert.throws(() => validateManifest([{ ...base, tier: "D" }]), /manifest: .*tier/);
  assert.throws(() => validateManifest([base, base]), /manifest: .*duplicate/);
  assert.throws(() => validateManifest([{ ...base, used: true, tier: "C" }]), /manifest: .*level C/);
});

test("per-clip licensing is stated explicitly for Freesound-derived datasets", () => {
  const entries = loadManifest();
  for (const id of ["fsd50k", "freesound-one-shot-percussive"]) {
    const entry = entries.find((candidate) => candidate.id === id);
    assert.ok(entry, `${id} is missing`);
    assert.equal(entry.licensePerClip, true, `${id}: licensePerClip`);
  }
});

test("counts are optional but must be consistent when present", () => {
  const base = loadManifest()[0];
  assert.doesNotThrow(() => validateManifest([{ ...base, clipsSeen: 10, clipsUsed: 4, downloadedAt: "2026-11-12" }]));
  assert.throws(() => validateManifest([{ ...base, clipsSeen: 1, clipsUsed: 2 }]), /clipsUsed exceeds clipsSeen/);
  assert.throws(() => validateManifest([{ ...base, clipsUsed: 1.5 }]), /clipsUsed must be a non-negative integer/);
  assert.throws(() => validateManifest([{ ...base, downloadedAt: "12.11.2026" }]), /downloadedAt/);
});

test("the manifest path points inside the repository", () => {
  assert.match(MANIFEST_PATH, /services\/training\/datasets\/manifest\.json$/);
});
